"""EA-002-Buck 上升/下降时间测试 v3（修正档位与触发电平）。

关键修正（对照 v2 的失败）：
1. 垂直档位必须让 **0 V 与 Vtarget 同时落在窗口内**：voff = -Vtarget/2，窗口 = Vtarget/2 ± 4*Vdiv
2. 触发电平必须落在信号实际范围内（上升用 10% Vtarget，下降用 90%）
3. 每次测试前复位，保证 PID 积分与 vref 干净
4. 每步打印固件状态与示波器状态，便于定位
"""
import socket, time, subprocess, sys, json
import numpy as np

SCOPE_IP, SCOPE_PORT = "192.168.9.1", 5025
JLINK = "C:/Users/electronic/.workbuddy-ai/skills/embeddedskills/jlink/scripts/jlink_exec.py"
PY = sys.executable
DEVICE = "STM32F103C8"
A_TARGET, A_OUTPUT = "0x2000002C", "0x20000013"
VREF_MIN, VREF_MAX = 3300, 12000


class Scope:
    def __init__(self, host=SCOPE_IP, port=SCOPE_PORT, timeout=25.0):
        self.s = socket.create_connection((host, port), timeout)
        self.s.settimeout(timeout)
        self.buf = b""

    def _fill(self):
        ch = self.s.recv(1 << 20)
        if not ch:
            raise ConnectionError("closed")
        self.buf += ch

    def write(self, cmd):
        self.s.sendall((cmd + "\n").encode())

    def read_line(self, settle=0.2, retries=3):
        for _ in range(retries):
            time.sleep(settle)
            deadline = time.time() + 2.0
            while b"\n" not in self.buf and time.time() < deadline:
                try:
                    self._fill()
                except socket.timeout:
                    break
            if b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                s = line.decode(errors="replace").strip()
                if s:
                    return s
        return ""

    def query(self, cmd, settle=0.2):
        self.write(cmd)
        return self.read_line(settle)

    def read_block(self):
        t0 = time.time()
        while b"#" not in self.buf:
            if time.time() - t0 > 8:
                raise TimeoutError(f"no block: {self.buf[:40]!r}")
            try:
                self._fill()
            except socket.timeout:
                continue
        self.buf = self.buf[self.buf.index(b"#") + 1:]
        while len(self.buf) < 1:
            self._fill()
        ndig = int(chr(self.buf[0]))
        need = 1 + ndig
        while len(self.buf) < need:
            self._fill()
        blen = int(self.buf[1:need])
        total = need + blen
        while len(self.buf) < total:
            self._fill()
        data = self.buf[need:total]
        self.buf = self.buf[total:]
        return data[:blen]

    def horiz_interval(self):
        """从 PREAMBLE 读真实采样间隔（0xB0，float32）"""
        import struct
        self.write(":WAVeform:SOURce C1")
        time.sleep(0.15)
        self.write(":WAVeform:PREamble?")
        raw = self.read_block()
        if len(raw) >= 0xB4:
            dt = struct.unpack("<f", raw[0xB0:0xB4])[0]
            if dt > 0:
                return dt
        return None

    def wave(self):
        vdiv = float(self.query(":CHANnel1:SCALe?"))
        voff = float(self.query(":CHANnel1:OFFSet?"))
        dt = self.horiz_interval()
        srate = 1.0 / dt if dt else float(self.query(":ACQuire:SRATe?"))
        self.write(":WAVeform:SOURce C1")
        self.write(":WAVeform:WIDTh BYTE")
        time.sleep(0.2)
        self.write(":WAVeform:DATA?")
        raw = self.read_block()
        codes = np.frombuffer(raw, dtype=np.int8).astype(np.float64)
        volts = codes / 256.0 * 8.0 * vdiv - voff
        t = np.arange(len(volts)) / srate
        return volts, t, {"vdiv": vdiv, "voff": voff, "srate": srate, "nbytes": len(raw)}

    def wait_stop(self, timeout=10.0):
        t0 = time.time()
        last = ""
        while time.time() - t0 < timeout:
            last = self.query(":TRIGger:STATus?")
            if last.startswith("Stop"):
                return True, last
            time.sleep(0.2)
        return False, last

    def close(self):
        self.s.close()


def jl(*args):
    r = subprocess.run([PY, JLINK] + list(args) +
                       ["--device", DEVICE, "--interface", "SWD", "--speed", "4000", "--json"],
                       capture_output=True, text=True, timeout=60)
    return r.stdout


