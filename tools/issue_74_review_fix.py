#!/usr/bin/env python3
from pathlib import Path

path = Path("src/buster/lib/compiler/frontend/c/c_gen.c")
text = path.read_text()

old = """                desired = c_ir_emit_cast(builder, desired, value_type_id, source);
                if (desired.value == IR_ID_UNDERLYING_INVALID)
"""
new = """                if (!selected->builtin_atomic_generic)
                {
                    desired = c_ir_emit_cast(builder, desired, value_type_id, source);
                }
                if (desired.value == IR_ID_UNDERLYING_INVALID)
"""
if text.count(old) != 1:
    raise SystemExit(f"expected one compare-exchange desired cast, found {text.count(old)}")
text = text.replace(old, new, 1)

old = """                    // Canonical pointer RMW takes an integer byte offset,
          // including GNU's unscaled offset, never a pointer value.
"""
new = """                    // Canonical pointer RMW takes an integer byte offset,
                    // including GNU's unscaled offset, never a pointer value.
"""
if text.count(old) != 1:
    raise SystemExit(f"expected one malformed comment indentation, found {text.count(old)}")
text = text.replace(old, new, 1)

path.write_text(text)
