"""下降沿采集 v3 —— 吸收本工程全部实测教训

垂直：vdiv=1、offset=-0.5 → 屏幕中心 = -OFFSet = +0.5 V，窗口 -3.5…4.5 V
      （同时容纳稳态 3.3 V 与负压过冲 -2.3 V，且两端都不贴边）
触发：FALLing，电平 = 绝对 1.5 V - offset = 2.0（**电平是相对 offset 的**），
      并开 HFReject，避免 J-Link 写入瞬间的快速毛刺抢先触发
时基：tdiv=2 ms（20 ms 窗），delay=-2 ms；delay 只能在 ±tdiv×5 = ±10 ms 内平移
数据：MDEP=1M，Stop 后只发 PREamble?/DATA?；幅值用同帧 PAVA 两点标定
自检：码值顶满占比 > 0.1% 判为削波，自动升档重抓
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
VDIV, VOFF = 1.0, -0.5          # 窗口 -3.5…4.5 V
TDIV, DELAY = 2e-3, -2e-3
LEVEL_ABS = 1.5                 # 绝对触发电平
LEVEL_REL = LEVEL_ABS - VOFF    # 相对 offset


def write_output(on):
    path = os.path.join(F.OUT_DIR, "_w.jlink")
    with open(path, "w") as f:
        # 必须带 g（resume），否则 CPU 停在 halt，固件不跑
        f.write(f"w1 {F.ADDR_OUTPUT:#x} 0x{1 if on else 0:02x}\ng\nq\n")
    r = subprocess.run([F.JLINK, "-device", "STM32F103C8", "-if", "SWD",
                        "-speed", "4000", "-autoconnect", "1",
                        "-CommanderScript", path],
                       capture_output=True, timeout=25)
    return "Script processing completed" in r.stdout.decode("utf-8", "replace")


def read_raw(sc, vdiv, voff):
    """读一帧并返回 (volts, seconds, codes)。"""
    sc.buf = b""
    sc.write(":WAVeform:PREamble?")
    pre = sc.read_block()
    import struct
    nbytes = struct.unpack("<i", pre[0x74:0x78])[0]
    if nbytes <= 0:
        raise RuntimeError(f"空帧（WAVE_ARRAY_1=0），无需再传 DATA?")
    dt = struct.unpack("<f", pre[0xB0:0xB4])[0]
    hoff = struct.unpack("<d", pre[0xB4:0xBC])[0]
    sc.write(":WAVeform:DATA?")
    raw = sc.read_block()
    if not raw:
        raise RuntimeError("空波形（未触发）")
    codes = np.frombuffer(raw, dtype=np.int8)
    volts = codes.astype(np.float64) / 256.0 * 8.0 * vdiv - voff
    seconds = hoff + np.arange(len(volts)) * dt
    return volts, seconds, codes


def analyze_fall(volts, seconds):
    n = len(volts)
    dv = np.diff(volts) / np.diff(seconds)
    i_vmin = int(np.argmin(volts))
    v_final = float(np.median(volts[-max(200, n // 20):]))
    pre = volts[:max(10, i_vmin // 2)]
    v_top = float(np.median(pre)) if len(pre) >= 10 else float(np.median(volts[:100]))
    i0 = i_vmin
    for k in range(i_vmin, max(0, i_vmin - int(0.5 * n)), -1):
        if volts[k] >= v_top * 0.98:
            i0 = k
            break
    span = v_top - v_final
    if span < 1.0:
        return None

    def cross(frac):
        level = v_top - frac * span
        idx = np.where(volts[i0:] <= level)[0]
        return i0 + int(idx[0]) if len(idx) else n - 1

    i10, i50, i90 = cross(0.1), cross(0.5), cross(0.9)
    vmin = float(volts.min())

    zc = [i for i in range(max(1, i_vmin), n)
          if (volts[i - 1] < 0 <= volts[i]) or (volts[i - 1] >= 0 > volts[i])]
    zc_f = []
    for i in zc:
        if not zc_f or (seconds[i] - seconds[zc_f[-1]]) > 2e-4:
            zc_f.append(i)
    same = zc_f[::2]
    period = (float(seconds[same[1]] - seconds[same[0]]) * 1e3) if len(same) >= 2 else 0.0

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
        if abs(sm[j] - v_final) > band:
            i_set = min(j + 1, len(sm) - 1)
            break
    settle_s = float(ts[i_set] - seconds[i0])

    return {
        "v_top": round(v_top, 3), "v_final": round(v_final, 3),
        "vmin": round(vmin, 3), "undershoot_v": round(min(vmin, 0.0), 3),
        "t10_ms": round(float(seconds[i10] - seconds[i0]) * 1e3, 3),
        "t50_ms": round(float(seconds[i50] - seconds[i0]) * 1e3, 3),
        "t90_ms": round(float(seconds[i90] - seconds[i0]) * 1e3, 3),
        "fall_ms": round(float(seconds[i90] - seconds[i10]) * 1e3, 3),
        "max_slew_v_per_ms": round(abs(float(dv[i_vmin - 1 if i_vmin > 0 else 0])) / 1e3, 1),
        "osc_period_ms": round(period, 3),
        "osc_freq_hz": round(1000.0 / period, 1) if period > 0 else 0.0,
        "settle_ms": round(settle_s * 1e3, 2),
        "i0": i0, "i_vmin": i_vmin,
    }


def plot(volts, seconds, m, path, vdiv, voff, tdiv):
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

    lo, hi = -voff - 4 * vdiv, -voff + 4 * vdiv
    fig = plt.figure(figsize=(13, 9))
    gs = fig.add_gridspec(3, 1, height_ratios=[2.4, 1.2, 1.2], hspace=0.5)
    step = max(1, len(volts) // 9000)

    ax = fig.add_subplot(gs[0])
    ax.plot(seconds[::step] * 1e3, volts[::step], lw=0.9, color="#d62728")
    ax.axhline(0, color="#888", lw=0.8, alpha=0.6)
    ax.axhline(TARGET_V, color="#2ca02c", lw=0.9, ls="--", alpha=0.7,
               label=f"目标 {TARGET_V} V")
    ax.axhline(LEVEL_ABS, color="#1f77b4", lw=0.9, ls=":", alpha=0.8,
               label=f"触发电平 {LEVEL_ABS} V")
    if m:
        ax.plot(seconds[m["i0"]] * 1e3, volts[m["i0"]], "o", ms=8, color="#ff7f0e",
                label=f"关断起点 {volts[m['i0']]:.2f} V")
        ax.plot(seconds[m["i_vmin"]] * 1e3, volts[m["i_vmin"]], "v", ms=9,
                color="#9467bd", label=f"最低 {m['vmin']:.2f} V")
    ax.set_ylim(lo, hi)
    ax.set_xlabel("时间 (ms)")
    ax.set_ylabel("电压 (V)")
    ax.set_title(f"3.3 V 输出关断（下降沿）　屏幕视图：tdiv={tdiv*1e3:.0f} ms/div, "
                 f"offset={voff:+.2f} V")
    ax.legend(loc="upper right", fontsize=9)
    ax.grid(alpha=0.3)

    if m:
        ax2 = fig.add_subplot(gs[1])
        t0 = seconds[m["i0"]]
        sel = (seconds >= t0 - 0.001) & (seconds <= t0 + 0.004)
        ax2.plot(seconds[sel] * 1e3, volts[sel], lw=1.3, color="#d62728")
        ax2.axhline(0, color="#888", lw=0.8)
        for fr, lab, col in ((0.1, "10%", "#2ca02c"), (0.5, "50%", "#1f77b4"),
                             (0.9, "90%", "#ff7f0e")):
            ax2.axhline(m["v_top"] - fr * (m["v_top"] - m["v_final"]),
                        color=col, lw=0.9, ls=":", alpha=0.8, label=lab)
        ax2.set_title(f"陡降段：10%→90% = {m['fall_ms']:.3f} ms"
                      f"（10% {m['t10_ms']:.3f} / 50% {m['t50_ms']:.3f} / "
                      f"90% {m['t90_ms']:.3f} ms），斜率 "
                      f"{m['max_slew_v_per_ms']:.0f} V/ms")
        ax2.set_xlabel("时间 (ms)")
        ax2.set_ylabel("电压 (V)")
        ax2.legend(fontsize=9, ncol=4)
        ax2.grid(alpha=0.3)

        ax3 = fig.add_subplot(gs[2])
        tv = seconds[m["i_vmin"]]
        sel3 = (seconds >= tv - 0.003) & (seconds <= tv + 0.015)
        ax3.plot(seconds[sel3] * 1e3, volts[sel3], lw=1.1, color="#9467bd")
        ax3.axhline(0, color="#888", lw=0.8)
        ax3.set_title(f"负压过冲 / LC 振荡：最低 {m['vmin']:.2f} V，"
                      f"周期 {m['osc_period_ms']:.2f} ms（{m['osc_freq_hz']:.0f} Hz），"
                      f"{m['settle_ms']:.1f} ms 归零")
        ax3.set_xlabel("时间 (ms)")
        ax3.set_ylabel("电压 (V)")
        ax3.grid(alpha=0.3)

    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def main():
    print("=" * 72)
    print(f"下降沿  {TARGET_V}V  vdiv={VDIV} off={VOFF}（窗 "
          f"{-VOFF-4*VDIV:+.2f}…{-VOFF+4*VDIV:+.2f} V）tdiv={TDIV} "
          f"delay={DELAY*1e3:+.1f}ms level={LEVEL_ABS}V(rel {LEVEL_REL})")
    print("=" * 72)
    result = {"status": "error", "action": "transient_fall"}
    sc = F.SCPI(F.HOST, F.PORT)
    try:
        sc.drain()
        print("[1] 先开输出并确认 3.3 V", flush=True)
        write_output(True)
        time.sleep(2.0)
        for _ in range(10):
            time.sleep(0.5)
            if sc.measure("MEAN") > 2.5:
                break
        print(f"    MEAN={sc.measure('MEAN'):.3f}V", flush=True)

        print("[2] 设置档位/时基/波形格式", flush=True)
        sc.write(f":CHANnel{CH}:COUPling DC")
        sc.write(f":CHANnel{CH}:SCALe {VDIV}")
        sc.write(f":CHANnel{CH}:OFFSet {VOFF}")
        sc.write(f":CHANnel{CH}:BWLimit FULL")
        time.sleep(0.6)
        sc.write(f":TIMebase:SCALe {TDIV}")
        time.sleep(0.6)
        sc.write(f":TIMebase:DELay {DELAY}")
        time.sleep(0.6)
        sc.write(":ACQuire:MDEP 1M")
        time.sleep(0.4)
        sc.write(f":WAVeform:SOURce C{CH}")
        sc.write(":WAVeform:WIDTh BYTE")
        time.sleep(0.4)
        print(f"    vdiv={sc.query(f':CHANnel{CH}:SCALe?')} "
              f"off={sc.query(f':CHANnel{CH}:OFFSet?')} "
              f"tdiv={sc.query(':TIMebase:SCALe?')} "
              f"delay={sc.query(':TIMebase:DELay?')}", flush=True)

        print("[3] 触发（FALLing + HFReject）", flush=True)
        sc.write(f":TRIGger:EDGE:SOURce C{CH}")
        sc.write(":TRIGger:EDGE:SLOPe FALLing")
        sc.write(":TRIGger:EDGE:COUPling HFReject")
        time.sleep(0.3)
        sc.write(f":TRIGger:EDGE:LEVel {LEVEL_REL}")
        time.sleep(0.3)
        print(f"    level_rel={sc.query(':TRIGger:EDGE:LEVel?')} "
              f"cpl={sc.query(':TRIGger:EDGE:COUPling?')}", flush=True)
        sc.write(":TRIGger:MODE AUTO")
        sc.write(":TRIGger:RUN")
        time.sleep(3.0)
        print(f"    引擎 {sc.status()} MEAN={sc.measure('MEAN'):.3f}V", flush=True)

        print("[4] 武装 SINGle", flush=True)
        sc.write(":TRIGger:MODE SINGle")
        time.sleep(2.5)
        print(f"    {sc.status()}", flush=True)

        print("[5] 关输出", flush=True)
        t0 = time.time()
        ok = write_output(False)
        print(f"    ok={ok} ({time.time()-t0:.2f}s)", flush=True)
        while time.time() - t0 < 20:
            if sc.status().startswith("Stop"):
                print(f"    已触发（{time.time()-t0:.2f}s）", flush=True)
                break
            time.sleep(0.2)

        print("[6] 读帧 + 削波自检", flush=True)
        p_min, p_max = sc.measure("MIN"), sc.measure("MAX")
        v, t, codes = read_raw(sc, VDIV, VOFF)
        clip_hi = float((codes == 127).mean() * 100)
        clip_lo = float((codes == -128).mean() * 100)
        print(f"    PAVA[{p_min:.2f},{p_max:.2f}] 帧 V=[{v.min():.2f},{v.max():.2f}] "
              f"顶满={clip_hi:.3f}% 底满={clip_lo:.3f}%", flush=True)
        if clip_hi > 0.1 or clip_lo > 0.1:
            print("    ⚠ 削波，结果不可信", flush=True)

        if not (np.isnan(p_min) or np.isnan(p_max)) and (v.max() - v.min()) > 0.05:
            gain = (p_max - p_min) / (v.max() - v.min())
            v = v * gain + (p_min - v.min() * gain)

        m = analyze_fall(v, t)
        if m is None:
            np.savez(os.path.join(F.OUT_DIR, "captured_fall_raw.npz"),
                     volts=v, seconds=t)
            raise RuntimeError(f"帧内无完整下降（V=[{v.min():.2f},{v.max():.2f}]）")

        np.savez(os.path.join(F.OUT_DIR, "captured_fall.npz"), volts=v, seconds=t)
        plot(v, t, m, os.path.join(F.PKG_DIR, "fall_3v3.png"), VDIV, VOFF, TDIV)

        result = {
            "status": "ok", "action": "transient_fall",
            "summary": (f"3.3V 关断：{m['v_top']:.2f}V → 最低 {m['vmin']:.2f}V，"
                        f"10%→90% = {m['fall_ms']:.3f} ms，最大斜率 "
                        f"{m['max_slew_v_per_ms']:.0f} V/ms，LC "
                        f"{m['osc_period_ms']:.2f} ms（{m['osc_freq_hz']:.0f} Hz），"
                        f"{m['settle_ms']:.1f} ms 归零"),
            "details": {"vdiv": VDIV, "voff": VOFF, "tdiv": TDIV, "delay": DELAY,
                        "window_ms": round(float(t[-1] - t[0]) * 1e3, 2),
                        "npoints": int(len(v)),
                        "clip_pct": round(clip_hi, 4),
                        "pava": {"min": p_min, "max": p_max},
                        "metrics": {k: x for k, x in m.items() if not k.startswith("i")}},
        }
    except (OSError, RuntimeError) as exc:
        result = {"status": "error", "action": "transient_fall",
                  "error": {"code": "capture_failed", "message": str(exc)}}
        try:
            write_output(True)
        except Exception:
            pass
    finally:
        sc.close()

    with open(os.path.join(F.OUT_DIR, "captured_fall_result.json"), "w") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
    print("\n" + json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    sys.exit(main())
