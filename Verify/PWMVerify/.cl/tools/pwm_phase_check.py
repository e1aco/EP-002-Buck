#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""PWMVerify 相位/死区实测：解析 Saleae 导出的数字 CSV，算两路 PWM 的
频率、占空比，以及"从定时器上升沿相对主定时器的滞后"。

用法:
    python pwm_phase_check.py <capture.csv> [--master <通道>] [--slave <通道>]
                              [--arr 719] [--timer-clk 72000000]

    --master/--slave 可给列名(如 "Channel 0")或列序号(如 0)。
    不给则自动在前两列里挑：先出现的当主，另一列当从。

输出: 频率、占空比、上升沿滞后(均值/中位/最小/最大)，并按
      滞后 ns = 计数 / timer_clk 反推实际滞后计数 N。
"""

import argparse
import csv
import re
import statistics
import sys

TRUE = {"1", "true", "high", "h", "on", "yes"}
FALSE = {"0", "false", "low", "l", "off", "no"}


def to_level(s):
    v = str(s).strip().strip('"').lower()
    if v in TRUE:
        return 1
    if v in FALSE:
        return 0
    try:
        return 1 if float(v) >= 0.5 else 0
    except ValueError:
        return None


def sniff_time_unit(header):
    h = header.lower()
    # 注意顺序：ns/us/ms 必须先于 s 判断，否则 "[ns]" 会被 "[s]" 之外的规则漏掉。
    # Saleae Logic 2 默认导出表头就是 "Time [s]"（单位=秒），早期版本漏了这条，
    # 导致落到下面的启发式把"秒"当成"微秒"，周期/频率整体差 1e6 倍（实测 2026-10-12）。
    if "[ns]" in h or "(ns)" in h:
        return 1e-9
    if "[us]" in h or "(us)" in h or "µs" in h:
        return 1e-6
    if "[ms]" in h or "(ms)" in h:
        return 1e-3
    if "[s]" in h or "(s)" in h:
        return 1.0
    return None


def load(path):
    with open(path, "r", encoding="utf-8-sig", errors="replace", newline="") as f:
        sample = f.read(8192)
        f.seek(0)
        try:
            dialect = csv.Sniffer().sniff(sample, delimiters=",;\t")
        except csv.Error:
            dialect = csv.excel
        rows = list(csv.reader(f, dialect))
    if not rows:
        raise SystemExit("CSV 为空")
    header = rows[0]
    unit = sniff_time_unit(",".join(header))
    data = [r for r in rows[1:] if r and r[0].strip() != ""]
    if not data:
        raise SystemExit("CSV 没有数据行")
    if unit is None:
        vals = []
        for r in data[:200]:
            try:
                vals.append(abs(float(r[0])))
            except ValueError:
                pass
        mx = max(vals) if vals else 1.0
        unit = 1e-9 if mx > 1e-2 else (1e-6 if mx > 1e-5 else 1.0)
    t = [float(r[0]) * unit for r in data]
    return header, t, data


def col_index(header, spec):
    spec = str(spec).strip()
    if spec.isdigit():
        return int(spec) + 1  # 第 0 列是时间
    for i, h in enumerate(header):
        if i and spec.lower() in h.strip().lower():
            return i
    raise SystemExit("找不到通道: %s (表头: %s)" % (spec, header))


def edges(header, t, data, ci):
    """返回 (上升沿时间列表, 下降沿时间列表)"""
    out_r, out_f = [], []
    prev = None
    for i, r in enumerate(data):
        lv = to_level(r[ci])
        if lv is None:
            continue
        if prev is not None and lv != prev:
            (out_r if lv == 1 else out_f).append(t[i])
        prev = lv
    return out_r, out_f


def period_stats(times):
    if len(times) < 2:
        return None
    ds = [b - a for a, b in zip(times, times[1:]) if b > a]
    if not ds:
        return None
    return statistics.median(ds)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--master", default=None)
    ap.add_argument("--slave", default=None)
    ap.add_argument("--arr", type=int, default=719)
    ap.add_argument("--timer-clk", type=float, default=72_000_000.0)
    a = ap.parse_args()

    header, t, data = load(a.csv)
    print("列: %s" % header)

    if a.master is None or a.slave is None:
        found = []
        for i in range(1, len(header)):
            r, f = edges(header, t, data, i)
            if len(r) >= 4:
                found.append((i, r, f))
        if len(found) < 2:
            raise SystemExit("自动识别失败：只找到 %d 个有跳变的通道，请用 --master/--slave 指定" % len(found))
        found.sort(key=lambda x: x[0])
        (ci_m, rm, fm), (ci_s, rs, fs) = found[0], found[1]
        print("自动选定: 主=%s  从=%s" % (header[ci_m], header[ci_s]))
    else:
        ci_m = col_index(header, a.master)
        ci_s = col_index(header, a.slave)
        rm, fm = edges(header, t, data, ci_m)
        rs, fs = edges(header, t, data, ci_s)

    def report(name, rises, falls):
        p = period_stats(rises)
        if p:
            print("%-12s 频率=%.3f kHz  周期=%.3f us  上升沿数=%d"
                  % (name, 1e-3 / p, p * 1e6, len(rises)))
        if rises and falls and len(rises) >= 2:
            # 占空比：每个上升沿到其后第一个下降沿
            ds = []
            for r in rises[:-1]:
                nxt = [f for f in falls if f > r]
                if nxt:
                    ds.append(nxt[0] - r)
            if ds:
                print("%-12s 占空比=%.2f %%" % (name, 100.0 * statistics.median(ds) / p))

    report("主(%s)" % header[ci_m], rm, fm)
    report("从(%s)" % header[ci_s], rs, fs)

    # 相位：每个主上升沿 -> 其后第一个从上升沿
    per = period_stats(rm)
    lags = []
    for r in rm:
        nxt = [s for s in rs if s > r]
        if not nxt:
            continue
        d = nxt[0] - r
        if per and d > 0.5 * per:   # 跨到下一个周期了，不算
            continue
        lags.append(d)
    if not lags:
        print("无法计算相位差（从通道上升沿不足）")
        return
    lag = statistics.median(lags)
    print("-" * 56)
    print("上升沿滞后: 中位=%.0f ns  均值=%.0f ns  最小=%.0f ns  最大=%.0f ns"
          % (lag * 1e9, statistics.mean(lags) * 1e9, min(lags) * 1e9, max(lags) * 1e9))
    print("换算计数: N_rise = %.2f counts (1 count = %.2f ns)"
          % (lag * a.timer_clk, 1e9 / a.timer_clk))

    # 下降沿超前量：从定时器的下降沿比主定时器"早"多少（内缩的另一半）
    # 内缩对称的条件是 超前量 == 滞后量；两者不等说明存在触发链路固有延迟。
    leads = []
    for f in fm:
        prev = [s for s in fs if s <= f]
        if not prev:
            continue
        d = f - prev[-1]
        if per and d > 0.5 * per:
            continue
        leads.append(d)
    if leads:
        lead = statistics.median(leads)
        print("下降沿超前: 中位=%.0f ns  均值=%.0f ns  最小=%.0f ns  最大=%.0f ns"
              % (lead * 1e9, statistics.mean(leads) * 1e9, min(leads) * 1e9, max(leads) * 1e9))
        print("换算计数: N_fall = %.2f counts" % (lead * a.timer_clk))
        print("内缩对称性: 上升滞后 %.2f cnt vs 下降超前 %.2f cnt -> %s"
              % (lag * a.timer_clk, lead * a.timer_clk,
                 "对称" if abs(lag - lead) * a.timer_clk < 0.6 else
                 "不对称（差 %.2f cnt，触发链路固有延迟所致）" % ((lag - lead) * a.timer_clk)))
    print("说明: 期望 N ≈ PWM_DEADTIME_CNT；触发同步电路本身有 1~2 count 固定延迟。")


if __name__ == "__main__":
    sys.exit(main())
