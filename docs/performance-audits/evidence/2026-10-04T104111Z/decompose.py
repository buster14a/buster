#!/usr/bin/env python3
"""Self-Ir decomposition of the removed validation pass from the retained
callgrind captures: base self Ir - probe self Ir per function, plus selected
inclusive values.

usage: decompose.py <evidence-dir> [self|sqlite]

Reads <w>-base.callgrind(.gz) and <w>-probe.callgrind(.gz). The annotate
listings (<w>-<s>.inclusive.txt / .exclusive.txt) are regenerated with
callgrind_annotate (valgrind) when absent; they are not committed because
they are 9 MB each and derivable.
"""
import gzip
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

directory = pathlib.Path(sys.argv[1])
workload = sys.argv[2] if len(sys.argv) > 2 else "self"


def capture(subject):
    plain = directory / f"{workload}-{subject}.callgrind"
    if plain.exists():
        return plain
    packed = directory / f"{workload}-{subject}.callgrind.gz"
    target = pathlib.Path(tempfile.gettempdir()) / plain.name
    with gzip.open(packed, "rb") as source, open(target, "wb") as sink:
        shutil.copyfileobj(source, sink)
    return target


def listing(subject, inclusive):
    kind = "inclusive" if inclusive else "exclusive"
    path = directory / f"{workload}-{subject}.{kind}.txt"
    if not path.exists():
        flags = ["--inclusive=yes"] if inclusive else []
        text = subprocess.run(["callgrind_annotate", *flags, "--threshold=99.99", str(capture(subject))],
                              capture_output=True, text=True, check=True).stdout
        path = pathlib.Path(tempfile.gettempdir()) / path.name
        path.write_text(text)
    return path


def table(path):
    out, section = {}, 0
    for line in path.read_text(errors="replace").splitlines():
        if re.match(r"^-+$", line):
            section += 1
            continue
        m = re.match(r"^\s*([\d,]+)\s+\(\s*[\d.]+%\)\s+(\S.*)$", line)
        if not m or section < 5:
            continue
        where = m.group(2)
        if "(" in where and "x)" in where:  # auto-annotation call lines
            continue
        name = where.split(" [")[0]
        if ":" not in name:
            continue
        key = name.rsplit(":", 1)[-1]
        out[key] = out.get(key, 0) + int(m.group(1).replace(",", ""))
    return out


def total(path):
    value = None
    for line in path.open(errors="replace"):
        m = re.match(r"^(summary|totals):\s+(\d+)", line)
        if m:
            value = int(m.group(2))
    return value


tb, tp = total(capture("base")), total(capture("probe"))
sb, sp = table(listing("base", False)), table(listing("probe", False))
ib, ip = table(listing("base", True)), table(listing("probe", True))
print(f"[{workload}] total Ir base {tb:,}  probe {tp:,}  delta {tp - tb:+,} ({100 * (tp - tb) / tb:+.2f}%)")
delta = {k: sb.get(k, 0) - sp.get(k, 0) for k in set(sb) | set(sp)}
removed = sum(v for v in delta.values() if v > 0)
added = sum(-v for v in delta.values() if v < 0)
print(f"self Ir removed: {removed:,} ({100 * removed / tb:.2f}%)   added: {added:,} ({100 * added / tb:.2f}%)")
print("\nSELF Ir removed by the probe (base self - probe self), share of base total:")
cumulative = 0
for k, v in sorted(delta.items(), key=lambda kv: -kv[1])[:45]:
    cumulative += v
    print(f"  {k:55} {v:>15,}  {100 * v / tb:5.2f}%  cum {100 * cumulative / tb:5.2f}%")
print("\nSELF Ir added by the probe (lowering fallback work):")
for k, v in sorted(delta.items(), key=lambda kv: kv[1])[:10]:
    if v < 0:
        print(f"  {k:55} {-v:>15,}  {100 * -v / tb:5.2f}%")
print("\nINCLUSIVE Ir, base vs probe (share of each total):")
for k in ["c_analyze_semantics_core", "c_parse_validate_lowering_constraints", "c_parse_expression_type_query",
          "c_type_parse_machine_run", "c_parse_validate_const_assignments", "c_parse_validate_statement_expression_range",
          "c_parse_validate_initializer_shape", "c_parse_infer_initializer_array_count_core", "c_parse_infer_file_array_bounds",
          "c_parse_validate_compound_literals", "c_parse_validate_generic_duplicates", "c_parse_validate_sizeof_operands",
          "c_parse_type_layout_solve", "c_parse_typed_constant", "c_parse_lookup_entity_symbol", "c_parse_scope_for_token",
          "c_lower_to_ir_reserved_run", "c_ir_global_initializer", "c_ir_query_execute", "c_ir_ssa_finish",
          "ir_prepare_canonical_module", "codegen_generate_canonical_module_with_trace", "c_preprocess", "c_parse_ast",
          "__memcpy_avx_unaligned_erms", "__memset_avx2_unaligned_erms"]:
    print(f"  {k:55} {ib.get(k, 0):>15,} {100 * ib.get(k, 0) / tb:6.2f}%   {ip.get(k, 0):>15,} {100 * ip.get(k, 0) / tp:6.2f}%")
