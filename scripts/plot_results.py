#!/usr/bin/env python3
"""Plot latency-vs-load curves from the tb_mesh sweep CSV."""

import csv
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def main() -> None:
    csv_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("results/sweep.csv")
    out_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("docs")
    out_dir.mkdir(exist_ok=True)

    series: dict[str, dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))
    with csv_path.open() as f:
        for row in csv.DictReader(f):
            s = series[row["pattern"]]
            s["accepted"].append(float(row["accepted_rate"]))
            s["avg_lat"].append(float(row["avg_latency"]))
            s["p99_lat"].append(float(row["p99_latency"]))
            s["offered"].append(float(row["offered_rate"]))

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.5))

    for pattern, s in series.items():
        ax1.plot(s["accepted"], s["avg_lat"], marker="o", label=f"{pattern} (avg)")
        ax1.plot(s["accepted"], s["p99_lat"], marker="s", linestyle="--",
                 label=f"{pattern} (p99)")
    ax1.set_xlabel("accepted injection rate (flits/node/cycle)")
    ax1.set_ylabel("latency (cycles)")
    ax1.set_title("Latency vs offered load — 4×4 deflection mesh")
    ax1.grid(alpha=0.3)
    ax1.legend()

    for pattern, s in series.items():
        ax2.plot(s["offered"], s["accepted"], marker="o", label=pattern)
    lim = max(max(s["offered"]) for s in series.values())
    ax2.plot([0, lim], [0, lim], linestyle=":", color="gray", label="ideal")
    ax2.set_xlabel("offered rate (flits/node/cycle)")
    ax2.set_ylabel("accepted rate")
    ax2.set_title("Throughput — credit backpressure at saturation")
    ax2.grid(alpha=0.3)
    ax2.legend()

    fig.tight_layout()
    out = out_dir / "latency_vs_load.png"
    fig.savefig(out, dpi=140)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