def wr(addr, val, width=32):
    jl("write-mem", "--address", addr, "--value", format(val, "X"), "--width", str(width))
    jl("go")


def state():
    out = jl("read-mem", "--address", "0x20000010", "--length", "40", "--width", "32")
    jl("go")
    try:
        j = json.loads(out)
        w = []
        for b in j["details"]["memory"]:
            w += [int(x, 16) for x in b["data"].split()]
        return {"cascade": w[0] & 0xFF, "fault": (w[0] >> 8) & 0xFF,
                "ready": (w[0] >> 16) & 0xFF, "output": (w[0] >> 24) & 0xFF,
                "vout": w[4], "iout": w[5], "vref": w[6], "target": w[7]}
    except Exception:
        return {}


def reset_clean():
    jl("reset")
    time.sleep(0.6)
    jl("go")
    time.sleep(1.2)


def pick_vdiv(mv):
    v = mv / 1000.0
    for d in (0.2, 0.5, 1.0, 2.0, 5.0):
        if v / d <= 7.0:          # 目标占 ≤7 格，留余量
            return d
    return 5.0


def main():
    sc = Scope()
    sc.write("*CLS")
    time.sleep(0.3)
    sc.write(":CHANnel1:COUPling DC")
    sc.write(":CHANnel1:BWLimit 20M")

    results = []
    waves = {}

    # ================= 上升 0 -> Vtarget =================
    for mv in (3300, 5000, 9000, 12000):
        vdiv = pick_vdiv(mv)
        voff = -(mv / 1000.0) / 2.0
        lvl = mv / 1000.0 * 0.1
        print(f"\n{'='*66}\n上升 0 -> {mv/1000:.1f} V   (Vdiv={vdiv}, offset={voff}, level={lvl:.2f})")

        reset_clean()
        wr(A_TARGET, mv, 32)
        time.sleep(0.3)

        sc.write(f":CHANnel1:SCALe {vdiv}")
        sc.write(f":CHANnel1:OFFSet {voff}")
        tdiv = 50E-3 if mv <= 9000 else 100E-3
        sc.write(f":TIMebase:SCALe {tdiv}")
        sc.write(f":TIMebase:DELay {tdiv * 5}")
        sc.write(":TRIGger:MODE EDGE")
        sc.write(":TRIGger:EDGE:SOURce C1")
        sc.write(":TRIGger:EDGE:SLOPe RISing")
        sc.write(f":TRIGger:EDGE:LEVel {lvl}")
        time.sleep(0.9)
        print(f"  触发前: {sc.query(':TRIGger:STATus?')}  MEAN={sc.query(':C1:PAVA? MEAN')}")

        sc.write(":TRIGger:MODE SINGle")
        time.sleep(0.6)
        print(f"  武装后: {sc.query(':TRIGger:STATus?')}")

        wr(A_OUTPUT, 1, 8)
        got, st = sc.wait_stop(10.0)
        print(f"  捕获: {'OK' if got else '超时(' + st + ')'}   固件={state()}")

        volts, t, cfg = sc.wave()
        sc.write(":TRIGger:MODE AUTO")

        v0 = float(np.median(volts[:max(200, len(volts)//50)]))
        vf = float(np.median(volts[-len(volts)//10:]))
        valid = (vf - v0) > 0.5
        rec = {"type": "rise", "target_mv": mv, "vdiv": vdiv, "voff": voff,
               "level": lvl, "v0": v0, "vfinal": vf, "valid": valid,
               "captured": got, "srate": cfg["srate"]}
        if valid:
            lo, hi = v0 + 0.1 * (vf - v0), v0 + 0.9 * (vf - v0)
            i_lo = int(np.argmax(volts >= lo))
            i_hi = int(np.argmax(volts >= hi))
            rise = (i_hi - i_lo) / cfg["srate"]
            peak = float(volts.max())
            rec.update({"rise_ms": rise * 1e3, "peak": peak,
                        "overshoot_pct": (peak - vf) / (vf - v0) * 100})
            band = 0.02 * vf
            outside = np.abs(volts - vf) > band
            si = len(volts) - 1
            for i in range(len(volts) - 1, -1, -1):
                if outside[i]:
                    si = i + 1
                    break
            rec["settle_ms"] = t[min(si, len(t) - 1)] * 1e3
            # 斜率（线性段）
            rec["slew_mv_per_ms"] = (vf - v0) / (rise * 1e3) * 1e3
            waves[f"rise_{mv}"] = (t, volts)
            print(f"  v0={v0:.3f} vf={vf:.3f} peak={peak:.3f}")
            print(f"  上升(10-90%)={rise*1e3:.2f} ms  超调={rec['overshoot_pct']:.2f}%  "
                  f"建立(±2%)={rec['settle_ms']:.1f} ms  平均斜率={rec['slew_mv_per_ms']:.0f} mV/ms")
        else:
            print(f"  无效: v0={v0:.3f} vf={vf:.3f}")
        results.append(rec)

    # ================= 下降 Vtarget -> 0 =================
    for mv in (12000, 9000, 5000, 3300):
        vdiv = pick_vdiv(mv)
        voff = -(mv / 1000.0) / 2.0
        lvl = mv / 1000.0 * 0.9
        print(f"\n{'='*66}\n下降 {mv/1000:.1f} V -> 0   (Vdiv={vdiv}, offset={voff}, level={lvl:.2f})")

        reset_clean()
        wr(A_TARGET, mv, 32)
        wr(A_OUTPUT, 1, 8)
        time.sleep(2.5)
        st_fw = state()
        print(f"  稳定: {st_fw}")

        sc.write(f":CHANnel1:SCALe {vdiv}")
        sc.write(f":CHANnel1:OFFSet {voff}")
        sc.write(":TIMebase:SCALe 2E-3")
        sc.write(":TIMebase:DELay 10E-3")
        sc.write(":TRIGger:MODE EDGE")
        sc.write(":TRIGger:EDGE:SOURce C1")
        sc.write(":TRIGger:EDGE:SLOPe FALLing")
        sc.write(f":TRIGger:EDGE:LEVel {lvl}")
        time.sleep(0.9)
        print(f"  触发前: {sc.query(':TRIGger:STATus?')}  MEAN={sc.query(':C1:PAVA? MEAN')}")

        sc.write(":TRIGger:MODE SINGle")
        time.sleep(0.6)

        wr(A_OUTPUT, 0, 8)
        got, st = sc.wait_stop(10.0)
        print(f"  捕获: {'OK' if got else '超时(' + st + ')'}")

        volts, t, cfg = sc.wave()
        sc.write(":TRIGger:MODE AUTO")

        v0 = float(np.median(volts[:max(200, len(volts)//50)]))
        vfin = float(np.median(volts[-len(volts)//10:]))
        valid = v0 > 0.5
        rec = {"type": "fall", "target_mv": mv, "vdiv": vdiv, "voff": voff,
               "level": lvl, "v0": v0, "vfinal": vfin, "valid": valid,
               "captured": got, "srate": cfg["srate"]}
        if valid:
            i_hi = int(np.argmax(volts <= v0 * 0.9))
            i_lo = int(np.argmax(volts <= v0 * 0.1))
            fall = (i_lo - i_hi) / cfg["srate"]
            i50 = int(np.argmax(volts <= v0 * 0.5))
            rec.update({"fall_ms": fall * 1e3, "t50_ms": t[min(i50, len(t) - 1)] * 1e3})
            waves[f"fall_{mv}"] = (t, volts)
            print(f"  v0={v0:.3f} 末端={vfin:.3f}")
            print(f"  下降(90-10%)={fall*1e3:.2f} ms   降到50%={rec['t50_ms']:.2f} ms")
        else:
            print(f"  无效: v0={v0:.3f}")
        results.append(rec)

    # 还原 3.3V
    reset_clean()
    wr(A_TARGET, 3300, 32)
    wr(A_OUTPUT, 1, 8)
    time.sleep(2.0)
    sc.write(":CHANnel1:SCALe 0.5")
    sc.write(":CHANnel1:OFFSet -1.65")
    sc.write(":TIMebase:SCALe 5E-4")
    sc.write(":TRIGger:EDGE:SLOPe RISing")
    sc.write(":TRIGger:EDGE:LEVel 1.65")
    print("\n还原:", state())
    sc.close()

    with open("transient_results.json", "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    np.savez_compressed("transient_waves.npz",
                        **{k: np.vstack([v[0], v[1]]) for k, v in waves.items()})
    print("已存 transient_results.json / transient_waves.npz")


if __name__ == "__main__":
    main()
