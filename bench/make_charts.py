#!/usr/bin/env python3
"""Render docs/benchmark.png from bench/results.json (matplotlib)."""
import json
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

with open("bench/results.json") as f:
    d = json.load(f)

lat = d["latency_ms"]
thr = d["throughput_req_s"]
mem = d["memory_after_kb"]

cases = ["S", "M", "L"]
series = [("QuotlyNative (C++)", "cpp", "#2ea77a"),
          ("quote-api JS, scale=1", "js-scale1", "#e8a33d"),
          ("quote-api JS, scale=2 (default)", "js-scale2", "#d96459")]

fig, axes = plt.subplots(1, 3, figsize=(15.5, 5.2),
                         gridspec_kw={"width_ratios": [2.2, 1, 0.8]})
fig.suptitle("QuotlyNative (C++) vs LyoSU quote-api (JS) — identical payloads, "
             "response caches defeated, same host", fontsize=12, fontweight="bold")

# ── panel 1: mean latency per payload size (log scale) ──────────────────────
ax = axes[0]
x = np.arange(len(cases))
w = 0.26
for i, (label, key, color) in enumerate(series):
    means = [lat[f"{c}/{key}"]["mean"] for c in cases]
    p95 = [lat[f"{c}/{key}"]["p95"] for c in cases]
    bars = ax.bar(x + (i - 1) * w, means, w, label=label, color=color, zorder=3)
    ax.scatter(x + (i - 1) * w, p95, marker="_", color="black", s=60, zorder=4)
    for bx, m in zip(x + (i - 1) * w, means):
        ax.annotate(f"{m:.0f}", (bx, m), textcoords="offset points",
                    xytext=(0, 3), ha="center", fontsize=8)
ax.set_yscale("log")
ax.set_xticks(x)
ax.set_xticklabels([f"{c}: {n} msg(s)" for c, n in
                    [("S", 1), ("M", 3), ("L", 5)]])
ax.set_ylabel("request latency, ms (log)")
ax.set_title("Mean latency — black tick = p95")
ax.legend(fontsize=8, loc="upper left")
ax.grid(axis="y", which="both", alpha=0.3, zorder=0)

# ── panel 2: concurrent throughput ──────────────────────────────────────────
ax = axes[1]
vals = [thr["cpp"], thr["js-scale1"], thr["js-scale2"]]
bars = ax.bar([s[0] for s in series], vals, color=[s[2] for s in series], zorder=3)
for b, v in zip(bars, vals):
    ax.annotate(f"{v:.1f}", (b.get_x() + b.get_width() / 2, v),
                textcoords="offset points", xytext=(0, 3), ha="center", fontsize=9)
ax.set_xticklabels(["C++", "JS s=1", "JS s=2"])
ax.set_ylabel("req/s (4 concurrent clients)")
ax.set_title("Throughput, 3-message quotes")
ax.grid(axis="y", alpha=0.3, zorder=0)

# ── panel 3: peak memory ────────────────────────────────────────────────────
ax = axes[2]
mb = [mem["cpp_peak_kb"] / 1024, mem["js_peak_kb"] / 1024]
bars = ax.bar(["C++", "JS"], mb, color=["#2ea77a", "#d96459"], zorder=3)
for b, v in zip(bars, mb):
    ax.annotate(f"{v:.0f} MB", (b.get_x() + b.get_width() / 2, v),
                textcoords="offset points", xytext=(0, 3), ha="center", fontsize=9)
ax.set_ylabel("peak RSS (VmHWM), MB")
ax.set_title(f"Memory under load\n({mb[1] / mb[0]:.0f}× lighter)")
ax.grid(axis="y", alpha=0.3, zorder=0)

fig.tight_layout(rect=[0, 0, 1, 0.93])
fig.savefig("docs/benchmark.png", dpi=150)
print("wrote docs/benchmark.png")
