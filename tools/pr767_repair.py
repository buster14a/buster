#!/usr/bin/env python3
from pathlib import Path
import re


def read(path):
    return Path(path).read_text(encoding="utf-8")


def write(path, text):
    Path(path).write_text(text, encoding="utf-8")


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one exact match, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


def sub_once(path, pattern, replacement, flags=0):
    text = read(path)
    changed, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"{path}: expected one regex match, found {count}: {pattern[:120]!r}")
    write(path, changed)


# Preserve all four psABI spellings in the object vocabulary.
replace_once(
    "src/buster/lib/compiler/object/object.h",
    """    OBJECT_RELOCATION_X86_64_GOTPCRELX,
    OBJECT_RELOCATION_X86_64_REX_GOTPCRELX,
    OBJECT_RELOCATION_COUNT,
""",
    """    OBJECT_RELOCATION_X86_64_GOTPCRELX,
    OBJECT_RELOCATION_X86_64_REX_GOTPCRELX,
    // R_X86_64_CODE_4_GOTPCRELX: the relaxable REX2 spelling.  The
    // instruction begins four bytes before its relocated field.
    OBJECT_RELOCATION_X86_64_CODE_4_GOTPCRELX,
    OBJECT_RELOCATION_COUNT,
""",
)
replace_once(
    "src/buster/lib/compiler/object/object.h",
    "// The three x86-64 GOT spellings share every rule but relaxation:",
    "// The four x86-64 GOT spellings share every rule but relaxation:",
)

replace_once(
    "src/buster/lib/compiler/assembly/x86_64_metadata.h",
    """    BUSTER_X86_METADATA_GOT_SITE_GOTPCRELX,
    BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX,
    BUSTER_X86_METADATA_GOT_SITE_COUNT,
""",
    """    BUSTER_X86_METADATA_GOT_SITE_GOTPCRELX,
    BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX,
    BUSTER_X86_METADATA_GOT_SITE_CODE_4_GOTPCRELX,
    BUSTER_X86_METADATA_GOT_SITE_COUNT,
""",
)
replace_once(
    "src/buster/lib/compiler/assembly/x86_64_metadata.h",
    """// the shape decidable at all: plain GOTPCREL promises nothing and keeps the
// closed MOV-r64 -> LEA family, while the two relaxable spellings open the
// ALU, TEST, PUSH and indirect-branch conversions as well.
""",
    """// the shape decidable at all: plain GOTPCREL promises nothing and is never
// decoded, while the relaxable spellings open the conversion rows whose exact
// two-, three-, or four-byte prefix width they promise.
""",
)

replace_once(
    "src/buster/lib/compiler/link/link.c",
    """        BusterX86MetadataGotSite site = kind == OBJECT_RELOCATION_X86_64_GOTPCRELX       ? BUSTER_X86_METADATA_GOT_SITE_GOTPCRELX
                                        : kind == OBJECT_RELOCATION_X86_64_REX_GOTPCRELX ? BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX
                                                                                         : BUSTER_X86_METADATA_GOT_SITE_GOTPCREL;
""",
    """        BusterX86MetadataGotSite site = kind == OBJECT_RELOCATION_X86_64_GOTPCRELX          ? BUSTER_X86_METADATA_GOT_SITE_GOTPCRELX
                                        : kind == OBJECT_RELOCATION_X86_64_REX_GOTPCRELX    ? BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX
                                        : kind == OBJECT_RELOCATION_X86_64_CODE_4_GOTPCRELX ? BUSTER_X86_METADATA_GOT_SITE_CODE_4_GOTPCRELX
                                                                                            : BUSTER_X86_METADATA_GOT_SITE_GOTPCREL;
""",
)

