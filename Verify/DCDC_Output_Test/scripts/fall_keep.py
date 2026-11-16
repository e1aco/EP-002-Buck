"""下降沿采集（保持 Stop 版）

按人工操作习惯：抓完就停在 Stop，帧一直留着，需要时再回 AUTO。
流程：
  1. 档位/时基/波形格式/存储深度全部在武装前设好并缓存
  2. 触发 FALLing @1.5V，AUTO 自由运行确认读到 3.3V
  3. 切 SINGle，等 3 s
  4. 关输出 → 等 Stop
  5. 读帧（只发 PREamble? + DATA?）→ **保持 Stop**
  6. 等 5 s 再读一次，验证帧未被冲掉
  7. 只恢复固件输出，示波器留在 Stop
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
TDIV, DELAY = 1e-3, -5e-4   # 窗 10 ms，只留 0.5 ms 基线
TRIG_LEVEL = 1.5
ARM_GAP = 3.0

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
PKG_DIR = os.path.normpath(os.path.join(OUT_DIR, "..", "PKG"))
os.makedirs(PKG_DIR, exist_ok=True)

JLINK = r"C:\Program Files\SEGGER\JLink_V970\JLink.exe"
ADDR_OUTPUT = 0x20000013


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
        self.buf = b""
        self.write(":WAVeform:PREamble?")
        pre = self.read_block()
        dt = struct.unpack("<f", pre[0xB0:0xB4])[0]
        hoff = struct.unpack("<d", pre[0xB4:0xBC])[0]
        self.write(":WAVeform:DATA?")
        raw = self.read_block()
        if not raw:
            raise RuntimeError("空波形")
        codes = np.frombuffer(raw, dtype=np.int8).astype(np.float64)
        volts = codes / 256.0 * 8.0 * vdiv - voff * 0.5
        seconds = hoff + np.arange(len(volts)) * dt
        return {"volts": volts, "seconds": seconds, "dt": dt,
                "npoints": len(volts), "window_s": len(volts) * dt}

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


def output_off_delayed(delay_ms=2500):
    """连接后先 sleep 再写 0：SWD 连接毛刺落在武装之前，不会抢触发。"""
    path = os.path.join(OUT_DIR, "_jlink_delay.jlink")
    with open(path, "w") as f:
        f.write(f"sleep {delay_ms}\nw1 {ADDR_OUTPUT:#x} 0x00\nq\n")
    return subprocess.Popen([JLINK, "-device", "STM32F103C8", "-if", "SWD",
                             "-speed", "4000", "-autoconnect", "1",
                             "-CommanderScript", path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def num(text, default):
    m = re.search(r"[-+]?\d+\.?\d*(?:[Ee][-+]?\d+)?", text or "")
    return float(m.group(0)) if m else default


def analyze(volts, seconds):
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
    i_pk = int(np.argmin(dv[i0:i_vmin])) + i0 if i_vmin > i0 else i0
    span = v_top - v_final

    def cross(frac):
        level = v_top - frac * span
        idx = np.where(volts[i0:] <= level)[0]
        return i0 + int(idx[0]) if len(idx) else n - 1

    i10, i50, i90 = cross(0.1), cross(0.5), cross(0.9)
    # 陡降段 = 10%→90% 的跨度（cross 的 frac 是"已跌落百分比"）
    vmin = float(volts.min())

    zc = [i for i in range(max(1, i_vmin), n)
          if (volts[i - 1] < 0 <= volts[i]) or (volts[i - 1] >= 0 > volts[i])]
    # 高速采样下零附近噪声会造出密集伪过零，按最小间隔 0.2 ms 合并
    zc_f = []
    for i in zc:
        if not zc_f or (seconds[i] - seconds[zc_f[-1]]) > 2e-4:
            zc_f.append(i)
    same = zc_f[::2]
    period_ms = (float(seconds[same[1]] - seconds[same[0]]) * 1e3) if len(same) >= 2 else 0.0

    # 归零：|V| < 2%·v_top 且此后保持（±5% 会提前判归零，尾巴被切在窗外）
    band = 0.02 * abs(v_top)
    i_set = n - 1
    for k in range(i_vmin, n):
        if abs(volts[k]) > band:
            i_set = k
    i_set = min(i_set + 1, n - 1)

    return {
        "v_top": round(v_top, 3), "v_final": round(v_final, 3),
        "vmin": round(vmin, 3), "undershoot_v": round(min(vmin, 0.0), 3),
        "t90_ms": round(float(seconds[i90] - seconds[i0]) * 1e3, 2),
        "t50_ms": round(float(seconds[i50] - seconds[i0]) * 1e3, 2),
        "t10_ms": round(float(seconds[i10] - seconds[i0]) * 1e3, 2),
        "steep_ms": round(float(seconds[i90] - seconds[i10]) * 1e3, 2),
        "max_slew_v_per_ms": round(abs(float(dv[i_pk])) / 1e3, 2),
        "osc_period_ms": round(period_ms, 2),
        "osc_freq_hz": round(1000.0 / period_ms, 1) if period_ms > 0 else 0.0,
        "settle_ms": round(float(seconds[i_set] - seconds[i0]) * 1e3, 1),
        "i0": i0, "i_vmin": i_vmin,
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
    ax.set_title(f"3.3 V 输出关断（下降沿）　记录窗 {(seconds[-1]-seconds[0])*1e3:.2f} ms")
    ax.legend(loc="upper right", fontsize=9)
    ax.grid(alpha=0.3)

    ax2 = fig.add_subplot(gs[1])
    t0 = seconds[m["i0"]]
    sel = (seconds >= t0 - 2e-4) & (seconds <= t0 + 1.5e-3)
    ax2.plot(seconds[sel] * 1e3, volts[sel], lw=1.4, color="#d62728")
    ax2.axhline(0, color="#888", lw=0.8)
    for frac, lab, col in ((0.9, "90%", "#ff7f0e"), (0.5, "50%", "#1f77b4"),
                           (0.1, "10%", "#2ca02c")):
        ax2.axhline(m["v_top"] - frac * (m["v_top"] - m["v_final"]),
                    color=col, lw=0.9, ls=":", alpha=0.8, label=lab)
    ax2.set_title(f"陡降段：10%→90% = {m['steep_ms']:.2f} ms"
                  f"（10% {m['t10_ms']:.2f} / 50% {m['t50_ms']:.2f} / "
                  f"90% {m['t90_ms']:.2f} ms），最大斜率 "
                  f"{m['max_slew_v_per_ms']:.0f} V/ms")
    ax2.set_xlabel("时间 (ms)")
    ax2.set_ylabel("电压 (V)")
    ax2.legend(fontsize=9, ncol=4)
    ax2.grid(alpha=0.3)

    ax3 = fig.add_subplot(gs[2])
    tv = seconds[m["i_vmin"]]
    sel3 = (seconds >= tv - 5e-4) & (seconds <= tv + 15e-3)
    ax3.plot(seconds[sel3] * 1e3, volts[sel3], lw=1.1, color="#9467bd")
    ax3.axhline(0, color="#888", lw=0.8)
    ax3.set_title(f"负压过冲 / LC 振荡：最低 {m['vmin']:.2f} V，"
                  f"周期 {m['osc_period_ms']:.2f} ms（{m['osc_freq_hz']:.0f} Hz），"
                  f"{m['settle_ms']:.0f} ms 归零")
    ax3.set_xlabel("时间 (ms)")
    ax3.set_ylabel("电压 (V)")
    ax3.grid(alpha=0.3)

    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def main():
    print("=" * 70)
    print(f"下降沿 {TARGET_V}V  vdiv={VDIV} off={VOFF} tdiv={TDIV} delay={DELAY} "
          f"level={TRIG_LEVEL}  抓完保持 Stop")
    print("=" * 70)
    sc = SCPI(HOST, PORT)
    result = {"status": "error", "action": "transient_fall"}
    try:
        sc.drain()
        print("[1] 武装前设好全部参数", flush=True)
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
        vdiv = num(sc.query(f":CHANnel{CH}:SCALe?"), VDIV)
        voff = num(sc.query(f":CHANnel{CH}:OFFSet?"), VOFF)
        tdiv = num(sc.query(":TIMebase:SCALe?"), 0.0)
        mdep = sc.query(":ACQuire:MDEP?")
        print(f"    vdiv={vdiv} off={voff} tdiv={tdiv} MDEP={mdep}", flush=True)

        print("[2] 触发 + AUTO 自由运行（先用贴近信号的电平把引擎带起来）", flush=True)
        sc.write(f":TRIGger:EDGE:SOURce C{CH}")
        sc.write(":TRIGger:EDGE:SLOPe FALLing")
        sc.write(f":TRIGger:EDGE:LEVel {TRIG_LEVEL + 1.6}")   # 3.1V，落在纹波带内
        sc.write(":TRIGger:MODE AUTO")
        sc.write(":TRIGger:RUN")
        ok_run = False
        for _ in range(20):
            time.sleep(0.6)
            if sc.status().startswith(("Auto", "Trig")):
                ok_run = True
                break
        mean_v = sc.measure("MEAN")
        print(f"    状态={sc.status()} 电压={mean_v:.3f}V", flush=True)
        if not ok_run or not (2.0 < mean_v < 4.5):
            raise RuntimeError(f"未就绪：status={sc.status()} MEAN={mean_v:.3f}V")

        print("[3] 先在 AUTO 里把电平挪到 1.5V", flush=True)
        sc.write(f":TRIGger:EDGE:LEVel {TRIG_LEVEL}")
        time.sleep(2.0)

        print("[4] 启动 J-Link（连上后等 2.5s 才写 0），趁这段时间武装", flush=True)
        t0 = time.time()
        proc = output_off_delayed(2500)
        time.sleep(1.5)          # 让 SWD 连接的毛刺先过去
        sc.write(":TRIGger:MODE SINGle")
        time.sleep(1.0)
        print(f"    armed status={sc.status()} level="
              f"{sc.query(':TRIGger:EDGE:LEVel?')}", flush=True)

        print("[5] 等触发", flush=True)
        captured = False
        end = time.time() + 25
        while time.time() < end:
            if sc.status().startswith("Stop"):
                captured = True
                print(f"    已触发（{time.time()-t0:.2f}s）", flush=True)
                break
            time.sleep(0.25)

        print("[6] 读帧", flush=True)
        p_min, p_max = sc.measure("MIN"), sc.measure("MAX")
        wf = sc.read_frame(vdiv, voff)
        v1, t1 = wf["volts"], wf["seconds"]
        print(f"    {wf['npoints']} 点 dt={wf['dt']:.2e}s 窗={wf['window_s']:.2f}s "
              f"V=[{v1.min():.2f},{v1.max():.2f}]", flush=True)
        print(f"    同帧 PAVA MIN={p_min:.2f} MAX={p_max:.2f}", flush=True)

        if not (np.isnan(p_min) or np.isnan(p_max)) and (v1.max() - v1.min()) > 0.05:
            gain = (p_max - p_min) / (v1.max() - v1.min())
            offc = p_min - v1.min() * gain
            v1 = v1 * gain + offc
            print(f"    标定 gain={gain:.4f} off={offc:+.4f}", flush=True)

        print("[7] 保持 Stop，5 s 后再读一次验证帧未丢", flush=True)
        time.sleep(5.0)
        print(f"    状态={sc.status()}", flush=True)
        wf2 = sc.read_frame(vdiv, voff)
        same = (len(wf2["volts"]) == len(v1)
                and abs(float(np.mean(wf2["volts"])) - float(np.mean(v1))) < 0.05)
        print(f"    二次读取一致：{same}（mean {np.mean(v1):.3f} vs "
              f"{np.mean(wf2['volts']):.3f}）", flush=True)

        print("[8] 只恢复固件输出（示波器留 Stop）", flush=True)
        ok2, _ = jlink([f"w1 {ADDR_OUTPUT:#x} 0x01"])
        print(f"    output=1 ok={ok2}", flush=True)

        m = analyze(v1, t1)
        np.savez(os.path.join(OUT_DIR, "captured_fall.npz"), volts=v1, seconds=t1)
        plot(v1, t1, m, os.path.join(PKG_DIR, "fall_3v3.png"))
        result = {
            "status": "ok", "action": "transient_fall",
            "summary": (f"3.3V 关断：{m['v_top']:.2f}V → 最低 {m['vmin']:.2f}V，"
                        f"90/50/10% = {m['t90_ms']:.2f}/{m['t50_ms']:.2f}/"
                        f"{m['t10_ms']:.2f} ms，最大斜率 {m['max_slew_v_per_ms']:.1f} V/ms，"
                        f"LC {m['osc_period_ms']:.2f} ms（{m['osc_freq_hz']:.0f} Hz），"
                        f"{m['settle_ms']:.0f} ms 归零"),
            "details": {"vdiv": vdiv, "voff": voff, "tdiv": tdiv,
                        "window_s": wf["window_s"], "npoints": wf["npoints"],
                        "frame_preserved": bool(same),
                        "metrics": {k: v for k, v in m.items() if not k.startswith("i")}},
        }
    except (OSError, RuntimeError) as exc:
        result = {"status": "error", "action": "transient_fall",
                  "error": {"code": "capture_failed", "message": str(exc)}}
    finally:
        sc.close()

    with open(os.path.join(OUT_DIR, "captured_fall_result.json"), "w") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
    print("\n" + json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    sys.exit(main())
