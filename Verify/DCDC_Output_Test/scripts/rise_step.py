"""上升沿采集（触发点锚在上升沿上）

之前失败的根因：触发电平取绝对 0.5 V，太低，被 J-Link/SWD 连接的毛刺抢先触发，
触发点比真实起升早 0.5~1 s，且抖动大 → 窄窗永远框不住。

修正：电平取绝对 2.5 V（相对 offset = 2.0）。毛刺到不了 2.5 V，
而 0→3.3 V 的上升必然跨过 → 触发点就落在上升沿上，窗口固定即可框全。

窗口：20 ms（起升 0.16 ms + 过冲回落 ~14 ms），delay=-2 ms 留 2 ms 0 V 基线。
"""
import json
import os
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fall_keep as F  # noqa: E402
import rise_final as RF  # noqa: E402

CH = F.CH
TARGET_V = 3.3
VDIV, VOFF = 1.0, 0.5
TDIV_LOCATE = 2e-2        # 定位用：200 ms 窗
TDIV = float(os.environ.get("RISE_TDIV", "2e-3"))   # 定稿用：20 ms 窗（高 10 倍分辨率）
PRE_FRAC = 0.03
MAX_SHIFT_FRAC = 0.9      # delay 上限 = tdiv*5，留余量
LEVEL_REL = 3.0 - VOFF          # 绝对 3.0V（过冲到 3.76V 必跨过）


def write_output(on):
    path = os.path.join(F.OUT_DIR, "_w.jlink")
    with open(path, "w") as f:
        f.write(f"w1 {F.ADDR_OUTPUT:#x} 0x{1 if on else 0:02x}\ng\nq\n")
    r = subprocess.run([F.JLINK, "-device", "STM32F103C8", "-if", "SWD",
                        "-speed", "4000", "-autoconnect", "1",
                        "-CommanderScript", path],
                       capture_output=True, timeout=25)
    return "Script processing completed" in r.stdout.decode("utf-8", "replace")


def main():
    print("=" * 70)
    print(f"上升沿  {TARGET_V}V  vdiv={VDIV} off={VOFF} tdiv={TDIV} "
          f"定位窗 {TDIV_LOCATE*1e3:.0f}ms → 取景窗 {TDIV*10*1e3:.0f}ms "
          f"level_rel={LEVEL_REL}（绝对 2.5V）")
    print("=" * 70)
    sc = F.SCPI(F.HOST, F.PORT)
    result = {"status": "error", "action": "transient_rise"}

    def shot(tdiv, delay):
        sc.write(f":CHANnel{CH}:COUPling DC")
        sc.write(f":CHANnel{CH}:SCALe {VDIV}")
        sc.write(f":CHANnel{CH}:OFFSet {VOFF}")
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
        sc.write(":TRIGger:EDGE:SLOPe RISing")
        sc.write(f":TRIGger:EDGE:LEVel {LEVEL_REL}")
        sc.write(":TRIGger:MODE AUTO")
        sc.write(":TRIGger:RUN")
        time.sleep(2.5)
        sc.write(":TRIGger:MODE SINGle")
        time.sleep(2.5)
        t0 = time.time()
        write_output(True)
        while time.time() - t0 < 20:
            if sc.status().startswith("Stop"):
                break
            time.sleep(0.2)
        p_min, p_max = sc.measure("MIN"), sc.measure("MAX")
        wf = sc.read_frame(VDIV, VOFF)
        v, t = wf["volts"], wf["seconds"]
        if not (np.isnan(p_min) or np.isnan(p_max)) and (v.max() - v.min()) > 0.05:
            gain = (p_max - p_min) / (v.max() - v.min())
            v = v * gain + (p_min - v.min() * gain)
        return v, t, wf

    try:
        sc.drain()
        write_output(False)
        time.sleep(1.5)

        # 触发点总比真实起升早 100~260 ms（SWD 写入时的瞬时扰动），而 delay 只能在
        # ±tdiv×5 内平移 → 快时基框不住。改为：宽窗完整抓，出图时裁到「起升→建立」。
        v = t = None
        m = None
        for attempt in range(3):
            print(f"[{attempt+1}] 宽窗完整抓：tdiv={TDIV_LOCATE}s（200 ms 窗）", flush=True)
            v1, t1, _ = shot(TDIV_LOCATE, -2e-3)
            m1 = RF.rise_metrics(v1, t1)
            if m1 is None:
                print(f"    未捕获上升 V=[{v1.min():.2f},{v1.max():.2f}]，重试", flush=True)
                continue
            i0 = m1["i0"]
            print(f"    起升 {t1[i0]*1e3:.2f} ms，建立 {m1['settle_ms']:.2f} ms 后",
                  flush=True)
            # 裁剪：起升前 2 ms 到建立后 10 ms（settle_ms 已相对起升）
            dt1 = float(t1[1] - t1[0])
            a = max(0, i0 - int(2e-3 / dt1))
            b = min(len(v1) - 1, i0 + int((m1["settle_ms"] + 10.0) * 1e-3 / dt1))
            v, t = v1[a:b + 1], t1[a:b + 1]
            m = RF.rise_metrics(v, t)
            if m is not None:
                print(f"    裁剪后窗={ (t[-1]-t[0])*1e3:.2f} ms "
                      f"V=[{v.min():.2f},{v.max():.2f}]", flush=True)
                break
            print("    裁剪后判据失败，重试", flush=True)
            v = t = m = None

        if v is None or m is None:
            raise RuntimeError("未能捕获完整上升沿")

        np.savez(os.path.join(F.OUT_DIR, "captured_rise.npz"), volts=v, seconds=t)
        RF.plot_rise(v, t - t[m["i0"]], m, os.path.join(F.PKG_DIR, "rise_3v3.png"))
        result = {
            "status": "ok", "action": "transient_rise",
            "summary": (f"3.3V 开启：{m['v_base']:.2f}V → {m['v_top']:.2f}V，"
                        f"10%→90% = {m['rise_ms']:.3f} ms，峰值 {m['vmax']:.2f} V"
                        f"（超调 {m['overshoot_pct']:.2f}%），"
                        f"{m['settle_ms']:.2f} ms 建立"),
            "details": {"capture_tdiv": TDIV_LOCATE, "capture_delay": -2e-3,
                        "cropped_window_ms": round(float(t[-1] - t[0]) * 1e3, 2),
                        "npoints": int(len(v)),
                        "metrics": {k: x for k, x in m.items() if not k.startswith("i")}},
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
