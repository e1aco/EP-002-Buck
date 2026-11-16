"""示波器 SCPI 波形读取库（修正版：BYTE 数据为有符号 int8，零点=屏幕中心）。

关键：Siglent `:WAVeform:DATA?` 在 WIDTh BYTE 下返回 **int8**，
      0 = 屏幕垂直中心（对应 -OFFSet 的电压），不是 uint8 的 128。
"""
import socket, time
import numpy as np

HOST, PORT = "192.168.9.1", 5025


class Scope:
    def __init__(self, host=HOST, port=PORT, timeout=20.0):
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

    def read_line(self, settle=0.12):
        time.sleep(settle)
        while b"\n" not in self.buf:
            self._fill()
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode(errors="replace").strip()

    def query(self, cmd, settle=0.12):
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

    def close(self):
        self.s.close()

    # ---------- 高层接口 ----------
    def snapshot(self):
        return {
            "vdiv": float(self.query(":CHANnel1:SCALe?")),
            "voff": float(self.query(":CHANnel1:OFFSet?")),
            "srate": float(self.query(":ACQuire:SRATe?")),
            "tdiv": float(self.query(":TIMebase:SCALe?")),
            "coupling": self.query(":CHANnel1:COUPling?"),
            "bwlimit": self.query(":CHANnel1:BWLimit?"),
            "probe": float(self.query(":CHANnel1:PROBe?")),
        }

    def meas(self, item):
        r = self.query(f":C1:PAVA? {item}")
        try:
            return float(r.split(",")[1].rstrip("V").rstrip("Hz"))
        except Exception:
            return float("nan")

    def waveform(self):
        """返回 (volts, seconds, info)"""
        cfg = self.snapshot()
        self.drain()
        self.write(":WAVeform:SOURce C1")
        self.write(":WAVeform:WIDTh BYTE")
        time.sleep(0.15)
        self.write(":WAVeform:DATA?")
        raw = self.read_block()

        codes = np.frombuffer(raw, dtype=np.int8).astype(np.float64)
        # int8: 0 = 屏幕中心；满 256 码 = 8 格
        volts = codes / 256.0 * 8.0 * cfg["vdiv"] - cfg["voff"]
        t = np.arange(len(codes)) / cfg["srate"]
        return volts, t, cfg

    def stop(self):
        self.write(":STOP")

    def run(self):
        self.write(":RUN")


def main():
    sc = Scope()
    sc.write("*CLS"); time.sleep(0.3)
    sc.stop(); time.sleep(0.8)

    cfg = sc.snapshot()
    print("配置:", {k: v for k, v in cfg.items()})
    print("SCPI: MAX=%.4f MIN=%.4f MEAN=%.4f PKPK=%.4f" % (
        sc.meas("MAX"), sc.meas("MIN"), sc.meas("MEAN"), sc.meas("PKPK")))

    volts, t, _ = sc.waveform()
    print(f"\n波形 {len(volts):,} 点, 跨度 {t[-1]*1e6:.1f} us")
    print(f"  电压 min={volts.min():.4f} max={volts.max():.4f} "
          f"mean={volts.mean():.4f} pkpk={(volts.max()-volts.min())*1e3:.1f}mV")

    # 去直流后的纹波统计
    ac = volts - volts.mean()
    print(f"  纹波: PKPK={(ac.max()-ac.min())*1e3:.1f}mV  RMS={np.sqrt(np.mean(ac**2))*1e3:.1f}mV")

    # 频谱确认基频
    n = len(volts)
    seg = volts[:min(n, 200000)]
    spec = np.abs(np.fft.rfft(seg - seg.mean()))
    freqs = np.fft.rfftfreq(len(seg), 1 / cfg["srate"])
    peak = freqs[np.argmax(spec[1:]) + 1]
    print(f"  主频 = {peak:.1f} Hz")

    sc.run()
    sc.close()


if __name__ == "__main__":
    main()