object_path = "src/buster/lib/compiler/object/object.c"
replace_once(
    object_path,
    """        case OBJECT_RELOCATION_X86_64_GOTPCREL:
        case OBJECT_RELOCATION_X86_64_GOTPCRELX:
        case OBJECT_RELOCATION_X86_64_REX_GOTPCRELX:
""",
    """        case OBJECT_RELOCATION_X86_64_GOTPCREL:
        case OBJECT_RELOCATION_X86_64_GOTPCRELX:
        case OBJECT_RELOCATION_X86_64_REX_GOTPCRELX:
        case OBJECT_RELOCATION_X86_64_CODE_4_GOTPCRELX:
""",
)
replace_once(
    object_path,
    """                                   : relocation_type == 41                        ? OBJECT_RELOCATION_X86_64_GOTPCRELX
                                   : relocation_type == 42                        ? OBJECT_RELOCATION_X86_64_REX_GOTPCRELX
                                   : relocation_type == 10 ? OBJECT_RELOCATION_ABSOLUTE32
""",
    """                                   : relocation_type == 41                        ? OBJECT_RELOCATION_X86_64_GOTPCRELX
                                   : relocation_type == 42                        ? OBJECT_RELOCATION_X86_64_REX_GOTPCRELX
                                   : relocation_type == 43                        ? OBJECT_RELOCATION_X86_64_CODE_4_GOTPCRELX
                                   : relocation_type == 10 ? OBJECT_RELOCATION_ABSOLUTE32
""",
)
replace_once(
    object_path,
    """    return kind == OBJECT_RELOCATION_X86_64_GOTPCREL || kind == OBJECT_RELOCATION_X86_64_GOTPCRELX ||
           kind == OBJECT_RELOCATION_X86_64_REX_GOTPCRELX;
""",
    """    return kind == OBJECT_RELOCATION_X86_64_GOTPCREL || kind == OBJECT_RELOCATION_X86_64_GOTPCRELX ||
           kind == OBJECT_RELOCATION_X86_64_REX_GOTPCRELX || kind == OBJECT_RELOCATION_X86_64_CODE_4_GOTPCRELX;
""",
)
replace_once(
    object_path,
    """               : kind == OBJECT_RELOCATION_X86_64_GOTPCRELX     ? 41
               : kind == OBJECT_RELOCATION_X86_64_REX_GOTPCRELX ? 42
               : kind == OBJECT_RELOCATION_X86_64_TLSGD         ? 19
""",
    """               : kind == OBJECT_RELOCATION_X86_64_GOTPCRELX        ? 41
               : kind == OBJECT_RELOCATION_X86_64_REX_GOTPCRELX    ? 42
               : kind == OBJECT_RELOCATION_X86_64_CODE_4_GOTPCRELX ? 43
               : kind == OBJECT_RELOCATION_X86_64_TLSGD            ? 19
""",
)
replace_once(
    object_path,
    """            case CODEGEN_MODULE_RELOCATION_X86_64_GOTPCREL: *destination = OBJECT_RELOCATION_X86_64_GOTPCREL; return true;
""",
    """            // The compiler emits MOV r64,[RIP+GOT], whose one-byte REX
            // prefix makes the relaxable spelling unambiguous.  Plain type 9 is
            // reserved for external legacy objects and is never decoded.
            case CODEGEN_MODULE_RELOCATION_X86_64_GOTPCREL: *destination = OBJECT_RELOCATION_X86_64_REX_GOTPCRELX; return true;
""",
)
sub_once(
    object_path,
    r'''    case 42:\n        return S8\("R_X86_64_REX_GOTPCRELX"\);''',
    '''    case 42:
        return S8("R_X86_64_REX_GOTPCRELX");
    case 43:
        return S8("R_X86_64_CODE_4_GOTPCRELX");''',
)
replace_once(
    object_path,
    "// The three GOT families stay distinct:",
    "// The four GOT families stay distinct:",
)

