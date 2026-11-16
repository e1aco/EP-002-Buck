"""下降沿采集（自动取景两遍法）

问题：触发点常比真实跌落早几毫秒（J-Link 退出瞬间的小扰动抢先触发），
导致窗口左侧一大段稳态、主体只占右半屏。

做法（按"平移示波器"的思路）：
  第 1 遍：按 delay=-0.5 ms 抓一帧，量出跌落起点在窗口里的位置 t_fall；
  平移：delay += (t_fall - 0.05×窗口)，让跌落起点落在左侧 5%；
  第 2 遍：用新 delay 重抓，主体（跌落 + 振荡衰减）占满屏幕。

其余约束与 fall_keep.py 相同：参数全在武装前设好；Stop 后只发 PREamble?/DATA?；
MDEP=1M 保证整段回传；幅值用同帧 PAVA 两点标定；抓完保持 Stop。
"""
import json
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fall_keep as F  # noqa: E402

CH = F.CH
TARGET_V = F.TARGET_V
VDIV, VOFF = F.VDIV, F.VOFF
TDIV = 1e-3          # 10 ms 窗口：装下完整振荡（±2% 判据约 8 ms）
FIRST_DELAY = -5e-4
LEVEL_NEAR = 3.1     # 贴近信号，让引擎跑起来
TRIG_LEVEL = 1.5
PRE_FRAC = 0.02      # 跌落起点落在窗口 2% 处（只留极短基线）


def wait_running(sc, tries=25):
    for _ in range(tries):
        time.sleep(0.6)
        if sc.status().startswith(("Auto", "Trig")):
            return True
    return False


LAST_CALIB = [1.0, 0.0]


def capture_once(sc, delay, tag):
    print(f"  [{tag}] tdiv={TDIV} delay={delay:.6f}s", flush=True)
    sc.write(f":TIMebase:SCALe {TDIV}")
    time.sleep(0.6)
    sc.write(f":TIMebase:DELay {delay}")
    time.sleep(0.6)

    sc.write(f":TRIGger:EDGE:LEVel {LEVEL_NEAR}")
    sc.write(":TRIGger:MODE AUTO")
    sc.write(":TRIGger:RUN")
    if not wait_running(sc):
        raise RuntimeError(f"引擎未运行（{sc.status()}）")
    print(f"       引擎就绪 {sc.status()} 电压={sc.measure('MEAN'):.3f}V", flush=True)

    sc.write(f":TRIGger:EDGE:LEVel {TRIG_LEVEL}")
    time.sleep(1.5)

    proc = F.output_off_delayed(2500)
    time.sleep(1.5)
    sc.write(":TRIGger:MODE SINGle")
    time.sleep(1.0)
    print(f"       已武装 {sc.status()}", flush=True)

    t0 = time.time()
    while time.time() - t0 < 25:
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
        offc = p_min - v.min() * gain
        LAST_CALIB[0], LAST_CALIB[1] = gain, offc
    else:
        gain, offc = LAST_CALIB
        print(f"       PAVA 无效，沿用上一遍标定 gain={gain:.4f} off={offc:+.4f}", flush=True)
    v = v * gain + offc
    print(f"       {wf['npoints']} 点 窗={wf['window_s']*1e3:.2f}ms "
          f"V=[{v.min():.2f},{v.max():.2f}] PAVA MIN={p_min:.2f}", flush=True)

    # 恢复固件输出（示波器保持 Stop）
    F.jlink([f"w1 {F.ADDR_OUTPUT:#x} 0x01"])
    time.sleep(2.5)
    return v, t


def main():
    print("=" * 70)
    print(f"下降沿自动取景  {TARGET_V}V  tdiv={TDIV}s  窗口={TDIV*10*1e3:.1f}ms")
    print("=" * 70)
    sc = F.SCPI(F.HOST, F.PORT)
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

        print("[第 1 遍] 量跌落在窗口中的位置", flush=True)
        v1, t1 = capture_once(sc, FIRST_DELAY, "pass1")
        m1 = F.analyze(v1, t1)
        t_fall = float(t1[m1["i0"]])
        win = float(t1[-1] - t1[0])
        print(f"    跌落起点在窗口 {t_fall*1e3:+.3f} ms（窗口 {win*1e3:.2f} ms）", flush=True)

        target = PRE_FRAC * win
        new_delay = FIRST_DELAY + (t_fall - target)
        print(f"[平移] delay {FIRST_DELAY:.6f} → {new_delay:.6f}s "
              f"（把跌落起点移到 {target*1e3:.2f} ms）", flush=True)

        print("[第 2 遍] 按新 delay 重抓", flush=True)
        v2, t2 = capture_once(sc, new_delay, "pass2")
        m2 = F.analyze(v2, t2)

        np.savez(os.path.join(F.OUT_DIR, "captured_fall.npz"), volts=v2, seconds=t2)
        # 横轴以关断时刻为零点，主体自然铺满
        F.plot(v2, t2 - t2[m2["i0"]], m2, os.path.join(F.PKG_DIR, "fall_3v3.png"))

        result = {
            "status": "ok", "action": "transient_fall",
            "summary": (f"3.3V 关断：{m2['v_top']:.2f}V → 最低 {m2['vmin']:.2f}V，"
                        f"10/50/90% = {m2['t10_ms']:.2f}/{m2['t50_ms']:.2f}/"
                        f"{m2['t90_ms']:.2f} ms（陡降 {m2['steep_ms']:.2f} ms），最大斜率 "
                        f"{m2['max_slew_v_per_ms']:.0f} V/ms，"
                        f"LC {m2['osc_period_ms']:.2f} ms（{m2['osc_freq_hz']:.0f} Hz），"
                        f"{m2['settle_ms']:.1f} ms 归零"),
            "details": {"tdiv": TDIV, "delay_used": round(new_delay, 6),
                        "window_ms": round(float(t2[-1] - t2[0]) * 1e3, 3),
                        "npoints": int(len(v2)),
                        "metrics": {k: x for k, x in m2.items() if not k.startswith("i")}},
        }
    except (OSError, RuntimeError) as exc:
        result = {"status": "error", "action": "transient_fall",
                  "error": {"code": "capture_failed", "message": str(exc)}}
    finally:
        sc.close()

    with open(os.path.join(F.OUT_DIR, "captured_fall_result.json"), "w") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
    print("\n" + json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    sys.exit(main())
