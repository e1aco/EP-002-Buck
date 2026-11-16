"""上升沿采集（自动取景，与下降沿同一套方法）

与下降沿的差别：
  - 起始状态相反：先把输出关掉、确认 0 V，再武装，最后开输出触发；
  - 垂直取景：0 V 放屏幕下部（vdiv=0.5, offset=+1.8 → 窗 -0.2…3.8 V）；
  - 触发方向 RISing，电平取 50%（1.65 V）；
  - 斜坡时长未知，先粗测一次定时基，再按两遍法平移取景。

流程：粗测（tdiv=50 ms）→ 按实测时长选最小时基 → 平移取景 → 定稿
"""
import json
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fall_keep as F  # noqa: E402

CH = F.CH
TARGET_V = 3.3
VDIV, VOFF = 0.5, 1.8          # 0 V 落在下部 5% 处
TRIG_LEVEL = TARGET_V * 0.5    # 1.65 V
LEVEL_REL = 2.0 - VOFF         # 电平相对 offset；绝对 2.0V，上升必跨过
PRE_FRAC = 0.02

LEGAL_TDIV = (1e-3, 2e-3, 5e-3, 1e-2, 2e-2, 5e-2, 1e-1, 2e-1, 5e-1, 1.0)


def wait_running(sc, tries=25):
    for _ in range(tries):
        time.sleep(0.6)
        if sc.status().startswith(("Auto", "Trig")):
            return True
    return False


def output_off_wait(sc):
    F.jlink([f"w1 {F.ADDR_OUTPUT:#x} 0x00"])
    time.sleep(2.0)