metadata_path = "src/buster/lib/compiler/assembly/x86_64_metadata.c"
replace_once(
    metadata_path,
    """    // Every source form here spends one opcode byte and one ModRM byte in
    // front of its displacement; a REX-prefixed site spends one more.
    BUSTER_X86_GOT_PREFIX_MIN = 2,
    BUSTER_X86_GOT_PREFIX_MAX = 3,
    BUSTER_X86_GOT_PREFIX_KINDS = BUSTER_X86_GOT_PREFIX_MAX - BUSTER_X86_GOT_PREFIX_MIN + 1,
    BUSTER_X86_GOT_REGISTERS = 16,
    BUSTER_X86_GOT_SCRATCH = 16,
""",
    """    // Every source form here spends one opcode byte and one ModRM byte in
    // front of its displacement.  REX adds one byte and REX2 adds two.
    BUSTER_X86_GOT_PREFIX_MIN = 2,
    BUSTER_X86_GOT_PREFIX_MAX = 4,
    BUSTER_X86_GOT_PREFIX_KINDS = BUSTER_X86_GOT_PREFIX_MAX - BUSTER_X86_GOT_PREFIX_MIN + 1,
    BUSTER_X86_GOT_REGISTERS = 16,
    BUSTER_X86_GOT_APX_FIRST_REGISTER = 16,
    // MOV, the eight ALU operations, and TEST, each at 32/64 bits where
    // applicable.  CALL/JMP/PUSH have no EGPR operand and are not CODE_4 rows.
    BUSTER_X86_GOT_APX_CLASS_COUNT = 20,
    BUSTER_X86_GOT_SCRATCH = 16,
""",
)
replace_once(
    metadata_path,
    """    BusterX86MetadataPhysicalQuery query = {
        .mnemonic = mnemonic, .operands = operands, .operand_count = operand_count,
        .address_size = 64, .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
    };
""",
    """    bool apx = false;
    for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
    {
        apx = apx || (operands[operand_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                      operands[operand_index].reg.index >= BUSTER_X86_GOT_APX_FIRST_REGISTER);
    }
    String8 apx_features[1] = {S8("APX_F")};
    BusterX86MetadataPhysicalQuery query = {
        .mnemonic = mnemonic, .operands = operands, .operand_count = operand_count,
        .features = {.names = apx ? apx_features : 0, .count = apx ? 1u : 0u},
        .address_size = 64, .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
    };
""",
)

