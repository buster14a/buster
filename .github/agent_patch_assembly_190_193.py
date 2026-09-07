from pathlib import Path
import re
import sys

root = Path(sys.argv[1]).resolve()
metadata_path = root / "src/buster/lib/compiler/assembly/x86_64_metadata.c"
assembly_path = root / "src/buster/lib/compiler/assembly/assembly.c"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


metadata = metadata_path.read_text(encoding="utf-8")

metadata = replace_once(
    metadata,
    "BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_tuple_scale(BusterX86MetadataForm const* form,\n"
    "                                                              BusterX86MetadataPatternSemantics const* pattern)\n",
    "BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_tuple_scale(BusterX86MetadataForm const* form,\n"
    "                                                              BusterX86MetadataPatternSemantics const* pattern,\n"
    "                                                              bool broadcast)\n",
    "tuple-scale signature",
)
metadata = replace_once(
    metadata,
    "    u32 result = 0;\n"
    "    switch (form->tuple_kind)\n",
    "    u32 result = 0;\n"
    "    if (broadcast && (form->tuple_kind == BUSTER_X86_METADATA_TUPLE_FULL ||\n"
    "                      form->tuple_kind == BUSTER_X86_METADATA_TUPLE_HALF))\n"
    "        result = element;\n"
    "    else switch (form->tuple_kind)\n",
    "broadcast tuple scale",
)
metadata = replace_once(
    metadata,
    "BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_address(BusterX86MetadataPhysicalMemory memory,\n"
    "                                                           BusterX86MetadataForm const* form,\n"
    "                                                           BusterX86MetadataPatternSemantics const* pattern,\n"
    "                                                           bool force_disp32,\n",
    "BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_address(BusterX86MetadataPhysicalMemory memory,\n"
    "                                                           BusterX86MetadataForm const* form,\n"
    "                                                           BusterX86MetadataPatternSemantics const* pattern,\n"
    "                                                           bool broadcast,\n"
    "                                                           bool force_disp32,\n",
    "address signature",
)
metadata = replace_once(
    metadata,
    "            u8 tuple_scale = buster_x86_metadata_emit_tuple_scale(form, pattern);\n",
    "            u8 tuple_scale = buster_x86_metadata_emit_tuple_scale(form, pattern, broadcast);\n",
    "tuple-scale call",
)
metadata = replace_once(
    metadata,
    "    if (has_memory && !moffs_form && !buster_x86_metadata_emit_address(memory, &form, &pattern, force_disp32, &address))\n",
    "    if (has_memory && !moffs_form &&\n"
    "        !buster_x86_metadata_emit_address(memory, &form, &pattern, query.attributes.broadcast_elements != 0, force_disp32, &address))\n",
    "address call",
)

hack_pattern = re.compile(
    r"    // APX REX2's unary qword IMUL memory row is encoded by F7 /5, whose\n"
    r"(?:.*\n)*?"
    r"    bool apx_rex2_mov_memory_qword = form\.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 &&\n"
    r"(?:.*\n)*?"
    r"                                     pattern\.has_modrm;\n"
)
metadata, count = hack_pattern.subn("", metadata, count=1)
if count != 1:
    raise RuntimeError(f"REX2 per-iclass hacks: expected one block, found {count}")
metadata = replace_once(
    metadata,
    "        bool scalar_memory_width_rex_w = form.prefix_kind == BUSTER_X86_METADATA_PREFIX_LEGACY ||\n"
    "                                         form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX;\n",
    "        bool scalar_memory_width_rex_w = form.prefix_kind == BUSTER_X86_METADATA_PREFIX_LEGACY ||\n"
    "                                         form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX ||\n"
    "                                         form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2;\n",
    "REX2 scalar memory width",
)
metadata = replace_once(
    metadata,
    "        if ((apx_rex2_unary_imul_qword || apx_rex2_mov_memory_qword) &&\n"
    "            physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&\n"
    "            (physical.width ? physical.width : physical.memory.source_width) == 64)\n"
    "            rex_w = true;\n",
    "",
    "remove REX2 W workaround",
)
metadata = replace_once(
    metadata,
    "    if (pattern.df64 && !pattern.has_w && !apx_rex2_unary_imul_qword && !apx_rex2_mov_memory_qword) rex_w = false;\n",
    "    if (pattern.df64 && !pattern.has_w) rex_w = false;\n",
    "simplify DF64 W",
)
metadata = replace_once(
    metadata,
    "    if (pattern.immune_rexw && !apx_rex2_unary_imul_qword && !apx_rex2_mov_memory_qword) rex_w = false;\n",
    "    if (pattern.immune_rexw) rex_w = false;\n",
    "simplify IMMUNE_REXW",
)

