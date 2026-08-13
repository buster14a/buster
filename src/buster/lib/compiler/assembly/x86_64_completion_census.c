#include <buster/lib/compiler/assembly/x86_64_completion_census.h>
#include <buster/lib/string.h>

// The census deliberately keeps source synthesis small and conservative.  A
// generated row is only marked source-capable after the public assembler has
// accepted a spelling and produced bytes/relocations equivalent to the direct
// metadata emitter.  Unsupported shapes remain explicit unresolved rows.

typedef struct BusterX86CompletionCensusSourceResult BusterX86CompletionCensusSourceResult;
struct BusterX86CompletionCensusSourceResult
{
    u8 classification;
    u32 byte_count;
    u32 relocation_count;
    u16 diagnostic_kind;
    u32 mismatch_index;
    u8 mismatch_direct_byte;
    u8 mismatch_source_byte;
    u8 bytes_match;
    u8 relocations_match;
};

BUSTER_GLOBAL_LOCAL bool buster_x86_completion_string_equal(String8 first, String8 second)
{
    u32 index = 0;
    if (first.length != second.length) return false;
    for (; index < first.length; index += 1)
        if ((u8)first.pointer[index] != (u8)second.pointer[index]) return false;
    return true;
}

BUSTER_GLOBAL_LOCAL String8 buster_x86_completion_register(Arena* arena, BusterX86MetadataPhysicalRegister reg)
{
    static String8 const gpr8[] = {S8_INITIALIZER("al"), S8_INITIALIZER("cl"), S8_INITIALIZER("dl"), S8_INITIALIZER("bl")};
    static String8 const gpr16[] = {S8_INITIALIZER("ax"), S8_INITIALIZER("cx"), S8_INITIALIZER("dx"), S8_INITIALIZER("bx"),
                                    S8_INITIALIZER("sp"), S8_INITIALIZER("bp"), S8_INITIALIZER("si"), S8_INITIALIZER("di")};
    static String8 const gpr32[] = {S8_INITIALIZER("eax"), S8_INITIALIZER("ecx"), S8_INITIALIZER("edx"), S8_INITIALIZER("ebx"),
                                    S8_INITIALIZER("esp"), S8_INITIALIZER("ebp"), S8_INITIALIZER("esi"), S8_INITIALIZER("edi")};
    static String8 const gpr64[] = {S8_INITIALIZER("rax"), S8_INITIALIZER("rcx"), S8_INITIALIZER("rdx"), S8_INITIALIZER("rbx"),
                                    S8_INITIALIZER("rsp"), S8_INITIALIZER("rbp"), S8_INITIALIZER("rsi"), S8_INITIALIZER("rdi")};
    static String8 const segment[] = {S8_INITIALIZER("es"), S8_INITIALIZER("cs"), S8_INITIALIZER("ss"),
                                      S8_INITIALIZER("ds"), S8_INITIALIZER("fs"), S8_INITIALIZER("gs")};
    switch (reg.physical_class)
    {
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR:
        if (reg.high_byte && reg.index >= 4 && reg.index < 8)
        {
            static String8 const high[] = {S8_INITIALIZER("ah"), S8_INITIALIZER("ch"), S8_INITIALIZER("dh"), S8_INITIALIZER("bh")};
            return high[reg.index - 4];
        }
        if (reg.width == 8 && reg.index < BUSTER_ARRAY_LENGTH(gpr8)) return gpr8[reg.index];
        if (reg.width == 16 && reg.index < BUSTER_ARRAY_LENGTH(gpr16)) return gpr16[reg.index];
        if (reg.width == 32 && reg.index < BUSTER_ARRAY_LENGTH(gpr32)) return gpr32[reg.index];
        if (reg.width == 64 && reg.index < BUSTER_ARRAY_LENGTH(gpr64)) return gpr64[reg.index];
        if (reg.width == 8) return string_format(arena, S8("r{u16}b"), reg.index);
        if (reg.width == 16) return string_format(arena, S8("r{u16}w"), reg.index);
        if (reg.width == 32) return string_format(arena, S8("r{u16}d"), reg.index);
        if (reg.width == 64) return string_format(arena, S8("r{u16}"), reg.index);
        return (String8){0};
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM: return string_format(arena, S8("xmm{u16}"), reg.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM: return string_format(arena, S8("ymm{u16}"), reg.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM: return string_format(arena, S8("zmm{u16}"), reg.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK: return string_format(arena, S8("k{u16}"), reg.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM: return string_format(arena, S8("tmm{u16}"), reg.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX: return string_format(arena, S8("mm{u16}"), reg.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_BND: return string_format(arena, S8("bnd{u16}"), reg.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_CONTROL: return string_format(arena, S8("cr{u16}"), reg.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_DEBUG: return string_format(arena, S8("dr{u16}"), reg.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT:
        return reg.index < BUSTER_ARRAY_LENGTH(segment) ? segment[reg.index] : (String8){0};
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL: return S8("bsr0");
    default: return (String8){0};
    }
}

BUSTER_GLOBAL_LOCAL String8 buster_x86_completion_immediate(Arena* arena, BusterX86MetadataPhysicalOperand operand)
{
    if (operand.has_unsigned_value) return string_format(arena, S8("0x{u64:x,no_prefix}"), operand.unsigned_value);
    if (!operand.has_value) return (String8){0};
    if (operand.value >= 0) return string_format(arena, S8("0x{u64:x,no_prefix}"), (u64)operand.value);
    if (operand.value == INT64_MIN) return S8("-0x8000000000000000");
    return string_format(arena, S8("-0x{u64:x,no_prefix}"), (u64)-operand.value);
}

BUSTER_GLOBAL_LOCAL String8 buster_x86_completion_symbol(Arena* arena, String8 symbol, s64 addend)
{
    if (!symbol.length) return (String8){0};
    if (!addend) return symbol;
    if (addend == INT64_MIN) return (String8){0};
    if (addend > 0) return string_format(arena, S8("{S8} + {s64}"), symbol, addend);
    return string_format(arena, S8("{S8} - {s64}"), symbol, -addend);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_completion_string_contains(String8 value, String8 needle)
{
    u32 offset = 0;
    u32 index = 0;
    bool equal = true;
    if (!needle.length || needle.length > value.length) return false;
    for (; offset + needle.length <= value.length; offset += 1)
    {
        equal = true;
        index = 0;
        for (; index < needle.length; index += 1)
            equal &= value.pointer[offset + index] == needle.pointer[index];
        if (equal) return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL String8 buster_x86_completion_segment(u8 segment)
{
    static String8 const names[] = {S8_INITIALIZER(""), S8_INITIALIZER("es:"), S8_INITIALIZER("cs:"),
                                    S8_INITIALIZER("ss:"), S8_INITIALIZER("ds:"), S8_INITIALIZER("fs:"), S8_INITIALIZER("gs:")};
    return segment < BUSTER_ARRAY_LENGTH(names) ? names[segment] : (String8){0};
}

BUSTER_GLOBAL_LOCAL bool buster_x86_completion_mask_decorator(BusterX86MetadataForm form, u32 visible_index)
{
    u32 current = 0;
    u32 operand_index = 0;
    for (; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        String8 atom = {0};
        if (!buster_x86_metadata_operand(form.id, operand_index, &metadata)) return false;
        if (!metadata.visible) continue;
        if (current == visible_index)
        {
            if (metadata.kind != BUSTER_X86_METADATA_OPERAND_REGISTER ||
                metadata.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK)
                return false;
            atom = buster_x86_metadata_string_span(metadata.atom);
            return !buster_x86_completion_string_contains(atom, S8("_R")) &&
                   !buster_x86_completion_string_contains(atom, S8("_N")) &&
                   !buster_x86_completion_string_contains(atom, S8("_B"));
        }
        current += 1;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_completion_apx_evex_memory_witness(BusterX86MetadataForm form)
{
    return form.id == 5584 &&
           buster_x86_completion_string_equal(buster_x86_metadata_string_span(form.iclass), S8("VMOVDQA32")) &&
           buster_x86_completion_string_equal(buster_x86_metadata_string_span(form.iform),
                                               S8("VMOVDQA32_XMMu32_MASKmskw_MEMu32_AVX512"));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_completion_hidden_bsr0(BusterX86MetadataForm form, bool* first)
{
    bool found = false;
    u32 operand_index = 0;
    BusterX86MetadataOperand metadata = {0};
    for (; operand_index < form.operand_count; operand_index += 1)
    {
        metadata = (BusterX86MetadataOperand){0};
        if (!buster_x86_metadata_operand(form.id, operand_index, &metadata)) return false;
        if (!metadata.visible && metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER &&
            buster_x86_completion_string_equal(buster_x86_metadata_string_span(metadata.atom), S8("XED_REG_BSR0")))
            found = true;
    }
    if (first)
    {
        String8 iform = buster_x86_metadata_string_span(form.iform);
        *first = buster_x86_completion_string_contains(iform, S8("BSRMOVH_BSR0")) ||
                 buster_x86_completion_string_contains(iform, S8("BSRMOVL_BSR0"));
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_completion_operand_schema_equal(BusterX86MetadataForm first_form,
                                                                      BusterX86MetadataForm second_form)
{
    u32 first_index = 0;
    BusterX86MetadataOperand first = {0};
    BusterX86MetadataOperand second = {0};
    if (first_form.operand_count != second_form.operand_count) return false;
    for (; first_index < first_form.operand_count; first_index += 1)
    {
        if (!buster_x86_metadata_operand(first_form.id, first_index, &first) ||
            !buster_x86_metadata_operand(second_form.id, first_index, &second))
            return false;
        if (first.kind != second.kind || first.visible != second.visible || first.access != second.access ||
            first.field_source != second.field_source || first.slot != second.slot ||
            first.physical_class != second.physical_class)
            return false;
        if (first.kind != BUSTER_X86_METADATA_OPERAND_IMMEDIATE &&
            (first.physical_width_flags != second.physical_width_flags ||
             !buster_x86_completion_string_equal(buster_x86_metadata_string_span(first.atom),
                                                 buster_x86_metadata_string_span(second.atom)) ||
             !buster_x86_completion_string_equal(buster_x86_metadata_string_span(first.width),
                                                 buster_x86_metadata_string_span(second.width))))
            return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_completion_has_compact_immediate_sibling(BusterX86MetadataForm form)
{
    BusterX86MetadataCandidateRange range = {0};
    BusterX86MetadataForm candidate = {0};
    u32 candidate_id = 0;
    u32 position = 0;
    if (!form.immediate_signed || form.immediate_width <= 1) return false;
    range = buster_x86_metadata_lookup_iclass(buster_x86_metadata_string_span(form.iclass));
    for (; position < range.count; position += 1)
    {
        if (!buster_x86_metadata_candidate_at(range, position, &candidate_id) || candidate_id == form.id ||
            !buster_x86_metadata_form(candidate_id, &candidate))
            continue;
        if (candidate.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED || candidate.immediate_width != 1 ||
            !candidate.immediate_signed || candidate.prefix_kind != form.prefix_kind || candidate.map != form.map ||
            candidate.mandatory_prefix != form.mandatory_prefix || candidate.field_flags != form.field_flags ||
            candidate.decorator_flags != form.decorator_flags || candidate.apx_flags != form.apx_flags ||
            candidate.amx_flags != form.amx_flags || candidate.mode_flags != form.mode_flags ||
            candidate.displacement_width != form.displacement_width || candidate.displacement_scale != form.displacement_scale ||
            candidate.relocation_base != form.relocation_base || !buster_x86_completion_operand_schema_equal(form, candidate))
            continue;
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL void buster_x86_completion_normalize_query(BusterX86MetadataForm form,
                                                                BusterX86MetadataPhysicalQuery query,
                                                                BusterX86MetadataPhysicalOperand* operands)
{
    u32 metadata_index = 0;
    u32 physical_index = 0;
    u32 visible_index = 0;
    u32 gpr_role_index = 0;
    BusterX86MetadataOperand metadata = {0};
    String8 atom = {0};
    bool compact_immediate_sibling = buster_x86_completion_has_compact_immediate_sibling(form);
    bool no_scale_displacement = buster_x86_completion_string_contains(buster_x86_metadata_string_span(form.attributes),
                                                                         S8("DISP8_NO_SCALE"));
    bool moffs = buster_x86_metadata_form_is_moffs(form.id);
    for (; metadata_index < form.operand_count && physical_index < query.operand_count; metadata_index += 1)
    {
        if (!buster_x86_metadata_operand(form.id, metadata_index, &metadata)) return;
        if (!metadata.visible) continue;
        if (buster_x86_completion_apx_evex_memory_witness(form) &&
            buster_x86_completion_mask_decorator(form, visible_index))
        {
            visible_index += 1;
            if (physical_index < query.operand_count &&
                operands[physical_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                operands[physical_index].reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK &&
                operands[physical_index].reg.index == 0)
                physical_index += 1;
            continue;
        }
        visible_index += 1;
        if ((form.apx_flags & BUSTER_X86_METADATA_APX_NDD) &&
            operands[physical_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            operands[physical_index].reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR)
        {
            atom = buster_x86_metadata_string_span(metadata.atom);
            if (metadata.field_source != BUSTER_X86_METADATA_FIELD_SOURCE_FIXED)
            {
                if (buster_x86_completion_string_contains(atom, S8("_N"))) gpr_role_index = 16;
                else if (buster_x86_completion_string_contains(atom, S8("_R"))) gpr_role_index = 17;
                else if (buster_x86_completion_string_contains(atom, S8("_B"))) gpr_role_index = 18;
                else gpr_role_index = physical_index;
                operands[physical_index].reg.index = (u16)gpr_role_index;
            }
        }
        else if (operands[physical_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY && moffs)
        {
            // A small absolute address is also representable by ordinary
            // ModRM MOV.  Use an address outside signed 32-bit range so the
            // public source selector must retain the A0-A3 moffs form.
            operands[physical_index].memory.has_base = false;
            operands[physical_index].memory.has_index = false;
            operands[physical_index].memory.rip_relative = false;
            operands[physical_index].memory.has_displacement = true;
            operands[physical_index].memory.displacement = INT64_C(0x1122334455667788);
            operands[physical_index].memory.has_segment = true;
            operands[physical_index].memory.segment = BUSTER_X86_METADATA_SEGMENT_ES;
        }
        else if (operands[physical_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY && no_scale_displacement)
        {
            operands[physical_index].memory.has_displacement = true;
            operands[physical_index].memory.displacement = 1;
        }
        else if (buster_x86_completion_apx_evex_memory_witness(form) &&
                 operands[physical_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
        {
            // This EVEX load is the bounded public-source witness for an
            // extended GPR address.  The element schema is u32, while the
            // source memory tuple is the full XMM destination.  Use r16 so
            // both dialects must retain the APX address extension instead
            // of proving only the ordinary low-register AVX-512 spelling.
            operands[physical_index].memory.has_base = true;
            operands[physical_index].memory.base = (BusterX86MetadataPhysicalRegister){
                .index = 16, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR};
            operands[physical_index].memory.source_width = 128;
            operands[physical_index].memory.has_displacement = false;
            operands[physical_index].memory.displacement = 0;
        }
        else if (operands[physical_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE &&
                 compact_immediate_sibling)
        {
            operands[physical_index].value = 0x100;
            operands[physical_index].has_value = true;
            operands[physical_index].has_unsigned_value = false;
            operands[physical_index].has_symbol = false;
        }
        physical_index += 1;
    }
}

BUSTER_GLOBAL_LOCAL String8 buster_x86_completion_memory_intel(Arena* arena,
                                                                BusterX86MetadataPhysicalOperand operand,
                                                                BusterX86MetadataPhysicalAttributes attributes)
{
    BusterX86MetadataPhysicalMemory memory = operand.memory;
    u16 width = memory.source_width ? memory.source_width : operand.width;
    String8 qualifier = width == 8 ? S8("byte ptr ") : width == 16 ? S8("word ptr ") : width == 32 ? S8("dword ptr ")
                          : width == 64 ? S8("qword ptr ") : width == 80 ? S8("tbyte ptr ") : width == 128 ? S8("xmmword ptr ")
                          : width == 256 ? S8("ymmword ptr ") : width == 512 ? S8("zmmword ptr ") : (String8){0};
    String8 segment = {0};
    String8 result = {0};
    bool term = false;
    String8 base = {0};
    String8 index = {0};
    String8 symbol = {0};
    if (!qualifier.length) return (String8){0};
    segment = memory.has_segment ? buster_x86_completion_segment(memory.segment) : (String8){0};
    if (memory.has_segment && !segment.length) return (String8){0};
    result = memory.has_segment ? string_format(arena, S8("{S8}{S8}["), qualifier, segment)
                                : string_format(arena, S8("{S8}["), qualifier);
    if (memory.has_base)
    {
        base = buster_x86_completion_register(arena, memory.base);
        if (!base.length) return (String8){0};
        result = string_format(arena, S8("{S8}{S8}"), result, base);
        term = true;
    }
    if (memory.rip_relative)
    {
        if (term || memory.has_index) return (String8){0};
        result = string_format(arena, S8("{S8}rip"), result);
        term = true;
    }
    if (memory.has_index)
    {
        index = buster_x86_completion_register(arena, memory.index);
        if (!index.length || !memory.scale) return (String8){0};
        result = term ? string_format(arena, S8("{S8} + {S8}*{u8}"), result, index, memory.scale)
                      : string_format(arena, S8("{S8}{S8}*{u8}"), result, index, memory.scale);
        term = true;
    }
    if (memory.has_symbol)
    {
        if (!memory.symbol.length || term) return (String8){0};
        symbol = buster_x86_completion_symbol(arena, memory.symbol, memory.addend);
        if (!symbol.length) return (String8){0};
        result = string_format(arena, S8("{S8}{S8}"), result, symbol);
        term = true;
    }
    if (memory.has_displacement || (!term && !memory.has_symbol))
    {
        if (memory.displacement < 0)
        {
            if (memory.displacement == INT64_MIN) return (String8){0};
            result = term ? string_format(arena, S8("{S8} - {s64}"), result, -memory.displacement)
                          : string_format(arena, S8("{S8}-{s64}"), result, -memory.displacement);
        }
        else result = term ? string_format(arena, S8("{S8} + {s64}"), result, memory.displacement)
                           : string_format(arena, S8("{S8}{s64}"), result, memory.displacement);
        term = true;
    }
    if (!term) return (String8){0};
    result = string_format(arena, S8("{S8}]"), result);
    if (attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST)
    {
        if (!attributes.broadcast_elements) return (String8){0};
        result = string_format(arena, S8("{S8}{{1to{u8}}}"), result, attributes.broadcast_elements);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 buster_x86_completion_memory_att(Arena* arena, BusterX86MetadataPhysicalOperand operand)
{
    BusterX86MetadataPhysicalMemory memory = operand.memory;
    String8 result = {0};
    String8 base = {0};
    String8 index = {0};
    if (memory.has_symbol && (memory.has_base || memory.has_index || memory.rip_relative)) return (String8){0};
    if (memory.has_symbol) result = buster_x86_completion_symbol(arena, memory.symbol, memory.addend);
    else if (memory.has_displacement) result = string_format(arena, S8("{s64}"), memory.displacement);
    else result = S8("0");
    base = memory.has_base ? buster_x86_completion_register(arena, memory.base) : (String8){0};
    index = memory.has_index ? buster_x86_completion_register(arena, memory.index) : (String8){0};
    if (memory.has_base && !base.length) return (String8){0};
    if (memory.has_index && (!index.length || !memory.scale)) return (String8){0};
    if (memory.rip_relative) return string_format(arena, S8("{S8}(%rip)"), result);
    if (memory.has_base && memory.has_index) return string_format(arena, S8("{S8}(%{S8},%{S8},{u8})"), result, base, index, memory.scale);
    if (memory.has_base) return string_format(arena, S8("{S8}(%{S8})"), result, base);
    if (memory.has_index) return string_format(arena, S8("{S8}(,%{S8},{u8})"), result, index, memory.scale);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 buster_x86_completion_mnemonic(BusterX86MetadataForm form)
{
    return buster_x86_metadata_string_span(form.iclass);
}

BUSTER_GLOBAL_LOCAL String8 buster_x86_completion_intel_source(Arena* arena, BusterX86MetadataForm form,
                                                               BusterX86MetadataPhysicalQuery query)
{
    String8 source = {0};
    bool wrote = false;
    bool bsr0_first = false;
    bool has_bsr0 = false;
    u32 physical_index = 0;
    u32 visible_index = 0;
    u32 metadata_index = 0;
    BusterX86MetadataOperand metadata = {0};
    bool mask_decorator = false;
    BusterX86MetadataPhysicalOperand operand = {0};
    String8 spelling = {0};
    source = buster_x86_completion_mnemonic(form);
    if (!source.length) return (String8){0};
    if (buster_x86_metadata_form_is_moffs(form.id))
    {
        String8 accumulator = {0};
        bool store = false;
        BusterX86MetadataPhysicalOperand source_memory = {0};
        if (query.operand_count != 1 || !query.operands ||
            query.operands[0].kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY || !form.fixed_byte_count)
            return (String8){0};
        source_memory = query.operands[0];
        accumulator = form.fixed_bytes[0] == 0xa0 || form.fixed_bytes[0] == 0xa2 ? S8("al") : S8("eax");
        if (form.fixed_bytes[0] == 0xa1 || form.fixed_bytes[0] == 0xa3)
        {
            source_memory.width = 32;
            source_memory.memory.source_width = 32;
        }
        spelling = buster_x86_completion_memory_intel(arena, source_memory, query.attributes);
        if (!spelling.length) return (String8){0};
        store = form.fixed_bytes[0] == 0xa2 || form.fixed_bytes[0] == 0xa3;
        return store ? string_format(arena, S8("{S8} {S8}, {S8}\n"), source, spelling, accumulator)
                     : string_format(arena, S8("{S8} {S8}, {S8}\n"), source, accumulator, spelling);
    }
    // These controls are encoded as hidden prefix fields. The public Intel
    // grammar has no bounded spelling for the canonical hidden segment or
    // legacy branch-hint forms, so keep them explicitly source-unrepresentable
    // rather than silently dropping architectural bytes.
    if (query.attributes.implicit_segment != BUSTER_X86_METADATA_SEGMENT_NONE || query.attributes.branch_hint)
        return (String8){0};
    if (query.attributes.lock) source = string_format(arena, S8("lock {S8}"), source);
    else if (query.attributes.rep) source = string_format(arena, S8("rep {S8}"), source);
    else if (query.attributes.repne) source = string_format(arena, S8("repne {S8}"), source);
    else if (query.attributes.notrack) source = string_format(arena, S8("notrack {S8}"), source);
    if (query.attributes.no_flags) source = string_format(arena, S8("{{nf}} {S8}"), source);
    has_bsr0 = buster_x86_completion_hidden_bsr0(form, &bsr0_first);
    if (has_bsr0 && bsr0_first && query.operand_count == 1)
    {
        source = string_format(arena, S8("{S8} bsr0"), source);
        wrote = true;
    }
    if (query.attributes.has_dfv)
    {
        source = string_format(arena, S8("{S8} 0"), source);
        wrote = true;
    }
    for (metadata_index = 0; metadata_index < form.operand_count; metadata_index += 1)
    {
        metadata = (BusterX86MetadataOperand){0};
        if (!buster_x86_metadata_operand(form.id, metadata_index, &metadata)) return (String8){0};
        if (!metadata.visible) continue;
        mask_decorator = buster_x86_completion_mask_decorator(form, visible_index);
        visible_index += 1;
        if (mask_decorator)
        {
            if (query.attributes.has_mask_register && physical_index < query.operand_count &&
                query.operands[physical_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                query.operands[physical_index].reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK &&
                query.operands[physical_index].reg.index == query.attributes.mask_register)
                physical_index += 1;
            else if (buster_x86_completion_apx_evex_memory_witness(form) && physical_index < query.operand_count &&
                     query.operands[physical_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                     query.operands[physical_index].reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK &&
                     query.operands[physical_index].reg.index == 0)
                // Form 5584 is the bounded EGPR-address witness. Its MASK1
                // metadata slot is optional and k0 has no public decorator.
                physical_index += 1;
            continue;
        }
        if (physical_index >= query.operand_count) return (String8){0};
        operand = query.operands[physical_index++];
        if (query.attributes.has_mask_register && operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK &&
            operand.reg.index == query.attributes.mask_register)
            continue;
        spelling = operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER
                       ? buster_x86_completion_register(arena, operand.reg)
                       : operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY
                             ? buster_x86_completion_memory_intel(arena, operand, query.attributes)
                             : buster_x86_completion_immediate(arena, operand);
        if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE ||
            operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE)
            spelling = operand.has_symbol ? buster_x86_completion_symbol(arena, operand.symbol, operand.addend)
                                          : buster_x86_completion_immediate(arena, operand);
        if (!spelling.length) return (String8){0};
        if (!wrote) source = string_format(arena, S8("{S8} {S8}"), source, spelling);
        else source = string_format(arena, S8("{S8}, {S8}"), source, spelling);
        if (!wrote && query.attributes.has_mask_register)
        {
            source = string_format(arena, S8("{S8} {{k{u8}}}"), source, query.attributes.mask_register);
            if (query.attributes.zeroing) source = string_format(arena, S8("{S8} {{z}}"), source);
        }
        wrote = true;
    }
    if (query.attributes.sae)
        source = string_format(arena, S8("{S8}, {{{S8}}}"), source,
                               query.attributes.rounding_mode == BUSTER_X86_METADATA_ROUNDING_NEAREST ? S8("rn-sae") : S8("sae"));
    if (has_bsr0 && !bsr0_first && query.operand_count == 1) source = string_format(arena, S8("{S8}, bsr0"), source);
    return string_format(arena, S8("{S8}\n"), source);
}

BUSTER_GLOBAL_LOCAL String8 buster_x86_completion_att_register(Arena* arena, BusterX86MetadataPhysicalRegister reg)
{
    String8 value = buster_x86_completion_register(arena, reg);
    return value.length ? string_format(arena, S8("%{S8}"), value) : (String8){0};
}

BUSTER_GLOBAL_LOCAL String8 buster_x86_completion_att_source(Arena* arena, BusterX86MetadataForm form,
                                                             BusterX86MetadataPhysicalQuery query)
{
    u32 index = 0;
    u32 reverse = 0;
    u32 metadata_index = 0;
    u32 physical_index = 0;
    u32 visible_index = 0;
    u32 source_operand_count = 0;
    BusterX86MetadataOperand metadata = {0};
    BusterX86MetadataPhysicalOperand source_operands[16] = {0};
    BusterX86MetadataPhysicalOperand operand = {0};
    String8 spelling = {0};
    String8 source = {0};
    // The AT&T bridge starts with the proven scalar/register and plain-memory
    // cohort.  Decorated EVEX, APX role controls, and hidden operands remain
    // explicitly unresolved until their dialect grammar is generalized.
    if (query.attributes.decorator_flags || query.attributes.apx_flags || query.attributes.amx_flags || query.attributes.has_dfv ||
        query.attributes.no_flags || query.attributes.branch_hint || query.attributes.notrack || query.attributes.rep ||
        query.attributes.repne || query.attributes.lock)
        return (String8){0};
    if (query.attributes.implicit_segment != BUSTER_X86_METADATA_SEGMENT_NONE) return (String8){0};
    if (buster_x86_completion_apx_evex_memory_witness(form))
    {
        for (metadata_index = 0; metadata_index < form.operand_count; metadata_index += 1)
        {
            metadata = (BusterX86MetadataOperand){0};
            if (!buster_x86_metadata_operand(form.id, metadata_index, &metadata)) return (String8){0};
            if (!metadata.visible) continue;
            if (buster_x86_completion_mask_decorator(form, visible_index))
            {
                visible_index += 1;
                if (physical_index < query.operand_count &&
                    query.operands[physical_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                    query.operands[physical_index].reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK &&
                    query.operands[physical_index].reg.index == 0)
                    physical_index += 1;
                continue;
            }
            visible_index += 1;
            if (physical_index >= query.operand_count || source_operand_count >= BUSTER_ARRAY_LENGTH(source_operands))
                return (String8){0};
            source_operands[source_operand_count++] = query.operands[physical_index++];
        }
        if (physical_index != query.operand_count) return (String8){0};
    }
    else
    {
        if (query.operand_count > BUSTER_ARRAY_LENGTH(source_operands)) return (String8){0};
        source_operand_count = query.operand_count;
        for (index = 0; index < query.operand_count; index += 1) source_operands[index] = query.operands[index];
    }
    for (index = 0; index < source_operand_count; index += 1)
        if (source_operands[index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
            (source_operands[index].memory.vsib || source_operands[index].memory.has_segment))
            return (String8){0};
    source = buster_x86_completion_mnemonic(form);
    for (reverse = source_operand_count; reverse; reverse -= 1)
    {
        index = reverse - 1;
        operand = source_operands[index];
        spelling = operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER
                       ? buster_x86_completion_att_register(arena, operand.reg)
                       : operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY
                             ? buster_x86_completion_memory_att(arena, operand)
                             : operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE
                                   ? string_format(arena, S8("${S8}"), buster_x86_completion_immediate(arena, operand))
                                   : operand.has_symbol ? string_format(arena, S8("${S8}"), buster_x86_completion_symbol(arena, operand.symbol, operand.addend))
                                                         : string_format(arena, S8("$0"));
        if (!spelling.length) return (String8){0};
        source = index + 1 == source_operand_count ? string_format(arena, S8("{S8} {S8}"), source, spelling)
                                                   : string_format(arena, S8("{S8}, {S8}"), source, spelling);
    }
    return string_format(arena, S8("{S8}\n"), source);
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_completion_relocation_kind(u8 kind)
{
    switch (kind)
    {
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE8: return ASSEMBLY_RELOCATION_X86_ABSOLUTE8;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE16: return ASSEMBLY_RELOCATION_X86_ABSOLUTE16;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32: return ASSEMBLY_RELOCATION_X86_ABSOLUTE32;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE64: return ASSEMBLY_RELOCATION_X86_ABSOLUTE64;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_SIGN_EXTENDED: return ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_ZERO_EXTENDED: return ASSEMBLY_RELOCATION_X86_ABSOLUTE32_ZERO_EXTENDED;
    case BUSTER_X86_METADATA_RELOCATION_PC8: return ASSEMBLY_RELOCATION_X86_PC8;
    case BUSTER_X86_METADATA_RELOCATION_PC16: return ASSEMBLY_RELOCATION_X86_PC16;
    case BUSTER_X86_METADATA_RELOCATION_PC32: return ASSEMBLY_RELOCATION_X86_PC32;
    case BUSTER_X86_METADATA_RELOCATION_PC64: return ASSEMBLY_RELOCATION_X86_PC64;
    default: return ASSEMBLY_RELOCATION_COUNT;
    }
}

BUSTER_GLOBAL_LOCAL bool buster_x86_completion_relocations_match(BusterX86MetadataRelocation const* direct, u32 direct_count,
                                                                  AssemblyEncodeResult source)
{
    u32 index = 0;
    BusterX86MetadataRelocation metadata = {0};
    AssemblyRelocation relocation = {0};
    u8 source_width = 0;
    if (direct_count != source.relocation_count) return false;
    for (index = 0; index < direct_count; index += 1)
    {
        metadata = direct[index];
        relocation = source.relocations[index];
        source_width = relocation.kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE64 || relocation.kind == ASSEMBLY_RELOCATION_X86_PC64 ? 8
                       : relocation.kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32 ||
                                 relocation.kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED ||
                                 relocation.kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32_ZERO_EXTENDED ||
                                 relocation.kind == ASSEMBLY_RELOCATION_X86_PC32
                             ? 4
                             : relocation.kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE16 || relocation.kind == ASSEMBLY_RELOCATION_X86_PC16 ? 2 : 1;
        if (metadata.offset != relocation.offset || metadata.width != source_width ||
            buster_x86_completion_relocation_kind(metadata.kind) != relocation.kind || metadata.addend != relocation.addend)
            return false;
        if (relocation.symbol >= source.symbol_count || !buster_x86_completion_string_equal(metadata.symbol,
                                                                                             source.symbols[relocation.symbol].name))
            return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_completion_alias_equivalent(u32 form_id, BusterX86MetadataPhysicalQuery query,
                                                                AssemblyEncodeResult source)
{
    BusterX86MetadataForm form = {0};
    BusterX86MetadataCandidateRange range = {0};
    u32 position = 0;
    u32 candidate_id = 0;
    BusterX86MetadataForm candidate = {0};
    bool schema_equal = true;
    u32 operand_index = 0;
    BusterX86MetadataOperand first = {0};
    BusterX86MetadataOperand second = {0};
    u8 bytes[32] = {0};
    BusterX86MetadataRelocation relocations[BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY] = {0};
    BusterX86MetadataEmitResult result = {0};
    if (!buster_x86_metadata_form(form_id, &form)) return false;
    range = buster_x86_metadata_lookup_iclass(buster_x86_metadata_string_span(form.iclass));
    for (position = 0; position < range.count; position += 1)
    {
        candidate_id = 0;
        candidate = (BusterX86MetadataForm){0};
        if (!buster_x86_metadata_candidate_at(range, position, &candidate_id) || candidate_id == form_id ||
            !buster_x86_metadata_form(candidate_id, &candidate) ||
            candidate.encoder_family == BUSTER_X86_METADATA_ENCODER_AMX)
            continue;
        if (candidate.operand_count != form.operand_count) continue;
        schema_equal = true;
        for (operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            first = (BusterX86MetadataOperand){0};
            second = (BusterX86MetadataOperand){0};
            if (!buster_x86_metadata_operand(form_id, operand_index, &first) ||
                !buster_x86_metadata_operand(candidate_id, operand_index, &second) ||
                first.slot != second.slot || first.visible != second.visible || first.kind != second.kind ||
                first.access != second.access || first.field_source != second.field_source ||
                first.physical_class != second.physical_class || first.physical_width_flags != second.physical_width_flags ||
                !buster_x86_completion_string_equal(buster_x86_metadata_string_span(first.atom),
                                                    buster_x86_metadata_string_span(second.atom)) ||
                !buster_x86_completion_string_equal(buster_x86_metadata_string_span(first.width),
                                                    buster_x86_metadata_string_span(second.width)))
            {
                schema_equal = false;
                break;
            }
        }
        if (!schema_equal) continue;
        memset(bytes, 0, sizeof(bytes));
        memset(relocations, 0, sizeof(relocations));
        result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = query, .form_id = candidate_id, .output = bytes, .output_capacity = sizeof(bytes),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        if (result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && result.byte_count == source.bytes.length &&
            (!result.byte_count || memcmp(bytes, source.bytes.pointer, result.byte_count) == 0) &&
            buster_x86_completion_relocations_match(relocations, result.relocation_count, source))
            return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL BusterX86CompletionCensusSourceResult buster_x86_completion_source_check(
    Arena* arena, Target target, BusterX86MetadataForm form, BusterX86MetadataPhysicalQuery query,
    u8 const* direct_bytes, u32 direct_byte_count, BusterX86MetadataRelocation const* direct_relocations,
    u32 direct_relocation_count, bool att)
{
    BusterX86CompletionCensusSourceResult result = {
        .classification = BUSTER_X86_COMPLETION_CENSUS_SOURCE_UNREPRESENTABLE,
        .diagnostic_kind = ASSEMBLY_DIAGNOSTIC_COUNT,
        .mismatch_index = UINT32_MAX};
    String8 source = {0};
    AssemblyEncodeResult encoded = {0};
    u32 shared = 0;
    u32 index = 0;
    if (!arena) return result;
    source = att ? buster_x86_completion_att_source(arena, form, query)
                 : buster_x86_completion_intel_source(arena, form, query);
    if (!source.length) return result;
    encoded = assembly_encode(arena, source,
                              (AssemblyEncodeOptions){.target = target,
                                                       .syntax = att ? ASSEMBLY_SYNTAX_ATT : ASSEMBLY_SYNTAX_INTEL});
    result.byte_count = (u32)encoded.bytes.length;
    result.relocation_count = encoded.relocation_count;
    result.diagnostic_kind = encoded.diagnostic_count ? (u16)encoded.diagnostics[0].kind : ASSEMBLY_DIAGNOSTIC_COUNT;
    if (encoded.diagnostic_count)
    {
        result.classification = encoded.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE
                                    ? BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED
                                    : BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED;
        return result;
    }
    result.bytes_match = encoded.bytes.length == direct_byte_count &&
                         (!direct_byte_count || memcmp(encoded.bytes.pointer, direct_bytes, direct_byte_count) == 0);
    result.relocations_match = buster_x86_completion_relocations_match(direct_relocations, direct_relocation_count, encoded);
    if (result.bytes_match && result.relocations_match)
    {
        result.classification = direct_relocation_count ? BUSTER_X86_COMPLETION_CENSUS_SOURCE_NORMALIZED_RELOCATION
                                                         : BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT;
        return result;
    }
    shared = BUSTER_MIN((u32)encoded.bytes.length, direct_byte_count);
    for (index = 0; index < shared; index += 1)
        if (encoded.bytes.pointer[index] != direct_bytes[index])
        {
            result.mismatch_index = index;
            result.mismatch_direct_byte = direct_bytes[index];
            result.mismatch_source_byte = encoded.bytes.pointer[index];
            break;
        }
    if (buster_x86_completion_alias_equivalent(form.id, query, encoded))
        result.classification = BUSTER_X86_COMPLETION_CENSUS_SOURCE_ALIAS_EQUIVALENT;
    else if (result.bytes_match && !result.relocations_match)
        result.classification = BUSTER_X86_COMPLETION_CENSUS_SOURCE_RELOCATION_MISMATCH;
    else if (!result.bytes_match && result.relocations_match)
        result.classification = BUSTER_X86_COMPLETION_CENSUS_SOURCE_BYTE_MISMATCH;
    else
        // A public spelling can resolve to a different legal metadata form
        // than the canonical row.  Without a decoder-level semantic proof,
        // report this as a different encoding rather than a product mismatch.
        result.classification = BUSTER_X86_COMPLETION_CENSUS_SOURCE_DIFFERENT_ENCODING;
    return result;
}

BUSTER_GLOBAL_LOCAL void buster_x86_completion_record_diagnostic(BusterX86CompletionCensusQuery query,
                                                                  BusterX86CompletionCensusResult* result,
                                                                  u32 form_id, u64 stable_hash, u8 dialect,
                                                                  BusterX86CompletionCensusSourceResult source)
{
    bool has_assembly_diagnostic = source.diagnostic_kind < ASSEMBLY_DIAGNOSTIC_COUNT;
    bool has_comparison_diagnostic = source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_DIFFERENT_ENCODING ||
                                     source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_BYTE_MISMATCH ||
                                     source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_RELOCATION_MISMATCH;
    if ((!has_assembly_diagnostic && !has_comparison_diagnostic) || !query.diagnostics) return;
    if (result->diagnostic_count < query.diagnostic_capacity)
    {
        query.diagnostics[result->diagnostic_count] = (BusterX86CompletionCensusDiagnostic){
            .form_id = form_id, .stable_hash = stable_hash, .dialect = dialect,
            .classification = source.classification, .assembly_diagnostic_kind = source.diagnostic_kind,
            .mismatch_index = source.mismatch_index, .direct_byte = source.mismatch_direct_byte,
            .source_byte = source.mismatch_source_byte};
        result->diagnostic_count += 1;
    }
    else result->diagnostic_dropped_count += 1;
}

BusterX86CompletionCensusResult buster_x86_completion_census_run(BusterX86CompletionCensusQuery query)
{
    BusterX86CompletionCensusResult result = {
        .required_form_count = buster_x86_metadata_form_count(),
        .intel_all_passed = true,
        .att_all_passed = true,
    };
    bool run_intel = false;
    bool run_att = false;
    u32 write_count = 0;
    u32 form_id = 0;
    BusterX86MetadataForm form = {0};
    bool normalized = false;
    bool policy = false;
    BusterX86CompletionCensusRecord record = {0};
    BusterX86MetadataPhysicalOperand canonical_operands[16] = {0};
    String8 features[1] = {0};
    char8 mnemonic_buffer[128] = {0};
    BusterX86MetadataPhysicalQuery canonical = {0};
    BusterX86MetadataPhysicalOperand direct_operands[16] = {0};
    BusterX86MetadataPhysicalOperand original_operands[16] = {0};
    u32 index = 0;
    BusterX86MetadataPhysicalOperand* operand = 0;
    BusterX86MetadataPhysicalQuery direct_query = {0};
    u8 direct_bytes[32] = {0};
    BusterX86MetadataRelocation direct_relocations[BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY] = {0};
    BusterX86MetadataEmitResult emitted = {0};
    BusterX86CompletionCensusSourceResult source = {0};
    // With neither dialect selected, preserve the ordinary behavior of
    // running both.  Structural-only is an explicit override: it performs
    // the structural/direct metadata pass while never constructing or
    // assembling an Intel or AT&T spelling.
    run_intel = !query.structural_only && (query.run_intel || (!query.run_intel && !query.run_att));
    run_att = !query.structural_only && (query.run_att || (!query.run_intel && !query.run_att));
    result.records_complete = !query.records || query.record_capacity >= result.required_form_count;
    write_count = query.records ? BUSTER_MIN(query.record_capacity, result.required_form_count) : 0;
    buster_x86_metadata_prewarm();
    for (form_id = 0; form_id < result.required_form_count; form_id += 1)
    {
        form = (BusterX86MetadataForm){0};
        if (!buster_x86_metadata_form(form_id, &form)) continue;
        result.scanned_form_count += 1;
        normalized = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED;
        policy = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED ||
                 form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64;
        if (normalized) result.normalized_form_count += 1;
        else result.non_normalized_form_count += 1;
        if (policy) result.policy_excluded_count += 1;
        record = (BusterX86CompletionCensusRecord){
            .form_id = form_id, .stable_hash = form.stable_hash, .coverage_class = form.coverage_class,
            .encoder_family = form.encoder_family, .test_class = form.test_class,
            .structural_class = normalized ? BUSTER_X86_COMPLETION_CENSUS_NOT_ATTEMPTED : BUSTER_X86_COMPLETION_CENSUS_STRUCTURAL_ONLY,
            .policy_excluded = policy, .intel_mismatch_index = UINT32_MAX, .att_mismatch_index = UINT32_MAX};
        if (normalized && !policy)
        {
            memset(canonical_operands, 0, sizeof(canonical_operands));
            memset(features, 0, sizeof(features));
            memset(mnemonic_buffer, 0, sizeof(mnemonic_buffer));
            canonical = (BusterX86MetadataPhysicalQuery){0};
            if (!buster_x86_metadata_canonical_query(form_id, &canonical, canonical_operands, features, mnemonic_buffer))
            {
                result.canonical_query_failed_count += 1;
                result.class_counts[BUSTER_X86_COMPLETION_CENSUS_CANONICAL_QUERY_UNREPRESENTABLE] += 1;
                if (run_intel) result.intel_all_passed = false;
                if (run_att) result.att_all_passed = false;
                record.structural_class = BUSTER_X86_COMPLETION_CENSUS_CANONICAL_QUERY_UNREPRESENTABLE;
            }
            else
            {
                // Give relocatable operands one stable source spelling.  The
                // canonical bytes stay deterministic while direct/source
                // relocation records can be compared rather than discarded.
                memset(direct_operands, 0, sizeof(direct_operands));
                memcpy(direct_operands, canonical_operands, sizeof(direct_operands));
                for (index = 0; index < canonical.operand_count; index += 1)
                {
                    operand = direct_operands + index;
                    if (operand->kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE ||
                        operand->kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE)
                    {
                        operand->has_value = false;
                        operand->has_unsigned_value = false;
                        operand->has_symbol = true;
                        operand->symbol = S8("target");
                        operand->addend = 0;
                    }
                }
                memcpy(original_operands, direct_operands, sizeof(original_operands));
                buster_x86_completion_normalize_query(form, canonical, direct_operands);
                direct_query = canonical;
                direct_query.operands = direct_operands;
                memset(direct_bytes, 0, sizeof(direct_bytes));
                memset(direct_relocations, 0, sizeof(direct_relocations));
                emitted = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                    .physical = direct_query, .form_id = form_id, .output = direct_bytes, .output_capacity = sizeof(direct_bytes),
                    .relocations = direct_relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(direct_relocations)});
                if (emitted.status != BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                    memcmp(direct_operands, original_operands, sizeof(direct_operands)) != 0)
                {
                    memcpy(direct_operands, original_operands, sizeof(direct_operands));
                    memset(direct_bytes, 0, sizeof(direct_bytes));
                    memset(direct_relocations, 0, sizeof(direct_relocations));
                    emitted = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                        .physical = direct_query, .form_id = form_id, .output = direct_bytes, .output_capacity = sizeof(direct_bytes),
                        .relocations = direct_relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(direct_relocations)});
                }
                record.canonical_query = true;
                record.canonical_operand_count = (u16)canonical.operand_count;
                record.metadata_status = (u16)emitted.status;
                record.metadata_byte_count = emitted.byte_count;
                record.metadata_relocation_count = emitted.relocation_count;
                if (emitted.status != BUSTER_X86_METADATA_ENCODE_SUCCESS)
                {
                    result.metadata_emit_failed_count += 1;
                    result.class_counts[BUSTER_X86_COMPLETION_CENSUS_DIRECT_EMIT_FAILURE] += 1;
                    if (run_intel) result.intel_all_passed = false;
                    if (run_att) result.att_all_passed = false;
                    record.structural_class = BUSTER_X86_COMPLETION_CENSUS_DIRECT_EMIT_FAILURE;
                }
                else
                {
                    record.metadata_emitted = true;
                    result.metadata_emitted_count += 1;
                    record.structural_class = BUSTER_X86_COMPLETION_CENSUS_DIRECT_EMITTED;
                    result.class_counts[BUSTER_X86_COMPLETION_CENSUS_DIRECT_EMITTED] += 1;
                    if (run_intel)
                    {
                        source = buster_x86_completion_source_check(
                            query.arena, query.target, form, direct_query, direct_bytes, emitted.byte_count, direct_relocations,
                            emitted.relocation_count, false);
                        record.intel_class = source.classification;
                        record.intel_capable = source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT ||
                                               source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_NORMALIZED_RELOCATION ||
                                               source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_ALIAS_EQUIVALENT;
                        record.intel_byte_count = source.byte_count;
                        record.intel_relocation_count = source.relocation_count;
                        record.intel_diagnostic_kind = source.diagnostic_kind;
                        record.intel_mismatch_index = source.mismatch_index;
                        result.intel_attempted_count += 1;
                        result.intel_class_counts[source.classification] += 1;
                        result.intel_exact_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT;
                        result.intel_normalized_relocation_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_NORMALIZED_RELOCATION;
                        result.intel_alias_equivalent_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_ALIAS_EQUIVALENT;
                        result.intel_policy_rejected_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED;
                        result.intel_different_encoding_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_DIFFERENT_ENCODING;
                        result.intel_byte_mismatch_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_BYTE_MISMATCH;
                        result.intel_relocation_mismatch_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_RELOCATION_MISMATCH;
                        result.intel_unresolved_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_UNREPRESENTABLE ||
                                                         source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED ||
                                                         source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED ||
                                                         source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_DIFFERENT_ENCODING;
                        result.intel_all_passed &= record.intel_capable;
                        buster_x86_completion_record_diagnostic(query, &result, form_id, form.stable_hash, 0, source);
                    }
                    if (run_att)
                    {
                        source = buster_x86_completion_source_check(
                            query.arena, query.target, form, direct_query, direct_bytes, emitted.byte_count, direct_relocations,
                            emitted.relocation_count, true);
                        record.att_class = source.classification;
                        record.att_capable = source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT ||
                                             source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_NORMALIZED_RELOCATION ||
                                             source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_ALIAS_EQUIVALENT;
                        record.att_byte_count = source.byte_count;
                        record.att_relocation_count = source.relocation_count;
                        record.att_diagnostic_kind = source.diagnostic_kind;
                        record.att_mismatch_index = source.mismatch_index;
                        result.att_attempted_count += 1;
                        result.att_class_counts[source.classification] += 1;
                        result.att_exact_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT;
                        result.att_normalized_relocation_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_NORMALIZED_RELOCATION;
                        result.att_alias_equivalent_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_ALIAS_EQUIVALENT;
                        result.att_policy_rejected_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED;
                        result.att_different_encoding_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_DIFFERENT_ENCODING;
                        result.att_byte_mismatch_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_BYTE_MISMATCH;
                        result.att_relocation_mismatch_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_RELOCATION_MISMATCH;
                        result.att_unresolved_count += source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_UNREPRESENTABLE ||
                                                       source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED ||
                                                       source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED ||
                                                       source.classification == BUSTER_X86_COMPLETION_CENSUS_SOURCE_DIFFERENT_ENCODING;
                        result.att_all_passed &= record.att_capable;
                        buster_x86_completion_record_diagnostic(query, &result, form_id, form.stable_hash, 1, source);
                    }
                }
            }
        }
        else if (policy)
        {
            record.structural_class = BUSTER_X86_COMPLETION_CENSUS_POLICY_EXCLUDED;
            result.class_counts[BUSTER_X86_COMPLETION_CENSUS_POLICY_EXCLUDED] += 1;
        }
        else result.class_counts[BUSTER_X86_COMPLETION_CENSUS_STRUCTURAL_ONLY] += 1;
        if (form_id < write_count)
        {
            query.records[form_id] = record;
            result.record_count += 1;
        }
    }
    // `structural_complete` means every requested row was scanned and materialized.  A
    // complete census can (and should) contain unresolved source cohorts; the
    // dialect pass booleans report that capability separately.
    result.metadata_blocked_count = result.canonical_query_failed_count + result.metadata_emit_failed_count;
    result.form_partition_complete = result.scanned_form_count == result.required_form_count &&
                                     result.required_form_count == BUSTER_X86_COMPLETION_CENSUS_EXPECTED_FORM_COUNT &&
                                     result.normalized_form_count == BUSTER_X86_COMPLETION_CENSUS_EXPECTED_NORMALIZED_COUNT &&
                                     result.non_normalized_form_count == BUSTER_X86_COMPLETION_CENSUS_EXPECTED_NON_NORMALIZED_COUNT &&
                                     result.normalized_form_count + result.non_normalized_form_count == result.required_form_count;
    result.normalized_partition_complete = result.metadata_emitted_count + result.metadata_blocked_count == result.normalized_form_count;
    result.metadata_partition_complete = result.metadata_emitted_count == BUSTER_X86_COMPLETION_CENSUS_EXPECTED_METADATA_EMITTED_COUNT &&
                                         result.metadata_blocked_count == BUSTER_X86_COMPLETION_CENSUS_EXPECTED_METADATA_BLOCKED_COUNT &&
                                         result.normalized_form_count == BUSTER_X86_COMPLETION_CENSUS_EXPECTED_NORMALIZED_COUNT;
    if (!query.structural_only)
    {
        result.source_partition_expected_count = result.metadata_emitted_count;
        result.intel_source_partition_count = result.intel_exact_count + result.intel_normalized_relocation_count +
                                              result.intel_alias_equivalent_count + result.intel_unresolved_count +
                                              result.intel_byte_mismatch_count + result.intel_relocation_mismatch_count;
        result.att_source_partition_count = result.att_exact_count + result.att_normalized_relocation_count +
                                            result.att_alias_equivalent_count + result.att_unresolved_count +
                                            result.att_byte_mismatch_count + result.att_relocation_mismatch_count;
    }
    result.structural_complete = result.records_complete && result.form_partition_complete && result.normalized_partition_complete &&
                                 result.metadata_partition_complete;
    result.diagnostics_complete = !query.diagnostics || result.diagnostic_dropped_count == 0;
    return result;
}

#if BUSTER_INCLUDE_TESTS
bool buster_x86_completion_census_test_query(u32 form_id, BusterX86MetadataPhysicalQuery* query,
                                              BusterX86MetadataPhysicalOperand operands[16], String8 features[1],
                                              char8 mnemonic_buffer[128])
{
    BusterX86MetadataForm form = {0};
    if (!query || !operands || !features || !mnemonic_buffer || !buster_x86_metadata_form(form_id, &form) ||
        !buster_x86_metadata_canonical_query(form_id, query, operands, features, mnemonic_buffer))
        return false;
    buster_x86_completion_normalize_query(form, *query, operands);
    query->operands = operands;
    return true;
}

BusterX86CompletionCensusClass buster_x86_completion_census_test_source_class(Arena* arena, Target target, u32 form_id,
                                                                               bool att)
{
    BusterX86MetadataForm form = {0};
    BusterX86MetadataPhysicalQuery query = {0};
    BusterX86MetadataPhysicalOperand operands[16] = {0};
    String8 features[1] = {0};
    char8 mnemonic_buffer[128] = {0};
    u8 bytes[32] = {0};
    BusterX86MetadataRelocation relocations[BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY] = {0};
    BusterX86MetadataEmitResult emitted = {0};
    BusterX86CompletionCensusSourceResult source = {0};
    if (!arena || !buster_x86_metadata_form(form_id, &form) ||
        !buster_x86_completion_census_test_query(form_id, &query, operands, features, mnemonic_buffer))
        return BUSTER_X86_COMPLETION_CENSUS_SOURCE_UNREPRESENTABLE;
    emitted = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
        .physical = query, .form_id = form_id, .output = bytes, .output_capacity = sizeof(bytes),
        .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
    if (emitted.status != BUSTER_X86_METADATA_ENCODE_SUCCESS)
        return BUSTER_X86_COMPLETION_CENSUS_DIRECT_EMIT_FAILURE;
    source = buster_x86_completion_source_check(arena, target, form, query, bytes, emitted.byte_count, relocations,
                                                 emitted.relocation_count, att);
    return (BusterX86CompletionCensusClass)source.classification;
}
#endif
