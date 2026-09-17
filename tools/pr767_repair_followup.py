#!/usr/bin/env python3
from pathlib import Path

metadata_path = Path("src/buster/lib/compiler/assembly/x86_64_metadata.c")
metadata = metadata_path.read_text(encoding="utf-8")
old = "BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_got_prepare_class(u32 class_index, u32 register_first, u32 register_count)\n"
new = "BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_nop_prepare(void);\n\n" + old
count = metadata.count(old)
if count != 1:
    raise SystemExit(f"expected one GOT preparation helper definition, found {count}")
metadata_path.write_text(metadata.replace(old, new, 1), encoding="utf-8")

object_path = Path("src/buster/lib/compiler/object/object.c")
object_source = object_path.read_text(encoding="utf-8")
sentinel = "            case CODEGEN_MODULE_RELOCATION_COUNT: return false;\n"
if sentinel not in object_source:
    marker = "            case CODEGEN_MODULE_RELOCATION_X86_64_PLT32: *destination = OBJECT_RELOCATION_X86_64_PLT32; return true;\n"
    marker_count = object_source.count(marker)
    if marker_count != 1:
        raise SystemExit(f"expected one PLT32 codegen relocation case, found {marker_count}")
    object_source = object_source.replace(marker, marker + sentinel, 1)
    object_path.write_text(object_source, encoding="utf-8")

Path(__file__).unlink()
