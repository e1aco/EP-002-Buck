"""边沿采集（按新 skill 规范）

配置顺序（社区 skill 实测约束）：
  CHANnel:SWITch ON → COUPling → SCALe → OFFSet
  → TIMebase:SCALe → DELay
  → TRIGger:EDGE:SOURce → SLOPe → COUPling(HFReject) → LEVel
  → MODE AUTO → RUN（轮询到 Auto/Trig）→ MODE SINGle（轮询到 Arm/Ready）

读帧：先取 PREamble 的 WAVE_ARRAY_1，为 0 直接判空帧（省一次 DATA? 传输）；
      读完查码值顶满占比，>0.1% 判削波并自动升档重抓；
      幅值用同帧 PAVA MIN/MAX 两点标定。

安全：每次采集前读固件 state，fault=1 先复位清除（12 V 档过冲会触发 OVP 锁存）。
"""
import json
import os
import struct
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fall_keep as F       # noqa: E402
import fall_v3 as V3        # noqa: E402
import rise_final as RF     # noqa: E402

CH = F.CH
TARGET_V = float(os.environ.get("TARGET_V", "3.3"))
ADDR_TARGET = 0x2000002C
LEGAL_VDIV = (0.2, 0.5, 1.0, 2.0, 5.0, 10.0)


# ---------------- 固件侧 ----------------
def jl(lines, timeout=25):
    p = os.path.join(F.OUT_DIR, "_ec.jlink")
    with open(p, "w") as f:
        f.write("\n".join(lines) + "\nq\n")
    r = subprocess.run([F.JLINK, "-device", "STM32F103C8", "-if", "SWD",
                        "-speed", "4000", "-autoconnect", "1",
                        "-CommanderScript", p],
                       capture_output=True, timeout=timeout)
    return r.stdout.decode("utf-8", "replace")


def fw_state():
    out = jl(["mem32 0x20000010 1", "mem32 0x20000020 1", "mem32 0x20000024 1"])
    st = vout = iout = 0
    for line in out.splitlines():
        if line.startswith("20000010"):
            st = int(line.split("=")[1].strip(), 16)
        elif line.startswith("20000020"):
            vout = int(line.split("=")[1].strip(), 16)
        elif line.startswith("20000024"):
            iout = int(line.split("=")[1].strip(), 16)
    # 布局：byte0=cascade byte1=fault byte2=ready byte3=output
    return {"raw": st, "cascade": st & 0xFF, "fault": (st >> 8) & 0xFF,
            "ready": (st >> 16) & 0xFF, "output": (st >> 24) & 0xFF,
            "vout": vout, "iout": iout}


def clear_fault():
    jl(["r", "g"])
    time.sleep(2.0)


def set_target(mv, on=True):
    cmds = [f"w4 {ADDR_TARGET:#x} {mv:#010x}"]
    if on:
        cmds.append(f"w1 {F.ADDR_OUTPUT:#x} 0x01")
    cmds.append("g")          # resume：否则 CPU 停 halt，写入不生效
    jl(cmds)


def output_off():
    jl([f"w1 {F.ADDR_OUTPUT:#x} 0x00", "g"])


def ensure_ok():
    """fault=1 就复位清掉（OVP 锁存只能靠重新初始化）。"""
    s = fw_state()
    if s["fault"]:
        print(f"    ⚠ fault=1（vout={s['vout']}mV）→ 复位清除", flush=True)
        clear_fault()
        s = fw_state()
    return s


