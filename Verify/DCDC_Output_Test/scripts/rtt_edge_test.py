"""RTT 控制下的边沿与纹波测试（先设电压再开输出，走完整 SetOutput 语义）

控制方式：JLinkGDBServerCL 的 RTT Telnet 端口 19021（双向）
  - 发：写 socket → 目标 down-buffer 0 → DRV_RTT_Read → APP_CMD_Task
  - 收：上行被遥测流占满，命令回复会被 NO_BLOCK_SKIP 丢掉
        → 状态一律用 J-Link 读 RAM 校验，不依赖 RTT 回复
"""
import os
import socket
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fall_keep as F       # noqa: E402
import fall_v3 as V3        # noqa: E402
import rise_final as RF     # noqa: E402

JL = r"C:\Program Files\SEGGER\JLink_V970\JLink.exe"
RTT_PORT = 19021
# 本次编译的 RAM 地址（重编译后须从 .map 重取）
A_STATE, A_VOUT, A_VREF, A_TGT = 0x20000014, 0x20000024, 0x2000002C, 0x20000030
CH = F.CH
TARGET_V = float(os.environ.get("TARGET_V", "3.3"))
LEGAL_VDIV = (0.2, 0.5, 1.0, 2.0, 5.0, 10.0)


def pick_vert(kind, v):
    """按软启动特性选垂直档位：上升峰值 ~1.12×V，下降负压 ~-0.75×V。"""
    if kind == "rise":
        lo, hi = -0.15 * v, 1.20 * v
    else:
        lo, hi = -0.85 * v, 1.05 * v
    need = (hi - lo) / 8.0 * 1.15
    vdiv = next((d for d in LEGAL_VDIV if d >= need), LEGAL_VDIV[-1])
    center = (lo + hi) / 2.0
    return vdiv, -center


# ---------------- 控制 ----------------
def rtt_send(*cmds):
    s = socket.create_connection(("127.0.0.1", RTT_PORT), timeout=3)
    s.settimeout(0.15)
    t0 = time.time()
    while time.time() - t0 < 0.5:      # 定时排空（上行是连续流，不能等 EOF）
        try:
            s.recv(8192)
        except socket.timeout:
            pass
    for c in cmds:
        s.sendall((c + "\n").encode())
        time.sleep(0.12)
    time.sleep(0.3)
    s.close()


def fw_state():
    p = os.path.join(F.OUT_DIR, "_st.jlink")
    with open(p, "w") as f:
        f.write("mem32 0x20000014 1\nmem32 0x20000024 1\n"
                "mem32 0x2000002C 1\nmem32 0x20000030 1\nq\n")
    r = subprocess.run([JL, "-device", "STM32F103C8", "-if", "SWD",
                        "-speed", "4000", "-autoconnect", "1",
                        "-CommanderScript", p],
                       capture_output=True, timeout=25)
    d = {}
    for line in r.stdout.decode("utf-8", "replace").splitlines():
        if line.startswith("20000014"):
            d["state"] = int(line.split("=")[1].strip(), 16)
        elif line.startswith("20000024"):
            d["vout"] = int(line.split("=")[1].strip(), 16)
        elif line.startswith("2000002C"):
            d["vref"] = int(line.split("=")[1].strip(), 16)
        elif line.startswith("20000030"):
            d["tgt"] = int(line.split("=")[1].strip(), 16)
    st = d.get("state", 0)
    d["fault"] = (st >> 8) & 0xFF
    d["output"] = (st >> 24) & 0xFF
    return d


