"""下降沿采集脚本 - 使用 scope_runtime.py 的 Scope 类

当前状态：输出 3.3V 稳定
目标：武装 SINGle → 关闭输出 → 捕获下降沿 → 分析 → 画图

关键参数（实测教训）：
- 窗口 3000 ms（下降沿含 LC 振荡，持续 5-10 ms，加上放电段总 ~2.5 s）
- 触发电平 = target * 0.45 = 1.485V（留裕量，不贴稳态）
- offset = -target/2 = -1.65V（0V 必须在窗口内）
- vdiv = 0.5V
"""
import json
import os
import subprocess
import sys
import time

import numpy as np

# 导入 skill 的 scope_runtime
SCOPE_SKILL_DIR = os.path.join(
    os.path.expanduser("~"),
    ".config", "opencode", "skills", "em-skills",
    "embeddedskills", "scope", "scripts"
)
sys.path.insert(0, SCOPE_SKILL_DIR)

from scope_runtime import Scope, ScopeError

HOST = "192.168.9.1"
PORT = 5025
CHANNEL = 1
TARGET_V = 3.3
WINDOW_MS = 3000.0
TRIGGER_LEVEL = TARGET_V * 0.45  # 1.485V

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
PKG_DIR = os.path.join(OUT_DIR, "..", "PKG")
os.makedirs(PKG_DIR, exist_ok=True)

JLINK = r"C:\Program Files\SEGGER\JLink_V970\JLink.exe"


def turn_off_output():
    """通过 J-Link 写 s_output_on=0 关闭输出。"""
    script_content = "w1 0x20000013 0x00\nq\n"
    script_path = os.path.join(OUT_DIR, "_jlink_off.jlink")
    with open(script_path, "w") as f:
        f.write(script_content)

    r = subprocess.run(
        [JLINK, "-device", "STM32F103C8", "-if", "SWD", "-speed", "4000",
         "-autoconnect", "1", "-CommanderScript", script_path],
        capture_output=True, timeout=15
    )
    try:
        out = r.stdout.decode("utf-8", errors="replace")
    except Exception:
        out = str(r.stdout)
    print(f"[JLINK] returncode={r.returncode}")
    # Check for success marker
    ok = "Writing 00 -> 20000013" in out and r.returncode == 0
    print(f"[JLINK] output:\n{out[-300:]}")
    return ok


def turn_on_output():
    """通过 J-Link 写 s_output_on=1 重新打开输出（恢复用）。"""
    script_content = "w1 0x20000013 0x01\nq\n"
    script_path = os.path.join(OUT_DIR, "_jlink_on.jlink")
    with open(script_path, "w") as f:
        f.write(script_content)

    r = subprocess.run(
        [JLINK, "-device", "STM32F103C8", "-if", "SWD", "-speed", "4000",
         "-autoconnect", "1", "-CommanderScript", script_path],
        capture_output=True, timeout=15
    )
    try:
        out = r.stdout.decode("utf-8", errors="replace")
    except Exception:
        out = str(r.stdout)
    print(f"[JLINK-ON] returncode={r.returncode}")
    ok = "Writing 01 -> 20000013" in out and r.returncode == 0
    return ok


