#!/usr/bin/env python3
"""Plot paired benchmark process medians (requires matplotlib)."""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interaction", type=Path, required=True)
    parser.add_argument("--boundary", type=Path, required=True)
    parser.add_argument("--subtree", type=Path, help="Plot incremental sidebar operations instead of the original extraction")
    parser.add_argument("--output", type=Path, required=True, help="Output filename without extension")
    args = parser.parse_args()
    interaction = json.loads(args.interaction.read_text())
    boundary = json.loads(args.boundary.read_text())
    cases = [(boundary, "command_catalogue_per_call_ms", 1000, "Command catalogue", "µs / call (100-call batches)"),
             (boundary, "pin_restore_dispatch_ms", 1, "Restore 1,000 pins", "Synchronous handler, ms"),
             (interaction, "ui_palette_query_ms", 1, "Open, type and close palette", "Through UI drain, ms")]
    figure_title = "Zitter product boundary: before vs after"
    if args.subtree:
        subtree = json.loads(args.subtree.read_text())
        cases = [(subtree, "leaf_move_settled_ms", 1, "Move one pin among 1,000", "Settled completion, ms"),
                 (boundary, "pin_move_settled_ms", 1, "Reparent a deep subtree", "Settled completion, ms"),
                 (boundary, "pin_restore_settled_ms", 1, "Reset the 1,000-pin catalogue", "Settled completion, ms")]
        figure_title = "Zitter native sidebar: before vs incremental updates"
    fig, axes = plt.subplots(1, 3, figsize=(12.6, 4.4), constrained_layout=True)
    colors = {"before": "#A4A8B4", "after": "#7764CB"}
    for ax, (report, metric, scale, title, unit) in zip(axes, cases):
        for index, backend in enumerate(("compositor", "metal")):
            for variant, offset in (("before", -.18), ("after", .18)):
                summary = report["comparisons"][backend][metric][variant]
                values = [r["operations"][metric]["median"] * scale for r in report["runs"]
                          if r["variant"] == variant and r["backend"] == backend]
                value = summary["median"] * scale
                x = index + offset
                ax.bar(x, value, width=.32, color=colors[variant], label=variant.title() if index == 0 else None)
                ax.plot([x] * len(values), values, "o", color="#292435", ms=3, alpha=.55)
                ax.annotate(f"{value:.1f}", (x, max(values)), xytext=(0, 5), textcoords="offset points",
                            ha="center", fontsize=9)
        ax.set_title(title, fontsize=11, weight="bold")
        ax.set_ylabel(unit, fontsize=9)
        ax.set_xticks([0, 1], ["Compositor", "Metal"])
        ax.set_ylim(0, ax.get_ylim()[1] * 1.18)
        ax.spines[["top", "right"]].set_visible(False)
        ax.grid(axis="y", alpha=.15)
        ax.set_axisbelow(True)
    axes[0].legend(frameon=False, loc="upper right", fontsize=9)
    fig.suptitle(figure_title, fontsize=15, weight="bold")
    fig.supxlabel("Dots: six fresh-process medians per condition. Warm OS caches. No monitor presentation or FPS measurement.", fontsize=9)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output.with_suffix(".png"), dpi=180)
    fig.savefig(args.output.with_suffix(".svg"))


if __name__ == "__main__":
    main()