prepare_replacement = r'''BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_got_prepare_class(u32 class_index, u32 register_first, u32 register_count)
{
    bool valid = class_index < BUSTER_ARRAY_LENGTH(buster_x86_metadata_got_classes) && register_count &&
                 register_first <= 32 && register_count <= 32 - register_first;
    if (valid)
    {
        BusterX86MetadataGotClass entry_class = buster_x86_metadata_got_classes[class_index];
        u32 operand_count = entry_class.shape == BUSTER_X86_GOT_SHAPE_SINGLE ? 1 : 2;
        u32 instances = entry_class.shape == BUSTER_X86_GOT_SHAPE_SINGLE ? 1 : register_count;
        u16 width = entry_class.operand_width;
        u32 source_form = UINT32_MAX;
        u32 direct_form = UINT32_MAX;
        s64 addend = -(s64)BUSTER_X86_GOT_FIELD_WIDTH;
        // Highest register first: that instance selects both forms for the
        // whole register group and therefore requires every prefix bit the
        // remaining instances may need.
        for (u32 instance = instances; valid && instance > 0; instance -= 1)
        {
            u32 reg = entry_class.shape == BUSTER_X86_GOT_SHAPE_SINGLE ? 0 : register_first + instance - 1;
            BusterX86MetadataPhysicalOperand source_operands[2] = {buster_x86_metadata_got_memory(width), {0}};
            BusterX86MetadataPhysicalOperand direct_operands[2] = {buster_x86_metadata_got_direct_operand(entry_class.direct, width), {0}};
            if (entry_class.shape == BUSTER_X86_GOT_SHAPE_DESTINATION)
            {
                source_operands[0] = buster_x86_metadata_got_register((u16)reg, width);
                source_operands[1] = buster_x86_metadata_got_memory(width);
                direct_operands[0] = source_operands[0];
                direct_operands[1] = buster_x86_metadata_got_direct_operand(entry_class.direct, width);
            }
            else if (entry_class.shape == BUSTER_X86_GOT_SHAPE_SOURCE)
            {
                source_operands[1] = buster_x86_metadata_got_register((u16)reg, width);
                direct_operands[0] = source_operands[1];
                direct_operands[1] = buster_x86_metadata_got_direct_operand(entry_class.direct, width);
            }
            u8 source_bytes[BUSTER_X86_GOT_SCRATCH] = {0};
            u8 direct_bytes[BUSTER_X86_GOT_SCRATCH] = {0};
            u32 source_size = 0;
            u32 direct_size = 0;
            BusterX86MetadataRelocation source_field = {0};
            BusterX86MetadataRelocation direct_field = {0};
            valid = buster_x86_metadata_got_form(entry_class.source_mnemonic, source_operands, operand_count, &source_form, source_bytes,
                                                 &source_size, &source_field) &&
                    buster_x86_metadata_got_form(entry_class.direct_mnemonic, direct_operands, operand_count, &direct_form, direct_bytes,
                                                 &direct_size, &direct_field);
            valid = valid && source_field.kind == BUSTER_X86_METADATA_RELOCATION_PC32 && source_field.addend == addend &&
                    source_field.offset >= BUSTER_X86_GOT_PREFIX_MIN && source_field.offset <= BUSTER_X86_GOT_PREFIX_MAX &&
                    direct_size <= source_size;
            u32 prefix = valid ? source_field.offset : 0;
            u32 pad = valid ? source_size - direct_size : 0;
            valid = valid && direct_field.offset + pad == prefix &&
                    direct_field.kind == (entry_class.patch == BUSTER_X86_METADATA_GOT_PATCH_PC32
                                              ? (u8)BUSTER_X86_METADATA_RELOCATION_PC32
                                              : (u8)BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32) &&
                    direct_field.addend == (entry_class.patch == BUSTER_X86_METADATA_GOT_PATCH_PC32 ? addend : 0);
            if (valid)
            {
                u32 slot = prefix - BUSTER_X86_GOT_PREFIX_MIN;
                BusterX86MetadataGotEntry entry = {0};
                memcpy(entry.source, source_bytes, prefix);
                memset(entry.direct, buster_x86_metadata_nop_byte, pad);
                memcpy(entry.direct + pad, direct_bytes, prefix - pad);
                // REX.X/B are ignored by RIP-relative addressing.  REX2 rows
                // stay exact: accepting noncanonical payload bits is optional,
                // while reading outside the promised four bytes is not.
                bool rex = prefix == 3 && (source_bytes[0] & 0xf0u) == 0x40u;
                entry.mask = rex ? (u8)0xfcu : (u8)0xffu;
                entry.patch = entry_class.patch;
                bool closed_load = class_index == 0 && reg < BUSTER_X86_GOT_APX_FIRST_REGISTER;
                valid = buster_x86_metadata_got_counts[slot] < BUSTER_X86_GOT_CAPACITY &&
                        buster_x86_metadata_got_distinct(slot, &entry, prefix) &&
                        (!closed_load || !buster_x86_metadata_got_load_count || buster_x86_metadata_got_load_slot == slot);
                if (valid)
                {
                    buster_x86_metadata_got_entries[slot][buster_x86_metadata_got_counts[slot]] = entry;
                    buster_x86_metadata_got_counts[slot] += 1;
                    if (closed_load)
                    {
                        buster_x86_metadata_got_load_slot = (u8)slot;
                        buster_x86_metadata_got_load_count += 1;
                    }
                }
            }
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_got_prepare(u8 requested)
{
    requested = (u8)(requested | ((requested & BUSTER_X86_GOT_PREPARE_RELAXABLE) ? BUSTER_X86_GOT_PREPARE_LOAD : 0));
    u8 missing = (u8)(requested & (u8)~buster_x86_metadata_got_prepared);
    if (missing)
    {
        BUSTER_CHECK_SERIAL_INITIALIZATION();
        buster_x86_metadata_prewarm();
        bool valid = buster_x86_metadata_nop_prepare() &&
                     (!(requested & BUSTER_X86_GOT_PREPARE_LOAD) || (missing & BUSTER_X86_GOT_PREPARE_LOAD) ||
                      (buster_x86_metadata_got_valid & BUSTER_X86_GOT_PREPARE_LOAD));
        if (valid && (missing & BUSTER_X86_GOT_PREPARE_LOAD))
        {
            valid = buster_x86_metadata_got_prepare_class(0, 0, BUSTER_X86_GOT_REGISTERS);
            buster_x86_metadata_got_valid |= valid ? BUSTER_X86_GOT_PREPARE_LOAD : 0;
        }
        if (valid && (missing & BUSTER_X86_GOT_PREPARE_RELAXABLE))
        {
            for (u32 class_index = 1; valid && class_index < BUSTER_ARRAY_LENGTH(buster_x86_metadata_got_classes); class_index += 1)
            {
                valid = buster_x86_metadata_got_prepare_class(class_index, 0, BUSTER_X86_GOT_REGISTERS);
            }
            for (u32 class_index = 0; valid && class_index < BUSTER_X86_GOT_APX_CLASS_COUNT; class_index += 1)
            {
                valid = buster_x86_metadata_got_prepare_class(class_index, BUSTER_X86_GOT_APX_FIRST_REGISTER, BUSTER_X86_GOT_REGISTERS);
            }
        }
        buster_x86_metadata_got_valid |= valid ? missing : 0;
        buster_x86_metadata_got_prepared |= missing;
    }
    return (buster_x86_metadata_got_valid & requested) == requested;
}
'''
sub_once(
    metadata_path,
    r'''BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_got_prepare\(u8 requested\)\n\{.*?(?=\nBusterX86MetadataGotPatch buster_x86_metadata_relax_got_reference)''',
    prepare_replacement,
    re.S,
)

