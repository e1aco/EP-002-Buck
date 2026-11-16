"""生成纹波-电压关系图。"""
import json
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

with open("ripple_results.json", encoding="utf-8") as f:
    R = json.load(f)

v = np.array([r["target_mv"] / 1000 for r in R])
vout = np.array([r["v_mean"] for r in R])
pkpk = np.array([r["pkpk_mv"] for r in R])
rms = np.array([r["rms_mv"] for r in R])
band = np.array([r["band_mv"] for r in R])
iout = np.array([r["fw_iout"] for r in R]) / 1000.0

fig, axes = plt.subplots(1, 3, figsize=(15, 4.4), dpi=120)

ax = axes[0]
ax.plot(v, pkpk, "o-", color="#d62728", label="PKPK")
ax.plot(v, band, "s-", color="#1f77b4", label="5-95% band")
ax.plot(v, rms, "^-", color="#2ca02c", label="AC-RMS")
ax.set_xlabel("Target Vout (V)"); ax.set_ylabel("mV")
ax.set_title("Ripple vs output voltage"); ax.legend(); ax.grid(alpha=.3)

ax = axes[1]
ax.plot(v, pkpk / (vout * 1000) * 100, "o-", color="#d62728", label="PKPK %")
ax.plot(v, band / (vout * 1000) * 100, "s-", color="#1f77b4", label="band %")
ax.set_xlabel("Target Vout (V)"); ax.set_ylabel("% of Vout")
ax.set_title("Ripple ratio"); ax.legend(); ax.grid(alpha=.3)

ax = axes[2]
ax.plot(v, iout, "o-", color="#9467bd")
ax.set_xlabel("Target Vout (V)"); ax.set_ylabel("Iout (A)")
ax.set_title("Load current (10 ohm resistor)")
for x, y in zip(v, iout):
    ax.annotate(f"{y:.2f}A", (x, y), textcoords="offset points", xytext=(0, 8),
                ha="center", fontsize=8)
ax.grid(alpha=.3)

fig.tight_layout()
fig.savefig("ripple_vs_voltage.png")
print("saved ripple_vs_voltage.png")
