"""上升沿采集（定稿：已验证的触发参数 + 两遍取景）

关键（全部实测）：
  - 垂直：vdiv=1V/div、offset=+0.5V（与下降沿同一套，已验证可读 0…3.3V）
  - 触发电平是**相对 offset** 的：绝对 0.5V → 设 0.0（设 1.65 会被钳到 2.05）
  - J-Link 脚本必须 `g`（resume），否则 CPU 停 halt、固件不跑、写入不生效
  - 写入 output=1 到实际起升有 ~0.5s 延迟，触发点常早于起升 → 必须两遍取景
  - MDEP=1M；Stop 后只发 PREamble?/DATA?；幅值用同帧 PAVA 两点标定
"""
import json
import os
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fall_keep as F  # noqa: E402

CH = F.CH
TARGET_V = 3.3
VDIV, VOFF = 1.0, 0.5
LEVEL_REL = 0.5 - VOFF          # 绝对 0.5V
PRE_FRAC = 0.03
LEGAL_TDIV = (1e-3, 2e-3, 5e-3, 1e-2, 2e-2, 5e-2, 1e-1, 2e-1, 5e-1, 1.0)


def write_output(on):
    path = os.path.join(F.OUT_DIR, "_w.jlink")
    with open(path, "w") as f:
        f.write(f"w1 {F.ADDR_OUTPUT:#x} 0x{1 if on else 0:02x}\ng\nq\n")
    r = subprocess.run([F.JLINK, "-device", "STM32F103C8", "-if", "SWD",
                        "-speed", "4000", "-autoconnect", "1",
                        "-CommanderScript", path],
                       capture_output=True, timeout=25)
    return "Script processing completed" in r.stdout.decode("utf-8", "replace")


def capture(sc, tdiv, delay, tag):
    print(f"  [{tag}] tdiv={tdiv} delay={delay:.6f}s", flush=True)
    write_output(False)
    time.sleep(1.5)

    sc.write(f":CHANnel{CH}:COUPling DC")
    sc.write(f":CHANnel{CH}:SCALe {VDIV}")
    sc.write(f":CHANnel{CH}:OFFSet {VOFF}")
    sc.write(f":CHANnel{CH}:BWLimit FULL")
    time.sleep(0.8)
    sc.write(f":TIMebase:SCALe {tdiv}")
    time.sleep(0.8)
    sc.write(f":TIMebase:DELay {delay}")
    time.sleep(0.8)

    sc.write(f":TRIGger:EDGE:SOURce C{CH}")
    sc.write(":TRIGger:EDGE:SLOPe RISing")
    sc.write(f":TRIGger:EDGE:LEVel {LEVEL_REL}")
    sc.write(":TRIGger:MODE AUTO")
    sc.write(":TRIGger:RUN")
    time.sleep(3.0)
    sc.write(":TRIGger:MODE SINGle")
    time.sleep(3.0)
    st = sc.status()
    print(f"       已武装 {st}", flush=True)
    if st.startswith("Stop"):
        sc.write(":TRIGger:MODE AUTO"); sc.write(":TRIGger:RUN")
        time.sleep(2.0)
        sc.write(":TRIGger:MODE SINGle")
        time.sleep(3.0)

    t0 = time.time()
    write_output(True)
    while time.time() - t0 < 20:
        if sc.status().startswith("Stop"):
            print(f"       已触发（{time.time()-t0:.2f}s）", flush=True)
            break
        time.sleep(0.2)

    p_min, p_max = sc.measure("MIN"), sc.measure("MAX")
    wf = sc.read_frame(VDIV, VOFF)
    v, t = wf["volts"], wf["seconds"]
    if not (np.isnan(p_min) or np.isnan(p_max)) and (v.max() - v.min()) > 0.05:
        gain = (p_max - p_min) / (v.max() - v.min())
        v = v * gain + (p_min - v.min() * gain)
    print(f"       窗={wf['window_s']*1e3:.1f}ms V=[{v.min():.2f},{v.max():.2f}] "
          f"PAVA[{p_min:.2f},{p_max:.2f}]", flush=True)
    return v, t, wf