relax_replacement = r'''BusterX86MetadataGotPatch buster_x86_metadata_relax_got_reference(BusterX86MetadataGotSite site, u8* section, u64 field_offset, u64 section_size,
                                                                 s64 addend)
{
    BusterX86MetadataGotPatch result = BUSTER_X86_METADATA_GOT_PATCH_NONE;
    // Plain R_X86_64_GOTPCREL does not promise an instruction boundary.
    // Refusing it is the only safe choice without materializing a real GOT
    // slot: no byte before its field is inspected or written.
    bool relaxable = site == BUSTER_X86_METADATA_GOT_SITE_GOTPCRELX ||
                     site == BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX ||
                     site == BUSTER_X86_METADATA_GOT_SITE_CODE_4_GOTPCRELX;
    if (section && relaxable && field_offset <= section_size && BUSTER_X86_GOT_FIELD_WIDTH <= section_size - field_offset &&
        buster_x86_metadata_got_prepare(BUSTER_X86_GOT_PREPARE_RELAXABLE))
    {
        u32 prefix = site == BUSTER_X86_METADATA_GOT_SITE_GOTPCRELX ? 2u
                     : site == BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX ? 3u
                                                                          : 4u;
        u32 slot = prefix - BUSTER_X86_GOT_PREFIX_MIN;
        if (field_offset >= prefix)
        {
            u8* sequence = section + field_offset - prefix;
            for (u32 index = 0; result == BUSTER_X86_METADATA_GOT_PATCH_NONE && index < buster_x86_metadata_got_counts[slot]; index += 1)
            {
                BusterX86MetadataGotEntry const* entry = &buster_x86_metadata_got_entries[slot][index];
                bool matched = (u8)(sequence[0] & entry->mask) == entry->source[0] &&
                               memcmp(sequence + 1, entry->source + 1, prefix - 1) == 0;
                if (matched && (entry->patch == BUSTER_X86_METADATA_GOT_PATCH_PC32 || addend == -(s64)BUSTER_X86_GOT_FIELD_WIDTH))
                {
                    memcpy(sequence, entry->direct, prefix);
                    result = (BusterX86MetadataGotPatch)entry->patch;
                }
            }
        }
    }
    return result;
}
'''
sub_once(
    metadata_path,
    r'''BusterX86MetadataGotPatch buster_x86_metadata_relax_got_reference\(BusterX86MetadataGotSite site, u8\* section, u64 field_offset, u64 section_size,\n                                                                 s64 addend\)\n\{.*?(?=\n// The complete walk:)''',
    relax_replacement,
    re.S,
)
replace_once(
    metadata_path,
    """// silently rewrites a different instruction. Plain R_X86_64_GOTPCREL
// promises nothing at all, so it keeps the one closed family it has always
// had -- MOV r64,[RIP+disp32] -> LEA, the first class below.
""",
    """// silently rewrites a different instruction. Plain R_X86_64_GOTPCREL
// promises nothing at all and is therefore never decoded. This compiler emits
// the unambiguous REX spelling for its own MOV-r64 GOT loads.
""",
)

