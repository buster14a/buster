#!/usr/bin/env python3
from pathlib import Path

path = Path("src/buster/lib/compiler/assembly/x86_64_metadata.c")
text = path.read_text(encoding="utf-8")
old = "BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_got_prepare_class(u32 class_index, u32 register_first, u32 register_count)\n"
new = "BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_nop_prepare(void);\n\n" + old
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one GOT preparation helper definition, found {count}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
Path(__file__).unlink()
