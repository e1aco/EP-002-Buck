"""下降沿采集（终版 v2）—— 关键：Stop 之后不再发任何设置命令

流程：
  1. 档位 vdiv=1V/div, offset=+0.5V（3.3V 位于屏幕上部 70%）
  2. 时基 0.5 s/div → 记录窗 2.5 s，delay=-0.3 s
  3. **先**设好 :WAVeform:SOURce / WIDTh 并缓存 vdiv/voff
  4. 触发 FALLing @1.5V，AUTO 自由运行，确认读到 3.3V
  5. 切 SINGle，等 3 s
  6. 关输出（J-Link 写 s_output_on=0）
  7. 等 Stop → 只发 PREamble? + DATA? 读帧（任何写命令都会重启采集、把帧冲掉）
  8. 用同帧 PAVA MIN/MAX 做两点标定 → 恢复输出

实测坑（全部踩过）：
  - `:RUN` 不存在 → 用 `:TRIGger:RUN`；否则一直 Stop，SINGle 空转
  - `:TRIGger:MODE` 是扫描方式，触发类型是 `:TRIGger:TYPE`
  - tdiv 只能 1-2-5 档（0.6 会被取整成 1.0）
  - 绝不调 AUToset（会把 vdiv 改成 20mV/div，3.3V 饱和、永不触发）
  - Stop 后发 `:WAVeform:SOURce`/`:WIDTh` 会重启采集 → 帧丢失
  - 电压换算有效 offset ≈ 回读值的一半，故用 PAVA 两点标定兜底
"""
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time

import numpy as np

HOST, PORT = "192.168.9.1", 5025
CH = 1
TARGET_V = 3.3
VDIV, VOFF = 1.0, 0.5
TDIV, DELAY = 0.5, -0.3
TRIG_LEVEL = 1.5
ARM_GAP = 3.0

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
PKG_DIR = os.path.normpath(os.path.join(OUT_DIR, "..", "PKG"))
os.makedirs(PKG_DIR, exist_ok=True)

JLINK = r"C:\Program Files\SEGGER\JLink_V970\JLink.exe"
ADDR_OUTPUT = 0x20000013

OFF_HORIZ_INTERVAL = 0xB0
OFF_HORIZ_OFFSET = 0xB4


