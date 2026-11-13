#!/usr/bin/env bash
# ============================================================================
# 一条命令完成 Saleae 波形采集 + PWM 相位解析（全自动，无需人工点 GUI）
#
#   bash saleae_capture.sh [持续时间秒=2]
#
# 做的事：
#   1) 用 logic_launch.sh 独立拉起 Logic 2（绕过 ELECTRON_RUN_AS_NODE / GPU 崩溃）
#   2) 通过 logic2-automation (gRPC 127.0.0.1:10430) 采集全部 8 个数字通道
#   3) 用 pwm_phase_check.py 自动识别在翻转的两路，算频率/占空比/相位
#
# 依赖：
#   - logic2-automation 装在 $SALEAE_PY 的 venv 里
#   - Logic 2 的 automationServerEnabled=true（已写入 %APPDATA%\Logic\config.json）
#
# ⚠️ 踩坑：export 的 directory 若给相对路径，Logic 会按 **它自己的 cwd**(D:\logic)
#    解析 -> CSV 落到 D:\logic\.cl\...。所以这里必须传**绝对路径**。
# ============================================================================
set -u

PROJ="C:/Users/admin/Desktop/XM/Buck/Verify/PWMVerify"
CL_TOOLS="C:/Users/admin/.workbuddy-ai/skills/cl/tools"
SALEAE_PY="${SALEAE_PY:-C:/Users/admin/.workbuddy-ai/binaries/python/envs/saleae/Scripts/python.exe}"
PY="${PY:-C:/Users/admin/AppData/Local/Programs/Python/Python311/python.exe}"
DURATION="${1:-2}"
OUT_DIR="$PROJ/.cl/capture/saleae"

export NO_PROXY="127.0.0.1,localhost" no_proxy="127.0.0.1,localhost"
unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy

cd "$PROJ" || exit 1

bash "$PROJ/.cl/tools/logic_launch.sh" 60 || exit 1

mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR/digital.csv"

echo "---- 采集 ${DURATION}s (全部 8 通道, 设备最高采样率) ----"
"$SALEAE_PY" "$CL_TOOLS/saleae_extract.py" auto \
  --duration "$DURATION" --channels 0,1,2,3,4,5,6,7 --sample-rate 24000000 \
  --out "$OUT_DIR" 2>&1 | grep -vE '^\s*\+|^RESULT' | tail -15

if [ ! -f "$OUT_DIR/digital.csv" ]; then
  echo "ERROR: 未生成 $OUT_DIR/digital.csv" >&2
  exit 1
fi

echo "---- 解析 (自动识别翻转通道) ----"
"$PY" "$PROJ/.cl/tools/pwm_phase_check.py" "$OUT_DIR/digital.csv" \
  --arr 3599 --timer-clk 72000000 2>&1 | tail -20
