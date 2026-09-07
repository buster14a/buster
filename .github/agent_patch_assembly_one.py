from pathlib import Path
import re
import sys

root = Path(sys.argv[1]).resolve()
issue = int(sys.argv[2])
metadata_path = root / "src/buster/lib/compiler/assembly/x86_64_metadata.c"
assembly_path = root / "src/buster/lib/compiler/assembly/assembly.c"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


if issue == 190:
    text = metadata_path.read_text(encoding="utf-8")
    old = """            p1 = has_architectural_mask
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
    new = """            bool has_vvvv_operand = vvvv_binding != 0;
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
            else if (pattern.has_nd && pattern.nd_value)
                p2 = (u8)(0x10 | (requested_nf ? 0x04 : 0) | (vvvv_index < 16 ? 0x08 : 0));
            else if (has_vvvv_operand)
                p2 = (u8)((requested_nf ? 0x04 : 0) | (vvvv_index < 16 ? 0x08 : 0));
            else
                p2 = (u8)(0x08 | (requested_nf ? 0x04 : 0));
"""
    text = replace_once(text, old, new, "APX EVEX pp/vvvv/V-prime")
    metadata_path.write_text(text, encoding="utf-8")
elif issue == 191:
    text = metadata_path.read_text(encoding="utf-8")
    text = replace_once(
        text,
        "BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_tuple_scale(BusterX86MetadataForm const* form,\n"
        "                                                              BusterX86MetadataPatternSemantics const* pattern)\n",
        "BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_tuple_scale(BusterX86MetadataForm const* form,\n"
        "                                                              BusterX86MetadataPatternSemantics const* pattern,\n"
        "                                                              bool broadcast)\n",
        "tuple-scale signature",
    )
    text = replace_once(
        text,
        "    u32 result = 0;\n    switch (form->tuple_kind)\n",
        "    u32 result = 0;\n"
        "    if (broadcast && (form->tuple_kind == BUSTER_X86_METADATA_TUPLE_FULL ||\n"
        "                      form->tuple_kind == BUSTER_X86_METADATA_TUPLE_HALF))\n"
        "        result = element;\n"
        "    else switch (form->tuple_kind)\n",
        "broadcast tuple scale",
    )
    text = replace_once(
        text,
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
    text = replace_once(
        text,
        "            u8 tuple_scale = buster_x86_metadata_emit_tuple_scale(form, pattern);\n",
        "            u8 tuple_scale = buster_x86_metadata_emit_tuple_scale(form, pattern, broadcast);\n",
        "tuple-scale call",
    )
    text = replace_once(
        text,
        "    if (has_memory && !moffs_form && !buster_x86_metadata_emit_address(memory, &form, &pattern, force_disp32, &address))\n",
        "    if (has_memory && !moffs_form &&\n"
        "        !buster_x86_metadata_emit_address(memory, &form, &pattern, query.attributes.broadcast_elements != 0, force_disp32, &address))\n",
        "direct address call",
    )
    text = replace_once(
        text,
        "            address_ready = buster_x86_metadata_emit_address(memory, form, plan->pattern, force_disp32, &address);\n",
        "            address_ready = buster_x86_metadata_emit_address(memory, form, plan->pattern,\n"
        "                                                               query.attributes.broadcast_elements != 0, force_disp32, &address);\n",
        "prepared address call",
    )
    metadata_path.write_text(text, encoding="utf-8")
elif issue == 192:
    text = metadata_path.read_text(encoding="utf-8")
    block = re.compile(
        r"    // APX REX2's unary qword IMUL memory row is encoded by F7 /5, whose\n"
        r"(?:.*\n)*?"
        r"    bool apx_rex2_mov_memory_qword = form\.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 &&\n"
        r"(?:.*\n)*?"
        r"                                     pattern\.has_modrm;\n"
    )
    text, count = block.subn("", text, count=1)
    if count != 1:
        raise RuntimeError(f"REX2 workaround block: expected one match, found {count}")
    text = replace_once(
        text,
        "        bool scalar_memory_width_rex_w = form.prefix_kind == BUSTER_X86_METADATA_PREFIX_LEGACY ||\n"
        "                                         form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX;\n",
        "        bool scalar_memory_width_rex_w = form.prefix_kind == BUSTER_X86_METADATA_PREFIX_LEGACY ||\n"
        "                                         form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX ||\n"
        "                                         form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2;\n",
        "REX2 scalar memory width",
    )
    text = replace_once(
        text,
        "        if ((apx_rex2_unary_imul_qword || apx_rex2_mov_memory_qword) &&\n"
        "            physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&\n"
        "            (physical.width ? physical.width : physical.memory.source_width) == 64)\n"
        "            rex_w = true;\n",
        "",
        "remove REX2 width workaround",
    )
    text = replace_once(
        text,
        "    if (pattern.df64 && !pattern.has_w && !apx_rex2_unary_imul_qword && !apx_rex2_mov_memory_qword) rex_w = false;\n",
        "    if (pattern.df64 && !pattern.has_w) rex_w = false;\n",
        "DF64 cleanup",
    )
    text = replace_once(
        text,
        "    if (pattern.immune_rexw && !apx_rex2_unary_imul_qword && !apx_rex2_mov_memory_qword) rex_w = false;\n",
        "    if (pattern.immune_rexw) rex_w = false;\n",
        "IMMUNE_REXW cleanup",
    )
    metadata_path.write_text(text, encoding="utf-8")
elif issue == 193:
    text = assembly_path.read_text(encoding="utf-8")
    old = """    char8 suffix = assembly_ascii_lower(mnemonic.pointer[mnemonic.length - 1]);
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
    new = """    char8 suffix = assembly_ascii_lower(mnemonic.pointer[mnemonic.length - 1]);
    u8 suffix_width = suffix == 'b' ? 8 : suffix == 'w' ? 16 : suffix == 'l' ? 32 : suffix == 'q' ? 64 : 0;
    // The full AT&T mnemonic is present in the x87 typed alias table (for
    // example `fiadds`), while the base candidate below is the unsuffixed
    // `fiadd` entry and therefore has no suffix_width metadata of its own.
    // Capture the typed width before stripping the suffix.  The x87 alias
    // table, rather than a generic rule, is the sole authority for `s`/`t`.
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
    text = replace_once(text, old, new, "restrict s/t suffixes")
    assembly_path.write_text(text, encoding="utf-8")
else:
    raise SystemExit(f"unsupported issue {issue}")

print(f"patched assembly issue {issue}")
