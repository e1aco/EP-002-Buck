"""EA-002-Buck 多电压纹波测试。

安全约定：
- 写 s_vref_target_mv 前强制钳位 3300..12000 mV（绕过固件钳位是上次 OVP 的原因）
- jlink_exec 的 --value 是十六进制
- 每次写完立即 go 恢复运行
"""
import socket, time, subprocess, sys, json
import numpy as np

SCOPE_IP, SCOPE_PORT = "192.168.9.1", 5025
JLINK = "C:/Users/electronic/.workbuddy-ai/skills/embeddedskills/jlink/scripts/jlink_exec.py"
PY = sys.executable
DEVICE = "STM32F103C8"
ADDR_TARGET = "0x2000002C"
ADDR_OUTPUT = "0x20000013"

VREF_MIN, VREF_MAX = 3300, 12000


# ---------------- 示波器 ----------------
class Scope:
    def __init__(self, host=SCOPE_IP, port=SCOPE_PORT, timeout=20.0):
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
        """读到非空行为止；空行视为响应未到，重试（Siglent 偶发延迟）"""
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

    def drain(self):
        self.s.settimeout(0.12)
        try:
            while True:
                ch = self.s.recv(65536)
                if not ch:
                    break
                self.buf += ch
        except socket.timeout:
            pass
        self.s.settimeout(20.0)
        self.buf = self.buf.lstrip(b"\r\n")

    def read_block(self):
        self.buf = self.buf.lstrip(b"\r\n")
        while len(self.buf) < 2:
            self._fill()
        if self.buf[0:1] != b"#":
            raise RuntimeError(f"not block: {self.buf[:32]!r}")
        ndig = int(self.buf[1:2])
        need = 2 + ndig
        while len(self.buf) < need:
            self._fill()
        blen = int(self.buf[2:need])
        total = need + blen
        while len(self.buf) < total:
            self._fill()
        data = self.buf[need:total]
        self.buf = self.buf[total:]
        return data

    def cfg(self):
        return {
            "vdiv": float(self.query(":CHANnel1:SCALe?")),
            "voff": float(self.query(":CHANnel1:OFFSet?")),
            "srate": float(self.query(":ACQuire:SRATe?")),
            "coup": self.query(":CHANnel1:COUPling?"),
        }

    def meas(self, item):
        r = self.query(f":C1:PAVA? {item}")
        try:
            return float(r.split(",")[1].rstrip("V").rstrip("Hz"))
        except Exception:
            return float("nan")

    def wave(self):
        c = self.cfg()
        self.drain()
        self.write(":WAVeform:SOURce C1")
        self.write(":WAVeform:WIDTh BYTE")
        time.sleep(0.15)
        self.write(":WAVeform:DATA?")
        raw = self.read_block()
        codes = np.frombuffer(raw, dtype=np.int8).astype(np.float64)   # int8!
        volts = codes / 256.0 * 8.0 * c["vdiv"] - c["voff"]
        return volts, c

    def close(self):
        self.s.close()


# ---------------- J-Link ----------------
def jlink(*args):
    cmd = [PY, JLINK] + list(args) + ["--device", DEVICE, "--interface", "SWD",
                                      "--speed", "4000", "--json"]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    try:
        j = json.loads(r.stdout)
        return j.get("status") == "ok", j
    except Exception:
        return False, {"raw": r.stdout[:200], "err": r.stderr[:200]}


def set_voltage(mv):
    """写目标电压（自动钳位），写完恢复运行"""
    if not (VREF_MIN <= mv <= VREF_MAX):
        raise ValueError(f"电压 {mv} mV 超出安全范围 {VREF_MIN}..{VREF_MAX}")
    ok, j = jlink("write-mem", "--address", ADDR_TARGET,
                  "--value", format(mv, "X"), "--width", "32")
    if not ok:
        raise RuntimeError(f"写电压失败: {j}")
    jlink("go")
    return True


def set_output(on):
    ok, j = jlink("write-mem", "--address", ADDR_OUTPUT,
                  "--value", "1" if on else "0", "--width", "8")
    jlink("go")
    return ok