# Existing closed-family tests now exercise the spelling that promises REX.
test_path = "src/buster/tests/compiler/assembly/x86_64_got_test.c"
text = read(test_path)
text, count = re.subn(
    r"\bBUSTER_X86_METADATA_GOT_SITE_GOTPCREL\b",
    "BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX",
    text,
)
if count < 1:
    raise SystemExit(f"{test_path}: no legacy GOTPCREL sites replaced")
marker = "    static u32 const values[] = {0, 1, 127, 128, 0x7fffffff, 0x80000000, 0xffffff80, 0xffffffff};\n"
if text.count(marker) != 1:
    raise SystemExit(f"{test_path}: values marker not unique")
extra_tests = r'''    // Plain type 9 names only a field. None of the eight REX-looking
    // bytes that previously matched may be consumed from the preceding
    // instruction, and failure must be atomic.
    for (u32 preceding = 0x40; preceding <= 0x4f; preceding += 1)
    {
        u8 ambiguous[] = {(u8)preceding, 0x8b, 0x05, 0x11, 0x22, 0x33, 0x44};
        u8 unchanged[sizeof(ambiguous)];
        memcpy(unchanged, ambiguous, sizeof(ambiguous));
        BUSTER_TEST(arguments,
                    buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_GOTPCREL, ambiguous, 3, sizeof(ambiguous), -4) ==
                        BUSTER_X86_METADATA_GOT_PATCH_NONE);
        BUSTER_TEST(arguments, memcmp(ambiguous, unchanged, sizeof(ambiguous)) == 0);
    }
    // Type 43 promises a four-byte REX2/opcode/ModRM prefix. Encoding one
    // EGPR source and replacement through the checked metadata front door
    // both forces preparation of the complete APX vocabulary and verifies the
    // relocation field stays at the promised offset.
    {
        u8 apx_source[16] = {0};
        u8 apx_direct[16] = {0};
        String8 apx_features[1] = {S8("APX_F")};
        BusterX86MetadataPhysicalOperand apx_operands[2] = {
            {.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER, .width = 64,
             .reg = {.index = 16, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR}},
            {.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY, .width = 64,
             .memory = {.has_symbol = true, .symbol = S8("target"), .rip_relative = true,
                        .has_displacement = true, .address_size = 64, .scale = 1}},
        };
        for (u32 output = 0; output < 2; output += 1)
        {
            BusterX86MetadataRelocation field = {0};
            BusterX86MetadataEmitResult encoded = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
                .physical = {.mnemonic = output ? S8("LEA") : S8("MOV"), .operands = apx_operands, .operand_count = 2,
                             .features = {.names = apx_features, .count = 1},
                             .address_size = 64, .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64},
                .output = output ? apx_direct : apx_source, .output_capacity = sizeof(apx_source),
                .relocations = &field, .relocation_capacity = 1,
            });
            BUSTER_TEST(arguments, encoded.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && encoded.byte_count == 8 &&
                                       encoded.relocation_count == 1 && field.offset == 4 && field.width == 4 &&
                                       field.kind == BUSTER_X86_METADATA_RELOCATION_PC32 && field.addend == -4 &&
                                       (output ? apx_direct[0] : apx_source[0]) == 0xd5);
        }
        memcpy(bytes, apx_source, 8);
        bytes[4] = 0x11;
        bytes[5] = 0x22;
        bytes[6] = 0x33;
        bytes[7] = 0x44;
        memcpy(expected, bytes, 8);
        memcpy(expected, apx_direct, 4);
        BUSTER_TEST(arguments,
                    buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_CODE_4_GOTPCRELX, bytes, 4, 8, -4) ==
                        BUSTER_X86_METADATA_GOT_PATCH_PC32);
        BUSTER_TEST(arguments, memcmp(bytes, expected, 8) == 0);
    }
'''
text = text.replace(marker, marker + extra_tests, 1)
write(test_path, text)

