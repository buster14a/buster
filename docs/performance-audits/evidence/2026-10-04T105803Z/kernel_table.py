#!/usr/bin/env python3
"""Render the per-kernel table of audit 2026-10-04T105803Z from the retained
hosted lab logs (IR_LAYOUT_LAB_KERNEL lines).

    python3 kernel_table.py hosted-37197982567

Prints, per kernel and layout, the three stage-1 process medians (ps per
unit), their median, the spread of the three medians over that median, the
ratio against the layout-A variant of the same kernel, and the baseline-ISA,
memory-form and object.c columns. No timing is performed here.
"""
import statistics
import sys
from pathlib import Path

REFERENCE = {
    "census": "A_pub", "uses": "A_pub", "defs_hot": "A_pub", "defs_cold": "A_pub",
    "chain": "A_con", "validate": "A_con", "compact": "A_con", "append": "A_reserved",
}


def parse(path):
    rows = {}
    for line in Path(path).read_text().splitlines():
        if line.startswith("IR_LAYOUT_LAB_KERNEL"):
            fields = dict(part.split("=", 1) for part in line.split()[1:])
            rows[(fields["kernel"], fields["layout"])] = fields
    return rows


def main(directory):
    base = Path(directory)
    processes = [parse(base / f"stage1-ssa-process{index}.txt") for index in (1, 2, 3)]
    columns = {
        "baseline-isa": parse(base / "stage1-ssa-baseline-isa.txt"),
        "memory-form": parse(base / "stage1-memory-form.txt"),
        "object.c": parse(base / "object-ssa.txt"),
    }
    header = f"{'kernel':26s}{'layout':16s}{'p1':>8s}{'p2':>8s}{'p3':>8s}{'median':>8s}{'spread%':>8s}{'vs A':>7s}"
    for name in columns:
        header += f" | {name:>12s}"
    print(header)
    current = None
    for key in processes[0]:
        kernel, layout = key
        values = [int(process[key]["ps_per_unit"]) for process in processes]
        median = statistics.median(values)
        spread = (max(values) - min(values)) / median * 100 if median else 0
        reference = REFERENCE.get(kernel)
        ratio = "-"
        if reference and (kernel, reference) in processes[0]:
            reference_median = statistics.median(int(process[(kernel, reference)]["ps_per_unit"]) for process in processes)
            ratio = f"{median / reference_median:.2f}"
        if kernel != current:
            print("-" * len(header))
            current = kernel
        line = f"{kernel:26s}{layout:16s}{values[0]:8d}{values[1]:8d}{values[2]:8d}{median:8.0f}{spread:8.1f}{ratio:>7s}"
        for name, rows in columns.items():
            line += f" | {int(rows[key]['ps_per_unit']):12d}"
        print(line)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "hosted-37197982567")