class SCPI:
    def __init__(self, host, port, timeout=5.0):
        self.sock = socket.create_connection((host, port), timeout)
        self.sock.settimeout(2.0)
        self.buf = b""

    def _fill(self, wait=2.0):
        self.sock.settimeout(wait)
        try:
            chunk = self.sock.recv(1 << 20)
        except socket.timeout:
            return False
        if not chunk:
            raise RuntimeError("connection closed")
        self.buf += chunk
        return True

    def drain(self, max_time=1.5):
        t0 = time.time()
        while time.time() - t0 < max_time:
            if not self._fill(0.15):
                break
            self.buf = b""
        self.buf = b""
        self.sock.settimeout(2.0)

    def write(self, cmd):
        self.sock.sendall((cmd + "\n").encode())

    def query(self, cmd, deadline=3.0):
        self.sock.sendall((cmd + "\n").encode())
        end = time.time() + deadline
        while b"\n" not in self.buf and time.time() < end:
            if not self._fill(0.3):
                continue
        if b"\n" not in self.buf:
            return ""
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode(errors="replace").strip()

    def read_block(self, deadline=25.0):
        end = time.time() + deadline
        while b"#" not in self.buf:
            if time.time() > end:
                raise RuntimeError(f"no block header: {self.buf[:40]!r}")
            self._fill(1.0)
        self.buf = self.buf[self.buf.index(b"#") + 1:]
        while len(self.buf) < 1:
            self._fill(1.0)
        nd = int(chr(self.buf[0]))
        need = 1 + nd
        while len(self.buf) < need:
            self._fill(1.0)
        blen = int(self.buf[1:need])
        total = need + blen
        while len(self.buf) < total:
            if time.time() > end:
                raise RuntimeError(f"block timeout {len(self.buf)}/{total}")
            self._fill(1.0)
        data = self.buf[need:total]
        self.buf = self.buf[total:]
        return data[:blen]

    def measure(self, param):
        r = self.query(f":C{CH}:PAVA? {param}")
        if not r or "****" in r:
            return float("nan")
        try:
            return float(r.split(",")[-1].rstrip("VHzS"))
        except (ValueError, IndexError):
            return float("nan")

    def status(self):
        return self.query(":TRIGger:STATus?")

    def read_frame(self, vdiv, voff):
        """Stop 之后调用：只读 PREamble? + DATA?，不发任何写命令。"""
        self.buf = b""
        self.write(":WAVeform:PREamble?")
        pre = self.read_block()
        dt = struct.unpack("<f", pre[OFF_HORIZ_INTERVAL:OFF_HORIZ_INTERVAL + 4])[0]
        hoff = struct.unpack("<d", pre[OFF_HORIZ_OFFSET:OFF_HORIZ_OFFSET + 8])[0]
        self.write(":WAVeform:DATA?")
        raw = self.read_block()
        if not raw:
            raise RuntimeError("空波形（WAVE_ARRAY=0）")
        codes = np.frombuffer(raw, dtype=np.int8).astype(np.float64)
        volts = codes / 256.0 * 8.0 * vdiv - voff * 0.5
        seconds = hoff + np.arange(len(volts)) * dt
        return {"volts": volts, "seconds": seconds, "dt": dt,
                "vdiv": vdiv, "voff": voff, "npoints": len(volts),
                "window_s": len(volts) * dt}

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def jlink(lines):
    path = os.path.join(OUT_DIR, "_jlink_tmp.jlink")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\nq\n")
    r = subprocess.run([JLINK, "-device", "STM32F103C8", "-if", "SWD",
                        "-speed", "4000", "-autoconnect", "1",
                        "-CommanderScript", path],
                       capture_output=True, timeout=25)
    out = r.stdout.decode("utf-8", "replace")
    return ("Script processing completed" in out), out


def output_off():
    return jlink([f"w1 {ADDR_OUTPUT:#x} 0x00"])


def output_on():
    return jlink([f"w1 {ADDR_OUTPUT:#x} 0x01"])


def num(text, default):
    m = re.search(r"[-+]?\d+\.?\d*(?:[Ee][-+]?\d+)?", text or "")
    return float(m.group(0)) if m else default


