"""RTT 命令发送（走 JLinkGDBServer 的 RTT Telnet 端口，双向）

用法：
  python rtt_send.py "V3300" "ON" "?"
  python rtt_send.py --wait 1.5 "OFF"

原理：JLinkGDBServerCL 的 -RTTTelnetPort 端口是双向的——写 socket 的数据会被
转发到目标的下行缓冲（down-buffer 0），目标上行写（up-buffer 0）从同一 socket 读回。
"""
import argparse
import socket
import sys
import time

DEFAULT_PORT = 19021


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmds", nargs="+", help="要发送的命令（自动补 \\n）")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--wait", type=float, default=0.6,
                    help="每条命令后等待回复的秒数")
    ap.add_argument("--timeout", type=float, default=3.0)
    args = ap.parse_args()

    s = socket.create_connection((args.host, args.port), timeout=args.timeout)
    s.settimeout(args.wait)

    # 丢掉挂接时的欢迎信息
    try:
        while True:
            d = s.recv(4096)
            if not d:
                break
    except socket.timeout:
        pass

    rc = 0
    for cmd in args.cmds:
        s.sendall((cmd + "\n").encode())
        time.sleep(0.05)
        buf = b""
        deadline = time.time() + args.wait
        while time.time() < deadline:
            try:
                d = s.recv(4096)
                if not d:
                    break
                buf += d
            except socket.timeout:
                break
        text = buf.decode(errors="replace").strip()
        print(f"> {cmd}")
        print(f"< {text if text else '(无回复)'}")
        if not text:
            rc = 1
    s.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