def read_state():
    ok, j = jlink("read-mem", "--address", "0x20000010", "--length", "40", "--width", "32")
    jlink("go")
    if not ok:
        return None
    words = []
    for blk in j["details"]["memory"]:
        for w in blk["data"].split():
            words.append(int(w, 16))
    b10 = words[0]                       # 0x20000010: cascade|fault|ready|output
    return {
        "cascade": b10 & 0xFF,
        "fault": (b10 >> 8) & 0xFF,
        "ready": (b10 >> 16) & 0xFF,
        "output": (b10 >> 24) & 0xFF,
        "vout_mv": words[4],
        "iout_ma": words[5],
        "vref_mv": words[6],
        "target_mv": words[7],
    }


def main():
    sc = Scope()
    sc.write("*CLS"); time.sleep(0.3)
    sc.write(":TRIGger:RUN"); time.sleep(1.0)

    print("初始状态:", read_state())

    points = [
        (3300, 0.5, -3.3),
        (5000, 1.0, -5.0),
        (9000, 2.0, -9.0),
        (12000, 2.0, -12.0),
    ]

    results = []
    for mv, vdiv, voff in points:
        print(f"\n{'='*60}\n目标 {mv/1000:.1f} V")
        set_voltage(mv)
        time.sleep(1.2)

        # DC 档位测平均值
        sc.write(f":CHANnel1:SCALe {vdiv}")
        sc.write(f":CHANnel1:OFFSet {voff}")
        sc.write(":CHANnel1:COUPling DC")
        sc.write(":TIMebase:SCALe 2E-4")
        time.sleep(0.8)
        v_mean = sc.meas("MEAN")
        v_max = sc.meas("MAX")
        v_min = sc.meas("MIN")

        # AC 耦合 + 小档位测纹波
        ac_div = 0.1 if mv <= 5000 else 0.2
        sc.write(":CHANnel1:COUPling AC")
        sc.write(f":CHANnel1:SCALe {ac_div}")
        sc.write(":CHANnel1:OFFSet 0")
        time.sleep(1.8)
        volts, c = sc.wave()

        ac = volts - volts.mean()
        pkpk = ac.max() - ac.min()
        rms = float(np.sqrt(np.mean(ac ** 2)))
        p = np.percentile(ac * 1e3, [1, 5, 50, 95, 99])
        band = p[3] - p[1]

        # 主频
        n = min(len(volts), 200000)
        seg = volts[:n] - volts[:n].mean()
        spec = np.abs(np.fft.rfft(seg))
        freqs = np.fft.rfftfreq(n, 1 / c["srate"])
        f0 = freqs[np.argmax(spec[1:]) + 1]

        st = read_state()
        print(f"  Vout(DC) = {v_mean:.3f} V  (min {v_min:.3f} / max {v_max:.3f})")
        print(f"  纹波 PKPK = {pkpk*1e3:.1f} mV   AC-RMS = {rms*1e3:.1f} mV")
        print(f"  5-95% 主带 = {band:.1f} mV   主频 = {f0:.1f} Hz")
        if st:
            print(f"  固件: vout={st['vout_mv']}mV iout={st['iout_ma']}mA "
                  f"target={st['target_mv']}mV fault={st['fault']}")

        results.append({
            "target_mv": mv, "v_mean": v_mean, "v_min": v_min, "v_max": v_max,
            "pkpk_mv": pkpk * 1e3, "rms_mv": rms * 1e3, "band_mv": band,
            "f0": f0, "ac_div": ac_div,
            "fw_vout": st["vout_mv"] if st else None,
            "fw_iout": st["iout_ma"] if st else None,
            "fault": st["fault"] if st else None,
        })

    # 还原 3.3V + DC 档位
    set_voltage(3300)
    time.sleep(1.0)
    sc.write(":CHANnel1:COUPling DC")
    sc.write(":CHANnel1:SCALe 0.5")
    sc.write(":CHANnel1:OFFSet -3.3")
    sc.write(":TIMebase:SCALe 5E-4")
    sc.close()

    with open("ripple_results.json", "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    print("\n结果已存 ripple_results.json")
    print("\n| 目标 | 实测Vout | 纹波PKPK | AC-RMS | 5-95%带 | 主频 |")
    print("|---|---|---|---|---|---|")
    for r in results:
        print(f"| {r['target_mv']/1000:.1f} V | {r['v_mean']:.3f} V | "
              f"{r['pkpk_mv']:.0f} mV | {r['rms_mv']:.1f} mV | "
              f"{r['band_mv']:.0f} mV | {r['f0']/1000:.1f} kHz |")


if __name__ == "__main__":
    main()