def analyze(volts, seconds):
    n = len(volts)
    v0 = float(np.median(volts[:max(200, n // 50)]))
    vf = float(np.median(volts[-max(200, n // 50):]))
    if v0 < 0.5 * TARGET_V:
        return None, f"起始电压 {v0:.2f}V 过低"
    if v0 - vf < 0.5:
        return None, f"未出现下降（{v0:.2f}→{vf:.2f}V）"

    i0 = int(np.argmax(volts <= v0 * 0.9))
    dv = np.diff(volts)
    i_pk = int(np.argmin(dv[i0:])) + i0
    peak = float(dv[i_pk])
    i_end = i_pk
    for k in range(i_pk, len(dv)):
        if abs(dv[k]) < abs(peak) * 0.1:
            i_end = k
            break
    i50 = int(np.argmax(volts <= v0 * 0.5))
    i10 = int(np.argmax(volts <= v0 * 0.1))
    vmin = float(volts.min())
    i_vmin = int(np.argmin(volts))
    dt = float(seconds[1] - seconds[0])

    period_ms = 0.0
    zc = [i for i in range(max(1, i_vmin), n)
          if (volts[i - 1] < 0 <= volts[i]) or (volts[i - 1] >= 0 > volts[i])]
    same = zc[::2]
    if len(same) >= 2:
        period_ms = float(seconds[same[1]] - seconds[same[0]]) * 1e3

    band = 0.02 * TARGET_V
    i_set = n - 1
    for i in range(i_vmin, n):
        if abs(volts[i]) > band:
            i_set = min(i + 1, n - 1)

    return {
        "v0": round(v0, 3), "vfinal": round(vf, 3), "vmin": round(vmin, 3),
        "undershoot_v": round(min(vmin, 0.0), 3),
        "steep_ms": round(float(seconds[i_end] - seconds[i0]) * 1e3, 2),
        "steep_slew_v_per_ms": round(abs(peak) / dt / 1e3, 3),
        "t50_ms": round(float(seconds[i50] - seconds[i0]) * 1e3, 2),
        "t10_ms": round(float(seconds[i10] - seconds[i0]) * 1e3, 2),
        "osc_period_ms": round(period_ms, 2),
        "osc_freq_hz": round(1000.0 / period_ms, 1) if period_ms > 0 else 0.0,
        "settle_ms": round(float(seconds[i_set] - seconds[i0]) * 1e3, 1),
        "_i0": i0, "_i_vmin": i_vmin,
    }, None


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
    gs = fig.add_gridspec(3, 1, height_ratios=[2, 1.2, 1.2], hspace=0.5)
    t_ms = seconds * 1e3
    step = max(1, len(volts) // 8000)

    ax = fig.add_subplot(gs[0])
    ax.plot(t_ms[::step], volts[::step], lw=0.8, color="#d62728")
    ax.axhline(0, color="#888", lw=0.8, alpha=0.6)
    ax.axhline(TARGET_V, color="#2ca02c", lw=0.9, ls="--", alpha=0.7,
               label=f"目标 {TARGET_V} V")
    ax.axhline(TRIG_LEVEL, color="#1f77b4", lw=0.9, ls=":", alpha=0.8,
               label=f"触发电平 {TRIG_LEVEL} V")
    if m:
        ax.plot(t_ms[m["_i0"]], volts[m["_i0"]], "o", ms=7, color="#ff7f0e",
                label="下降起点")
        ax.plot(t_ms[m["_i_vmin"]], volts[m["_i_vmin"]], "v", ms=8,
                color="#9467bd", label=f"最低 {m['vmin']:.2f} V")
    ax.set_xlabel("时间 (ms)")
    ax.set_ylabel("电压 (V)")
    ax.set_title(f"3.3 V 输出关断（下降沿）　记录窗 {seconds[-1]-seconds[0]:.2f} s")
    ax.legend(loc="upper right", fontsize=9)
    ax.grid(alpha=0.3)

    if m:
        ax2 = fig.add_subplot(gs[1])
        t0 = seconds[m["_i0"]]
        sel = (seconds >= t0 - 0.005) & (seconds <= t0 + 0.10)
        ax2.plot(seconds[sel] * 1e3, volts[sel], lw=1.2, color="#d62728")
        ax2.axhline(0, color="#888", lw=0.8)
        ax2.axhline(TARGET_V * 0.5, color="#1f77b4", lw=0.9, ls=":",
                    label=f"50% @ {m['t50_ms']:.2f} ms")
        ax2.set_title(f"陡降段：{m['steep_ms']:.2f} ms，斜率 "
                      f"{m['steep_slew_v_per_ms']:.2f} V/ms")
        ax2.set_xlabel("时间 (ms)")
        ax2.set_ylabel("电压 (V)")
        ax2.legend(fontsize=9)
        ax2.grid(alpha=0.3)

        ax3 = fig.add_subplot(gs[2])
        tv = seconds[m["_i_vmin"]]
        sel3 = (seconds >= tv - 0.005) & (seconds <= tv + 0.020)
        ax3.plot(seconds[sel3] * 1e3, volts[sel3], lw=1.0, color="#9467bd")
        ax3.axhline(0, color="#888", lw=0.8)
        ax3.set_title(f"负压过冲 / LC 振荡：最低 {m['vmin']:.2f} V，"
                      f"周期 {m['osc_period_ms']:.2f} ms（{m['osc_freq_hz']:.0f} Hz）")
        ax3.set_xlabel("时间 (ms)")
        ax3.set_ylabel("电压 (V)")
        ax3.grid(alpha=0.3)

    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def main():
    print("=" * 70)
    print(f"下降沿  {TARGET_V}V  vdiv={VDIV}  off={VOFF}  tdiv={TDIV}  "
          f"delay={DELAY}  level={TRIG_LEVEL}  武装后等 {ARM_GAP}s")
    print("=" * 70)
    result = {"status": "error", "action": "transient_fall"}
    sc = SCPI(HOST, PORT)
    try:
        sc.drain()

        print("[1] 档位 + 时基 + 波形格式（全部在武装前设好）", flush=True)
        sc.write(f":CHANnel{CH}:COUPling DC")
        sc.write(f":CHANnel{CH}:SCALe {VDIV}")
        sc.write(f":CHANnel{CH}:OFFSet {VOFF}")
        sc.write(f":CHANnel{CH}:BWLimit FULL")
        time.sleep(0.8)
        sc.write(f":TIMebase:SCALe {TDIV}")
        time.sleep(0.8)
        sc.write(f":TIMebase:DELay {DELAY}")
        time.sleep(0.8)
        # 关键：波形源/宽度在武装前设好，Stop 后不再写
        # 存储深度降到 1M，保证一次 DATA? 能取回整段记录（10M 时只回传一半）
        sc.write(":ACQuire:MDEP 1M")
        time.sleep(0.5)
        sc.write(f":WAVeform:SOURce C{CH}")
        sc.write(":WAVeform:WIDTh BYTE")
        time.sleep(0.5)
        vdiv_c = num(sc.query(f":CHANnel{CH}:SCALe?"), VDIV)
        voff_c = num(sc.query(f":CHANnel{CH}:OFFSet?"), VOFF)
        tdiv_rb = num(sc.query(":TIMebase:SCALe?"), 0.0)
        print(f"    vdiv={vdiv_c}  off={voff_c}  tdiv={tdiv_rb}", flush=True)
        if abs(tdiv_rb - TDIV) > 1e-6:
            raise RuntimeError(f"tdiv 未生效：{tdiv_rb}")

        print("[2] 触发 + AUTO 自由运行", flush=True)
        sc.write(f":TRIGger:EDGE:SOURce C{CH}")
        sc.write(":TRIGger:EDGE:SLOPe FALLing")
        sc.write(f":TRIGger:EDGE:LEVel {TRIG_LEVEL}")
        sc.write(":TRIGger:MODE AUTO")
        sc.write(":TRIGger:RUN")
        running = False
        for _ in range(20):
            time.sleep(0.6)
            if sc.status().startswith("Auto"):
                running = True
                break
        mean_v = sc.measure("MEAN")
        print(f"    状态={sc.status()}  电压={mean_v:.3f}V", flush=True)
        if not running:
            raise RuntimeError(f"引擎未自由运行（{sc.status()}）")
        if not (2.0 < mean_v < 4.5):
            raise RuntimeError(f"读不到 3.3V（{mean_v:.3f}V），输出未开")

        print("[3] 切 SINGle 并等待就绪", flush=True)
        sc.write(":TRIGger:MODE SINGle")
        time.sleep(ARM_GAP)
        st = sc.status()
        print(f"    mode={sc.query(':TRIGger:MODE?')}  status={st}", flush=True)
        if st.startswith("Stop"):
            raise RuntimeError("武装即被旧数据触发，需重新 RUN 再武装")

        print("[4] 关输出", flush=True)
        t0 = time.time()
        ok, out = output_off()
        print(f"    {time.time()-t0:.2f}s  ok={ok}", flush=True)
        if not ok:
            print(out[-300:], flush=True)
            raise RuntimeError("J-Link 关输出失败")

        print("[5] 等触发", flush=True)
        captured = False
        end = time.time() + 25.0
        while time.time() < end:
            if sc.status().startswith("Stop"):
                captured = True
                print(f"    已触发（{time.time()-t0:.2f}s）", flush=True)
                break
            time.sleep(0.25)
        if not captured:
            print(f"    超时（{sc.status()}）", flush=True)

        print("[6] 读冻结帧（只发 PREamble? + DATA?）", flush=True)
        p_min = sc.measure("MIN")
        p_max = sc.measure("MAX")
        p_mean = sc.measure("MEAN")
        print(f"    同帧 PAVA: MEAN={p_mean:.3f} MIN={p_min:.3f} MAX={p_max:.3f}", flush=True)
        wf = sc.read_frame(vdiv_c, voff_c)
        volts, seconds = wf["volts"], wf["seconds"]
        print(f"    {wf['npoints']} 点 dt={wf['dt']:.2e}s 窗={wf['window_s']:.2f}s "
              f"V=[{volts.min():.2f},{volts.max():.2f}]", flush=True)

        # 两点标定：用同帧 PAVA MIN/MAX 对齐波形 min/max
        if not (np.isnan(p_min) or np.isnan(p_max)) and (volts.max() - volts.min()) > 0.05:
            gain = (p_max - p_min) / (volts.max() - volts.min())
            offc = p_min - volts.min() * gain
            volts = volts * gain + offc
            print(f"    标定 gain={gain:.4f} off={offc:+.4f} → "
                  f"V=[{volts.min():.2f},{volts.max():.2f}]", flush=True)

        print("[7] 恢复", flush=True)
        sc.write(":TRIGger:MODE AUTO")
        sc.write(":TRIGger:RUN")
        ok2, _ = output_on()
        print(f"    output=1 ok={ok2}", flush=True)

        print("[8] 分析", flush=True)
        m, err = analyze(volts, seconds)
        if m is None:
            result = {"status": "error", "action": "transient_fall",
                      "error": {"code": "invalid_waveform", "message": err,
                                "v_min": float(volts.min()),
                                "v_max": float(volts.max()),
                                "window_s": wf["window_s"]}}
            np.savez(os.path.join(OUT_DIR, "captured_fall_raw.npz"),
                     volts=volts, seconds=seconds)
            plot(volts, seconds, None, os.path.join(PKG_DIR, "fall_3v3_raw.png"))
        else:
            np.savez(os.path.join(OUT_DIR, "captured_fall.npz"),
                     volts=volts, seconds=seconds)
            plot(volts, seconds, m, os.path.join(PKG_DIR, "fall_3v3.png"))
            clean = {k: v for k, v in m.items() if not k.startswith("_")}
            result = {
                "status": "ok", "action": "transient_fall",
                "summary": (f"3.3V 关断：{m['v0']:.2f}V → 最低 {m['vmin']:.2f}V，"
                            f"陡降 {m['steep_ms']:.2f} ms，50% @ {m['t50_ms']:.2f} ms，"
                            f"10% @ {m['t10_ms']:.2f} ms，"
                            f"LC {m['osc_period_ms']:.2f} ms/{m['osc_freq_hz']:.0f} Hz，"
                            f"{m['settle_ms']:.0f} ms 归零"),
                "details": {"target_v": TARGET_V, "vdiv": vdiv_c, "voff": voff_c,
                            "tdiv": TDIV, "delay": DELAY,
                            "trigger_level": TRIG_LEVEL,
                            "window_s": wf["window_s"], "npoints": wf["npoints"],
                            "pava_frame": {"mean": p_mean, "min": p_min, "max": p_max},
                            "metrics": clean},
                "context": {"provider": "scope"},
            }
    except (OSError, RuntimeError) as exc:
        result = {"status": "error", "action": "transient_fall",
                  "error": {"code": "capture_failed", "message": str(exc)}}
        try:
            sc.write(":TRIGger:MODE AUTO")
            sc.write(":TRIGger:RUN")
        except Exception:
            pass
    finally:
        sc.close()

    with open(os.path.join(OUT_DIR, "captured_fall_result.json"), "w") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
    print("\n" + json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    sys.exit(main())
