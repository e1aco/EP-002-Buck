#!/usr/bin/env bash
# ============================================================================
# 独立启动 Saleae Logic 2（无需用户手动点击 GUI）
#
# 背景 / 踩坑记录（2026-10-12 实测）：
#   在 WorkBuddy/CodeBuddy 的 shell 里直接跑 Logic.exe 会"秒退且无任何日志"，
#   原因是宿主注入的两个环境变量把 Electron 打回了纯 Node 模式：
#       ELECTRON_RUN_AS_NODE=1        -> Electron 不启动 GUI，当 Node 用
#       NODE_OPTIONS=--require=...    -> 还会被塞进一个 shim
#   （佐证：`Logic.exe --help` 打印的是 Node.js 的 usage，不是 Electron 的。）
#   清掉这两个变量后又暴露第二层问题：GPU 进程 access violation
#   (exit_code=-1073741819) -> "GPU process isn't usable. Goodbye."
#   用 --in-process-gpu 把 GPU 放进主进程即可绕过。
#
# 用法：
#   bash logic_launch.sh [等待端口秒数, 默认60]
# 退出码：0=端口已开；1=超时未开
# ============================================================================
set -u

LOGIC_EXE="${LOGIC_EXE:-D:/logic/Logic.exe}"
LOGIC_PORT="${LOGIC_PORT:-10430}"
LOGIC_LOG="${LOGIC_LOG:-/tmp/logic_run.log}"
WAIT_SEC="${1:-60}"

if [ ! -f "$LOGIC_EXE" ]; then
  echo "ERROR: 找不到 $LOGIC_EXE（可用 LOGIC_EXE 环境变量覆盖）" >&2
  exit 1
fi

# 已在运行（端口已开）就直接返回，避免起第二个实例
if python -c "
import socket,sys
s=socket.socket(); s.settimeout(1)
try: s.connect(('127.0.0.1',$LOGIC_PORT)); sys.exit(0)
except Exception: sys.exit(1)
finally: s.close()" 2>/dev/null; then
  echo "Logic 2 已在运行，自动化端口 $LOGIC_PORT 可用"
  exit 0
fi

cd "$(dirname "$LOGIC_EXE")" || exit 1

# 关键：剥掉宿主注入的 Electron 干扰变量
env -u ELECTRON_RUN_AS_NODE -u NODE_OPTIONS -u BASH_ENV \
    "$LOGIC_EXE" --in-process-gpu >> "$LOGIC_LOG" 2>&1 &
LOGIC_PID=$!
echo "已拉起 Logic 2 (pid=$LOGIC_PID)，等待自动化端口 $LOGIC_PORT ..."

for _ in $(seq 1 "$WAIT_SEC"); do
  if python -c "
import socket,sys
s=socket.socket(); s.settimeout(1)
try: s.connect(('127.0.0.1',$LOGIC_PORT)); sys.exit(0)
except Exception: sys.exit(1)
finally: s.close()" 2>/dev/null; then
    echo "OK: 自动化端口 $LOGIC_PORT 已就绪"
    exit 0
  fi
  sleep 1
done

echo "TIMEOUT: $WAIT_SEC 秒内端口 $LOGIC_PORT 未打开，见日志 $LOGIC_LOG" >&2
exit 1
