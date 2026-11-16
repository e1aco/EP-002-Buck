"""上升沿采集（简洁版：先关输出 → 武装 → 再开输出）

顺序要点（与下降沿镜像）：
  1. 关输出并确认 0 V（J-Link 读回 output=0 / vout=0）
  2. 设档位/时基/波形格式，电平取绝对 2.0 V（相对 offset 0.2）
  3. AUTO 跑起来 → 切 SINGle → 确认不是 Stop
  4. **再**写 output=1（不带 sleep），上升过程跨过 2.0 V 触发
  5. Stop 后只发 PREamble?/DATA?，用同帧 PAVA 标定

垂直：vdiv=0.5、offset=+1.8 → 窗 −0.2…3.8 V，0 V 在底部附近、3.3 V 在顶部附近。
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
VDIV, VOFF = 0.5, 1.8
TDIV = float(os.environ.get("RISE_TDIV", "2e-2"))
DELAY = -TDIV * 0.5
LEVEL_REL = 2.0 - VOFF          # 电平相对 offset，绝对 2.0 V
ARM_GAP = 3.0


def fw_state():
    """读固件 output / vout（十六进制解析）。"""
    path = os.path.join(F.OUT_DIR, "_jlink_read.jlink")
    with open(path, "w") as f:
        f.write("mem32 0x20000010 1\nmem32 0x20000020 1\nq\n")
    r = subprocess.run([F.JLINK, "-device", "STM32F103C8", "-if", "SWD",
                        "-speed", "4000", "-autoconnect", "1",
                        "-CommanderScript", path],
                       capture_output=True, timeout=25)
    out = r.stdout.decode("utf-8", "replace")
    vals = {}
    for line in out.splitlines():
        if line.startswith("20000010"):
            vals["state"] = int(line.split("=")[1].strip(), 16)
        elif line.startswith("20000020"):
            vals["vout"] = int(line.split("=")[1].strip(), 16)
    return vals


def write_output(on):
    path = os.path.join(F.OUT_DIR, "_jlink_out.jlink")
    with open(path, "w") as f:
        # 必须带 g（resume）：否则 CPU 停在 halt，固件不跑，写入不生效
        f.write(f"w1 {F.ADDR_OUTPUT:#x} 0x{1 if on else 0:02x}\ng\nq\n")
    r = subprocess.run([F.JLINK, "-device", "STM32F103C8", "-if", "SWD",
                        "-speed", "4000", "-autoconnect", "1",
                        "-CommanderScript", path],
                       capture_output=True, timeout=25)
    return "Script processing completed" in r.stdout.decode("utf-8", "replace")


def main():
    print("=" * 70)
    print(f"上升沿  {TARGET_V}V  vdiv={VDIV} off={VOFF} tdiv={TDIV} "
          f"delay={DELAY:.4f} level_rel={LEVEL_REL:.2f}(绝对 2.0V)")
    print("=" * 70)
    result = {"status": "error", "action": "transient_rise"}
    sc = F.SCPI(F.HOST, F.PORT)
    try:
        sc.drain()
        print("[1] 关输出并确认 0 V", flush=True)
        write_output(False)
        time.sleep(2.0)
        st = fw_state()
        print(f"    output={(st.get('state', 0) >> 24) & 0xFF} "
              f"vout={st.get('vout', 0)} mV", flush=True)

        print("[2] 设档位/时基/波形格式", flush=True)
        sc.write(f":CHANnel{CH}:COUPling DC")
        sc.write(f":CHANnel{CH}:SCALe {VDIV}")
        sc.write(f":CHANnel{CH}:OFFSet {VOFF}")
        sc.write(f":CHANnel{CH}:BWLimit FULL")
        time.sleep(0.8)
        sc.write(f":TIMebase:SCALe {TDIV}")
        time.sleep(0.8)
        sc.write(f":TIMebase:DELay {DELAY}")
        time.sleep(0.8)
        sc.write(":ACQuire:MDEP 1M")
        time.sleep(0.5)
        sc.write(f":WAVeform:SOURce C{CH}")
        sc.write(":WAVeform:WIDTh BYTE")
        time.sleep(0.5)
        print(f"    vdiv={sc.query(f':CHANnel{CH}:SCALe?')} "
              f"off={sc.query(f':CHANnel{CH}:OFFSet?')} "
              f"tdiv={sc.query(':TIMebase:SCALe?')}", flush=True)

        print("[3] 触发 + AUTO + 切 SINGle", flush=True)
        sc.write(f":TRIGger:EDGE:SOURce C{CH}")
        sc.write(":TRIGger:EDGE:SLOPe RISing")
        sc.write(f":TRIGger:EDGE:LEVel {LEVEL_REL}")
        sc.write(":TRIGger:MODE AUTO")
        sc.write(":TRIGger:RUN")
        time.sleep(3.0)
        print(f"    引擎 status={sc.status()} "
              f"MEAN={sc.measure('MEAN')} level={sc.query(':TRIGger:EDGE:LEVel?')}",
              flush=True)
        sc.write(":TRIGger:MODE SINGle")
        time.sleep(ARM_GAP)
        st_now = sc.status()
        print(f"    已武装 status={st_now}", flush=True)
        if st_now.startswith("Stop"):
            raise RuntimeError("武装即被触发，需回 AUTO 重来")

        print("[4] 开输出（不带 sleep，直接写 1）", flush=True)
        t0 = time.time()
        ok = write_output(True)
        print(f"    ok={ok} ({time.time()-t0:.2f}s)", flush=True)

        print("[5] 等触发", flush=True)
        captured = False
        while time.time() - t0 < 25:
            if sc.status().startswith("Stop"):
                captured = True
                print(f"    已触发（{time.time()-t0:.2f}s）", flush=True)
                break
            time.sleep(0.2)
        if not captured:
            print(f"    超时（{sc.status()}）", flush=True)

        print("[6] 读冻结帧", flush=True)
        p_min, p_max = sc.measure("MIN"), sc.measure("MAX")
        print(f"    同帧 PAVA MIN={p_min:.3f} MAX={p_max:.3f}", flush=True)
        wf = sc.read_frame(VDIV, VOFF)
        v, t = wf["volts"], wf["seconds"]
        print(f"    {wf['npoints']} 点 窗={wf['window_s']*1e3:.1f}ms "
              f"raw V=[{v.min():.2f},{v.max():.2f}]", flush=True)
        if not (np.isnan(p_min) or np.isnan(p_max)) and (v.max() - v.min()) > 0.05:
            gain = (p_max - p_min) / (v.max() - v.min())
            offc = p_min - v.min() * gain
            v = v * gain + offc
            print(f"    标定 gain={gain:.4f} off={offc:+.4f} → "
                  f"V=[{v.min():.2f},{v.max():.2f}]", flush=True)

        np.savez(os.path.join(F.OUT_DIR, "captured_rise.npz"), volts=v, seconds=t)
        idx = np.linspace(0, len(v) - 1, 15).astype(int)
        print("    profile:", " ".join(f"{t[i]*1e3:.1f}ms:{v[i]:.2f}V" for i in idx),
              flush=True)

        result = {"status": "ok", "action": "transient_rise",
                  "details": {"tdiv": TDIV, "delay": DELAY,
                              "window_ms": round(wf["window_s"] * 1e3, 2),
                              "npoints": int(len(v)),
                              "v_range": [round(float(v.min()), 3),
                                          round(float(v.max()), 3)]},
                  "summary": f"窗口 {wf['window_s']*1e3:.1f} ms，"
                             f"V=[{v.min():.2f},{v.max():.2f}]"}
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
