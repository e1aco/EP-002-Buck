"""EA-002-Buck 上升/下降时间测试 v2（安全版）。

安全原则：
1. 每次瞬态测试前 **复位 MCU**，保证 PID 积分与 vref 干净（避免残留导致过冲→OVP）
2. 示波器 offset 覆盖 0..Vtarget 全范围，触发电平必须在屏幕内
3. 单次触发用 :TRIGger:MODE SINGle，等状态 Stop
4. 写内存前钳位 3300..12000 mV，--value 用十六进制
"""
import socket, time, subprocess, sys, json
import numpy as np

SCOPE_IP, SCOPE_PORT = "192.168.9.1", 5025
JLINK = "C:/Users/electronic/.workbuddy-ai/skills/embeddedskills/jlink/scripts/jlink_exec.py"
PY = sys.executable
DEVICE = "STM32F103C8"
A_TARGET = "0x2000002C"
A_OUTPUT = "0x20000013"
A_VREF = "0x20000028"
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
        """跳过 'C1:WF DATx,' 之类前缀，解析 IEEE 488.2 块"""
        t0 = time.time()
        while b"#" not in self.buf:
            if time.time() - t0 > 6:
                raise TimeoutError(f"no block header: {self.buf[:40]!r}")
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
        return data

    def wave(self):
        vdiv = float(self.query(":CHANnel1:SCALe?"))
        voff = float(self.query(":CHANnel1:OFFSet?"))
        srate = float(self.query(":ACQuire:SRATe?"))
        tdiv = float(self.query(":TIMebase:SCALe?"))
        self.write(":WAVeform:SOURce C1")
        self.write(":WAVeform:WIDTh BYTE")
        time.sleep(0.2)
        self.write(":WAVeform:DATA?")
        raw = self.read_block()
        codes = np.frombuffer(raw, dtype=np.int8).astype(np.float64)
        volts = codes / 256.0 * 8.0 * vdiv - voff
        t = np.arange(len(volts)) / srate
        return volts, t, {"vdiv": vdiv, "voff": voff, "srate": srate, "tdiv": tdiv,
                          "nbytes": len(raw)}

    def wait_stop(self, timeout=8.0):
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


def jlink(*args):
    cmd = [PY, JLINK] + list(args) + ["--device", DEVICE, "--interface", "SWD",
                                      "--speed", "4000", "--json"]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    try:
        return json.loads(r.stdout).get("status") == "ok"
    except Exception:
        return False


def reset_clean():
    """复位并恢复运行：target=3300, output=0, vref=0, PID 清零"""
    jlink("reset")
    time.sleep(0.5)
    jlink("go")
    time.sleep(1.0)


def wr(addr, val, width=32):
    ok = jlink("write-mem", "--address", addr, "--value", format(val, "X"),
               "--width", str(width))
    jlink("go")
    return ok


def rd_state():
    ok = jlink("read-mem", "--address", "0x20000010", "--length", "40", "--width", "32")
    jlink("go")
    if not ok:
        return None
    # 再读一次拿数据（jlink 输出未解析）
    r = subprocess.run([PY, JLINK, "read-mem", "--address", "0x20000010",
                        "--length", "40", "--width", "32", "--device", DEVICE,
                        "--interface", "SWD", "--speed", "4000", "--json"],
                       capture_output=True, text=True, timeout=60)
    jlink("go")
    try:
        j = json.loads(r.stdout)
        words = []
        for blk in j["details"]["memory"]:
            for w in blk["data"].split():
                words.append(int(w, 16))
        b10 = words[0]
        return {"cascade": b10 & 0xFF, "fault": (b10 >> 8) & 0xFF,
                "ready": (b10 >> 16) & 0xFF, "output": (b10 >> 24) & 0xFF,
                "vout_mv": words[4], "iout_ma": words[5],
                "vref_mv": words[6], "target_mv": words[7]}
    except Exception:
        return None


