"""5 V / 9 V / 12 V 上升沿 + 下降沿批量采集

要点（全部来自本工程实测）：
  - 目标电压：写固件 `s_vref_target_mv`（0x2000002C），主循环自行软启动到该值
  - J-Link 脚本必须 `g`（resume），否则 CPU 停 halt、写入不生效
  - 触发加 HFReject；电平是相对 offset 的（绝对 = offset + LEVel）
  - 屏幕中心 = −OFFSet，窗口 = −OFFSet ± 4×SCALe
  - 过冲可达设定值的 ~2 倍、负压过冲约 −0.7 倍 → 垂直档位按 span≈2.7×V 留余量
  - 每次读帧都查码值顶满占比，>0.1% 即削波，自动升一档重抓
  - 时基 1 ms/div（10 ms 窗）兼顾起落与振铃；delay 取 ±4.4 ms 内
"""
import json
import os
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fall_keep as F       # noqa: E402
import fall_v3 as V3        # noqa: E402
import rise_final as RF     # noqa: E402

CH = F.CH
ADDR_TARGET = 0x2000002C
LEGAL_VDIV = (0.2, 0.5, 1.0, 2.0, 5.0, 10.0)


def jl(lines):
    p = os.path.join(F.OUT_DIR, "_sw.jlink")
    with open(p, "w") as f:
        f.write("\n".join(lines) + "\ng\nq\n")
    r = subprocess.run([F.JLINK, "-device", "STM32F103C8", "-if", "SWD",
                        "-speed", "4000", "-autoconnect", "1",
                        "-CommanderScript", p],
                       capture_output=True, timeout=25)
    return r.stdout.decode("utf-8", "replace")


def set_target(mv):
    jl([f"w4 {ADDR_TARGET:#x} {mv:#010x}", f"w1 {F.ADDR_OUTPUT:#x} 0x01"])


def output_off():
    jl([f"w1 {F.ADDR_OUTPUT:#x} 0x00"])


def pick_vdiv(target_v):
    span = 2.7 * target_v          # 过冲 ~2x + 负压 ~-0.7x
    need = span / 8.0 * 1.3
    for d in LEGAL_VDIV:
        if d >= need:
            return d
    return LEGAL_VDIV[-1]


def capture(sc, target_v, vdiv, slope, tdiv=1e-3, delay=-0.5e-3):
    """抓一次边沿。slope: 'RISing' / 'FALLing'。"""
    off = -(0.65 * target_v)       # 中心放在 0.65×V（过冲与负压大致对称）
    level_abs = target_v * (0.6 if slope == "RISing" else 0.5)
    level_rel = level_abs - off

    sc.write(f":CHANnel{CH}:SWITch ON")   # 通道关闭时设量程会被静默忽略
    sc.write(f":CHANnel{CH}:COUPling DC")
    sc.write(f":CHANnel{CH}:SCALe {vdiv}")
    sc.write(f":CHANnel{CH}:OFFSet {off}")
    sc.write(f":CHANnel{CH}:BWLimit FULL")
    time.sleep(0.6)
    sc.write(f":TIMebase:SCALe {tdiv}")
    time.sleep(0.6)
    sc.write(f":TIMebase:DELay {delay}")
    time.sleep(0.6)
    sc.write(":ACQuire:MDEP 1M")
    time.sleep(0.4)
    sc.write(f":WAVeform:SOURce C{CH}")
    sc.write(":WAVeform:WIDTh BYTE")
    time.sleep(0.4)
    sc.write(f":TRIGger:EDGE:SOURce C{CH}")
    sc.write(f":TRIGger:EDGE:SLOPe {slope}")
    sc.write(":TRIGger:EDGE:COUPling HFReject")
    time.sleep(0.3)
    sc.write(f":TRIGger:EDGE:LEVel {level_rel:.3f}")
    time.sleep(0.3)

    # 起始状态：上升沿要先把输出关掉；下降沿要先把输出稳定到目标值
    if slope == "RISing":
        output_off()
        time.sleep(1.5)
    else:
        set_target(int(target_v * 1000))
        time.sleep(3.0)

    sc.write(":TRIGger:MODE AUTO")
    sc.write(":TRIGger:RUN")
    for _ in range(24):                    # 轮询代替 sleep(3.0)
        if sc.status().startswith(("Auto", "Trig")):
            break
        time.sleep(0.25)
    sc.write(":TRIGger:MODE SINGle")
    for _ in range(24):                    # 轮询代替 sleep(2.5)
        if sc.status().startswith(("Arm", "Ready")):
            break
        time.sleep(0.2)

    t0 = time.time()
    if slope == "RISing":
        set_target(int(target_v * 1000))
    else:
        output_off()
    while time.time() - t0 < 20:
        if sc.status().startswith("Stop"):
            break
        time.sleep(0.2)

    st = sc.status()
    p_min, p_max = sc.measure("MIN"), sc.measure("MAX")
    try:
        v, t, codes = V3.read_raw(sc, vdiv, off)
    except RuntimeError as exc:
        print(f"      [{slope}] 读帧失败：{exc}（status={st}）", flush=True)
        return None
    if len(v) < 1000:
        return None
    clip = float((codes == 127).mean() * 100 + (codes == -128).mean() * 100)
    if not (np.isnan(p_min) or np.isnan(p_max)) and (v.max() - v.min()) > 0.05:
        gain = (p_max - p_min) / (v.max() - v.min())
        v = v * gain + (p_min - v.min() * gain)
    return v, t, clip, off, level_abs, st