def rise_metrics(volts, seconds):
    n = len(volts)
    v_top = float(np.median(volts[-max(200, n // 10):]))
    v_base = float(np.median(volts[:max(200, n // 40)]))
    span = v_top - v_base
    if span < 1.0:
        return None
    thr = v_base + 0.05 * span
    i0 = 0
    for k in range(n):
        if volts[k] <= thr:
            i0 = k

    def cross(frac):
        lv = v_base + frac * span
        idx = np.where(volts[i0:] >= lv)[0]
        return i0 + int(idx[0]) if len(idx) else n - 1

    i10, i50, i90 = cross(0.1), cross(0.5), cross(0.9)
    vmax = float(volts.max())
    i_vmax = int(np.argmax(volts))
    # 建立时间必须在**中值平滑后**判：开关纹波尖峰一直超带宽，否则永不收敛
    dt = float(seconds[1] - seconds[0])
    k = max(1, int(round(1e-3 / dt)))
    nb = n // k
    if nb >= 8:
        sm = np.median(volts[:nb * k].reshape(nb, k), axis=1)
        ts = seconds[:nb * k:k] + dt * k / 2
    else:
        sm, ts = volts, seconds
    band = 0.02 * abs(span)
    i_set = len(sm) - 1
    for j in range(len(sm) - 1, -1, -1):
        if abs(sm[j] - v_top) > band:
            i_set = min(j + 1, len(sm) - 1)
            break
    settle_s = float(ts[i_set] - seconds[i0])
    return {
        "v_base": round(v_base, 3), "v_top": round(v_top, 3),
        "vmax": round(vmax, 3),
        "overshoot_pct": round((vmax - v_top) / span * 100, 2),
        "t10_ms": round(float(seconds[i10] - seconds[i0]) * 1e3, 3),
        "t50_ms": round(float(seconds[i50] - seconds[i0]) * 1e3, 3),
        "t90_ms": round(float(seconds[i90] - seconds[i0]) * 1e3, 3),
        "rise_ms": round(float(seconds[i90] - seconds[i10]) * 1e3, 3),
        "settle_ms": round(settle_s * 1e3, 2),
        "slew_v_per_ms": round(span / max(1e-9, float(seconds[i90] - seconds[i10])), 3),
        "i0": i0, "i_vmax": i_vmax, "i_set": i_set,
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
            label=f"起升 {volts[m['i0']]:.2f} V")
    ax.plot(t_ms[m["i_vmax"]], volts[m["i_vmax"]], "^", ms=9, color="#9467bd",
            label=f"峰值 {m['vmax']:.2f} V")
    ax.set_ylim(min(volts.min(), 0) - 0.35, volts.max() + 0.35)
    ax.set_xlabel("时间 (ms)")
    ax.set_ylabel("电压 (V)")
    ax.set_title(f"3.3 V 输出开启（上升沿）　记录窗 "
                 f"{(seconds[-1]-seconds[0])*1e3:.1f} ms")
    ax.legend(loc="lower right", fontsize=9)
    ax.grid(alpha=0.3)

    ax2 = fig.add_subplot(gs[1])
    t0 = seconds[m["i0"]]
    win = (seconds[-1] - seconds[0])
    sel = (seconds >= t0 - 0.02 * win) & (seconds <= t0 + 0.5 * win)
    ax2.plot(seconds[sel] * 1e3, volts[sel], lw=1.3, color="#1f77b4")
    ax2.axhline(0, color="#888", lw=0.8)
    for frac, lab, col in ((0.1, "10%", "#2ca02c"), (0.5, "50%", "#1f77b4"),
                           (0.9, "90%", "#ff7f0e")):
        ax2.axhline(m["v_base"] + frac * (m["v_top"] - m["v_base"]),
                    color=col, lw=0.9, ls=":", alpha=0.8, label=lab)
    ax2.set_title(f"上升段：10%→90% = {m['rise_ms']:.2f} ms"
                  f"（10% {m['t10_ms']:.2f} / 50% {m['t50_ms']:.2f} / "
                  f"90% {m['t90_ms']:.2f} ms），斜率 {m['slew_v_per_ms']:.1f} V/ms")
    ax2.set_ylim(min(volts.min(), 0) - 0.35, volts.max() + 0.35)
    ax2.set_xlabel("时间 (ms)")
    ax2.set_ylabel("电压 (V)")
    ax2.legend(fontsize=9, ncol=4)
    ax2.grid(alpha=0.3)

    ax3 = fig.add_subplot(gs[2])
    tv = seconds[m["i_vmax"]]
    sel3 = (seconds >= tv - 0.15 * win) & (seconds <= tv + 0.85 * win)
    ax3.plot(seconds[sel3] * 1e3, volts[sel3], lw=1.1, color="#9467bd")
    ax3.axhline(TARGET_V, color="#2ca02c", lw=0.9, ls="--", alpha=0.7)
    ax3.set_title(f"峰值与建立：峰值 {m['vmax']:.2f} V，超调 "
                  f"{m['overshoot_pct']:.2f}%，{m['settle_ms']:.2f} ms 建立")
    ax3.set_ylim(min(volts.min(), 0) - 0.35, volts.max() + 0.35)
    ax3.set_xlabel("时间 (ms)")
    ax3.set_ylabel("电压 (V)")
    ax3.grid(alpha=0.3)

    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def main():
    print("=" * 70)
    print(f"上升沿  {TARGET_V}V  vdiv={VDIV} off={VOFF} level_rel={LEVEL_REL}")
    print("=" * 70)
    sc = F.SCPI(F.HOST, F.PORT)
    result = {"status": "error", "action": "transient_rise"}
    try:
        sc.drain()
        sc.write(":ACQuire:MDEP 1M")
        sc.write(f":WAVeform:SOURce C{CH}")
        sc.write(":WAVeform:WIDTh BYTE")
        time.sleep(0.8)

        print("[粗测] 1 s 窗量起升位置与时长", flush=True)
        v1, t1, wf1 = capture(sc, 1e-1, -5e-2, "survey")
        m1 = rise_metrics(v1, t1)
        if m1 is None:
            raise RuntimeError("粗测未捕获上升（帧内未从 0 升到 3.3V）")
        dur = (m1["t90_ms"] + m1["settle_ms"]) * 1e-3
        t_rise = float(t1[m1["i0"]])
        print(f"    起升 {t_rise*1e3:+.1f} ms，10→90% {m1['rise_ms']:.2f} ms，"
              f"建立 {m1['settle_ms']:.2f} ms", flush=True)

        need = max(dur * 1.35, 3e-3)
        tdiv = next((d for d in LEGAL_TDIV if d * 10 >= need), LEGAL_TDIV[-1])
        win = tdiv * 10
        print(f"[定时基] 需要 ≥{need*1e3:.1f} ms → tdiv={tdiv}（窗 {win*1e3:.1f} ms）",
              flush=True)

        # 触发点比真实起升早约 0.5 s（J-Link 写入→固件起升的延迟），
        # 所以必须：宽窗定位起升位置 → 用该位置算窄窗 delay → 抓不到就重测重试
        v3 = t3 = None
        new_delay = None
        for attempt in range(4):
            print(f"[定位 {attempt+1}] 1 s 窗量起升位置", flush=True)
            vs, ts_, _ = capture(sc, 1e-1, -5e-2, f"locate{attempt+1}")
            ms = rise_metrics(vs, ts_)
            if ms is None:
                print("    未定位到起升，重试", flush=True)
                continue
            t_rise = float(ts_[ms["i0"]])
            new_delay = t_rise - PRE_FRAC * win
            print(f"    起升在触发后 {t_rise*1e3:.1f} ms → 窄窗 delay={new_delay:.6f}s",
                  flush=True)

            v3, t3, _ = capture(sc, tdiv, new_delay, f"frame{attempt+1}")
            m3 = rise_metrics(v3, t3)
            if m3 is not None:
                break
            print("    窄窗未命中（延迟有抖动），重测", flush=True)
            v3 = t3 = m3 = None
        if v3 is None or m3 is None:
            raise RuntimeError("多次尝试仍未在窄窗内命中上升沿")

        np.savez(os.path.join(F.OUT_DIR, "captured_rise.npz"), volts=v3, seconds=t3)
        plot_rise(v3, t3 - t3[m3["i0"]], m3,
                  os.path.join(F.PKG_DIR, "rise_3v3.png"))

        result = {
            "status": "ok", "action": "transient_rise",
            "summary": (f"3.3V 开启：{m3['v_base']:.2f}V → {m3['v_top']:.2f}V，"
                        f"10%→90% = {m3['rise_ms']:.2f} ms，峰值 {m3['vmax']:.2f} V"
                        f"（超调 {m3['overshoot_pct']:.2f}%），"
                        f"{m3['settle_ms']:.2f} ms 建立"),
            "details": {"tdiv": tdiv, "delay_used": round(new_delay, 6),
                        "window_ms": round(float(t3[-1] - t3[0]) * 1e3, 2),
                        "npoints": int(len(v3)),
                        "metrics": {k: x for k, x in m3.items() if not k.startswith("i")}},
        }
    except (OSError, RuntimeError) as exc:
        result = {"status": "error", "action": "transient_rise",
                  "error": {"code": "capture_failed", "message": str(exc)}}
        try:
            write_output(True)
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