# ---------------- 示波器侧 ----------------
def configure(sc, vdiv, off, tdiv, delay, slope, level_abs):
    """严格按 skill 规定的顺序配置，每步回读校验（数值容差）。"""
    level_rel = level_abs - off
    sc.write(f":CHANnel{CH}:SWITch ON")        # 通道关闭时设量程会被静默忽略
    sc.write(f":CHANnel{CH}:COUPling DC")
    sc.write(f":CHANnel{CH}:SCALe {vdiv}")
    sc.write(f":CHANnel{CH}:OFFSet {off}")
    sc.write(f":CHANnel{CH}:BWLimit FULL")
    time.sleep(0.35)
    sc.write(f":TIMebase:SCALe {tdiv}")
    sc.write(f":TIMebase:DELay {delay}")
    time.sleep(0.35)
    sc.write(":ACQuire:MDEP 1M")
    sc.write(f":WAVeform:SOURce C{CH}")
    sc.write(":WAVeform:WIDTh BYTE")
    time.sleep(0.3)
    sc.write(f":TRIGger:EDGE:SOURce C{CH}")     # 先选源
    sc.write(f":TRIGger:EDGE:SLOPe {slope}")
    sc.write(":TRIGger:EDGE:COUPling HFReject")  # 滤掉写入瞬间的快毛刺
    time.sleep(0.25)
    sc.write(f":TRIGger:EDGE:LEVel {level_rel:.4f}")   # 再设电平
    time.sleep(0.3)

    sc.drain()
    vd = F.num(sc.query(f":CHANnel{CH}:SCALe?"), -1)
    sc.drain()
    of = F.num(sc.query(f":CHANnel{CH}:OFFSet?"), 1e9)
    sc.drain()
    td = F.num(sc.query(":TIMebase:SCALe?"), -1)
    sc.drain()
    dl = F.num(sc.query(":TIMebase:DELay?"), 1e9)
    sc.drain()
    lv = F.num(sc.query(f":TRIGger:EDGE:LEVel?"), 1e9)
    ok = (abs(vd - vdiv) < 1e-3 * max(vdiv, 1e-9) + 1e-6
          and abs(td - tdiv) < 1e-3 * max(tdiv, 1e-9) + 1e-12
          and abs(of - off) < 1e-3 and abs(lv - level_rel) < 1e-2)
    if not ok:
        print(f"    ⚠ 回读不符 vdiv={vd} off={of} tdiv={td} delay={dl} lvl={lv}",
              flush=True)
    return ok


def wait_state(sc, want, tries=30, gap=0.2):
    for _ in range(tries):
        sc.drain()
        st = sc.status()
        if st.startswith(want):
            return st
        time.sleep(gap)
    return sc.status()


def read_frame(sc, vdiv, off):
    """先查 WAVE_ARRAY_1；空帧直接返回 None，不做无用的 DATA? 传输。"""
    sc.buf = b""
    sc.write(":WAVeform:PREamble?")
    pre = sc.read_block()
    nbytes = struct.unpack("<i", pre[0x74:0x78])[0]
    if nbytes <= 0:
        return None
    dt = struct.unpack("<f", pre[0xB0:0xB4])[0]
    hoff = struct.unpack("<d", pre[0xB4:0xBC])[0]
    sc.write(":WAVeform:DATA?")
    raw = sc.read_block()
    if not raw:
        return None
    codes = np.frombuffer(raw, dtype=np.int8)
    volts = codes.astype(np.float64) / 256.0 * 8.0 * vdiv - off
    seconds = hoff + np.arange(len(volts)) * dt
    return volts, seconds, codes