replace_once(
    "src/buster/tests/compiler/object/object_test.c",
    """    u32 gotpc_rel_types[] = {9, 41, 42};
    ObjectRelocationKind gotpc_rel_kinds[] = {OBJECT_RELOCATION_X86_64_GOTPCREL, OBJECT_RELOCATION_X86_64_GOTPCRELX,
                                              OBJECT_RELOCATION_X86_64_REX_GOTPCRELX};
""",
    """    u32 gotpc_rel_types[] = {9, 41, 42, 43};
    ObjectRelocationKind gotpc_rel_kinds[] = {OBJECT_RELOCATION_X86_64_GOTPCREL, OBJECT_RELOCATION_X86_64_GOTPCRELX,
                                              OBJECT_RELOCATION_X86_64_REX_GOTPCRELX, OBJECT_RELOCATION_X86_64_CODE_4_GOTPCRELX};
""",
)

doc_path = "docs/x86-64-got-authority.md"
replace_once(
    doc_path,
    """| 9 `R_X86_64_GOTPCREL` | `X86_64_GOTPCREL` | promises nothing | the closed MOV r64 -> LEA family only |
| 41 `R_X86_64_GOTPCRELX` | `X86_64_GOTPCRELX` | opcode and ModRM | every row with no REX prefix |
| 42 `R_X86_64_REX_GOTPCRELX` | `X86_64_REX_GOTPCRELX` | one REX prefix more | every REX-prefixed row |
""",
    """| 9 `R_X86_64_GOTPCREL` | `X86_64_GOTPCREL` | promises nothing | never decoded; the link fails by symbol unless a real GOT slot exists |
| 41 `R_X86_64_GOTPCRELX` | `X86_64_GOTPCRELX` | opcode and ModRM | every row with no REX prefix |
| 42 `R_X86_64_REX_GOTPCRELX` | `X86_64_REX_GOTPCRELX` | one REX prefix more | every REX-prefixed row |
| 43 `R_X86_64_CODE_4_GOTPCRELX` | `X86_64_CODE_4_GOTPCRELX` | REX2, opcode and ModRM | MOV, TEST and ALU rows using r16-r31 |
""",
)
replace_once(
    doc_path,
    """Plain `R_X86_64_GOTPCREL` prepares the
closed family alone, which is what this compiler's own `-fPIC` output needs;
the rest is derived only when an object actually carries a relaxable spelling.
""",
    """Plain `R_X86_64_GOTPCREL` is never decoded because it promises no
instruction boundary. This compiler's own `-fPIC` MOV-r64 loads are written as
`R_X86_64_REX_GOTPCRELX`; the wider vocabulary is derived only when an object
actually carries a relaxable spelling.
""",
)

# Neither helper is part of the final candidate tree.
Path(".github/workflows/pr767-code4-fix.yml").unlink()
Path(__file__).unlink()
