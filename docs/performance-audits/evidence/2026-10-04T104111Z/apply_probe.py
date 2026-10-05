#!/usr/bin/env python3
"""Research probe: remove the C frontend's validate-then-discard pass.

Deletes the single call to c_parse_validate_lowering_constraints in
c_parse.c while keeping c_parse_index_scope_children (lowering reads that
index). Never a production change: it removes diagnostics for invalid input.
On valid input it isolates what the separate validation pass costs.

usage: apply_probe.py <checkout-root>
"""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
path = root / "src/buster/lib/compiler/frontend/c/c_parse.c"
text = path.read_text()
needle = (
    "        c_parse_index_scope_children(&result, arena);\n"
    "        c_parse_validate_lowering_constraints(&machine, arena, &result, preprocess);\n"
)
replacement = (
    "        c_parse_index_scope_children(&result, arena);\n"
    "        /* RESEARCH PROBE: separate validation pass removed; never production. */\n"
)
count = text.count(needle)
if count != 1:
    sys.exit(f"expected exactly one validation call site, found {count}")
path.write_text(text.replace(needle, replacement))
print(f"probe applied to {path}")
