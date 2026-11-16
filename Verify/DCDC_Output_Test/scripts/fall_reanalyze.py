"""下降沿重新分析 + 出图（用已保存的 captured_fall.npz，不重新采集）

修正点：下降起点必须锚定在**最陡跌落**处往前找，不能取"第一个 ≤ 0.9×v0 的点"
（纹波会先命中，导致起点落在稳态里，后续时间指标全部虚高）。
"""
import json
import os

import numpy as np

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
PKG_DIR = os.path.normpath(os.path.join(OUT_DIR, "..", "PKG"))
TARGET_V = 3.3


def analyze_fall(volts, seconds):
    n = len(volts)
    dv = np.diff(volts) / np.diff(seconds)

    # 纹波边沿斜率与真实跌落同量级（−87 vs −94 V/ms），不能靠最陡点定位。
    # 改用最低点回溯：先找全局最低（负压过冲），再往前找到仍处稳态的位置。
    i_vmin = int(np.argmin(volts))
    v_final = float(np.median(volts[-max(200, n // 20):]))
    v_top = float(np.median(volts[max(0, i_vmin - int(0.3 * n)):max(10, i_vmin - int(0.05 * n))]))
    i0 = i_vmin
    for k in range(i_vmin, max(0, i_vmin - int(0.3 * n)), -1):
        if volts[k] >= v_top * 0.98:
            i0 = k
            break
    i_pk = int(np.argmin(dv[i0:i_vmin])) + i0 if i_vmin > i0 else i0
    span = v_top - v_final

    def cross(frac):
        level = v_top - frac * span
        idx = np.where(volts[i0:] <= level)[0]
        return i0 + int(idx[0]) if len(idx) else n - 1

    i10, i50, i90 = cross(0.1), cross(0.5), cross(0.9)
    i_end = i50
    for k in range(i_pk, n):
        if abs(dv[k]) < abs(dv[i_pk]) * 0.1:
            i_end = k
            break

    vmin = float(volts.min())

    # LC 周期：最低点之后同向过零间隔
    zc = [i for i in range(max(1, i_vmin), n)
          if (volts[i - 1] < 0 <= volts[i]) or (volts[i - 1] >= 0 > volts[i])]
    same = zc[::2]
    period_ms = (float(seconds[same[1]] - seconds[same[0]]) * 1e3) if len(same) >= 2 else 0.0

    # 归零：相对 v_final，偏差进入 ±2%·span 并保持
    band = 0.02 * abs(span)
    i_set = n - 1
    for k in range(i_vmin, n):
        if abs(volts[k] - v_final) > band:
            i_set = k
    i_set = min(i_set + 1, n - 1)

    return {
        "v_top": round(v_top, 3),
        "v_final": round(v_final, 3),
        "vmin": round(vmin, 3),
        "undershoot_v": round(min(vmin, 0.0), 3),
        "t90_ms": round(float(seconds[i90] - seconds[i0]) * 1e3, 2),
        "t50_ms": round(float(seconds[i50] - seconds[i0]) * 1e3, 2),
        "t10_ms": round(float(seconds[i10] - seconds[i0]) * 1e3, 2),
        "steep_ms": round(float(seconds[i_end] - seconds[i0]) * 1e3, 2),
        "max_slew_v_per_ms": round(abs(float(dv[i_pk])) / 1e3, 2),
        "osc_period_ms": round(period_ms, 2),
        "osc_freq_hz": round(1000.0 / period_ms, 1) if period_ms > 0 else 0.0,
        "settle_ms": round(float(seconds[i_set] - seconds[i0]) * 1e3, 1),
        "i0": i0, "i_pk": i_pk, "i_vmin": i_vmin, "i_set": i_set,
    }


def plot(volts, seconds, m, path):
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
    ax.plot(t_ms[::step], volts[::step], lw=0.8, color="#d62728")
    ax.axhline(0, color="#888", lw=0.8, alpha=0.6)
    ax.axhline(TARGET_V, color="#2ca02c", lw=0.9, ls="--", alpha=0.7,
               label=f"目标 {TARGET_V} V")
    ax.plot(t_ms[m["i0"]], volts[m["i0"]], "o", ms=8, color="#ff7f0e",
            label=f"关断起点 {volts[m['i0']]:.2f} V")
    ax.plot(t_ms[m["i_vmin"]], volts[m["i_vmin"]], "v", ms=9, color="#9467bd",
            label=f"最低 {m['vmin']:.2f} V")
    ax.set_xlabel("时间 (ms)")
    ax.set_ylabel("电压 (V)")
    ax.set_title(f"3.3 V 输出关断（下降沿）　记录窗 {seconds[-1]-seconds[0]:.2f} s")
    ax.legend(loc="upper right", fontsize=9)
    ax.grid(alpha=0.3)

    # 陡降段：起点前后 ±15 ms
    ax2 = fig.add_subplot(gs[1])
    t0 = seconds[m["i0"]]
    sel = (seconds >= t0 - 0.005) & (seconds <= t0 + 0.030)
    ax2.plot(seconds[sel] * 1e3, volts[sel], lw=1.4, color="#d62728")
    ax2.axhline(0, color="#888", lw=0.8)
    for frac, lab, col in ((0.9, "90%", "#ff7f0e"), (0.5, "50%", "#1f77b4"),
                           (0.1, "10%", "#2ca02c")):
        ax2.axhline(m["v_top"] - frac * (m["v_top"] - m["v_final"]),
                    color=col, lw=0.9, ls=":", alpha=0.8, label=lab)
    ax2.set_title(f"陡降段：{m['steep_ms']:.2f} ms，最大斜率 "
                  f"{m['max_slew_v_per_ms']:.1f} V/ms，"
                  f"50% @ {m['t50_ms']:.2f} ms")
    ax2.set_xlabel("时间 (ms)")
    ax2.set_ylabel("电压 (V)")
    ax2.legend(fontsize=9, ncol=4)
    ax2.grid(alpha=0.3)

    # LC 振荡：最低点前后
    ax3 = fig.add_subplot(gs[2])
    tv = seconds[m["i_vmin"]]
    sel3 = (seconds >= tv - 0.006) & (seconds <= tv + 0.025)
    ax3.plot(seconds[sel3] * 1e3, volts[sel3], lw=1.1, color="#9467bd")
    ax3.axhline(0, color="#888", lw=0.8)
    ax3.set_title(f"负压过冲 / LC 振荡：最低 {m['vmin']:.2f} V，"
                  f"周期 {m['osc_period_ms']:.2f} ms（{m['osc_freq_hz']:.0f} Hz）")
    ax3.set_xlabel("时间 (ms)")
    ax3.set_ylabel("电压 (V)")
    ax3.grid(alpha=0.3)

    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def main():
    d = np.load(os.path.join(OUT_DIR, "captured_fall.npz"))
    volts, seconds = d["volts"], d["seconds"]
    m = analyze_fall(volts, seconds)
    plot(volts, seconds, m, os.path.join(PKG_DIR, "fall_3v3.png"))

    summary = (f"3.3V 关断：{m['v_top']:.2f}V → 最低 {m['vmin']:.2f}V，"
               f"90% {m['t90_ms']:.2f} ms / 50% {m['t50_ms']:.2f} ms / "
               f"10% {m['t10_ms']:.2f} ms，最大斜率 {m['max_slew_v_per_ms']:.0f} V/ms，"
               f"LC {m['osc_period_ms']:.2f} ms（{m['osc_freq_hz']:.0f} Hz），"
               f"{m['settle_ms']:.0f} ms 归零")
    out = {"status": "ok", "action": "transient_fall", "summary": summary,
           "details": {"window_s": float(seconds[-1] - seconds[0]),
                       "npoints": int(len(volts)),
                       "metrics": {k: v for k, v in m.items() if not k.startswith("i")}}}
    with open(os.path.join(OUT_DIR, "captured_fall_result.json"), "w") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)
    print(summary)
    print(json.dumps(out["details"]["metrics"], ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