def output_on_delayed(delay_ms=2500):
    path = os.path.join(F.OUT_DIR, "_jlink_rise.jlink")
    with open(path, "w") as f:
        f.write(f"sleep {delay_ms}\nw1 {F.ADDR_OUTPUT:#x} 0x01\nq\n")
    import subprocess
    return subprocess.Popen([F.JLINK, "-device", "STM32F103C8", "-if", "SWD",
                             "-speed", "4000", "-autoconnect", "1",
                             "-CommanderScript", path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def capture_rise(sc, tdiv, delay, tag):
    print(f"  [{tag}] tdiv={tdiv} delay={delay:.6f}s", flush=True)
    output_off_wait(sc)

    sc.write(f":TIMebase:SCALe {tdiv}")
    time.sleep(0.6)
    sc.write(f":TIMebase:DELay {delay}")
    time.sleep(0.6)

    sc.write(":TRIGger:EDGE:SLOPe RISing")
    sc.write(f":TRIGger:EDGE:LEVel {LEVEL_REL}")
    sc.write(":TRIGger:MODE AUTO")
    sc.write(":TRIGger:RUN")
    # 关断态下电平高于信号，AUTO 停在 Arm 属正常；只要不是 Stop 就算引擎在跑
    time.sleep(2.0)
    st = sc.status()
    if st.startswith("Stop"):
        raise RuntimeError(f"引擎未启动（{st}）")
    print(f"       status={st} level={sc.query(':TRIGger:EDGE:LEVel?')} "
          f"MEAN={sc.measure('MEAN')}", flush=True)

    proc = output_on_delayed(2500)
    time.sleep(1.5)
    sc.write(":TRIGger:MODE SINGle")
    time.sleep(1.0)
    print(f"       已武装 {sc.status()}", flush=True)

    t0 = time.time()
    while time.time() - t0 < 30:
        if sc.status().startswith("Stop"):
            print(f"       已触发（{time.time()-t0:.2f}s）", flush=True)
            break
        time.sleep(0.2)
    try:
        proc.wait(timeout=10)
    except Exception:
        pass

    p_min, p_max = sc.measure("MIN"), sc.measure("MAX")
    wf = sc.read_frame(VDIV, VOFF)
    v, t = wf["volts"], wf["seconds"]
    if not (np.isnan(p_min) or np.isnan(p_max)) and (v.max() - v.min()) > 0.05:
        gain = (p_max - p_min) / (v.max() - v.min())
        v = v * gain + (p_min - v.min() * gain)
    print(f"       {wf['npoints']} 点 窗={wf['window_s']*1e3:.1f}ms "
          f"V=[{v.min():.2f},{v.max():.2f}]", flush=True)
    return v, t


def analyze_rise(volts, seconds):
    """上升沿：起点=离开 0 V，终点=进入 ±2%·Vtop 并保持。"""
    n = len(volts)
    v_top = float(np.median(volts[-max(200, n // 10):]))
    v_base = float(np.median(volts[:max(200, n // 20)]))
    span = v_top - v_base
    if span < 0.5:
        return None

    # 起点：最后一次低于 base+5%·span
    thr = v_base + 0.05 * span
    i0 = 0
    for k in range(n):
        if volts[k] <= thr:
            i0 = k

    def cross(frac):
        level = v_base + frac * span
        idx = np.where(volts[i0:] >= level)[0]
        return i0 + int(idx[0]) if len(idx) else n - 1

    i10, i50, i90 = cross(0.1), cross(0.5), cross(0.9)
    vmax = float(volts.max())
    i_vmax = int(np.argmax(volts))

    band = 0.02 * abs(span)
    i_set = n - 1
    for k in range(i_vmax, n):
        if abs(volts[k] - v_top) > band:
            i_set = k
    i_set = min(i_set + 1, n - 1)

    return {
        "v_base": round(v_base, 3), "v_top": round(v_top, 3),
        "vmax": round(vmax, 3),
        "overshoot_pct": round((vmax - v_top) / span * 100, 2) if span > 0 else 0.0,
        "t10_ms": round(float(seconds[i10] - seconds[i0]) * 1e3, 2),
        "t50_ms": round(float(seconds[i50] - seconds[i0]) * 1e3, 2),
        "t90_ms": round(float(seconds[i90] - seconds[i0]) * 1e3, 2),
        "rise_ms": round(float(seconds[i90] - seconds[i10]) * 1e3, 2),
        "settle_ms": round(float(seconds[i_set] - seconds[i0]) * 1e3, 1),
        "slew_mv_per_ms": round(span / max(1e-9, float(seconds[i90] - seconds[i10])) * 1e3 / 1e3, 1),
        "i0": i0, "i_vmax": i_vmax,
    }


def plot_rise(volts, seconds, m, path):
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

    fig = plt.figure(figsize=(13, 9))
    gs = fig.add_gridspec(3, 1, height_ratios=[2, 1.3, 1.3], hspace=0.5)
    t_ms = seconds * 1e3
    step = max(1, len(volts) // 9000)

    ax = fig.add_subplot(gs[0])
    ax.plot(t_ms[::step], volts[::step], lw=0.8, color="#1f77b4")
    ax.axhline(0, color="#888", lw=0.8, alpha=0.6)
    ax.axhline(TARGET_V, color="#2ca02c", lw=0.9, ls="--", alpha=0.7,
               label=f"目标 {TARGET_V} V")
    ax.plot(t_ms[m["i0"]], volts[m["i0"]], "o", ms=8, color="#ff7f0e",
            label=f"起始 {volts[m['i0']]:.2f} V")
    ax.plot(t_ms[m["i_vmax"]], volts[m["i_vmax"]], "^", ms=9, color="#9467bd",
            label=f"峰值 {m['vmax']:.2f} V")
    ax.set_xlabel("时间 (ms)")
    ax.set_ylabel("电压 (V)")
    ax.set_title(f"3.3 V 输出开启（上升沿）　记录窗 "
                 f"{(seconds[-1]-seconds[0])*1e3:.2f} ms")
    ax.legend(loc="lower right", fontsize=9)
    ax.grid(alpha=0.3)

    ax2 = fig.add_subplot(gs[1])
    t0 = seconds[m["i0"]]
    win = (seconds[-1] - seconds[0]) * 0.35
    sel = (seconds >= t0 - 0.02 * win) & (seconds <= t0 + win)
    ax2.plot(seconds[sel] * 1e3, volts[sel], lw=1.3, color="#1f77b4")
    ax2.axhline(0, color="#888", lw=0.8)
    for frac, lab, col in ((0.1, "10%", "#2ca02c"), (0.5, "50%", "#1f77b4"),
                           (0.9, "90%", "#ff7f0e")):
        ax2.axhline(m["v_base"] + frac * (m["v_top"] - m["v_base"]),
                    color=col, lw=0.9, ls=":", alpha=0.8, label=lab)
    ax2.set_title(f"上升段：10%→90% = {m['rise_ms']:.2f} ms"
                  f"（10% {m['t10_ms']:.2f} / 50% {m['t50_ms']:.2f} / "
                  f"90% {m['t90_ms']:.2f} ms），平均斜率 "
                  f"{m['slew_mv_per_ms']:.1f} mV/ms")
    ax2.set_xlabel("时间 (ms)")
    ax2.set_ylabel("电压 (V)")
    ax2.legend(fontsize=9, ncol=4)
    ax2.grid(alpha=0.3)

    ax3 = fig.add_subplot(gs[2])
    tv = seconds[m["i_vmax"]]
    span = (seconds[-1] - seconds[0]) * 0.5
    sel3 = (seconds >= tv - 0.1 * span) & (seconds <= tv + span)
    ax3.plot(seconds[sel3] * 1e3, volts[sel3], lw=1.1, color="#9467bd")
    ax3.axhline(TARGET_V, color="#2ca02c", lw=0.9, ls="--", alpha=0.7)
    ax3.set_title(f"峰值与建立：峰值 {m['vmax']:.2f} V，超调 "
                  f"{m['overshoot_pct']:.2f}%，{m['settle_ms']:.1f} ms 建立")
    ax3.set_xlabel("时间 (ms)")
    ax3.set_ylabel("电压 (V)")
    ax3.grid(alpha=0.3)

    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def main():
    print("=" * 70)
    print(f"上升沿自动取景  {TARGET_V}V  vdiv={VDIV} off={VOFF} level={TRIG_LEVEL}")
    print("=" * 70)
    sc = F.SCPI(F.HOST, F.PORT)
    result = {"status": "error", "action": "transient_rise"}
    try:
        sc.drain()
        sc.write(f":CHANnel{CH}:COUPling DC")
        sc.write(f":CHANnel{CH}:SCALe {VDIV}")
        sc.write(f":CHANnel{CH}:OFFSet {VOFF}")
        sc.write(f":CHANnel{CH}:BWLimit FULL")
        sc.write(":ACQuire:MDEP 1M")
        sc.write(f":WAVeform:SOURce C{CH}")
        sc.write(":WAVeform:WIDTh BYTE")
        time.sleep(1.0)

        print("[粗测] tdiv=50 ms 量斜坡时长", flush=True)
        v0, t0 = capture_rise(sc, 5e-2, -5e-3, "survey")
        m0 = analyze_rise(v0, t0)
        if m0 is None:
            raise RuntimeError("粗测未捕获到上升沿（信号未从 0 V 升起）")
        dur = (m0["t90_ms"] + m0["settle_ms"]) * 1e-3
        print(f"    斜坡≈{dur*1e3:.1f} ms（10→90% {m0['rise_ms']:.1f} ms，"
              f"建立 {m0['settle_ms']:.1f} ms）", flush=True)

        need = dur * 1.25
        tdiv = next((d for d in LEGAL_TDIV if d * 10 >= need), LEGAL_TDIV[-1])
        win = tdiv * 10
        print(f"[定时基] 需要 ≥{need*1e3:.1f} ms → tdiv={tdiv} （窗 {win*1e3:.0f} ms）",
              flush=True)

        print("[第 1 遍] 量上升起点位置", flush=True)
        v1, t1 = capture_rise(sc, tdiv, -PRE_FRAC * win, "pass1")
        m1 = analyze_rise(v1, t1)
        t_rise = float(t1[m1["i0"]])
        delay1 = -PRE_FRAC * win
        target = PRE_FRAC * win
        new_delay = delay1 + (t_rise - target)
        print(f"    起点在 {t_rise*1e3:+.2f} ms → delay {delay1:.6f}→{new_delay:.6f}s",
              flush=True)

        print("[第 2 遍] 平移取景", flush=True)
        v2, t2 = capture_rise(sc, tdiv, new_delay, "pass2")
        m2 = analyze_rise(v2, t2)
        if m2 is None:
            raise RuntimeError("定稿采集未捕获到上升沿")

        np.savez(os.path.join(F.OUT_DIR, "captured_rise.npz"), volts=v2, seconds=t2)
        plot_rise(v2, t2 - t2[m2["i0"]], m2, os.path.join(F.PKG_DIR, "rise_3v3.png"))

        result = {
            "status": "ok", "action": "transient_rise",
            "summary": (f"3.3V 开启：{m2['v_base']:.2f}V → {m2['v_top']:.2f}V，"
                        f"10%→90% = {m2['rise_ms']:.2f} ms，峰值 {m2['vmax']:.2f} V"
                        f"（超调 {m2['overshoot_pct']:.2f}%），"
                        f"{m2['settle_ms']:.1f} ms 建立"),
            "details": {"tdiv": tdiv, "delay_used": round(new_delay, 6),
                        "window_ms": round(float(t2[-1] - t2[0]) * 1e3, 2),
                        "npoints": int(len(v2)),
                        "metrics": {k: x for k, x in m2.items() if not k.startswith("i")}},
        }
    except (OSError, RuntimeError) as exc:
        result = {"status": "error", "action": "transient_rise",
                  "error": {"code": "capture_failed", "message": str(exc)}}
        try:
            F.jlink([f"w1 {F.ADDR_OUTPUT:#x} 0x01"])
        except Exception:
            pass
    finally:
        sc.close()

    with open(os.path.join(F.OUT_DIR, "captured_rise_result.json"), "w") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
    print("\n" + json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    sys.exit(main())
