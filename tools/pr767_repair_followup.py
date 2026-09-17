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

# The original exhaustive tail described plain type 9 as a closed MOV family.
# That is the unsafe policy #764 removes. The full relaxable vocabulary is
# already covered above, so retain the finite byte-domain walk as a stronger
# failure-atomicity regression for the spelling that promises no boundary.
test_path = Path("src/buster/tests/compiler/assembly/x86_64_got_test.c")
test_source = test_path.read_text(encoding="utf-8")
old_tail = '''    // Exhaust the finite REX/ModRM domain using independent oracle matching.
    // A site that promises nothing takes the MOV-r64 family and no other row
    // of the table, however much a neighbouring byte looks like one.
    for (u32 rex = 0; rex < 256; rex += 1)
    {
        for (u32 modrm = 0; modrm < 256; modrm += 1)
        {
            memcpy(bytes, x86_got_got_oracle[0], 7);
            bytes[0] = (u8)rex;
            bytes[2] = (u8)modrm;
            memcpy(expected, bytes, 7);
            bool valid = false;
            for (u32 reg = 0; reg < 16; reg += 1)
            {
                for (u32 variant = 0; variant < 4; variant += 1)
                {
                    if (rex == (u32)(x86_got_got_oracle[reg][0] | variant) && modrm == x86_got_got_oracle[reg][2])
                    {
                        memcpy(expected, x86_got_address_oracle[reg], 7);
                        valid = true;
                    }
                }
            }
            BUSTER_TEST(arguments, (buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX, bytes, 3, 7, -4) ==
                                    BUSTER_X86_METADATA_GOT_PATCH_PC32) == valid);
            BUSTER_TEST(arguments, memcmp(bytes, expected, 7) == 0);
        }
    }
    for (u32 opcode = 0; opcode < 256; opcode += 1)
    {
        memcpy(bytes, x86_got_got_oracle[0], 7);
        bytes[1] = (u8)opcode;
        memcpy(expected, bytes, 7);
        bool valid = opcode == x86_got_got_oracle[0][1];
        if (valid) memcpy(expected, x86_got_address_oracle[0], 7);
        BUSTER_TEST(arguments, (buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX, bytes, 3, 7, -4) ==
                                BUSTER_X86_METADATA_GOT_PATCH_PC32) == valid);
        BUSTER_TEST(arguments, memcmp(bytes, expected, 7) == 0);
    }
'''
new_tail = '''    // Plain type 9 describes only the four-byte field. Exhaust every possible
    // byte in the three positions before it and prove the implementation
    // neither recognizes nor writes any of them.
    for (u32 rex = 0; rex < 256; rex += 1)
    {
        for (u32 modrm = 0; modrm < 256; modrm += 1)
        {
            memcpy(bytes, x86_got_got_oracle[0], 7);
            bytes[0] = (u8)rex;
            bytes[2] = (u8)modrm;
            memcpy(expected, bytes, 7);
            BUSTER_TEST(arguments,
                        buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_GOTPCREL, bytes, 3, 7, -4) ==
                            BUSTER_X86_METADATA_GOT_PATCH_NONE);
            BUSTER_TEST(arguments, memcmp(bytes, expected, 7) == 0);
        }
    }
    for (u32 opcode = 0; opcode < 256; opcode += 1)
    {
        memcpy(bytes, x86_got_got_oracle[0], 7);
        bytes[1] = (u8)opcode;
        memcpy(expected, bytes, 7);
        BUSTER_TEST(arguments,
                    buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_GOTPCREL, bytes, 3, 7, -4) ==
                        BUSTER_X86_METADATA_GOT_PATCH_NONE);
        BUSTER_TEST(arguments, memcmp(bytes, expected, 7) == 0);
    }
'''
count = test_source.count(old_tail)
if count != 1:
    raise SystemExit(f"expected one legacy GOT exhaustive tail, found {count}")
test_path.write_text(test_source.replace(old_tail, new_tail, 1), encoding="utf-8")

Path(__file__).unlink()