def capture(sc, vdiv, off, tdiv, delay, slope, level_abs, tag):
    """一次完整采集。返回 (volts, seconds, codes, 用时s) 或 None。"""
    t_start = time.time()
    configure(sc, vdiv, off, tdiv, delay, slope, level_abs)

    # 起始状态：上升沿先关输出；下降沿先稳定到目标
    if slope == "RISing":
        output_off()
    else:
        set_target(int(TARGET_V * 1000))
    time.sleep(1.2 if slope == "RISing" else 2.5)

    sc.write(":TRIGger:MODE AUTO")
    sc.write(":TRIGger:RUN")
    st = wait_state(sc, ("Auto", "Trig"))          # 轮询代替 sleep(3.0)
    sc.write(":TRIGger:MODE SINGle")
    st2 = wait_state(sc, ("Arm", "Ready"))         # 轮询代替 sleep(2.5)
    print(f"      [{tag}] 引擎={st} 武装={st2}", flush=True)

    t_trig = time.time()
    if slope == "RISing":
        set_target(int(TARGET_V * 1000))
    else:
        output_off()
    while time.time() - t_trig < 20:
        if sc.status().startswith("Stop"):
            break
        time.sleep(0.2)

    p_min, p_max = sc.measure("MIN"), sc.measure("MAX")
    got = read_frame(sc, vdiv, off)
    if got is None:
        print(f"      [{tag}] 空帧（WAVE_ARRAY_1=0），status={sc.status()}", flush=True)
        return None
    v, t, codes = got
    clip = float((codes == 127).mean() * 100 + (codes == -128).mean() * 100)
    if not (np.isnan(p_min) or np.isnan(p_max)) and (v.max() - v.min()) > 0.05:
        gain = (p_max - p_min) / (v.max() - v.min())
        v = v * gain + (p_min - v.min() * gain)
    dt_all = time.time() - t_start
    print(f"      [{tag}] 顶满={clip:.3f}% V=[{v.min():.2f},{v.max():.2f}] "
          f"用时={dt_all:.1f}s", flush=True)
    return v, t, codes, clip, dt_all