def analyze_fall(volts, seconds, target_v):
    """下降沿分析：陡降段、放电段、负压过冲、LC振荡。"""
    n = len(volts)
    v0 = float(np.median(volts[:max(200, n // 60)]))
    vf = float(np.median(volts[-max(200, n // 12):]))

    if v0 < 0.3 * target_v:
        print(f"[ANALYSIS] v0={v0:.3f}V too low (expected ~{target_v}V)")
        return None

    # 陡降段起点：电压降到 80% v0
    i_start = int(np.argmax(volts <= v0 * 0.8))
    if i_start >= n - 1:
        print("[ANALYSIS] No steep drop detected")
        return None

    dv = np.diff(volts)
    seg = dv[i_start:]
    peak_slope = float(seg.min()) if len(seg) else 0.0
    i_end = i_start
    if peak_slope < 0:
        for k in range(i_start, len(dv)):
            if abs(dv[k]) < abs(peak_slope) * 0.1:
                i_end = k
                break

    i10 = int(np.argmax(volts <= v0 * 0.1))
    i50 = int(np.argmax(volts <= v0 * 0.5))
    vmin = float(volts.min())
    i_vmin = int(np.argmin(volts))

    # LC 振荡周期分析
    osc_period_ms = 0.0
    if i_vmin < n - 1:
        after_min = volts[i_vmin:]
        zero_crossings = []
        for i in range(1, len(after_min)):
            if (after_min[i-1] < 0 <= after_min[i]) or (after_min[i-1] >= 0 > after_min[i]):
                zero_crossings.append(i + i_vmin)
        if len(zero_crossings) >= 3:
            same_dir = [zero_crossings[i] for i in range(0, len(zero_crossings), 2)]
            if len(same_dir) >= 2:
                osc_period_ms = float(seconds[same_dir[1]] - seconds[same_dir[0]]) * 1e3

    # 衰减时间：从 vmin 到电压稳定在 ±2% v0 内
    settle_idx = n - 1
    band = 0.02 * max(v0, 1.0)
    for i in range(i_vmin, n):
        if abs(volts[i]) > band:
            settle_idx = min(i + 1, n - 1)

    return {
        "v0": round(v0, 3),
        "vfinal": round(vf, 3),
        "vmin": round(vmin, 3),
        "undershoot_v": round(vmin if vmin < 0 else 0.0, 3),
        "steep_ms": round(float(seconds[i_end] - seconds[i_start]) * 1e3, 2) if i_end > i_start else 0.0,
        "v_after_steep": round(float(volts[min(i_end + 1, n - 1)]), 3),
        "t50_ms": round(float(seconds[i50]) * 1e3, 2) if i50 > 0 and i50 < n else float("nan"),
        "t10_ms": round(float(seconds[i10]) * 1e3, 2) if i10 > 0 and i10 < n else float("nan"),
        "osc_period_ms": round(osc_period_ms, 2),
        "osc_freq_hz": round(1000.0 / osc_period_ms, 1) if osc_period_ms > 0 else 0,
        "settle_ms": round(float(seconds[settle_idx]) * 1e3, 2),
        "i_start": i_start,
        "i_end": i_end,
        "i_vmin": i_vmin,
    }


def plot_fall(volts, seconds, metrics, target_v, save_path):
    """画下降沿波形图：全景 + 局部放大。"""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    # 降采样到 ~5000 点
    n = len(volts)
    step = max(1, n // 5000)
    v = volts[::step]
    t = seconds[::step]

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 10), gridspec_kw={"height_ratios": [3, 2]})

    # 全景图
    t_ms = t * 1e3
    ax1.plot(t_ms, v, color="#e74c3c", linewidth=0.8, label="V_out")
    ax1.axhline(y=target_v, color="#2ecc71", linestyle="--", alpha=0.5, label=f"Target {target_v}V")
    ax1.axhline(y=0, color="#95a5a6", linestyle="-", alpha=0.3)

    if metrics:
        # 标注关键点（映射到降采样索引）
        i_start_ds = metrics["i_start"] // step
        i_end_ds = metrics["i_end"] // step
        i_vmin_ds = metrics["i_vmin"] // step

        if i_start_ds < len(v):
            ax1.plot(t_ms[i_start_ds], v[i_start_ds], "o", color="#f39c12",
                     markersize=8, zorder=5, label=f"Start ({metrics['steep_ms']:.1f}ms)")
        if i_vmin_ds < len(v):
            ax1.plot(t_ms[i_vmin_ds], v[i_vmin_ds], "v", color="#9b59b6",
                     markersize=8, zorder=5, label=f"Vmin={metrics['vmin']:.1f}V")
        ax1.axhline(y=target_v * 0.5, color="#3498db", linestyle=":", alpha=0.5,
                    label=f"50% ({metrics['t50_ms']:.0f}ms)")

    ax1.set_xlabel("Time (ms)")
    ax1.set_ylabel("Voltage (V)")
    title = f"Fall Edge: {target_v}V Output Turn-Off"
    if metrics:
        title += f" (settle={metrics['settle_ms']:.0f}ms, Vmin={metrics['vmin']:.1f}V)"
    ax1.set_title(title)
    ax1.legend(loc="upper right", fontsize=8)
    ax1.grid(True, alpha=0.3)

    # 局部放大：vmin 前后窗口
    if metrics and metrics["i_vmin"] > 0:
        t_vmin = seconds[metrics["i_vmin"]]
        # 放大 vmin 前后 8ms（足够看到 LC 振荡前几个周期）
        mask = (seconds >= t_vmin - 0.002) & (seconds <= t_vmin + 0.012)
        if mask.sum() > 10:
            v_zoom = volts[mask]
            t_zoom = seconds[mask] * 1e3
            # 降采样
            if len(v_zoom) > 3000:
                zstep = len(v_zoom) // 3000
                v_zoom = v_zoom[::zstep]
                t_zoom = t_zoom[::zstep]

            ax2.plot(t_zoom, v_zoom, color="#e74c3c", linewidth=0.8)
            ax2.axhline(y=0, color="#95a5a6", linestyle="-", alpha=0.5)
            if metrics["vmin"] < 0:
                ax2.axhline(y=metrics["vmin"], color="#9b59b6", linestyle="--",
                            alpha=0.5, label=f"Vmin={metrics['vmin']:.1f}V")
            if metrics["osc_period_ms"] > 0:
                freq = metrics["osc_freq_hz"]
                ax2.set_title(f"LC Oscillation Zoom: period={metrics['osc_period_ms']:.2f}ms, "
                              f"freq={freq:.0f}Hz")
            else:
                ax2.set_title("Fall Edge Zoom (steep drop + LC oscillation)")
            ax2.set_xlabel("Time (ms)")
            ax2.set_ylabel("Voltage (V)")
            ax2.legend(fontsize=8)
            ax2.grid(True, alpha=0.3)
    else:
        ax2.text(0.5, 0.5, "No zoom available", ha="center", va="center",
                 transform=ax2.transAxes)

    plt.tight_layout()
    plt.savefig(save_path, dpi=150, bbox_inches="tight")
    print(f"[PLOT] saved to {save_path}")
    plt.close()


def main():
    print("=" * 60)
    print(f"Fall Edge Capture: target={TARGET_V}V, window={WINDOW_MS}ms")
    print("=" * 60)

    sc = Scope(HOST, PORT)

    try:
        # 0) 确保引擎在跑——先设安全档位再查
        sc.drain()
        # 先设 1V/div + AUTO 触发让引擎恢复（不用 AUToset，避免档位被改乱）
        sc.write(":C1:COUPling DC")
        sc.write(":C1:VDIV 1.00E+00V")
        sc.write(":C1:OFSt -1.65E+00V")
        sc.write(":C1:BWL FULL")
        sc.write(":TIMebase:TDIV 100MS")
        sc.write(":TIMebase:DELay 0")
        sc.write(":TRIGger:MODE AUTO")
        sc.write(":TRIGger:EDGE:SOURce C1")
        sc.write(":TRIGger:EDGE:SLOPe RISing")
        sc.write(":TRIGger:EDGE:LEVel 0.1")
        sc.write(":RUN")
        time.sleep(3.0)
        sc.drain()

        mean_v = float("nan")
        for _ in range(3):
            r = sc.query(f":C{CHANNEL}:PAVA? MEAN")
            if r and "****" not in r:
                try:
                    mean_v = float(r.split(",")[-1].rstrip("V"))
                    break
                except (ValueError, IndexError):
                    pass
            time.sleep(1.0)

        print(f"\n[CHECK] Current MEAN = {mean_v:.3f}V")
        if np.isnan(mean_v) or mean_v < 2.0:
            print("[ERROR] Output not at target voltage. Need stable 3.3V.")
            sc.close()
            return 1

        # 1) 设置垂直档位
        vdiv = 0.5
        voff = -1.65
        sc.set_channel(CHANNEL, vdiv=vdiv, voff=voff, coupling="DC", bwlimit="FULL")
        print(f"[SET] Channel: DC, vdiv={vdiv}V, offset={voff}V, BW=FULL")

        # 2) 设置时基: window=3000ms -> tdiv=600ms
        tdiv_s = WINDOW_MS / 5.0 / 1000.0  # 0.6s
        delay_s = tdiv_s * 2.0  # 1.2s
        sc.set_timebase(tdiv=tdiv_s, delay=delay_s)
        time.sleep(1.0)
        print(f"[SET] Timebase: tdiv={tdiv_s:.3f}s, delay={delay_s:.3f}s")

        # 确保引擎在跑——换档后 PAVA 可能暂时返回 ****
        sc.write(":RUN")
        time.sleep(2.0)
        sc.drain()

        # 武装前的 PAVA 检查（带 AUToset 恢复）
        def safe_mean():
            r = sc.query(f":C{CHANNEL}:PAVA? MEAN")
            if r and "****" not in r:
                try:
                    return float(r.split(",")[-1].rstrip("V"))
                except (ValueError, IndexError):
                    pass
            return float("nan")

        cur = safe_mean()
        if np.isnan(cur):
            # 不用 AUToset（它会把档位搞乱），改为重设档位 + RUN + 多等一会
            print("[ARM] PAVA stuck after setting, retrying with RUN + wait...")
            sc.set_channel(CHANNEL, vdiv=vdiv, voff=voff, coupling="DC", bwlimit="FULL")
            sc.set_timebase(tdiv=tdiv_s, delay=delay_s)
            sc.write(":TRIGger:MODE AUTO")
            sc.write(":TRIGger:EDGE:SOURce C1")
            sc.write(":TRIGger:EDGE:SLOPe RISing")
            sc.write(":TRIGger:EDGE:LEVel 0.1")  # 低电平让噪声跨过
            sc.write(":RUN")
            time.sleep(5.0)
            sc.drain()
            cur = safe_mean()
            print(f"[ARM] After retry: MEAN={cur:.3f}V")

        if np.isnan(cur) or cur < 2.0:
            print(f"[ERROR] Cannot establish measurement (MEAN={cur})")
            sc.close()
            return 1

        mean_v = cur

        # 3) 武装 SINGle
        pre_level = mean_v - 0.05
        print(f"\n[ARM] pre_level={pre_level:.3f}V, trigger_level={TRIGGER_LEVEL:.3f}V")

        for lv in (pre_level, pre_level - 0.05, pre_level - 0.1):
            sc.set_trigger(slope="FALLing", level=max(lv, 0.05), mode="AUTO", channel=CHANNEL)
            time.sleep(2.0)
            cur_mean = safe_mean()
            print(f"[ARM] AUTO level={lv:.3f}V -> MEAN={cur_mean:.3f}V")
            if not np.isnan(cur_mean) and cur_mean > 1.0:
                break
        else:
            sc.close()
            print("[ERROR] Failed to establish acquisition")
            return 1

        # 引擎在跑，切 SINGle
        sc.write(":TRIGger:MODE SINGle")
        time.sleep(0.8)
        mode_r = sc.query(":TRIGger:MODE?")
        print(f"[ARM] After SINGle: mode={mode_r}")

        # 挪到真正的触发电平
        sc.write(f":TRIGger:EDGE:LEVel {TRIGGER_LEVEL}")
        time.sleep(0.5)
        print(f"[ARM] Trigger level set to {TRIGGER_LEVEL:.3f}V")

        # 等待仪器进入等待状态
        time.sleep(1.5)
        print("[ARM] SINGle armed, waiting for trigger...")

        # 4) 关闭输出触发下降沿
        print("\n[TRIGGER] Turning off output via J-Link...")
        t0 = time.time()
        ok = turn_off_output()
        t1 = time.time()
        print(f"[TRIGGER] J-Link done in {t1-t0:.2f}s, ok={ok}")

        if not ok:
            print("[ERROR] J-Link failed to turn off output")
            sc.run()
            return 1

        # 5) 等待触发完成
        print("[WAIT] Waiting for trigger...")
        ok, status = sc.wait_stop(timeout=30.0, poll=0.5)
        if ok:
            print(f"[WAIT] Trigger captured! status={status}")
        else:
            print(f"[WAIT] Timeout (status={status}), attempting waveform read anyway...")

        # 6) 读波形
        print("\n[WAVE] Reading waveform...")
        try:
            wf = sc.waveform(CHANNEL)
        except ScopeError as e:
            print(f"[ERROR] Waveform read failed: {e}")
            sc.run()
            return 1

        # 7) 恢复示波器
        sc.run()
        print("[RESTORE] Scope back to RUN mode")

    except Exception as e:
        print(f"[EXCEPTION] {e}")
        try:
            sc.run()
        except:
            pass
        sc.close()
        return 1
    finally:
        try:
            sc.close()
        except:
            pass

    # 8) 检查数据
    if wf["npoints"] < 100:
        print(f"[ERROR] Too few points: {wf['npoints']}")
        return 1

    volts = wf["volts"]
    seconds = wf["seconds"]

    print(f"[DATA] {wf['npoints']} points, V=[{volts.min():.3f}, {volts.max():.3f}], "
          f"t=[{seconds[0]:.4f}, {seconds[-1]:.4f}]s, "
          f"dt={wf['dt']:.3e}s")

    # 9) 分析
    metrics = analyze_fall(volts, seconds, TARGET_V)

    if metrics is None:
        print("[ERROR] Analysis failed: waveform doesn't contain expected fall edge")
        print(f"  v0={float(volts[0]):.3f}, vend={float(volts[-1]):.3f}")
        np.save(os.path.join(OUT_DIR, "captured_fall_raw.npy"), volts)
        # 仍然画一个原始图
        plot_fall(volts, seconds, None, TARGET_V, os.path.join(PKG_DIR, "fall_3v3_raw.png"))
        return 1

    # 10) 保存数据
    npz_path = os.path.join(OUT_DIR, "captured_fall.npz")
    np.savez(npz_path, volts=volts, seconds=seconds,
             vdiv=wf["vdiv"], voff=wf["voff"])
    print(f"\n[SAVE] Raw data: {npz_path}")

    # 11) 画图
    png_path = os.path.join(PKG_DIR, "fall_3v3.png")
    plot_fall(volts, seconds, metrics, TARGET_V, png_path)

    # 12) 输出结果
    result = {
        "status": "ok",
        "action": "transient_fall",
        "summary": (f"下降沿: v0={metrics['v0']}V -> vmin={metrics['vmin']}V, "
                    f"陡降={metrics['steep_ms']}ms, 到50%={metrics['t50_ms']}ms, "
                    f"振荡周期={metrics['osc_period_ms']}ms({metrics['osc_freq_hz']}Hz), "
                    f"衰减={metrics['settle_ms']}ms"),
        "details": {
            "target_v": TARGET_V,
            "window_ms": float(seconds[-1] - seconds[0]) * 1e3,
            "npoints": wf["npoints"],
            "vdiv": wf["vdiv"],
            "voff": wf["voff"],
            "trigger_level": TRIGGER_LEVEL,
            "metrics": metrics,
        }
    }

    json_path = os.path.join(OUT_DIR, "captured_fall_result.json")
    with open(json_path, "w") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)

    print(f"\n{'='*60}")
    print(f"RESULT: {result['summary']}")
    print(f"{'='*60}")
    print(json.dumps(result, ensure_ascii=False, indent=2))

    # 13) 尝试恢复输出（方便下次测试）
    print("\n[RECOVERY] Attempting to re-enable output...")
    turn_on_output()
    time.sleep(2.0)

    return 0


if __name__ == "__main__":
    sys.exit(main())