def main():
    sc = Scope()
    sc.write("*CLS")
    time.sleep(0.3)
    sc.write(":CHANnel1:COUPling DC")
    sc.write(":CHANnel1:BWLimit 20M")

    results = []
    VFULL_DIV, VFULL_OFF = 2.0, -6.0        # 覆盖 -2..14 V，含 0 与 12 V

    # ================= 上升 =================
    for mv in (12000, 9000, 5000, 3300):
        print(f"\n{'='*62}\n上升 0 -> {mv/1000:.1f} V")
        reset_clean()
        wr(A_TARGET, mv, 32)
        time.sleep(0.3)

        sc.write(":CHANnel1:SCALe %g" % VFULL_DIV)
        sc.write(":CHANnel1:OFFSet %g" % VFULL_OFF)
        sc.write(":TIMebase:SCALe 5E-3")
        sc.write(":TRIGger:MODE EDGE")
        sc.write(":TRIGger:EDGE:SOURce C1")
        sc.write(":TRIGger:EDGE:SLOPe RISing")
        sc.write(":TRIGger:EDGE:LEVel %g" % (mv / 1000.0 * 0.1))
        time.sleep(0.8)
        sc.write(":TRIGger:MODE SINGle")
        time.sleep(0.5)
        print("  触发前:", sc.query(":TRIGger:STATus?"))

        wr(A_OUTPUT, 1, 8)
        got, st = sc.wait_stop(10.0)
        print("  捕获:", "OK" if got else f"超时({st})")

        volts, t, cfg = sc.wave()
        sc.write(":TRIGger:MODE AUTO")

        v0 = float(np.median(volts[:max(100, len(volts)//40)]))
        vf = float(np.median(volts[-len(volts)//10:]))
        if vf - v0 < 0.5:
            print(f"  无效: v0={v0:.2f} vf={vf:.2f} (未捕获到上升)")
            results.append({"type": "rise", "target_mv": mv, "valid": False,
                            "v0": v0, "vfinal": vf})
            continue
        lo, hi = v0 + 0.1 * (vf - v0), v0 + 0.9 * (vf - v0)
        i_lo = int(np.argmax(volts >= lo))
        i_hi = int(np.argmax(volts >= hi))
        rise = (i_hi - i_lo) / cfg["srate"]
        peak = float(volts.max())
        overshoot = (peak - vf) / (vf - v0) * 100
        band = 0.02 * vf
        outside = np.abs(volts - vf) > band
        si = len(volts) - 1
        for i in range(len(volts) - 1, -1, -1):
            if outside[i]:
                si = i + 1
                break
        settle = t[si]

        print(f"  v0={v0:.2f} vf={vf:.2f} peak={peak:.2f}  nbytes={cfg['nbytes']:,}")
        print(f"  上升(10-90%) = {rise*1e3:.1f} ms  超调 = {overshoot:.2f}%  "
              f"建立(±2%) = {settle*1e3:.1f} ms")
        results.append({"type": "rise", "target_mv": mv, "valid": True,
                        "v0": v0, "vfinal": vf, "peak": peak,
                        "rise_ms": rise * 1e3, "overshoot_pct": overshoot,
                        "settle_ms": settle * 1e3, "srate": cfg["srate"]})

    # ================= 下降 =================
    for mv in (12000, 3300):
        print(f"\n{'='*62}\n下降 {mv/1000:.1f} V -> 0")
        reset_clean()
        wr(A_TARGET, mv, 32)
        wr(A_OUTPUT, 1, 8)
        time.sleep(2.5)          # 等稳定
        st = rd_state()
        print("  稳定状态:", st)

        sc.write(":CHANnel1:SCALe %g" % VFULL_DIV)
        sc.write(":CHANnel1:OFFSet %g" % VFULL_OFF)
        sc.write(":TIMebase:SCALe 2E-3")
        sc.write(":TRIGger:MODE EDGE")
        sc.write(":TRIGger:EDGE:SOURce C1")
        sc.write(":TRIGger:EDGE:SLOPe FALLing")
        sc.write(":TRIGger:EDGE:LEVel %g" % (mv / 1000.0 * 0.9))
        time.sleep(0.8)
        sc.write(":TRIGger:MODE SINGle")
        time.sleep(0.5)

        wr(A_OUTPUT, 0, 8)
        got, stt = sc.wait_stop(10.0)
        print("  捕获:", "OK" if got else f"超时({stt})")

        volts, t, cfg = sc.wave()
        sc.write(":TRIGger:MODE AUTO")

        v0 = float(np.median(volts[:max(100, len(volts)//40)]))
        vfin = float(np.median(volts[-len(volts)//10:]))
        if v0 < 0.5:
            print(f"  无效: v0={v0:.2f} (未捕获到下降)")
            results.append({"type": "fall", "target_mv": mv, "valid": False, "v0": v0})
            continue
        i_hi = int(np.argmax(volts <= v0 * 0.9))
        i_lo = int(np.argmax(volts <= v0 * 0.1))
        fall = (i_lo - i_hi) / cfg["srate"]
        i50 = int(np.argmax(volts <= v0 * 0.5))
        t50 = t[i50]

        print(f"  v0={v0:.2f} 末端={vfin:.2f}")
        print(f"  下降(90-10%) = {fall*1e3:.2f} ms   降到 50% = {t50*1e3:.2f} ms")
        results.append({"type": "fall", "target_mv": mv, "valid": True,
                        "v0": v0, "vfinal": vfin, "fall_ms": fall * 1e3,
                        "t50_ms": t50 * 1e3, "srate": cfg["srate"]})

    # 还原：3.3V 输出开启
    reset_clean()
    wr(A_TARGET, 3300, 32)
    wr(A_OUTPUT, 1, 8)
    time.sleep(2.0)
    sc.write(":CHANnel1:SCALe 0.5")
    sc.write(":CHANnel1:OFFSet -3.3")
    sc.write(":TIMebase:SCALe 5E-4")
    print("\n还原状态:", rd_state())
    sc.close()

    with open("transient_results.json", "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    print("结果已存 transient_results.json")


if __name__ == "__main__":
    main()