def plot_pair(v, t, m, path, vdiv, off, title, kind):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager
    for name in ("Microsoft YaHei", "SimHei", "Noto Sans CJK SC"):
        try:
            font_manager.findfont(name, fallback_to_default=False)
            plt.rcParams["font.sans-serif"] = [name]
            break
        except Exception:
            continue
    plt.rcParams["axes.unicode_minus"] = False

    lo, hi = -off - 4 * vdiv, -off + 4 * vdiv
    fig = plt.figure(figsize=(12.5, 7))
    gs = fig.add_gridspec(2, 1, height_ratios=[2, 1.15], hspace=0.45)
    step = max(1, len(v) // 9000)
    color = "#d62728" if kind == "fall" else "#1f77b4"

    ax = fig.add_subplot(gs[0])
    ax.plot(t[::step] * 1e3, v[::step], lw=0.9, color=color)
    ax.axhline(0, color="#888", lw=0.8, alpha=0.6)
    ax.axhline(TARGET_V, color="#2ca02c", lw=0.9, ls="--", alpha=0.7,
               label=f"目标 {TARGET_V:g} V")
    ax.set_ylim(lo, hi)
    ax.set_xlabel("时间 (ms)")
    ax.set_ylabel("电压 (V)")
    ax.set_title(title)
    ax.legend(loc="upper right", fontsize=9)
    ax.grid(alpha=0.3)

    ax2 = fig.add_subplot(gs[1])
    t0 = t[m["i0"]]
    span = 0.003 if kind == "rise" else 0.005
    sel = (t >= t0 - 0.001) & (t <= t0 + span)
    ax2.plot(t[sel] * 1e3, v[sel], lw=1.3, color=color)
    ax2.axhline(0, color="#888", lw=0.8)
    ax2.set_xlabel("时间 (ms)")
    ax2.set_ylabel("电压 (V)")
    ax2.set_title("细节放大")
    ax2.grid(alpha=0.3)

    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def main():
    print("=" * 72)
    print(f"边沿采集（新 skill 规范）  目标 {TARGET_V:g} V")
    print("=" * 72)
    s = ensure_ok()
    print(f"固件: fault={s['fault']} output={s['output']} vout={s['vout']}mV", flush=True)

    sc = F.SCPI(F.HOST, F.PORT)
    summary = {}
    try:
        sc.drain()
        set_target(int(TARGET_V * 1000))
        time.sleep(3.0)
        s = ensure_ok()
        print(f"已设定 {TARGET_V:g} V → vout={s['vout']}mV iout={s['iout']}mA", flush=True)

        # 上升沿：窗口要容纳 ~2× 过冲；vdiv 按峰值留余量
        for kind, slope in (("rise", "RISing"), ("fall", "FALLing")):
            print(f"\n--- {'上升沿' if kind == 'rise' else '下降沿'} ---", flush=True)
            # 垂直档位按目标电压自适应：
            # 上升沿要容纳 ~2.1× 过冲（lo=-0.3V, hi=2.1V）；下降沿要容纳 ~-0.8× 负压
            if kind == "rise":
                need = (2.1 * TARGET_V - (-0.3 * TARGET_V)) / 8.0 * 1.15
                center = 0.9 * TARGET_V
            else:
                need = (1.1 * TARGET_V - (-0.8 * TARGET_V)) / 8.0 * 1.15
                center = 0.15 * TARGET_V
            vdiv = next((d for d in LEGAL_VDIV if d >= need), LEGAL_VDIV[-1])
            off = -center
            # 上升沿触发点偏移每次都在变 → 先用 20 ms 窗兜住，再按实测位置取景
            tdiv = 2e-3 if kind == "rise" else 1e-3
            delay = -2e-3 if kind == "rise" else 4.4e-3
            level_abs = TARGET_V * 0.76

            got = None
            for attempt in range(3):
                res = capture(sc, vdiv, off, tdiv, delay, slope, level_abs,
                              f"{kind}#{attempt+1}")
                if res is None:
                    ensure_ok()
                    continue
                v, t, codes, clip, used = res
                if clip > 0.1:
                    nv = LEGAL_VDIV[min(LEGAL_VDIV.index(vdiv) + 1,
                                        len(LEGAL_VDIV) - 1)]
                    print(f"      削波 {clip:.2f}% → vdiv {vdiv:g}→{nv:g}", flush=True)
                    vdiv = nv
                    off = -(0.65 * TARGET_V) * (vdiv / 1.0)
                    continue
                m = (RF.rise_metrics(v, t) if kind == "rise"
                     else V3.analyze_fall(v, t))
                if m is None:
                    print("      判据不成立，重试", flush=True)
                    ensure_ok()
                    continue
                if kind == "rise":
                    pos = (t[m["i0"]] - t[0]) / (t[-1] - t[0])
                    if pos > 0.25:      # 偏右就左移再抓一次
                        nd = delay + (t[m["i0"]] - t[0]) - 0.05 * tdiv * 10
                        nd = max(-5 * tdiv, min(nd, 5 * tdiv))
                        print(f"      起升在 {pos*100:.0f}% → delay {delay*1e3:+.2f}"
                              f"→{nd*1e3:+.2f}ms 重抓", flush=True)
                        res2 = capture(sc, vdiv, off, tdiv, nd, slope, level_abs,
                                       f"{kind}#frame")
                        if res2 is not None:
                            v2, t2, c2, clip2, used2 = res2
                            m2 = RF.rise_metrics(v2, t2)
                            if m2 is not None and clip2 <= 0.1:
                                v, t, m, clip, used = v2, t2, m2, clip2, used + used2
                                delay = nd
                got = (v, t, m, clip, used)
                break

            if got is None:
                summary[kind] = {"status": "fail"}
                continue

            v, t, m, clip, used = got
            np.savez(os.path.join(F.OUT_DIR, f"edge_{kind}_{TARGET_V:g}v.npz"),
                     volts=v, seconds=t)
            plot_pair(v, t, m,
                      os.path.join(F.PKG_DIR, f"{kind}_{TARGET_V:g}v.png"),
                      vdiv, off,
                      f"{TARGET_V:g} V 输出{'开启（上升沿）' if kind == 'rise' else '关断（下降沿）'}"
                      f"　tdiv={tdiv*1e3:.0f} ms/div, offset={off:+.2f} V",
                      kind)
            clean = {k: x for k, x in m.items() if not k.startswith("i")}
            print("      " + json.dumps(clean, ensure_ascii=False), flush=True)
            summary[kind] = {"status": "ok", "vdiv": vdiv, "off": off,
                             "clip_pct": round(clip, 4), "耗时_s": round(used, 1),
                             **clean}

        set_target(int(TARGET_V * 1000))
    finally:
        sc.close()

    with open(os.path.join(F.OUT_DIR, "edge_result.json"), "w") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)
    print("\n" + json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