# ---------------- 示波器 ----------------
def configure(sc, vdiv, off, tdiv, delay, slope, level_abs):
    level_rel = level_abs - off
    sc.write(f":CHANnel{CH}:SWITch ON")
    sc.write(f":CHANnel{CH}:COUPling DC")
    sc.write(f":CHANnel{CH}:SCALe {vdiv}")
    sc.write(f":CHANnel{CH}:OFFSet {off}")
    sc.write(f":CHANnel{CH}:BWLimit FULL")
    time.sleep(0.3)
    sc.write(f":TIMebase:SCALe {tdiv}")
    sc.write(f":TIMebase:DELay {delay}")
    time.sleep(0.3)
    sc.write(":ACQuire:MDEP 1M")
    sc.write(f":WAVeform:SOURce C{CH}")
    sc.write(":WAVeform:WIDTh BYTE")
    time.sleep(0.25)
    sc.write(f":TRIGger:EDGE:SOURce C{CH}")
    sc.write(f":TRIGger:EDGE:SLOPe {slope}")
    sc.write(":TRIGger:EDGE:COUPling HFReject")
    time.sleep(0.2)
    sc.write(f":TRIGger:EDGE:LEVel {level_rel:.4f}")
    time.sleep(0.25)


def wait_state(sc, want, tries=30, gap=0.2):
    for _ in range(tries):
        sc.drain()
        st = sc.status()
        if st.startswith(want):
            return st
        time.sleep(gap)
    return sc.status()


def shot(sc, action, vdiv, off, tdiv, delay, slope, level_abs, tag):
    """武装 → 执行 action（RTT 命令）→ 等 Stop → 读帧。"""
    configure(sc, vdiv, off, tdiv, delay, slope, level_abs)
    sc.write(":TRIGger:MODE AUTO")
    sc.write(":TRIGger:RUN")
    wait_state(sc, ("Auto", "Trig"))
    sc.write(":TRIGger:MODE SINGle")
    wait_state(sc, ("Arm", "Ready"))

    t0 = time.time()
    action()
    while time.time() - t0 < 25:
        if sc.status().startswith("Stop"):
            break
        time.sleep(0.15)
    print(f"      [{tag}] status={sc.status()} ({time.time()-t0:.2f}s)", flush=True)

    p_min, p_max = sc.measure("MIN"), sc.measure("MAX")
    try:
        v, t, codes = V3.read_raw(sc, vdiv, off)
    except RuntimeError as exc:
        print(f"      [{tag}] {exc}", flush=True)
        return None
    clip = float((codes == 127).mean() * 100 + (codes == -128).mean() * 100)
    if not (np.isnan(p_min) or np.isnan(p_max)) and (v.max() - v.min()) > 0.05:
        gain = (p_max - p_min) / (v.max() - v.min())
        v = v * gain + (p_min - v.min() * gain)
    print(f"      [{tag}] 顶满={clip:.3f}% V=[{v.min():.2f},{v.max():.2f}]", flush=True)
    return v, t, clip


