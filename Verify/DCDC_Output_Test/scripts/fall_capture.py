"""下降沿采集（最简流程 + 自带 SCPI 客户端，所有循环都有上界）

流程：
  1. 档位：3.3V 放屏幕上部（vdiv=1V/div, offset=+0.5V → 70% 高度）
  2. 时基：1.0 s/div（1-2-5 合法档位）→ 记录窗 5 s，delay=-0.5 s → 触发点在左侧 10%
  3. 触发：FALLing @1.5V（3.3V 的 45%）
  4. 切 SINGle（引擎已在跑）
  5. 关输出（J-Link 写 cascade=0 / vref=0 / output=0）
  6. 等 Stop → 读波形 → 恢复输出

踩过的坑：
  - tdiv 只能 1-2-5 档，0.6 被取整成 1.0
  - 绝不调 AUToset：会把 vdiv 设成 20mV/div → 3.3V 饱和（127码≈90.7mV）→ 永不触发
  - 命令头必须是 :CHANnel<n>，写成 :C1annel 会让仪器解析错乱、查询全部超时
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
VDIV, VOFF = 1.0, 0.5        # 3.3V 位于 2.8 格（70% 高度）
TDIV, DELAY = 1.0, -0.5      # 记录窗 5s，触发点左侧 10%
TRIG_LEVEL = 1.5

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
PKG_DIR = os.path.normpath(os.path.join(OUT_DIR, "..", "PKG"))
os.makedirs(PKG_DIR, exist_ok=True)

JLINK = r"C:\Program Files\SEGGER\JLink_V970\JLink.exe"
ADDR_CASCADE, ADDR_OUTPUT, ADDR_VREF = 0x20000010, 0x20000013, 0x20000028

OFF_HORIZ_INTERVAL = 0xB0
OFF_HORIZ_OFFSET = 0xB4


class SCPI:
    """有界 SCPI 客户端。"""

    def __init__(self, host, port, timeout=5.0):
        self.sock = socket.create_connection((host, port), timeout)
        self.sock.settimeout(2.0)
        self.buf = b""

    # ---- 底层 ----
    def _fill(self, wait=2.0):
        self.sock.settimeout(wait)
        try:
            chunk = self.sock.recv(1 << 20)
        except socket.timeout:
            return False
        if not chunk:
            raise RuntimeError("connection closed by instrument")
        self.buf += chunk
        return True

    def drain(self, max_bytes=1 << 22, max_time=1.5):
        """清残留，字节数与时间双上界。"""
        t0 = time.time()
        got = 0
        while got < max_bytes and time.time() - t0 < max_time:
            if not self._fill(0.15):
                break
            got += len(self.buf)
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

    def read_block(self, deadline=20.0):
        """IEEE 488.2 定长块，自动跳过前缀。"""
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
                raise RuntimeError(f"block timeout: {len(self.buf)}/{total}")
            self._fill(1.0)
        data = self.buf[need:total]
        self.buf = self.buf[total:]
        return data[:blen]

    # ---- 语义 ----
    def measure(self, param):
        r = self.query(f":C{CH}:PAVA? {param}")
        if not r or "****" in r:
            return float("nan")
        try:
            return float(r.split(",")[-1].rstrip("V").rstrip("Hz").rstrip("S"))
        except (ValueError, IndexError):
            return float("nan")

    def status(self):
        return self.query(":TRIGger:STATus?")

    def waveform(self):
        self.write(f":WAVeform:SOURce C{CH}")
        self.write(":WAVeform:WIDTh BYTE")
        time.sleep(0.2)
        self.buf = b""
        self.write(":WAVeform:PREamble?")
        pre = self.read_block()
        if len(pre) < OFF_HORIZ_OFFSET + 8:
            raise RuntimeError(f"preamble too short: {len(pre)}")
        dt = struct.unpack("<f", pre[OFF_HORIZ_INTERVAL:OFF_HORIZ_INTERVAL + 4])[0]
        hoff = struct.unpack("<d", pre[OFF_HORIZ_OFFSET:OFF_HORIZ_OFFSET + 8])[0]

        vdiv_r = self.query(f":C{CH}:VDIV?")
        voff_r = self.query(f":C{CH}:OFSt?")

        def _num(text, default):
            m = re.search(r"[-+]?\d+\.?\d*(?:[Ee][-+]?\d+)?", text or "")
            return float(m.group(0)) if m else default

        vdiv = _num(vdiv_r, VDIV)
        voff = _num(voff_r, VOFF)

        self.write(":WAVeform:DATA?")
        raw = self.read_block()
        if len(raw) == 0:
            raise RuntimeError("空波形（WAVE_ARRAY=0）：未触发")

        codes = np.frombuffer(raw, dtype=np.int8).astype(np.float64)
        volts = codes / 256.0 * 8.0 * vdiv - voff
        seconds = hoff + np.arange(len(volts)) * dt
        return {"volts": volts, "seconds": seconds, "dt": dt, "vdiv": vdiv,
                "voff": voff, "npoints": len(volts),
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
    out = r.stdout.decode("utf-8", errors="replace")
    return ("Script processing completed" in out), out


def output_off():
    """等价固件 APP_CONTROL_SetOutput(0)：只清 output 标志，主循环立刻把占空比归 0。

    不写 cascade/vref——多余写入只会让恢复更麻烦。
    """
    return jlink([f"w1 {ADDR_OUTPUT:#x} 0x00"])


def output_on():
    return jlink([f"w1 {ADDR_OUTPUT:#x} 0x01"])


def analyze(volts, seconds):
    n = len(volts)
    v0 = float(np.median(volts[:max(200, n // 100)]))
    if v0 < 0.5 * TARGET_V:
        return None, f"v0={v0:.2f}V 过低（未捕获到 3.3V 稳态）"
    vf = float(np.median(volts[-max(200, n // 50):]))

    i0 = int(np.argmax(volts <= v0 * 0.9))
    if i0 <= 0:
        return None, "未找到下降起点"

    dv = np.diff(volts)
    i_pk = int(np.argmin(dv[i0:])) + i0
    peak = float(dv[i_pk])
    i_end = i_pk
    for k in range(i_pk, len(dv)):
        if abs(dv[k]) < abs(peak) * 0.1:
            i_end = k
            break

    i10 = int(np.argmax(volts <= v0 * 0.1))
    i50 = int(np.argmax(volts <= v0 * 0.5))
    vmin = float(volts.min())
    i_vmin = int(np.argmin(volts))

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

    dt = float(seconds[1] - seconds[0])
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

    fig = plt.figure(figsize=(13, 9))
    gs = fig.add_gridspec(3, 1, height_ratios=[2, 1.2, 1.2], hspace=0.45)
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
    ax.set_title(f"3.3 V 输出关断（下降沿）　记录窗 {seconds[-1]-seconds[0]:.1f} s")
    ax.legend(loc="upper right", fontsize=9)
    ax.grid(alpha=0.3)

    if m:
        ax2 = fig.add_subplot(gs[1])
        t0 = seconds[m["_i0"]]
        sel = (seconds >= t0 - 0.002) & (seconds <= t0 + 0.030)
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
        sel3 = (seconds >= tv - 0.004) & (seconds <= tv + 0.012)
        ax3.plot(seconds[sel3] * 1e3, volts[sel3], lw=1.0, color="#9467bd")
        ax3.axhline(0, color="#888", lw=0.8)
        ax3.set_title(f"负压过冲/LC 振荡：最低 {m['vmin']:.2f} V，"
                      f"周期 {m['osc_period_ms']:.2f} ms（{m['osc_freq_hz']:.0f} Hz）")
        ax3.set_xlabel("时间 (ms)")
        ax3.set_ylabel("电压 (V)")
        ax3.grid(alpha=0.3)

    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def main():
    print("=" * 66)
    print(f"下降沿采集  {TARGET_V}V  vdiv={VDIV}V/div  off={VOFF}V  "
          f"tdiv={TDIV}s  delay={DELAY}s  level={TRIG_LEVEL}V")
    print("=" * 66)
    result = {"status": "error", "action": "transient_fall"}
    sc = SCPI(HOST, PORT)
    try:
        sc.drain()

        print("[1] 设置档位（3.3V 放上部）", flush=True)
        sc.write(f":CHANnel{CH}:COUPling DC")
        sc.write(f":CHANnel{CH}:SCALe {VDIV}")
        sc.write(f":CHANnel{CH}:OFFSet {VOFF}")
        sc.write(f":CHANnel{CH}:BWLimit FULL")
        time.sleep(1.0)
        vdiv_rb = sc.query(f":CHANnel{CH}:SCALe?")
        voff_rb = sc.query(f":CHANnel{CH}:OFFSet?")
        print(f"    vdiv={vdiv_rb}  offset={voff_rb}", flush=True)
        if "1.00E+00" not in vdiv_rb:
            raise RuntimeError(f"vdiv 未生效：{vdiv_rb}")

        print("[2] 设置时基", flush=True)
        sc.write(f":TIMebase:SCALe {TDIV}")
        time.sleep(1.0)
        sc.write(f":TIMebase:DELay {DELAY}")
        time.sleep(1.0)
        tdiv_rb = sc.query(":TIMebase:SCALe?")
        delay_rb = sc.query(":TIMebase:DELay?")
        print(f"    tdiv={tdiv_rb}  delay={delay_rb}", flush=True)
        if "1.00E+00" not in tdiv_rb:
            raise RuntimeError(f"tdiv 未生效（只能 1-2-5 档）：{tdiv_rb}")

        print("[3] 触发 + 读当前电压", flush=True)
        # 注意：:TRIGger:MODE 是扫描方式（AUTO/NORMal/SINGle），不是触发类型；
        # 触发类型是 :TRIGger:TYPE（默认 EDGE），写 MODE EDGE 会被判非法命令。
        sc.write(f":TRIGger:EDGE:SOURce C{CH}")
        sc.write(":TRIGger:EDGE:SLOPe FALLing")
        sc.write(f":TRIGger:EDGE:LEVel {TRIG_LEVEL}")
        sc.write(":TRIGger:MODE AUTO")
        sc.write(":TRIGger:RUN")
        # 等引擎真正自由运行（Auto/Trig'd），否则后面切 SINGle 是空转
        running = False
        for _ in range(15):
            time.sleep(0.6)
            st = sc.status()
            if st.startswith("Auto") or st.startswith("Trig"):
                running = True
                break
        print(f"    引擎状态 {sc.status()}", flush=True)
        mean_v = sc.measure("MEAN")
        print(f"    当前电压 {mean_v:.3f} V", flush=True)
        if not running:
            raise RuntimeError(f"采集引擎未运行（status={sc.status()}），需重启示波器")
        if not (2.0 < mean_v < 4.5):
            raise RuntimeError(f"读不到 3.3V（{mean_v:.3f}V），确认输出已开")

        print("[4] 切 SINGle", flush=True)
        sc.write(":TRIGger:MODE SINGle")
        time.sleep(0.8)
        print(f"    mode={sc.query(':TRIGger:MODE?')}  "
              f"status={sc.status()}", flush=True)

        print("[5] 关输出（J-Link）", flush=True)
        t0 = time.time()
        ok, out = output_off()
        print(f"    {time.time()-t0:.2f}s  ok={ok}", flush=True)
        if not ok:
            print(out[-400:], flush=True)
            sc.write(":TRIGger:MODE AUTO")
            raise RuntimeError("J-Link 关输出失败")

        print("[6] 等触发", flush=True)
        captured = False
        end = time.time() + 20.0
        while time.time() < end:
            st = sc.status()
            if st.startswith("Stop"):
                captured = True
                print(f"    已触发（{st}）", flush=True)
                break
            time.sleep(0.4)
        if not captured:
            print("    超时未触发，仍尝试读取", flush=True)

        print("[7] 读波形", flush=True)
        wf = sc.waveform()
        volts, seconds = wf["volts"], wf["seconds"]
        print(f"    {wf['npoints']} 点  dt={wf['dt']:.3e}s  窗口={wf['window_s']:.3f}s  "
              f"V=[{volts.min():.2f},{volts.max():.2f}]  "
              f"t=[{seconds[0]:.2f},{seconds[-1]:.2f}]s", flush=True)

        print("[8] 恢复输出", flush=True)
        sc.write(":TRIGger:MODE AUTO")
        sc.write(":TRIGger:RUN")
        ok2, _ = output_on()
        print(f"    output=1 ok={ok2}", flush=True)

        print("[9] 分析", flush=True)
        m, err = analyze(volts, seconds)
        if m is None:
            result = {"status": "error", "action": "transient_fall",
                      "error": {"code": "invalid_waveform", "message": err,
                                "v_first": float(volts[0]),
                                "v_last": float(volts[-1]),
                                "v_min": float(volts.min()),
                                "v_max": float(volts.max())}}
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
                            f"10% @ {m['t10_ms']:.2f} ms，LC {m['osc_period_ms']:.2f} ms/"
                            f"{m['osc_freq_hz']:.0f} Hz，{m['settle_ms']:.0f} ms 归零"),
                "details": {"target_v": TARGET_V, "vdiv": wf["vdiv"],
                            "voff": wf["voff"], "tdiv": TDIV, "delay": DELAY,
                            "trigger_level": TRIG_LEVEL, "window_s": wf["window_s"],
                            "npoints": wf["npoints"], "metrics": clean},
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
