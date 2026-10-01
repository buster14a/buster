#!/usr/bin/env python3
"""Summarize the temporary #59 encoder run diagnostic from a compiler log."""

import argparse
import hashlib
import json
from pathlib import Path


def opcode_names(header: Path) -> dict[int, str]:
    source = header.read_text()
    enum = source.split("typedef enum MachineOpcode", 1)[1].split("}", 1)[0]
    names = {}
    for line in enum.splitlines():
        name = line.split("//", 1)[0].strip().rstrip(",")
        if name.startswith("MACHINE_") and "=" not in name:
            names[len(names)] = name
    return names


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--form-tag", choices=("ENCODE_RUN_FORM", "ENCODE_RUN_OPCODE"),
                        default="ENCODE_RUN_FORM")
    parser.add_argument("--machine-header", type=Path,
                        default=Path("src/buster/lib/compiler/codegen/machine.h"))
    args = parser.parse_args()
    rows = args.log.read_bytes()
    names = opcode_names(args.machine_header)
    total = {"functions": 0, "machine_rows": 0, "before_edit_sites": 0,
             "after_edit_sites": 0}
    forms: dict[int, dict] = {}
    for line in rows.decode(errors="replace").splitlines():
        if line.startswith("ENCODE_RUN_TOTAL,"):
            values = [int(value) for value in line.split(",")[1:]]
            if len(values) != 3:
                raise ValueError(f"malformed total: {line}")
            total["functions"] += 1
            for key, value in zip(("machine_rows", "before_edit_sites", "after_edit_sites"), values):
                total[key] += value
        elif line.startswith(args.form_tag + ","):
            values = [int(value) for value in line.split(",")[1:]]
            if len(values) != 14:
                raise ValueError(f"malformed form: {line}")
            table, opcode, table_rows, eligible, byte_count, *tail = values
            bins, longest = tail[:8], tail[8]
            form = forms.setdefault(table, {"table": table, "opcode": opcode,
                                            "table_rows": 0, "eligible_rows": 0,
                                            "eligible_bytes": 0, "runs": [0] * 8,
                                            "longest": 0})
            if form["opcode"] != opcode:
                raise ValueError(f"table {table} observed with multiple opcodes")
            form["table_rows"] += table_rows
            form["eligible_rows"] += eligible
            form["eligible_bytes"] += byte_count
            form["longest"] = max(form["longest"], longest)
            form["runs"] = [left + right for left, right in zip(form["runs"], bins)]
    for form in forms.values():
        form["opcode_name"] = names.get(form["opcode"], "unknown")
        form["runs_ge4"] = sum(form["runs"][3:])
        form["rows_in_runs_ge4"] = (form["eligible_rows"] - form["runs"][0]
                                    - 2 * form["runs"][1] - 3 * form["runs"][2])
        if form["rows_in_runs_ge4"] < 0:
            raise ValueError(f"invalid run accounting for table {form['table']}")
    summary = {"log_sha256": hashlib.sha256(rows).hexdigest(), **total,
               "forms": sorted(forms.values(), key=lambda form: -form["eligible_rows"])}
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