def plot(v, t, m, path, vdiv, off, title, kind):
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
    ax.legend(loc="lower right", fontsize=9)
    ax.grid(alpha=0.3)

    ax2 = fig.add_subplot(gs[1])
    t0 = t[m["i0"]]
    span = (t[-1] - t[0]) * 0.35
    sel = (t >= t0 - 0.02 * span) & (t <= t0 + span)
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
    print(f"RTT 控制边沿测试  目标 {TARGET_V:g} V（先设电压再开输出）")
    print("=" * 72)
    print("固件:", fw_state(), flush=True)

    rtt_send("OFF")
    time.sleep(1.5)
    rtt_send(f"V{int(TARGET_V*1000)}")
    time.sleep(0.5)
    print("设定后:", fw_state(), flush=True)

    sc = F.SCPI(F.HOST, F.PORT)
    summary = {}
    try:
        sc.drain()

        # ---- 上升沿：软启动斜坡，用 100 ms 窗 ----
        print("\n--- 上升沿（软启动）---", flush=True)
        vd_r, off_r = pick_vert("rise", TARGET_V)
        print(f"      垂直: vdiv={vd_r:g} off={off_r:+.2f} "
              f"(窗 {-off_r-4*vd_r:+.1f}…{-off_r+4*vd_r:+.1f} V)", flush=True)
        r = shot(sc, lambda: rtt_send("ON"), vd_r, off_r, 1e-2, -5e-3,
                 "RISing", 0.5, "rise")
        if r:
            v, t, clip = r
            m = RF.rise_metrics(v, t)
            if m:
                np.savez(os.path.join(F.OUT_DIR, "rtt_rise.npz"), volts=v, seconds=t)
                plot(v, t, m, os.path.join(F.PKG_DIR, f"rtt_rise_{TARGET_V:g}v.png"),
                     vd_r, off_r,
                     f"{TARGET_V:g} V 开启（RTT 软启动）　tdiv=10 ms/div", "rise")
                print("      " + f"10-90%={m['rise_ms']:.2f}ms 峰值={m['vmax']:.2f}V "
                      f"超调={m['overshoot_pct']:.1f}% 建立={m['settle_ms']:.1f}ms",
                      flush=True)
                summary["rise"] = {k: x for k, x in m.items() if not k.startswith("i")}
                summary["rise"]["clip_pct"] = round(clip, 4)

        time.sleep(2.0)
        print("稳态:", fw_state(), flush=True)

        # ---- 下降沿 ----
        print("\n--- 下降沿 ---", flush=True)
        vd_f, off_f = pick_vert("fall", TARGET_V)
        print(f"      垂直: vdiv={vd_f:g} off={off_f:+.2f} "
              f"(窗 {-off_f-4*vd_f:+.1f}…{-off_f+4*vd_f:+.1f} V)", flush=True)
        r = shot(sc, lambda: rtt_send("OFF"), vd_f, off_f, 1e-3, 4.4e-3,
                 "FALLing", TARGET_V * 0.76, "fall")
        if r:
            v, t, clip = r
            m = V3.analyze_fall(v, t)
            if m:
                np.savez(os.path.join(F.OUT_DIR, "rtt_fall.npz"), volts=v, seconds=t)
                plot(v, t, m, os.path.join(F.PKG_DIR, f"rtt_fall_{TARGET_V:g}v.png"),
                     vd_f, off_f,
                     f"{TARGET_V:g} V 关断（RTT）　tdiv=1 ms/div", "fall")
                print("      " + f"10-90%={m['fall_ms']:.3f}ms 最低={m['vmin']:.2f}V "
                      f"LC={m['osc_period_ms']:.2f}ms/{m['osc_freq_hz']:.0f}Hz "
                      f"归零={m['settle_ms']:.1f}ms", flush=True)
                summary["fall"] = {k: x for k, x in m.items() if not k.startswith("i")}
                summary["fall"]["clip_pct"] = round(clip, 4)

        # ---- 纹波：重新开输出，AC 耦合 ----
        print("\n--- 纹波 ---", flush=True)
        rtt_send("ON")
        time.sleep(4.0)
        sc.write(f":CHANnel{CH}:SWITch ON")
        sc.write(f":CHANnel{CH}:COUPling AC")
        sc.write(f":CHANnel{CH}:SCALe 0.1")
        sc.write(f":CHANnel{CH}:OFFSet 0")
        time.sleep(0.5)
        sc.write(":TRIGger:MODE AUTO")
        sc.write(":TRIGger:RUN")
        wait_state(sc, ("Auto", "Trig"))
        time.sleep(2.0)
        pkpk = sc.measure("PKPK")
        rms = sc.measure("RMS")
        print(f"      PKPK={pkpk*1e3:.1f} mV  RMS={rms*1e3:.1f} mV", flush=True)
        summary["ripple"] = {"pkpk_mv": round(pkpk * 1e3, 1), "rms_mv": round(rms * 1e3, 1)}
        sc.write(f":CHANnel{CH}:COUPling DC")

        print("\n稳态:", fw_state(), flush=True)
    finally:
        sc.close()

    import json
    with open(os.path.join(F.OUT_DIR, f"rtt_edge_{TARGET_V:g}v.json"), "w") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)
    print("\n" + json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