old_apx = """            p1 = has_architectural_mask
                     ? (u8)((pattern.w ? 0x80 : 0) | pp)
                     : (u8)(!apx_evex_fixed_width_no_w && apx_width == 64 ? 0x80 : apx_width == 16 ? 0x01 : 0);
            if (pattern.has_scc)
                p1 |= (u8)(0x04 | ((query.attributes.dfv & 0xf) << 3));
            else if (pattern.has_nd && pattern.nd_value)
                p1 |= (u8)((((~vvvv_index) & 0xf) << 3) | 0x04);
            else
                p1 |= apx_evex_fixed_width_no_w ? (u8)(0x7c | pp) : 0x7c;
            if (has_memory && memory.has_index && !memory.vsib && (memory.index.index & 16)) p1 &= (u8)~0x04;
            if (pattern.has_scc)
                p2 = (u8)pattern.scc_value;
            else if (pattern.has_nd && pattern.nd_value)
                p2 = (u8)(0x10 | (requested_nf ? 0x04 : 0) | (vvvv_index < 16 ? 0x08 : 0));
            else
                p2 = (u8)(0x08 | (requested_nf ? 0x04 : 0));
"""
new_apx = """            bool has_vvvv_operand = vvvv_binding &&
                                    vvvv_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER;
            p1 = has_architectural_mask
                     ? (u8)((pattern.w ? 0x80 : 0) | pp)
                     : (u8)((!apx_evex_fixed_width_no_w && apx_width == 64 ? 0x80 : apx_width == 16 ? 0x01 : 0) | pp);
            if (pattern.has_scc)
                p1 |= (u8)(0x04 | ((query.attributes.dfv & 0xf) << 3));
            else if (has_vvvv_operand)
                p1 |= (u8)((((~vvvv_index) & 0xf) << 3) | 0x04);
            else
                p1 |= 0x7c;
            if (has_memory && memory.has_index && !memory.vsib && (memory.index.index & 16)) p1 &= (u8)~0x04;
            if (pattern.has_scc)
                p2 = (u8)pattern.scc_value;
            else
                p2 = (u8)((pattern.has_nd && pattern.nd_value ? 0x10 : 0) | (requested_nf ? 0x04 : 0) |
                          (!has_vvvv_operand || vvvv_index < 16 ? 0x08 : 0));
"""
metadata = replace_once(metadata, old_apx, new_apx, "APX EVEX pp/vvvv/V-prime")

metadata_path.write_text(metadata, encoding="utf-8")

assembly = assembly_path.read_text(encoding="utf-8")
old_suffix = """    char8 suffix = assembly_ascii_lower(mnemonic.pointer[mnemonic.length - 1]);
    u8 suffix_width = suffix == 'b' ? 8 : suffix == 'w' ? 16 : suffix == 'l' ? 32 : suffix == 'q' ? 64
                     : suffix == 's' ? 32
                     : suffix == 't' ? 80
                                      : 0;
    if (!suffix_width)
    {
        return false;
    }
    // The full AT&T mnemonic is present in the x87 typed alias table (for
    // example `fiadds`), while the base candidate below is the unsuffixed
    // `fiadd` entry and therefore has no suffix_width metadata of its own.
    // Capture the typed width before stripping the suffix.
    AssemblyInstructionInfo suffixed_info = {.opcode = ASSEMBLY_OPCODE_COUNT};
    if (assembly_instruction_lookup(target, syntax, mnemonic, &suffixed_info) && suffixed_info.suffix_width)
    {
        suffix_width = suffixed_info.suffix_width;
    }
"""
new_suffix = """    char8 suffix = assembly_ascii_lower(mnemonic.pointer[mnemonic.length - 1]);
    u8 suffix_width = suffix == 'b' ? 8 : suffix == 'w' ? 16 : suffix == 'l' ? 32 : suffix == 'q' ? 64 : 0;
    // The full AT&T mnemonic is present in the x87 typed alias table (for
    // example `fiadds`), while the base candidate below is the unsuffixed
    // `fiadd` entry and therefore has no suffix_width metadata of its own.
    // Capture the typed width before stripping the suffix.  In particular,
    // `s` and `t` are x87 type suffixes, not generic integer-width suffixes:
    // accept them only when the handwritten alias table defines the complete
    // spelling and its architectural width.
    AssemblyInstructionInfo suffixed_info = {.opcode = ASSEMBLY_OPCODE_COUNT};
    if (assembly_instruction_lookup(target, syntax, mnemonic, &suffixed_info) && suffixed_info.suffix_width)
    {
        suffix_width = suffixed_info.suffix_width;
    }
    if (!suffix_width)
    {
        return false;
    }
"""
assembly = replace_once(assembly, old_suffix, new_suffix, "typed s/t suffixes")
assembly = replace_once(
    assembly,
    "    else if (!has_suffix_alias)\n"
    "    {\n"
    "        mnemonic_suffix_base = (String8){0};\n"
    "    }\n",
    "    else\n"
    "    {\n"
    "        // Once the complete mnemonic exists, operand rejection belongs\n"
    "        // to that instruction.  Never reinterpret it by stripping a\n"
    "        // trailing character and selecting a different mnemonic.\n"
    "        mnemonic_suffix_base = (String8){0};\n"
    "    }\n",
    "exact mnemonic wins over suffix fallback",
)
assembly_path.write_text(assembly, encoding="utf-8")

print("patched assembly issues 190-193")
