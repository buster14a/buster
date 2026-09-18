from pathlib import Path
import re


def replace_once(text: str, pattern: str, replacement: str, label: str, flags: int = 0) -> str:
    result, count = re.subn(pattern, lambda _match: replacement, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    return result


metadata_path = Path("src/buster/lib/compiler/assembly/x86_64_metadata.c")
metadata = metadata_path.read_text()

single_scalar_pattern = r'''BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_single_scalar_width\(u16 flags\)\n\{.*?\n    return 0;\n\}'''
single_scalar_match = re.search(single_scalar_pattern, metadata, flags=re.S)
if not single_scalar_match:
    raise SystemExit("single scalar width helper not found")
single_physical_helper = r'''

BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_single_physical_width(u16 flags)
{
    u16 fixed_flags = flags & (BUSTER_X86_METADATA_PHYSICAL_WIDTH_8 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_16 |
                               BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_64 |
                               BUSTER_X86_METADATA_PHYSICAL_WIDTH_80 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_128 |
                               BUSTER_X86_METADATA_PHYSICAL_WIDTH_256 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_512 |
                               BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024);
    if (!fixed_flags || (fixed_flags & (u16)(fixed_flags - 1))) return 0;
    if (fixed_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_8) return 8;
    if (fixed_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_16) return 16;
    if (fixed_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_32) return 32;
    if (fixed_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_64) return 64;
    if (fixed_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_80) return 80;
    if (fixed_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_128) return 128;
    if (fixed_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_256) return 256;
    if (fixed_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_512) return 512;
    if (fixed_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024) return 1024;
    return 0;
}'''
metadata = metadata[: single_scalar_match.end()] + single_physical_helper + metadata[single_scalar_match.end() :]

source_query_replacement = r'''// A source-assembled conversion must bind the memory width published by each
// compatible metadata row. EVEX FULL/HALF rows publish a scalar element plus
// a tuple; legacy/VEX/XOP CONVERT rows publish one fixed q/dq/qq memory width.
// Project each candidate independently, retaining an explicit qualifier as a
// constraint. Distinct successful unsized widths are rejected by the caller
// instead of being selected by table order or shortest encoding.
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_prepare_source_tuple_query(
    BusterX86MetadataForm form, BusterX86MetadataPatternSemantics const* pattern,
    BusterX86MetadataPhysicalQuery query, BusterX86MetadataPhysicalOperand* candidate_operands,
    bool* source_width_valid, u16* source_tuple_width, u8* source_memory_operand)
{
    bool evex_tuple = form.encoder_family == BUSTER_X86_METADATA_ENCODER_EVEX && !form.apx_flags && !form.amx_flags &&
                      pattern->has_tuple_control &&
                      (pattern->tuple_control_kind == BUSTER_X86_METADATA_TUPLE_FULL ||
                       pattern->tuple_control_kind == BUSTER_X86_METADATA_TUPLE_HALF);
    bool fixed_conversion = form.encoder_family != BUSTER_X86_METADATA_ENCODER_EVEX &&
                            buster_x86_metadata_string_input_equal(form.category.offset, S8("CONVERT"));
    if ((!evex_tuple && !fixed_conversion) || !query.operands || !query.operand_count || query.operand_count > 16)
        return false;

    u16 encoded_width = 0;
    u16 tuple_width = evex_tuple ? buster_x86_metadata_emit_tuple_memory_width(*pattern) : 0;
    u32 metadata_memory_count = 0;
    u32 physical_memory_index = UINT32_MAX;
    u32 physical_index = 0;
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form.id, index, &metadata)) return false;
        if (!metadata.visible) continue;
        bool writemask_default = buster_x86_metadata_emit_is_writemask_operand(metadata) &&
                                 (physical_index >= query.operand_count ||
                                  buster_x86_metadata_emit_operand_class(query.operands[physical_index]) !=
                                      BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK);
        if (writemask_default) continue;
        if (metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY)
        {
            metadata_memory_count += 1;
            physical_memory_index = physical_index;
            encoded_width = evex_tuple
                                ? buster_x86_metadata_single_scalar_width(metadata.physical_width_flags)
                                : buster_x86_metadata_single_physical_width(metadata.physical_width_flags);
        }
        physical_index += 1;
    }
    if (!evex_tuple) tuple_width = encoded_width;
    if (metadata_memory_count != 1 || !encoded_width || !tuple_width || physical_memory_index >= query.operand_count ||
        query.operands[physical_memory_index].kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
        return false;

    bool broadcast = (query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST) != 0;
    u16 required_source_width = evex_tuple && broadcast ? encoded_width : tuple_width;
    u16 source_width = query.operands[physical_memory_index].memory.source_width;
    *source_width_valid = !source_width || source_width == required_source_width;
    memcpy(candidate_operands, query.operands, query.operand_count * sizeof(*candidate_operands));
    candidate_operands[physical_memory_index].width = encoded_width;
    *source_tuple_width = tuple_width;
    *source_memory_operand = (u8)physical_memory_index;
    return true;
}
'''
metadata = replace_once(
    metadata,
    r'''// Source tuple size and encoded element type are distinct for conversions\.\n.*?\n\}\n\n(?=// AT&T memory operands intentionally omit)''',
    source_query_replacement + "\n",
    "source tuple helper",
    flags=re.S,
)

metadata = replace_once(
    metadata,
    r'''            bool source_tuple_query_possible = query\.source_semantics && query\.operand_count == 2 &&\n.*?query\.operands\[0\]\.reg\.index >= 16\);\n''',
    '''            bool source_tuple_query_possible = query.source_semantics && query.operands && query.operand_count &&
                                               query.operand_count <= 16 && query.address_size == 64;
            if (source_tuple_query_possible)
            {
                bool has_source_memory = false;
                for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
                {
                    has_source_memory |= query.operands[operand_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY;
                }
                source_tuple_query_possible = has_source_memory;
            }
''',
    "source tuple query gate",
    flags=re.S,
)

metadata = replace_once(
    metadata,
    r'''                u16 candidate_source_tuple_width = 0;\n''',
    '''                u16 candidate_source_tuple_width = 0;
                u8 candidate_source_memory_operand = UINT8_MAX;
''',
    "candidate source memory state",
)

metadata = replace_once(
    metadata,
    r'''buster_x86_metadata_prepare_source_tuple_query\(form, filter_view, query, candidate_operands,\n\s*&source_width_valid, &candidate_source_tuple_width\);''',
    '''buster_x86_metadata_prepare_source_tuple_query(form, filter_view, query, candidate_operands,
                                                                       &source_width_valid, &candidate_source_tuple_width,
                                                                       &candidate_source_memory_operand);''',
    "source tuple call",
)

metadata = replace_once(
    metadata,
    r'''                    diagnostic_operand = 1;\n                    diagnostic_value = query\.operands\[1\]\.memory\.source_width;''',
    '''                    diagnostic_operand = candidate_source_memory_operand < query.operand_count
                                             ? candidate_source_memory_operand
                                             : 0;
                    diagnostic_value = candidate_source_memory_operand < query.operand_count
                                           ? query.operands[candidate_source_memory_operand].memory.source_width
                                           : 0;''',
    "source width diagnostic",
)

metadata = replace_once(
    metadata,
    r'''                if \(candidate_source_tuple_width && !query\.operands\[1\]\.memory\.source_width &&\n                    !\(query\.attributes\.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST\)\)\n                \{\n                    // An unsized narrowing load can bind the same XMM destination\n                    // with different source tuples\. Encoding length and form order\n                    // cannot choose its semantics; require a source qualifier\.\n                    ambiguous_source_tuple \|= inferred_source_tuple_width &&\n                                              inferred_source_tuple_width != candidate_source_tuple_width;\n                    inferred_source_tuple_width = candidate_source_tuple_width;\n                \}''',
    '''                if (candidate_source_tuple_width && candidate_source_memory_operand < query.operand_count &&
                    !query.operands[candidate_source_memory_operand].memory.source_width &&
                    !(query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST))
                {
                    // An unsized conversion may bind the same visible operands
                    // with different source tuples. Encoding length and form order
                    // cannot choose its semantics; require a source qualifier.
                    ambiguous_source_tuple |= inferred_source_tuple_width &&
                                              inferred_source_tuple_width != candidate_source_tuple_width;
                    inferred_source_tuple_width = candidate_source_tuple_width;
                }''',
    "source tuple ambiguity",
)

metadata_path.write_text(metadata)

assembly_path = Path("src/buster/lib/compiler/assembly/assembly.c")
assembly = assembly_path.read_text()

conversion_guard = r'''                bool conversion_source_width = false;
                BusterX86MetadataCandidateRange conversion_candidates = buster_x86_metadata_lookup_mnemonic(mnemonic);
                for (u32 candidate_index = 0; candidate_index < conversion_candidates.count; candidate_index += 1)
                {
                    u32 form_id = 0;
                    BusterX86MetadataForm form = {0};
                    if (buster_x86_metadata_candidate_at(conversion_candidates, candidate_index, &form_id) &&
                        buster_x86_metadata_form(form_id, &form) &&
                        assembly_word_equal(buster_x86_metadata_string_span(form.category), S8("CONVERT")))
                    {
                        conversion_source_width = true;
                        break;
                    }
                }
                if (conversion_source_width)
                {
                    // The selector projects and validates this qualifier from
                    // each compatible conversion row. Do not compare it with
                    // the destination vector width in the syntax adapter.
                    continue;
                }
'''
assembly = replace_once(
    assembly,
    r'''            if \(explicit_width && physical\[index\]\.memory\.source_width > 64\)\n            \{\n''',
    '''            if (explicit_width && physical[index].memory.source_width > 64)
            {
''' + conversion_guard,
    "conversion aggregate qualifier guard",
)
assembly_path.write_text(assembly)

test_path = Path("src/buster/tests/compiler/assembly/x86_64_metadata_test.c")
tests = test_path.read_text()

conversion_tests = r'''
    // Legacy and VEX conversions publish the source memory tuple directly as
    // q/dq/qq metadata. Exercise both dialects and repeat each exact source so
    // form selection cannot depend on mutable candidate state.
    typedef struct X86ConversionSourceCase X86ConversionSourceCase;
    struct X86ConversionSourceCase
    {
        String8 source;
        AssemblySyntax syntax;
        u8 bytes[6];
        u8 byte_count;
    };
    X86ConversionSourceCase const conversion_sources[] = {
        {S8("cvtps2pd 64(%rax), %xmm0\n"), ASSEMBLY_SYNTAX_ATT, {0x0f, 0x5a, 0x40, 0x40}, 4},
        {S8("cvtps2pd xmm0, qword ptr [rax+64]\n"), ASSEMBLY_SYNTAX_INTEL, {0x0f, 0x5a, 0x40, 0x40}, 4},
        {S8("vcvtps2pd 64(%rax), %xmm0\n"), ASSEMBLY_SYNTAX_ATT, {0xc5, 0xf8, 0x5a, 0x40, 0x40}, 5},
        {S8("vcvtps2pd xmm0, qword ptr [rax+64]\n"), ASSEMBLY_SYNTAX_INTEL, {0xc5, 0xf8, 0x5a, 0x40, 0x40}, 5},
        {S8("vcvtps2pd 64(%rax), %ymm0\n"), ASSEMBLY_SYNTAX_ATT, {0xc5, 0xfc, 0x5a, 0x40, 0x40}, 5},
        {S8("vcvtps2pd ymm0, xmmword ptr [rax+64]\n"), ASSEMBLY_SYNTAX_INTEL, {0xc5, 0xfc, 0x5a, 0x40, 0x40}, 5},
        {S8("cvtpd2ps 64(%rax), %xmm0\n"), ASSEMBLY_SYNTAX_ATT, {0x66, 0x0f, 0x5a, 0x40, 0x40}, 5},
        {S8("cvtpd2ps xmm0, xmmword ptr [rax+64]\n"), ASSEMBLY_SYNTAX_INTEL, {0x66, 0x0f, 0x5a, 0x40, 0x40}, 5},
        {S8("vcvtpd2ps xmm0, xmmword ptr [rax+64]\n"), ASSEMBLY_SYNTAX_INTEL, {0xc5, 0xf9, 0x5a, 0x40, 0x40}, 5},
        {S8("vcvtpd2ps xmm0, ymmword ptr [rax+64]\n"), ASSEMBLY_SYNTAX_INTEL, {0xc5, 0xfd, 0x5a, 0x40, 0x40}, 5},
        {S8("cvtss2sd 64(%rax), %xmm0\n"), ASSEMBLY_SYNTAX_ATT, {0xf3, 0x0f, 0x5a, 0x40, 0x40}, 5},
        {S8("cvtss2sd xmm0, dword ptr [rax+64]\n"), ASSEMBLY_SYNTAX_INTEL, {0xf3, 0x0f, 0x5a, 0x40, 0x40}, 5},
        {S8("cvtsd2ss 64(%rax), %xmm0\n"), ASSEMBLY_SYNTAX_ATT, {0xf2, 0x0f, 0x5a, 0x40, 0x40}, 5},
        {S8("cvtsd2ss xmm0, qword ptr [rax+64]\n"), ASSEMBLY_SYNTAX_INTEL, {0xf2, 0x0f, 0x5a, 0x40, 0x40}, 5},
        {S8("vcvtss2sd 64(%rax), %xmm1, %xmm0\n"), ASSEMBLY_SYNTAX_ATT, {0xc5, 0xf2, 0x5a, 0x40, 0x40}, 5},
        {S8("vcvtss2sd xmm0, xmm1, dword ptr [rax+64]\n"), ASSEMBLY_SYNTAX_INTEL, {0xc5, 0xf2, 0x5a, 0x40, 0x40}, 5},
        {S8("vcvtsd2ss 64(%rax), %xmm1, %xmm0\n"), ASSEMBLY_SYNTAX_ATT, {0xc5, 0xf3, 0x5a, 0x40, 0x40}, 5},
        {S8("vcvtsd2ss xmm0, xmm1, qword ptr [rax+64]\n"), ASSEMBLY_SYNTAX_INTEL, {0xc5, 0xf3, 0x5a, 0x40, 0x40}, 5},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(conversion_sources); index += 1)
    {
        X86ConversionSourceCase test_case = conversion_sources[index];
        for (u32 repeat = 0; repeat < 2; repeat += 1)
        {
            AssemblyEncodeResult encoded = assembly_encode(arguments->arena, test_case.source,
                (AssemblyEncodeOptions){.target = target, .syntax = test_case.syntax});
            bool case_valid = encoded.diagnostic_count == 0 && encoded.relocation_count == 0 &&
                x86_64_metadata_test_bytes_equal(encoded.bytes.pointer, (u32)encoded.bytes.length,
                                                 test_case.bytes, test_case.byte_count);
            if (!case_valid)
            {
                arguments->show(arguments, S8("CONVERSION_SOURCE_WIDTH source: {S8}"), test_case.source);
            }
            valid &= case_valid;
        }
    }
    String8 const invalid_conversion_widths[] = {
        S8("vcvtps2pd xmm0, xmmword ptr [rax+64]\n"),
        S8("vcvtps2pd ymm0, qword ptr [rax+64]\n"),
        S8("vcvtpd2ps xmm0, qword ptr [rax+64]\n"),
        S8("cvtss2sd xmm0, qword ptr [rax+64]\n"),
        S8("cvtsd2ss xmm0, dword ptr [rax+64]\n"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid_conversion_widths); index += 1)
    {
        AssemblyEncodeResult rejected = assembly_encode(arguments->arena, invalid_conversion_widths[index],
            (AssemblyEncodeOptions){.target = target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        bool case_valid = rejected.diagnostic_count != 0 && rejected.bytes.length == 0 && rejected.relocation_count == 0;
        if (!case_valid)
        {
            arguments->show(arguments, S8("CONVERSION_INVALID_WIDTH accepted: {S8}"), invalid_conversion_widths[index]);
        }
        valid &= case_valid;
    }
    String8 const ambiguous_conversion_widths[] = {
        S8("vcvtpd2ps xmm0, [rax+64]\n"),
        S8("vcvtpd2ps 64(%rax), %xmm0\n"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(ambiguous_conversion_widths); index += 1)
    {
        AssemblySyntax syntax = index ? ASSEMBLY_SYNTAX_ATT : ASSEMBLY_SYNTAX_INTEL;
        AssemblyEncodeResult rejected = assembly_encode(arguments->arena, ambiguous_conversion_widths[index],
            (AssemblyEncodeOptions){.target = target, .syntax = syntax});
        valid &= rejected.diagnostic_count != 0 && rejected.bytes.length == 0 && rejected.relocation_count == 0;
    }
    Target no_avx_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .os = OPERATING_SYSTEM_LINUX,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2}, 1),
    };
    AssemblyEncodeResult feature_disabled = assembly_encode(arguments->arena,
        S8("vcvtps2pd 64(%rax), %ymm0\n"),
        (AssemblyEncodeOptions){.target = no_avx_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    valid &= feature_disabled.diagnostic_count != 0 && feature_disabled.bytes.length == 0 &&
             feature_disabled.relocation_count == 0;
'''

tests = replace_once(
    tests,
    r'''\n    return valid;\n\}\n\nBUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_mask_is_decorator''',
    "\n" + conversion_tests + '''\n    return valid;\n}\n\nBUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_mask_is_decorator''',
    "conversion regression insertion",
)
test_path.write_text(tests)

docs_path = Path("docs/agents/driver.md")
docs = docs_path.read_text()
docs = replace_once(
    docs,
    r'''For two-operand EVEX vector loads/conversions, an ordinary memory qualifier\n.*?it does not replace all legacy/VEX source inference\.\n''',
    '''For source-assembled conversion forms, an ordinary memory qualifier names
its source width, not the destination mnemonic suffix or register width.
Legacy, VEX and XOP candidates publish that fixed width directly; EVEX
FULL/HALF candidates publish a scalar element plus a source tuple. A broadcast
qualifier names the scalar element. The selector projects each compatible
candidate independently and keeps an explicit qualifier as a constraint. For
example, masked `vcvtps2pd zmm0, m256` and VEX `vcvtps2pd ymm0, m128` both
read 32-bit elements, while masked `vcvtpd2ps ymm0, m512` reads 64-bit
elements. AT&T's unqualified memory spelling uses the same candidate contract.
When the same visible operands admit different unsized source widths, selection
rejects the source as ambiguous; encoding length and candidate order never
choose the number of input lanes.
''',
    "driver source-width contract",
    flags=re.S,
)
docs_path.write_text(docs)