def plot_generic(v, t, m, path, vdiv, off, tdiv, title, kind):
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
    fig = plt.figure(figsize=(12, 6.5))
    gs = fig.add_gridspec(2, 1, height_ratios=[2, 1.2], hspace=0.45)
    step = max(1, len(v) // 9000)

    ax = fig.add_subplot(gs[0])
    ax.plot(t[::step] * 1e3, v[::step], lw=0.9,
            color="#d62728" if kind == "fall" else "#1f77b4")
    ax.axhline(0, color="#888", lw=0.8, alpha=0.6)
    ax.axhline(m.get("_target", 0), color="#2ca02c", lw=0.9, ls="--", alpha=0.7,
               label=f"目标 {m.get('_target', 0):.0f} V")
    ax.set_ylim(lo, hi)
    ax.set_xlabel("时间 (ms)")
    ax.set_ylabel("电压 (V)")
    ax.set_title(f"{title}　屏幕视图：tdiv={tdiv*1e3:.0f} ms/div, "
                 f"offset={off:+.2f} V, vdiv={vdiv:g} V/div")
    ax.legend(loc="upper right", fontsize=9)
    ax.grid(alpha=0.3)

    ax2 = fig.add_subplot(gs[1])
    key = "i0" if "i0" in m else None
    if key:
        t0 = t[m["i0"]]
        span = 0.002 if kind == "rise" else 0.004
        sel = (t >= t0 - 0.001) & (t <= t0 + span)
        ax2.plot(t[sel] * 1e3, v[sel], lw=1.3,
                 color="#d62728" if kind == "fall" else "#1f77b4")
        ax2.axhline(0, color="#888", lw=0.8)
        ax2.set_xlabel("时间 (ms)")
        ax2.set_ylabel("电压 (V)")
        ax2.grid(alpha=0.3)

    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def main():
    targets = [(5.0, "5 V"), (9.0, "9 V"), (12.0, "12 V")]
    summary = []
    sc = F.SCPI(F.HOST, F.PORT)
    try:
        sc.drain()
        for tv, label in targets:
            print("=" * 70, flush=True)
            print(f"### {label}", flush=True)
            set_target(int(tv * 1000))
            time.sleep(3.5)
            mean_v = sc.measure("MEAN")
            print(f"    稳定电压 MEAN={mean_v:.2f}V", flush=True)

            for kind in ("rise", "fall"):
                vdiv = pick_vdiv(tv)
                got = None
                for attempt in range(3):
                    slope = "RISing" if kind == "rise" else "FALLing"
                    res = capture(sc, tv, vdiv, slope)
                    if res is None:
                        print(f"    [{kind}] 第{attempt+1}次未触发，重试", flush=True)
                        time.sleep(2.0)
                        continue
                    v, t, clip, off, lvl, st = res
                    if clip > 0.1:
                        print(f"    [{kind}] vdiv={vdiv:g} 削波 {clip:.2f}% → 升档",
                              flush=True)
                        vdiv = LEGAL_VDIV[min(LEGAL_VDIV.index(vdiv) + 1,
                                              len(LEGAL_VDIV) - 1)]
                        continue
                    m = (RF.rise_metrics(v, t) if kind == "rise"
                         else V3.analyze_fall(v, t))
                    if m is None:
                        print(f"    [{kind}] vdiv={vdiv:g} 未捕获 "
                              f"V=[{v.min():.2f},{v.max():.2f}]，重试", flush=True)
                        continue
                    m["_target"] = tv
                    got = (v, t, m, vdiv, off, clip)
                    break

                if got is None:
                    print(f"    [{kind}] 三次尝试均失败", flush=True)
                    summary.append({"v": tv, "kind": kind, "status": "fail"})
                    continue

                v, t, m, vdiv, off, clip = got
                np.savez(os.path.join(F.OUT_DIR, f"captured_{kind}_{int(tv)}v.npz"),
                         volts=v, seconds=t)
                png = os.path.join(F.PKG_DIR, f"{kind}_{int(tv)}v.png")
                plot_generic(v, t, m, png, vdiv, off, 1e-3,
                             f"{label} 输出{'开启（上升沿）' if kind == 'rise' else '关断（下降沿）'}",
                             kind)
                clean = {k: x for k, x in m.items()
                         if not k.startswith("_") and not k.startswith("i")}
                print(f"    [{kind}] vdiv={vdiv:g} off={off:+.2f} 削波={clip:.3f}% "
                      f"V=[{v.min():.2f},{v.max():.2f}]", flush=True)
                print(f"         {json.dumps(clean, ensure_ascii=False)}", flush=True)
                summary.append({"v": tv, "kind": kind, "status": "ok",
                                "vdiv": vdiv, "off": off, "clip_pct": round(clip, 4),
                                **clean})

            set_target(int(tv * 1000))
            time.sleep(2.0)
    finally:
        try:
            set_target(3300)
            time.sleep(2.0)
        except Exception:
            pass
        sc.close()

    with open(os.path.join(F.OUT_DIR, "sweep_edges_result.json"), "w") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)
    print("\n" + json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
