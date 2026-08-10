#include <buster/tests/compiler/assembly/x86_64_metadata_test.h>
#if BUSTER_INCLUDE_TESTS

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_string_equal(BusterX86MetadataString first, String8 second)
{
    if (first.length != second.length || (!second.pointer && second.length)) return false;
    for (u32 index = 0; index < first.length; index += 1)
    {
        if (buster_x86_metadata_string_byte(first, index) != (u8)second.pointer[index]) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_string_contains(BusterX86MetadataString value, String8 needle)
{
    if (!needle.length || value.length < needle.length) return false;
    String8 span = buster_x86_metadata_string_span(value);
    if (span.length < needle.length) return false;
    for (u32 offset = 0; offset + needle.length <= span.length; offset += 1)
    {
        if ((u8)span.pointer[offset] != (u8)needle.pointer[0])
        {
            continue;
        }
        bool equal = true;
        for (u32 index = 1; index < needle.length && equal; index += 1)
            equal = (u8)span.pointer[offset + index] == (u8)needle.pointer[index];
        if (equal) return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_pattern_has_token(BusterX86MetadataString value, String8 token)
{
    if (!token.length || value.length < token.length) return false;
    String8 span = buster_x86_metadata_string_span(value);
    if (span.length < token.length) return false;
    for (u32 offset = 0; offset + token.length <= span.length; offset += 1)
    {
        if (offset && (u8)span.pointer[offset - 1] != ' ') continue;
        if (offset + token.length < span.length && (u8)span.pointer[offset + token.length] != ' ') continue;
        bool equal = true;
        for (u32 index = 0; index < token.length; index += 1)
            equal &= (u8)span.pointer[offset + index] == (u8)token.pointer[index];
        if (equal) return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_iform_id(String8 iform, u32* form_id)
{
    BusterX86MetadataCandidateRange range = buster_x86_metadata_lookup_iform(iform);
    return range.count == 1 && buster_x86_metadata_candidate_at(range, 0, form_id);
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_find_token_form(String8 iclass, String8 token, u8 visible_operand_kind,
                                                               u32* form_id, BusterX86MetadataForm* form_result)
{
    BusterX86MetadataCandidateRange range = buster_x86_metadata_lookup_iclass(iclass);
    for (u32 position = 0; position < range.count; position += 1)
    {
        u32 candidate_id = 0;
        BusterX86MetadataForm candidate = {0};
        if (!buster_x86_metadata_candidate_at(range, position, &candidate_id) ||
            !buster_x86_metadata_form(candidate_id, &candidate) ||
            candidate.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED ||
            !x86_64_metadata_test_pattern_has_token(candidate.pattern, token))
            continue;
        bool shape_matches = false;
        for (u32 operand_index = 0; operand_index < candidate.operand_count; operand_index += 1)
        {
            BusterX86MetadataOperand operand = {0};
            if (!buster_x86_metadata_operand(candidate_id, operand_index, &operand))
            {
                shape_matches = false;
                break;
            }
            if (operand.visible)
            {
                if (operand.kind != visible_operand_kind)
                {
                    shape_matches = false;
                    break;
                }
                shape_matches = true;
            }
        }
        if (shape_matches)
        {
            if (form_id) *form_id = candidate_id;
            if (form_result) *form_result = candidate;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_string8(BusterX86MetadataString value, char8* buffer, u32 capacity, String8* result)
{
    if (!buffer || !result || value.length >= capacity) return false;
    for (u32 index = 0; index < value.length; index += 1) buffer[index] = (char8)buster_x86_metadata_string_byte(value, index);
    *result = (String8){.pointer = buffer, .length = value.length};
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_candidate(BusterX86MetadataCandidateRange range, u32 position, u32* form_id)
{
    return buster_x86_metadata_candidate_at(range, position, form_id);
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_coverage_candidate(BusterX86MetadataCoverageRange range, u32 position,
                                                                  u32* coverage_id)
{
    return buster_x86_metadata_coverage_candidate_at(range, position, coverage_id);
}

BUSTER_GLOBAL_LOCAL u32 x86_64_metadata_test_visible_operand_count(u32 form_id, u16 operand_count)
{
    u32 visible_count = 0;
    for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand operand = {0};
        if (buster_x86_metadata_operand(form_id, operand_index, &operand)) visible_count += operand.visible != 0;
    }
    return visible_count;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_range_is_sorted(BusterX86MetadataCandidateRange range)
{
    u32 previous = 0;
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 current = 0;
        if (!x86_64_metadata_test_candidate(range, index, &current) || (index && current <= previous)) return false;
        previous = current;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_range_contains_iclass(BusterX86MetadataCandidateRange range, String8 iclass)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 form_id = 0;
        BusterX86MetadataForm form = {0};
        if (!x86_64_metadata_test_candidate(range, index, &form_id) || !buster_x86_metadata_form(form_id, &form)) return false;
        if (x86_64_metadata_test_string_equal(form.iclass, iclass)) return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL u32 x86_64_metadata_test_filtered_count(BusterX86MetadataCandidateRange range, BusterX86MetadataFilter filter,
                                                             u32 expected_form_id, bool* found)
{
    BusterX86MetadataCandidateIterator iterator = buster_x86_metadata_filter(range, filter);
    u32 count = 0;
    *found = false;
    u32 form_id = 0;
    while (buster_x86_metadata_candidate_next(&iterator, &form_id))
    {
        count += 1;
        *found |= form_id == expected_form_id;
    }
    return count;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataOperandSignature x86_64_metadata_test_physical_signature(u8 kind, u8 physical_class,
                                                                                                u16 physical_width_flags)
{
    return (BusterX86MetadataOperandSignature){
        .kind = kind,
        .physical_class = physical_class,
        .physical_width_flags = physical_width_flags,
        .has_physical_class = true,
        .has_physical_width = physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_ANY,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataResolveResult x86_64_metadata_test_resolve_physical(
    String8 mnemonic, BusterX86MetadataOperandSignature const* operands, u32 operand_count, String8 const* features,
    u32 feature_count, u16 required_field_flags, u16 forbidden_field_flags, u16 decorator_flags, u16 apx_flags, u16 amx_flags,
    bool include_privileged, bool include_not64, u32* form_ids, u32 form_id_capacity)
{
    return buster_x86_metadata_resolve(
        (BusterX86MetadataResolveQuery){
            .mnemonic = mnemonic,
            .operands = operands,
            .operand_count = operand_count,
            .features = {.names = features, .count = feature_count},
            .decorator_flags = decorator_flags,
            .apx_flags = apx_flags,
            .amx_flags = amx_flags,
            .required_field_flags = required_field_flags,
            .forbidden_field_flags = forbidden_field_flags,
            .include_privileged = include_privileged,
            .include_not64 = include_not64,
        },
        form_ids, form_id_capacity);
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_build_signatures(u32 form_id, bool include_implicit,
                                                                BusterX86MetadataOperandSignature* signatures,
                                                                char8 atom_buffers[16][128], char8 width_buffers[16][128],
                                                                String8* feature, char8* feature_buffer, u32 feature_capacity,
                                                                u32* signature_count)
{
    BusterX86MetadataForm form = {0};
    if (!signatures || !feature || !feature_buffer || !signature_count || !buster_x86_metadata_form(form_id, &form)) return false;
    u32 count = 0;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand operand = {0};
        if (!buster_x86_metadata_operand(form_id, operand_index, &operand)) return false;
        if (!include_implicit && !operand.visible) continue;
        if (count >= 16 || !x86_64_metadata_test_string8(operand.atom, atom_buffers[count], 128, &signatures[count].atom) ||
            !x86_64_metadata_test_string8(operand.width, width_buffers[count], 128, &signatures[count].width))
        {
            return false;
        }
        signatures[count].kind = operand.kind;
        signatures[count].field_source = operand.field_source;
        signatures[count].access = operand.access;
        signatures[count].has_atom = operand.atom.length != 0;
        signatures[count].has_width = operand.width.length != 0;
        signatures[count].has_field_source = true;
        signatures[count].has_access = true;
        signatures[count].has_slot = operand.slot != UINT8_MAX;
        signatures[count].slot = operand.slot;
        signatures[count].has_visible = include_implicit;
        signatures[count].visible = operand.visible;
        count += 1;
    }
    BusterX86MetadataString feature_string = form.isa_set.length ? form.isa_set : form.extension;
    if (!x86_64_metadata_test_string8(feature_string, feature_buffer, feature_capacity, feature)) return false;
    *signature_count = count;
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_first_normalized_form(BusterX86MetadataCandidateRange range, u32* form_id,
                                                                      BusterX86MetadataForm* form)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 candidate = 0;
        BusterX86MetadataForm candidate_form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) return false;
        if (candidate_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED)
        {
            if (form) *form = candidate_form;
            if (form_id) *form_id = candidate;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_first_form_with_extension(BusterX86MetadataCandidateRange range, String8 extension,
                                                                         u32* form_id, BusterX86MetadataForm* form)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 candidate = 0;
        BusterX86MetadataForm candidate_form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) return false;
        if (candidate_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
            x86_64_metadata_test_string_equal(candidate_form.extension, extension))
        {
            if (form) *form = candidate_form;
            if (form_id) *form_id = candidate;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_first_form_with_decorators(BusterX86MetadataCandidateRange range, u16 decorators,
                                                                          u32* form_id, BusterX86MetadataForm* form)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 candidate = 0;
        BusterX86MetadataForm candidate_form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) return false;
        if (candidate_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
            (candidate_form.decorator_flags & decorators) == decorators)
        {
            if (form) *form = candidate_form;
            if (form_id) *form_id = candidate;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_first_form_with_operand_kind(BusterX86MetadataCandidateRange range, u8 kind,
                                                                            bool visible, u32* form_id,
                                                                            BusterX86MetadataForm* form)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 candidate = 0;
        BusterX86MetadataForm candidate_form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) return false;
        if (candidate_form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED) continue;
        for (u32 operand_index = 0; operand_index < candidate_form.operand_count; operand_index += 1)
        {
            BusterX86MetadataOperand operand = {0};
            if (!buster_x86_metadata_operand(candidate, operand_index, &operand)) return false;
            if (operand.kind == kind && (!visible || operand.visible))
            {
                if (form) *form = candidate_form;
                if (form_id) *form_id = candidate;
                return true;
            }
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_first_form_with_coverage(BusterX86MetadataCandidateRange range, u8 coverage_class,
                                                                        u32* form_id, BusterX86MetadataForm* form)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 candidate = 0;
        BusterX86MetadataForm candidate_form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) return false;
        if (candidate_form.coverage_class == coverage_class)
        {
            if (form) *form = candidate_form;
            if (form_id) *form_id = candidate;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_first_form_with_apx(BusterX86MetadataCandidateRange range, u32* form_id,
                                                                    BusterX86MetadataForm* form)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 candidate = 0;
        BusterX86MetadataForm candidate_form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) return false;
        if (candidate_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED && candidate_form.apx_flags)
        {
            if (form) *form = candidate_form;
            if (form_id) *form_id = candidate;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL u32 x86_64_metadata_test_max_visible_operand_count(BusterX86MetadataCandidateRange range)
{
    u32 maximum = 0;
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 form_id = 0;
        BusterX86MetadataForm form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &form_id) || !buster_x86_metadata_form(form_id, &form)) continue;
        maximum = BUSTER_MAX(maximum, x86_64_metadata_test_visible_operand_count(form_id, form.operand_count));
    }
    return maximum;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataResolveResult x86_64_metadata_test_resolve_form(String8 mnemonic, u32 form_id,
                                                                                      bool include_implicit,
                                                                                      bool include_privileged, bool include_not64,
                                                                                      u16 decorator_flags, u16 apx_flags,
                                                                                      u16 amx_flags, u8 address_size, u32* form_ids,
                                                                                      u32 form_id_capacity)
{
    BusterX86MetadataResolveResult invalid = {.status = BUSTER_X86_METADATA_RESOLVE_INVALID_INPUT};
    BusterX86MetadataOperandSignature signatures[16] = {0};
    char8 atom_buffers[16][128] = {0};
    char8 width_buffers[16][128] = {0};
    char8 feature_buffer[256] = {0};
    String8 feature = {0};
    u32 signature_count = 0;
    if (!x86_64_metadata_test_build_signatures(form_id, include_implicit, signatures, atom_buffers, width_buffers, &feature,
                                                feature_buffer, sizeof(feature_buffer), &signature_count))
    {
        return invalid;
    }
    String8 feature_names[1] = {feature};
    return buster_x86_metadata_resolve(
        (BusterX86MetadataResolveQuery){
            .mnemonic = mnemonic,
            .operands = signatures,
            .operand_count = signature_count,
            .features = {.names = feature_names, .count = 1},
            .decorator_flags = decorator_flags,
            .apx_flags = apx_flags,
            .amx_flags = amx_flags,
            .address_size = address_size,
            .include_implicit = include_implicit,
            .include_privileged = include_privileged,
            .include_not64 = include_not64,
        },
        form_ids, form_id_capacity);
}

#if !BUSTER_SINGLE_THREADED
enum
{
    X86_64_METADATA_TEST_MAX_THREAD_COUNT = 8,
};

typedef struct X86_64MetadataConcurrentLookupState X86_64MetadataConcurrentLookupState;
struct X86_64MetadataConcurrentLookupState
{
    AtomicU64 start;
    AtomicU64 failed;
};

BUSTER_GLOBAL_LOCAL void x86_64_metadata_test_concurrent_lookup(void* argument)
{
    X86_64MetadataConcurrentLookupState* state = (X86_64MetadataConcurrentLookupState*)argument;
    while (!atomic_u64_add(&state->start, 0))
    {
    }
    for (u32 iteration = 0; iteration < 64; iteration += 1)
    {
        BusterX86MetadataCandidateRange range = buster_x86_metadata_lookup_mnemonic(S8("mov"));
        u32 form_id = 0;
        if (!range.count || !x86_64_metadata_test_candidate(range, 0, &form_id) || form_id >= buster_x86_metadata_form_count())
        {
            atomic_u64_increment(&state->failed);
            return;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_concurrent_lookup_stress(u32 requested_thread_count)
{
    // The contract the threads below depend on: every table and demand-filled
    // cache this module answers from is written while the process is still
    // serial, so the lookups are pure reads. Without it the first lookup in
    // each thread would race the decode, and the module says so through
    // BUSTER_CHECK_SERIAL_INITIALIZATION rather than producing torn answers.
    buster_x86_metadata_prewarm();
    X86_64MetadataConcurrentLookupState state = {0};
    OsThreadHandle* threads[X86_64_METADATA_TEST_MAX_THREAD_COUNT] = {0};
    u32 thread_count = 0;
    bool created = true;
    BUSTER_CHECK(requested_thread_count && requested_thread_count <= BUSTER_ARRAY_LENGTH(threads));
    for (u32 index = 0; index < requested_thread_count; index += 1)
    {
        threads[index] = os_thread_create((ThreadCreateOptions){
            .callback = &x86_64_metadata_test_concurrent_lookup,
            .argument = &state,
        });
        if (!threads[index])
        {
            created = false;
            break;
        }
        thread_count += 1;
    }
    atomic_u64_increment(&state.start);
    for (u32 index = 0; index < thread_count; index += 1) created &= os_thread_join(threads[index]);
    return created && thread_count == requested_thread_count && !atomic_u64_add(&state.failed, 0);
}
#endif

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_reg(u8 physical_class, u16 index, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
        .width = width,
        .reg = {.index = index, .width = width, .physical_class = physical_class},
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_mem_base(u16 base, u16 width, s64 displacement)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
        .width = width,
        .memory = {
            .base = {.index = base, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
            .displacement = displacement,
            .address_size = 64,
            .scale = 1,
            .has_base = true,
            .has_displacement = displacement != 0,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_mem_index(u16 index, u8 scale, u16 width,
                                                                                              s64 displacement)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
        .width = width,
        .memory = {
            .index = {.index = index, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
            .displacement = displacement,
            .address_size = 64,
            .scale = scale,
            .has_index = true,
            .has_displacement = displacement != 0,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_mem_rip(String8 symbol, s64 addend, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
        .width = width,
        .memory = {
            .symbol = symbol,
            .addend = addend,
            .address_size = 64,
            .has_symbol = true,
            .rip_relative = true,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_imm(s64 value, u16 width);
BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_relative(s64 value, u16 width);
BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_absolute(String8 symbol, u16 width);
BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalQuery x86_64_metadata_test_physical_query(
    String8 mnemonic, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataPhysicalAttributes attributes, String8 const* features, u32 feature_count);

typedef struct X86_64MetadataCanonicalGateCase X86_64MetadataCanonicalGateCase;
struct X86_64MetadataCanonicalGateCase
{
    u32 form_id;
    u8 required_count;
    String8 required[3];
};

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_build_gate_query(
    u32 form_id, BusterX86MetadataPhysicalQuery* query, BusterX86MetadataPhysicalOperand operands[16],
    char8 mnemonic_buffer[128])
{
    BusterX86MetadataForm form = {0};
    if (!query || !operands || !mnemonic_buffer || !buster_x86_metadata_form(form_id, &form) || form.iclass.length >= 128) return false;
    for (u32 character = 0; character < form.iclass.length; character += 1)
        mnemonic_buffer[character] = (char8)buster_x86_metadata_string_byte(form.iclass, character);
    u32 operand_count = 0;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form_id, operand_index, &metadata)) return false;
        if (!metadata.visible) continue;
        if (operand_count >= 16) return false;
        u16 width = metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024 ? 1024
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_512 ? 512
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_256 ? 256
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_128 ? 128
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_64 ? 64
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 ? 32
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_16 ? 16
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_8 ? 8
                                                                                           : 64;
        if (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER)
            operands[operand_count] = x86_64_metadata_test_physical_reg(metadata.physical_class, 0, width);
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY)
            operands[operand_count] = x86_64_metadata_test_physical_mem_base(0, width, 0);
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_IMMEDIATE)
            operands[operand_count] = x86_64_metadata_test_physical_imm(0, width);
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_RELATIVE)
            operands[operand_count] = x86_64_metadata_test_physical_relative(0, width);
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_ABSOLUTE)
            operands[operand_count] = x86_64_metadata_test_physical_absolute(S8("gate"), width);
        else
            return false;
        operand_count += 1;
    }
    *query = x86_64_metadata_test_physical_query(
        (String8){.pointer = mnemonic_buffer, .length = form.iclass.length}, operands, operand_count,
        (BusterX86MetadataPhysicalAttributes){0}, 0, 0);
    query->include_privileged = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED;
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_canonical_gate(X86_64MetadataCanonicalGateCase test_case)
{
    BusterX86MetadataPhysicalOperand operands[16] = {0};
    char8 mnemonic_buffer[128] = {0};
    BusterX86MetadataPhysicalQuery query = {0};
    if (!x86_64_metadata_test_build_gate_query(test_case.form_id, &query, operands, mnemonic_buffer)) return false;

    String8 required_features[3] = {test_case.required[0], test_case.required[1], test_case.required[2]};
    query.features.names = required_features;
    query.features.count = test_case.required_count;
    bool requires_apx = false;
    for (u32 feature_index = 0; feature_index < test_case.required_count; feature_index += 1)
        requires_apx |= required_features[feature_index].length == 3 && required_features[feature_index].pointer[0] == 'a' &&
                        required_features[feature_index].pointer[1] == 'p' && required_features[feature_index].pointer[2] == 'x';
    if (requires_apx)
    {
        for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
        {
            if (operands[operand_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                operands[operand_index].reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR)
                operands[operand_index].reg.index = 16;
        }
    }
    BusterX86MetadataSelectResult selected = buster_x86_metadata_select_form(query);
    if (selected.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || selected.form_id != test_case.form_id) return false;

    for (u32 removed_index = 0; removed_index < test_case.required_count; removed_index += 1)
    {
        String8 reduced_features[3] = {0};
        u32 reduced_count = 0;
        for (u32 feature_index = 0; feature_index < test_case.required_count; feature_index += 1)
        {
            if (feature_index != removed_index) reduced_features[reduced_count++] = required_features[feature_index];
        }
        query.features.names = reduced_features;
        query.features.count = reduced_count;
        BusterX86MetadataSelectResult missing = buster_x86_metadata_select_form(query);
        if (missing.status != BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_raw_conjunction_rejected(u32 form_id, String8 raw_feature)
{
    BusterX86MetadataPhysicalOperand operands[16] = {0};
    char8 mnemonic_buffer[128] = {0};
    BusterX86MetadataPhysicalQuery query = {0};
    if (!x86_64_metadata_test_build_gate_query(form_id, &query, operands, mnemonic_buffer)) return false;
    String8 raw_features[1] = {raw_feature};
    query.features.names = raw_features;
    query.features.count = BUSTER_ARRAY_LENGTH(raw_features);
    return buster_x86_metadata_select_form(query).status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_imm(s64 value, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE,
        .width = width,
        .value = value,
        .has_value = true,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_imm_u64(u64 value, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE,
        .width = width,
        .unsigned_value = value,
        .has_unsigned_value = true,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_relative(s64 value, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE,
        .width = width,
        .value = value,
        .has_value = true,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_absolute(String8 symbol, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE,
        .width = width,
        .symbol = symbol,
        .has_symbol = true,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalQuery x86_64_metadata_test_physical_query(
    String8 mnemonic, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataPhysicalAttributes attributes, String8 const* features, u32 feature_count)
{
    return (BusterX86MetadataPhysicalQuery){
        .mnemonic = mnemonic,
        .operands = operands,
        .operand_count = operand_count,
        .features = {.names = features, .count = feature_count},
        .attributes = attributes,
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataEmitResult x86_64_metadata_test_emit_form(
    String8 mnemonic, u32 form_id, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataPhysicalAttributes attributes, String8 const* features, u32 feature_count, u8* output,
    u32 output_capacity, BusterX86MetadataRelocation* relocations, u32 relocation_capacity)
{
    BusterX86MetadataPhysicalQuery physical = x86_64_metadata_test_physical_query(mnemonic, operands, operand_count, attributes,
                                                                                     features, feature_count);
    return buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
        .physical = physical,
        .form_id = form_id,
        .output = output,
        .output_capacity = output_capacity,
        .relocations = relocations,
        .relocation_capacity = relocation_capacity,
    });
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_bytes_equal(u8 const* actual, u32 actual_count, u8 const* expected, u32 expected_count)
{
    if (actual_count != expected_count || (expected_count && (!actual || !expected))) return false;
    for (u32 index = 0; index < expected_count; index += 1)
    {
        if (actual[index] != expected[index]) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_emit_exact(
    String8 mnemonic, u32 form_id, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataPhysicalAttributes attributes, String8 const* features, u32 feature_count, u8 const* expected,
    u32 expected_count)
{
    u8 output[32] = {0};
    BusterX86MetadataRelocation relocations[8] = {0};
    BusterX86MetadataEmitResult result = x86_64_metadata_test_emit_form(mnemonic, form_id, operands, operand_count, attributes,
                                                                          features, feature_count, output, sizeof(output), relocations,
                                                                          BUSTER_ARRAY_LENGTH(relocations));
    return result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && result.relocation_count == 0 &&
           x86_64_metadata_test_bytes_equal(output, result.byte_count, expected, expected_count);
}

UnitTestResult x86_64_metadata_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    X86_64MetadataCanonicalGateCase const canonical_gate_cases[] = {
        {8014, 1, {S8("enqcmd")}},
        {8015, 1, {S8("fred")}},
        {8099, 1, {S8("hreset")}},
        {424, 1, {S8("invlpgb")}},
        {8100, 1, {S8("invpcid")}},
        {8714, 1, {S8("keylocker")}},
        {8017, 1, {S8("lkgs")}},
        {9599, 1, {S8("monitor")}},
        {8820, 1, {S8("msrlist")}},
        {8818, 1, {S8("msr-imm")}},
        {8822, 1, {S8("pbndkb")}},
        {8824, 1, {S8("pconfig")}},
        {8844, 1, {S8("sgx")}},
        {8888, 1, {S8("smap")}},
        {433, 1, {S8("snp")}},
        {8890, 1, {S8("tdx")}},
        {9066, 1, {S8("wbnoinvd")}},
        {9067, 1, {S8("wrmsrns")}},
        {10971, 1, {S8("xsave")}},
        {11009, 1, {S8("xsaves")}},
        {10993, 1, {S8("vmx")}},
        {454, 1, {S8("svm")}},
        {1750, 2, {S8("apx"), S8("enqcmd")}},
        {1848, 2, {S8("apx"), S8("invpcid")}},
        {2932, 2, {S8("apx"), S8("msr-imm")}},
        {1847, 2, {S8("apx"), S8("vmx")}},
        {1886, 2, {S8("apx"), S8("movdir64b")}},
    };
    for (u32 gate_index = 0; gate_index < BUSTER_ARRAY_LENGTH(canonical_gate_cases); gate_index += 1)
        BUSTER_TEST(arguments, x86_64_metadata_test_canonical_gate(canonical_gate_cases[gate_index]));
    BUSTER_TEST(arguments, x86_64_metadata_test_raw_conjunction_rejected(1750, S8("APX_F_ENQCMD")));
    BUSTER_TEST(arguments, x86_64_metadata_test_raw_conjunction_rejected(1848, S8("APX_F_INVPCID")));
    BUSTER_TEST(arguments, x86_64_metadata_test_raw_conjunction_rejected(2932, S8("APX_F_MSR_IMM")));
    BUSTER_TEST(arguments, x86_64_metadata_test_raw_conjunction_rejected(1847, S8("APX_F_VMX")));
    BUSTER_TEST(arguments, x86_64_metadata_test_raw_conjunction_rejected(1886, S8("APX_F_MOVDIR64B")));
    BUSTER_TEST(arguments, buster_x86_metadata_test_eamode_alias_forms(423, 424) &&
                               !buster_x86_metadata_test_eamode_alias_forms(423, 10993));
    BusterX86MetadataCounts counts = buster_x86_metadata_counts();
    BusterX86MetadataValidationResult validation = {0};
    BUSTER_TEST(arguments, buster_x86_metadata_schema_version() == 2);
    BUSTER_TEST(arguments, buster_x86_metadata_form_count() == 11013);
    BUSTER_TEST(arguments, buster_x86_metadata_normalized_form_count() == 10636);
    BUSTER_TEST(arguments, buster_x86_metadata_coverage_count() == 11013);
    BUSTER_TEST(arguments, buster_x86_metadata_operand_count() == 32813);
    BUSTER_TEST(arguments, buster_x86_metadata_string_pool_size() == 1726254);
    BUSTER_TEST(arguments, buster_x86_metadata_validate(&validation) && validation.valid &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_NONE);
    BUSTER_TEST(arguments, counts.total_form_count == 11013 && counts.normalized_form_count == 10636 && counts.coverage_count == 11013);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_DIRECT] == 0);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_NORMALIZED] == 10636);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_NOT64] == 268);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_PRIVILEGED] == 109);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_RESERVED] == 0);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_UNSUPPORTED_TOKEN] == 0);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_UNCLASSIFIED] == 0);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_NONE] == 10636);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_MODE_NOT64] == 268);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_CPL0] == 109);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_UNKNOWN_PATTERN_TOKEN] == 0);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_UNKNOWN_OPERAND_TOKEN] == 0);

    BusterX86MetadataForm first_form = {0};
    BusterX86MetadataForm last_form = {0};
    BusterX86MetadataCoverage first_coverage = {0};
    BusterX86MetadataCoverage last_coverage = {0};
    BUSTER_TEST(arguments, buster_x86_metadata_form(0, &first_form) && first_form.id == 0);
    BUSTER_TEST(arguments, buster_x86_metadata_form(11012, &last_form) && last_form.id == 11012);
    BUSTER_TEST(arguments, buster_x86_metadata_coverage(0, &first_coverage) && first_coverage.id == 0);
    BUSTER_TEST(arguments, buster_x86_metadata_coverage(11012, &last_coverage) && last_coverage.id == 11012);
    BUSTER_TEST(arguments, !buster_x86_metadata_form(11013, &first_form));
    BUSTER_TEST(arguments, !buster_x86_metadata_coverage(11013, &first_coverage));
    BUSTER_TEST(arguments, !buster_x86_metadata_form(0, 0) && !buster_x86_metadata_coverage(0, 0));

    BusterX86MetadataString pool_string = {0};
    u32 pool_size = buster_x86_metadata_string_pool_size();
    BUSTER_TEST(arguments, buster_x86_metadata_string(0, &pool_string) && pool_string.length == 0);
    BUSTER_TEST(arguments, buster_x86_metadata_string(pool_size - 1, &pool_string) && pool_string.length == 0);
    BUSTER_TEST(arguments, !buster_x86_metadata_string(pool_size, &pool_string));
    BUSTER_TEST(arguments, !buster_x86_metadata_string(UINT32_MAX, &pool_string));
    BusterX86MetadataOperand operand = {0};
    BUSTER_TEST(arguments, !buster_x86_metadata_operand(0, first_form.operand_count, &operand));
    BUSTER_TEST(arguments, !buster_x86_metadata_operand(11013, 0, &operand));

    BusterX86MetadataValidationPatch patch = {
        .kind = BUSTER_X86_METADATA_PATCH_FORM_SOURCE_OFFSET,
        .index = 0,
        .value = 0xffffffffu,
    };
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_STRING_OFFSET);
    patch = (BusterX86MetadataValidationPatch){
        .kind = BUSTER_X86_METADATA_PATCH_FORM_ICLASS_OFFSET,
        .index = 0,
        .value = 0xffffffffu,
    };
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_STRING_OFFSET);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_STABLE_HASH, .index = 0, .value = 0};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_FORM_HASH);
    patch.value = 1;
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_FORM_HASH);
    patch = (BusterX86MetadataValidationPatch){
        .kind = BUSTER_X86_METADATA_PATCH_FORM_OPERAND_RANGE,
        .index = 0,
        .value = ((u64)32813 << 32) | 1,
    };
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_OPERAND_RANGE);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_COVERAGE_CLASS, .index = 0, .value = 0xff};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENUM);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_RESERVED, .index = 0, .value = 1};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_RESERVED);
    patch.value = 0x100;
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_RESERVED);
    patch.value = 0x10000;
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_RESERVED);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_RESERVED2, .index = 0, .value = 1};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_RESERVED);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_OPERAND_RESERVED, .index = 0, .value = 0x010203};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_RESERVED);

    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_COVERAGE_SOURCE_HASH, .index = 0, .value = 0};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_HASH);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_COVERAGE_FORM_ID, .index = 0, .value = 11013};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_FORM_ID);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_COVERAGE_FORM_ID, .index = 1, .value = 0};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_FORM_ID);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_COVERAGE_SOURCE_OFFSET, .index = 0, .value = 0};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_SOURCE);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_COVERAGE_REASON_OFFSET, .index = 0, .value = 1};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_REASON);
    patch = (BusterX86MetadataValidationPatch){
        .kind = BUSTER_X86_METADATA_PATCH_COVERAGE_REASON_ID,
        .index = 0,
        .value = BUSTER_X86_METADATA_REASON_CPL0,
    };
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_REASON);
    patch = (BusterX86MetadataValidationPatch){
        .kind = BUSTER_X86_METADATA_PATCH_COVERAGE_ENCODER_FAMILY,
        .index = 0,
        .value = BUSTER_X86_METADATA_ENCODER_SYSTEM,
    };
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_CLASSIFICATION);
    patch = (BusterX86MetadataValidationPatch){
        .kind = BUSTER_X86_METADATA_PATCH_COVERAGE_TEST_CLASS,
        .index = 0,
        .value = BUSTER_X86_METADATA_TEST_PRIVILEGED_SCHEMA,
    };
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_CLASSIFICATION);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_COVERAGE_CLASS, .index = 0, .value = 0xff};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENUM);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_INDEX_CAPACITY, .index = 0, .value = 0};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_FIELD_FLAGS, .index = 0, .value = UINT16_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_DECORATOR_FLAGS, .index = 0, .value = UINT16_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_APX_FLAGS, .index = 0, .value = UINT16_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_AMX_FLAGS, .index = 0, .value = UINT16_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_MODE_FLAGS, .index = 0, .value = UINT16_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_ENCODING_WIDTHS, .index = 0, .value = 3};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_MANDATORY_PREFIX, .index = 0, .value = 0x67};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_OPERAND_KIND, .index = 0, .value = UINT8_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENUM);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_OPERAND_FIELD_SOURCE, .index = 0, .value = UINT8_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENUM);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_OPERAND_ACCESS, .index = 0, .value = UINT8_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENUM);

#if !BUSTER_SINGLE_THREADED
    u64 metadata_worker_count = buster_test_worker_count(X86_64_METADATA_TEST_MAX_THREAD_COUNT);
    BUSTER_TEST(arguments, x86_64_metadata_test_concurrent_lookup_stress((u32)metadata_worker_count));
#endif

    BusterX86MetadataCandidateRange mov = buster_x86_metadata_lookup_mnemonic(S8("MOV"));
    BusterX86MetadataCandidateRange mov_iclass = buster_x86_metadata_lookup_iclass(S8("MOV"));
    BusterX86MetadataCandidateRange mov_again = buster_x86_metadata_lookup_mnemonic(S8("mov"));
    BUSTER_TEST(arguments, mov.count > 0 && mov.first == mov_again.first && mov.count == mov_again.count &&
                               mov.index_kind == mov_again.index_kind && mov.index_kind != mov_iclass.index_kind);
    BUSTER_TEST(arguments, x86_64_metadata_test_range_is_sorted(mov) &&
                               x86_64_metadata_test_range_contains_iclass(mov, S8("MOV")));
    BUSTER_TEST(arguments, buster_x86_metadata_lookup_mnemonic(S8("not_an_x86_mnemonic")).count == 0);

    BusterX86MetadataCandidateRange call = buster_x86_metadata_lookup_mnemonic(S8("call"));
    BusterX86MetadataCandidateRange ret = buster_x86_metadata_lookup_mnemonic(S8("ret"));
    BusterX86MetadataCandidateRange call_iclass = buster_x86_metadata_lookup_iclass(S8("CALL_NEAR"));
    BusterX86MetadataCandidateRange ret_iclass = buster_x86_metadata_lookup_iclass(S8("RET_NEAR"));
    BusterX86MetadataCandidateRange xop = buster_x86_metadata_lookup_mnemonic(S8("VPMACSSWW"));
    BusterX86MetadataCandidateRange avx512 = buster_x86_metadata_lookup_mnemonic(S8("V4FMADDPS"));
    BusterX86MetadataCandidateRange system = buster_x86_metadata_lookup_mnemonic(S8("WRMSR"));
    BusterX86MetadataCandidateRange amx = buster_x86_metadata_lookup_mnemonic(S8("TILELOADD"));
    BusterX86MetadataCandidateRange avx10 = buster_x86_metadata_lookup_iform(
        S8("VCVTBF42HF8_XMMhf8_MASKmskw_XMMbf4_AVX512"));
    BusterX86MetadataCandidateRange apx = buster_x86_metadata_lookup_iform(S8("LDTILECFG_MEM_APX"));
    BUSTER_TEST(arguments, call.count > 0 && ret.count > 0 && call_iclass.count > 0 && ret_iclass.count > 0 && xop.count > 0 &&
                               avx512.count > 0 && system.count > 0 && amx.count > 0 && avx10.count > 0 && apx.count > 0);
    BUSTER_TEST(arguments, x86_64_metadata_test_range_is_sorted(call) && x86_64_metadata_test_range_is_sorted(ret) &&
                               x86_64_metadata_test_range_is_sorted(call_iclass) && x86_64_metadata_test_range_is_sorted(ret_iclass) &&
                               x86_64_metadata_test_range_is_sorted(xop) && x86_64_metadata_test_range_is_sorted(avx512) &&
                               x86_64_metadata_test_range_is_sorted(system) && x86_64_metadata_test_range_is_sorted(amx) &&
                               x86_64_metadata_test_range_is_sorted(avx10) && x86_64_metadata_test_range_is_sorted(apx));
    BUSTER_TEST(arguments, x86_64_metadata_test_range_contains_iclass(call, S8("CALL_NEAR")) &&
                               x86_64_metadata_test_range_contains_iclass(ret, S8("RET_NEAR")) &&
                               x86_64_metadata_test_range_contains_iclass(xop, S8("VPMACSSWW")) &&
                               x86_64_metadata_test_range_contains_iclass(avx512, S8("V4FMADDPS")) &&
                               x86_64_metadata_test_range_contains_iclass(system, S8("WRMSR")) &&
                               x86_64_metadata_test_range_contains_iclass(amx, S8("TILELOADD")));

    if (mov.count)
    {
        u32 mov_id = 0;
        BusterX86MetadataForm mov_form = {0};
        char8 iform_buffer[512] = {0};
        String8 iform_key = {0};
        bool found_iform = false;
        for (u32 index = 0; index < mov.count && !found_iform; index += 1)
        {
            found_iform = x86_64_metadata_test_candidate(mov, index, &mov_id) && buster_x86_metadata_form(mov_id, &mov_form) &&
                          mov_form.iform.length > 0 && x86_64_metadata_test_string8(mov_form.iform, iform_buffer, sizeof(iform_buffer), &iform_key);
        }
        BUSTER_TEST(arguments, found_iform);
        if (found_iform)
        {
            BusterX86MetadataCandidateRange iform = buster_x86_metadata_lookup_iform(iform_key);
            BUSTER_TEST(arguments, iform.count > 0);
            for (u32 index = 0; index < iform.count; index += 1)
            {
                u32 iform_id = 0;
                BusterX86MetadataForm iform_form = {0};
                BUSTER_TEST(arguments, x86_64_metadata_test_candidate(iform, index, &iform_id) &&
                                           buster_x86_metadata_form(iform_id, &iform_form) &&
                                           x86_64_metadata_test_string_equal(iform_form.iform, iform_key));
            }
            BusterX86MetadataFilter shape_filter = {
                .has_operand_count = true,
                .has_visible_operand_count = true,
                .operand_count = mov_form.operand_count,
                .visible_operand_count = (u16)x86_64_metadata_test_visible_operand_count(mov_id, mov_form.operand_count),
                .operand_shape_count = mov_form.operand_count,
            };
            for (u32 index = 0; index < mov_form.operand_count && index < BUSTER_X86_METADATA_MAX_OPERAND_SHAPE; index += 1)
            {
                BusterX86MetadataOperand shape_operand = {0};
                BUSTER_TEST(arguments, buster_x86_metadata_operand(mov_id, index, &shape_operand));
                shape_filter.operand_shape[index] = (BusterX86MetadataOperandShape){
                    .kind = shape_operand.kind,
                    .visible = shape_operand.visible,
                };
            }
            bool found = false;
            u32 shape_count = x86_64_metadata_test_filtered_count(mov, shape_filter, mov_id, &found);
            BUSTER_TEST(arguments, found && shape_count > 0);
        }
    }

    if (system.count)
    {
        u32 system_id = 0;
        BusterX86MetadataFilter privileged_filter = {.privileged_only = true};
        bool found = false;
        BUSTER_TEST(arguments, x86_64_metadata_test_candidate(system, 0, &system_id));
        u32 privileged_count = x86_64_metadata_test_filtered_count(system, privileged_filter, system_id, &found);
        BUSTER_TEST(arguments, found && privileged_count == system.count);
        BusterX86MetadataForm system_form = {0};
        BusterX86MetadataCoverage system_coverage = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_form(system_id, &system_form) &&
                               system_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED &&
                               system_form.reason_id == BUSTER_X86_METADATA_REASON_CPL0);
        BUSTER_TEST(arguments, buster_x86_metadata_coverage(system_id, &system_coverage) &&
                               system_coverage.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED &&
                               system_coverage.reason_id == BUSTER_X86_METADATA_REASON_CPL0);
    }

    if (xop.count)
    {
        u32 xop_id = 0;
        BusterX86MetadataForm xop_form = {0};
        BUSTER_TEST(arguments, x86_64_metadata_test_candidate(xop, 0, &xop_id) && buster_x86_metadata_form(xop_id, &xop_form));
        BusterX86MetadataFilter xop_filter = {
            .require_64_bit = true,
            .has_isa_set = true,
            .isa_set = xop_form.isa_set,
            .has_prefix_kind = true,
            .prefix_kind = xop_form.prefix_kind,
            .has_encoder_family = true,
            .encoder_family = xop_form.encoder_family,
        };
        BusterX86MetadataCandidateIterator iterator = buster_x86_metadata_filter(xop, xop_filter);
        u32 filtered_id = 0;
        u32 filtered_count = 0;
        while (buster_x86_metadata_candidate_next(&iterator, &filtered_id))
        {
            BusterX86MetadataForm filtered_form = {0};
            BUSTER_TEST(arguments, buster_x86_metadata_form(filtered_id, &filtered_form) &&
                                       filtered_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                                       filtered_form.prefix_kind == xop_form.prefix_kind &&
                                       filtered_form.encoder_family == xop_form.encoder_family &&
                                       x86_64_metadata_test_string_equal(filtered_form.isa_set, S8("XOP")));
            filtered_count += 1;
        }
        BUSTER_TEST(arguments, filtered_count > 0);
    }

    if (avx10.count)
    {
        u32 avx10_id = 0;
        BusterX86MetadataForm avx10_form = {0};
        BUSTER_TEST(arguments, x86_64_metadata_test_candidate(avx10, 0, &avx10_id) && buster_x86_metadata_form(avx10_id, &avx10_form));
        BusterX86MetadataFilter avx10_filter = {
            .require_64_bit = true,
            .has_isa_set = true,
            .isa_set = avx10_form.isa_set,
            .has_prefix_kind = true,
            .prefix_kind = avx10_form.prefix_kind,
            .has_encoder_family = true,
            .encoder_family = avx10_form.encoder_family,
        };
        bool found = false;
        u32 avx10_count = x86_64_metadata_test_filtered_count(avx10, avx10_filter, avx10_id, &found);
        BUSTER_TEST(arguments, found && avx10_count > 0);
    }

    u32 first_form_hash_id = UINT32_MAX;
    u32 first_coverage_hash_id = UINT32_MAX;
    BusterX86MetadataCandidateRange form_hash = buster_x86_metadata_lookup_form_hash(first_form.stable_hash);
    BusterX86MetadataCoverageRange coverage_hash = buster_x86_metadata_lookup_coverage_hash(first_coverage.source_hash);
    BUSTER_TEST(arguments, form_hash.count == 1 && x86_64_metadata_test_candidate(form_hash, 0, &first_form_hash_id) &&
                               first_form_hash_id == first_form.id);
    BUSTER_TEST(arguments, coverage_hash.count == 1 && x86_64_metadata_test_coverage_candidate(coverage_hash, 0, &first_coverage_hash_id) &&
                               first_coverage_hash_id == first_coverage.id);
    BUSTER_TEST(arguments, buster_x86_metadata_lookup_form_hash(0).count == 0 && buster_x86_metadata_lookup_coverage_hash(0).count == 0);

    {
        BusterX86MetadataForm mov_form = {0};
        u32 mov_form_id = 0;
        bool found_mov = x86_64_metadata_test_first_form_with_extension(mov, S8("BASE"), &mov_form_id, &mov_form) ||
                         x86_64_metadata_test_first_normalized_form(mov, &mov_form_id, &mov_form);
        u32 resolved_ids[512] = {0};
        u32 resolved_again[512] = {0};
        BusterX86MetadataResolveResult resolved = {0};
        BusterX86MetadataResolveResult resolved_again_result = {0};
        BusterX86MetadataOperandSignature signatures[16] = {0};
        char8 atom_buffers[16][128] = {0};
        char8 width_buffers[16][128] = {0};
        char8 feature_buffer[256] = {0};
        String8 feature = {0};
        u32 signature_count = 0;
        bool built_mov = found_mov && x86_64_metadata_test_build_signatures(mov_form_id, false, signatures, atom_buffers, width_buffers,
                                                                               &feature, feature_buffer, sizeof(feature_buffer),
                                                                               &signature_count);
        BUSTER_TEST(arguments, built_mov);
        if (built_mov)
        {
            String8 features[1] = {feature};
            BusterX86MetadataResolveQuery query = {
                .mnemonic = S8("MoV"),
                .operands = signatures,
                .operand_count = signature_count,
                .features = {.names = features, .count = 1},
            };
            resolved = buster_x86_metadata_resolve(query, resolved_ids, BUSTER_ARRAY_LENGTH(resolved_ids));
            BUSTER_TEST(arguments, resolved.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && resolved.required_candidate_count > 0 &&
                                       resolved.candidate_count == resolved.required_candidate_count);
            bool ordered = true;
            for (u32 index = 1; index < resolved.candidate_count; index += 1) ordered &= resolved_ids[index] > resolved_ids[index - 1];
            BUSTER_TEST(arguments, ordered);
            query.mnemonic = S8("mov");
            resolved_again_result = buster_x86_metadata_resolve(query, resolved_again, BUSTER_ARRAY_LENGTH(resolved_again));
            bool same = resolved_again_result.status == resolved.status && resolved_again_result.candidate_count == resolved.candidate_count;
            for (u32 index = 0; same && index < resolved.candidate_count; index += 1) same &= resolved_again[index] == resolved_ids[index];
            BUSTER_TEST(arguments, same);

            u32 maximum = x86_64_metadata_test_max_visible_operand_count(mov);
            if (maximum < 16)
            {
                BusterX86MetadataResolveResult wrong_count = buster_x86_metadata_resolve(
                    (BusterX86MetadataResolveQuery){
                        .mnemonic = S8("mov"), .operands = signatures, .operand_count = maximum + 1,
                        .features = {.names = features, .count = 1},
                    },
                    resolved_ids, BUSTER_ARRAY_LENGTH(resolved_ids));
                BUSTER_TEST(arguments, wrong_count.status == BUSTER_X86_METADATA_RESOLVE_WRONG_OPERAND_COUNT);
            }
            if (signature_count)
            {
                BusterX86MetadataOperandSignature bad_signatures[16] = {0};
                memcpy(bad_signatures, signatures, sizeof(bad_signatures));
                bad_signatures[0].width = S8("__metadata_width_mismatch__");
                bad_signatures[0].has_width = true;
                BusterX86MetadataResolveResult mismatch = buster_x86_metadata_resolve(
                    (BusterX86MetadataResolveQuery){
                        .mnemonic = S8("mov"), .operands = bad_signatures, .operand_count = signature_count,
                        .features = {.names = features, .count = 1},
                    },
                    resolved_ids, BUSTER_ARRAY_LENGTH(resolved_ids));
                BUSTER_TEST(arguments, mismatch.status == BUSTER_X86_METADATA_RESOLVE_OPERAND_CLASS_WIDTH_MISMATCH);
                BusterX86MetadataResolveResult unsupported_decorator = buster_x86_metadata_resolve(
                    (BusterX86MetadataResolveQuery){
                        .mnemonic = S8("mov"), .operands = signatures, .operand_count = signature_count,
                        .features = {.names = features, .count = 1}, .decorator_flags = BUSTER_X86_METADATA_DECORATOR_SAE,
                    },
                    resolved_ids, BUSTER_ARRAY_LENGTH(resolved_ids));
                BUSTER_TEST(arguments, unsupported_decorator.status == BUSTER_X86_METADATA_RESOLVE_UNSUPPORTED_DECORATOR);
            }
        }
    }

    {
        BusterX86MetadataCandidateRange addps = buster_x86_metadata_lookup_mnemonic(S8("ADDPS"));
        BusterX86MetadataForm sse_form = {0};
        u32 sse_form_id = 0;
        bool found_sse = x86_64_metadata_test_first_form_with_extension(addps, S8("SSE"), &sse_form_id, &sse_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult sse_result = {0};
        if (found_sse) sse_result = x86_64_metadata_test_resolve_form(S8("ADDPS"), sse_form_id, false, false, false, 0, 0, 0, 0, ids,
                                                                       BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, found_sse && sse_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && sse_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange vaddps = buster_x86_metadata_lookup_mnemonic(S8("VADDPS"));
        BusterX86MetadataForm avx_form = {0};
        u32 avx_form_id = 0;
        bool found_avx = x86_64_metadata_test_first_form_with_extension(vaddps, S8("AVX"), &avx_form_id, &avx_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult avx_result = {0};
        if (found_avx) avx_result = x86_64_metadata_test_resolve_form(S8("VADDPS"), avx_form_id, false, false, false, 0, 0, 0, 0, ids,
                                                                       BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, found_avx && avx_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && avx_result.candidate_count > 0);
        if (found_avx)
        {
            BusterX86MetadataOperandSignature signatures[16] = {0};
            char8 atom_buffers[16][128] = {0};
            char8 width_buffers[16][128] = {0};
            char8 feature_buffer[256] = {0};
            String8 feature = {0};
            u32 signature_count = 0;
            bool built = x86_64_metadata_test_build_signatures(avx_form_id, false, signatures, atom_buffers, width_buffers, &feature,
                                                                feature_buffer, sizeof(feature_buffer), &signature_count);
            String8 no_features[1] = {S8("__no_feature__")};
            BusterX86MetadataResolveResult disabled = {0};
            if (built)
            {
                disabled = buster_x86_metadata_resolve(
                    (BusterX86MetadataResolveQuery){
                        .mnemonic = S8("VADDPS"), .operands = signatures, .operand_count = signature_count,
                        .features = {.names = no_features, .count = 1},
                    },
                    ids, BUSTER_ARRAY_LENGTH(ids));
            }
            BUSTER_TEST(arguments, built && disabled.status == BUSTER_X86_METADATA_RESOLVE_UNAVAILABLE_TARGET_FEATURE);
        }
    }

    {
        BusterX86MetadataCandidateRange v4fmaddps = buster_x86_metadata_lookup_mnemonic(S8("V4FMADDPS"));
        BusterX86MetadataForm avx512_form = {0};
        u32 avx512_form_id = 0;
        bool found_avx512 = x86_64_metadata_test_first_normalized_form(v4fmaddps, &avx512_form_id, &avx512_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult avx512_result = {0};
        if (found_avx512)
        {
            avx512_result = x86_64_metadata_test_resolve_form(S8("V4FMADDPS"), avx512_form_id, false, false, false,
                                                               avx512_form.decorator_flags, 0, 0, 0, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, found_avx512 && avx512_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   avx512_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange vrndscaleps = buster_x86_metadata_lookup_mnemonic(S8("VRNDSCALEPS"));
        BusterX86MetadataForm sae_form = {0};
        u32 sae_form_id = 0;
        bool found_sae = x86_64_metadata_test_first_form_with_decorators(vrndscaleps, BUSTER_X86_METADATA_DECORATOR_SAE, &sae_form_id,
                                                                          &sae_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult sae_result = {0};
        if (found_sae)
        {
            sae_result = x86_64_metadata_test_resolve_form(S8("VRNDSCALEPS"), sae_form_id, false, false, false,
                                                            sae_form.decorator_flags, 0, 0, 0, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, found_sae && sae_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && sae_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange xop_range = buster_x86_metadata_lookup_mnemonic(S8("VPMACSSWW"));
        u32 xop_id = 0;
        BusterX86MetadataForm xop_form = {0};
        bool found_xop = x86_64_metadata_test_first_normalized_form(xop_range, &xop_id, &xop_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult xop_result = {0};
        if (found_xop) xop_result = x86_64_metadata_test_resolve_form(S8("VPMACSSWW"), xop_id, false, false, false, 0, 0, 0, 0, ids,
                                                                       BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, found_xop && xop_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && xop_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange amx_range = buster_x86_metadata_lookup_mnemonic(S8("TILELOADD"));
        BusterX86MetadataForm amx_form = {0};
        u32 amx_id = 0;
        bool found_amx = x86_64_metadata_test_first_form_with_extension(amx_range, S8("AMX_TILE"), &amx_id, &amx_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult amx_result = {0};
        if (found_amx) amx_result = x86_64_metadata_test_resolve_form(S8("TILELOADD"), amx_id, false, false, false, amx_form.decorator_flags,
                                                                       0, amx_form.amx_flags, 64, ids, BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, found_amx && amx_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && amx_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange apx_range = buster_x86_metadata_lookup_mnemonic(S8("ADD"));
        BusterX86MetadataForm apx_form = {0};
        u32 apx_id = 0;
        bool found_apx = x86_64_metadata_test_first_form_with_apx(apx_range, &apx_id, &apx_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult apx_result = {0};
        if (found_apx) apx_result = x86_64_metadata_test_resolve_form(S8("ADD"), apx_id, false, false, false, apx_form.decorator_flags,
                                                                       apx_form.apx_flags, 0, 0, ids, BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, found_apx && apx_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && apx_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange avx10_range = buster_x86_metadata_lookup_iform(
            S8("VCVTBF42HF8_XMMhf8_MASKmskw_XMMbf4_AVX512"));
        BusterX86MetadataForm avx10_form = {0};
        u32 avx10_id = 0;
        bool found_avx10 = x86_64_metadata_test_first_normalized_form(avx10_range, &avx10_id, &avx10_form);
        char8 mnemonic_buffer[128] = {0};
        String8 mnemonic = {0};
        if (found_avx10) found_avx10 = x86_64_metadata_test_string8(avx10_form.iclass, mnemonic_buffer, sizeof(mnemonic_buffer), &mnemonic);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult avx10_result = {0};
        if (found_avx10) avx10_result = x86_64_metadata_test_resolve_form(mnemonic, avx10_id, false, false, false,
                                                                           avx10_form.decorator_flags, avx10_form.apx_flags,
                                                                           avx10_form.amx_flags, 0, ids, BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, found_avx10 && avx10_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   avx10_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange jmp_range = buster_x86_metadata_lookup_mnemonic(S8("JMP"));
        BusterX86MetadataForm relative_form = {0};
        BusterX86MetadataForm memory_form = {0};
        u32 relative_id = 0;
        u32 memory_id = 0;
        bool found_relative = x86_64_metadata_test_first_form_with_operand_kind(jmp_range, BUSTER_X86_METADATA_OPERAND_RELATIVE, true,
                                                                                  &relative_id, &relative_form);
        bool found_memory = x86_64_metadata_test_first_form_with_operand_kind(jmp_range, BUSTER_X86_METADATA_OPERAND_MEMORY, true,
                                                                               &memory_id, &memory_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult relative_result = {0};
        BusterX86MetadataResolveResult memory_result = {0};
        if (found_relative)
        {
            relative_result = x86_64_metadata_test_resolve_form(S8("JMP"), relative_id, false, false, false,
                                                                 relative_form.decorator_flags, 0, 0, 64, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        if (found_memory)
        {
            memory_result = x86_64_metadata_test_resolve_form(S8("JMP"), memory_id, false, false, false,
                                                               memory_form.decorator_flags, 0, 0, 64, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, found_relative && relative_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   relative_result.candidate_count > 0);
        BUSTER_TEST(arguments, found_memory && memory_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && memory_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange ret_range = buster_x86_metadata_lookup_mnemonic(S8("RET"));
        BusterX86MetadataForm implicit_form = {0};
        u32 implicit_id = 0;
        bool found_implicit = false;
        for (u32 index = 0; index < ret_range.count && !found_implicit; index += 1)
        {
            u32 candidate = 0;
            BusterX86MetadataForm candidate_form = {0};
            if (!buster_x86_metadata_candidate_at(ret_range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) continue;
            if (candidate_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                x86_64_metadata_test_visible_operand_count(candidate, candidate_form.operand_count) == 0)
            {
                implicit_id = candidate;
                implicit_form = candidate_form;
                found_implicit = true;
            }
        }
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult implicit_result = {0};
        if (found_implicit)
        {
            implicit_result = x86_64_metadata_test_resolve_form(S8("RET"), implicit_id, true, false, false,
                                                                 implicit_form.decorator_flags, 0, 0, 0, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, found_implicit && implicit_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   implicit_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange system_range = buster_x86_metadata_lookup_mnemonic(S8("WRMSR"));
        BusterX86MetadataForm system_form = {0};
        u32 system_id = 0;
        bool found_system = x86_64_metadata_test_first_form_with_coverage(system_range, BUSTER_X86_METADATA_COVERAGE_PRIVILEGED, &system_id,
                                                                           &system_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult system_result = {0};
        BusterX86MetadataResolveResult excluded_result = {0};
        if (found_system)
        {
            system_result = x86_64_metadata_test_resolve_form(S8("WRMSR"), system_id, false, true, false, system_form.decorator_flags, 0,
                                                               0, 0, ids, BUSTER_ARRAY_LENGTH(ids));
            excluded_result = x86_64_metadata_test_resolve_form(S8("WRMSR"), system_id, false, false, false, system_form.decorator_flags,
                                                                 0, 0, 0, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, found_system && system_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && system_result.candidate_count > 0);
        BUSTER_TEST(arguments, found_system &&
                                   excluded_result.status == BUSTER_X86_METADATA_RESOLVE_AMBIGUOUS_OR_UNSUPPORTED_METADATA);
    }

    {
        struct
        {
            String8 mnemonic;
            String8 extension;
        } extension_cases[] = {
            {S8("VFMADDPS"), S8("FMA4")},
            {S8("BEXTR"), S8("TBM")},
            {S8("PFADD"), S8("3DNOW")},
        };
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(extension_cases); case_index += 1)
        {
            BusterX86MetadataCandidateRange range = buster_x86_metadata_lookup_mnemonic(extension_cases[case_index].mnemonic);
            BusterX86MetadataForm form = {0};
            u32 form_id = 0;
            bool found = x86_64_metadata_test_first_form_with_extension(range, extension_cases[case_index].extension, &form_id, &form);
            u32 ids[128] = {0};
            BusterX86MetadataResolveResult resolved = {0};
            if (found)
            {
                resolved = x86_64_metadata_test_resolve_form(extension_cases[case_index].mnemonic, form_id, false, false, false,
                                                              form.decorator_flags, form.apx_flags, form.amx_flags, 0, ids,
                                                              BUSTER_ARRAY_LENGTH(ids));
            }
            BUSTER_TEST(arguments, found && resolved.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && resolved.candidate_count > 0);
        }
    }

    {
        // MODE_16|MODE_32|MODE_64 is a legal multi-mode row.  It must remain
        // visible to the ordinary 64-bit resolver, while an explicit
        // MODE_NOT64 row is inspection-only.
        u16 multi_mode = BUSTER_X86_METADATA_MODE_16 | BUSTER_X86_METADATA_MODE_32 | BUSTER_X86_METADATA_MODE_64;
        u16 not64_mode = BUSTER_X86_METADATA_MODE_16 | BUSTER_X86_METADATA_MODE_32;
        BUSTER_TEST(arguments, buster_x86_metadata_test_execution_mode_matches(
                                   multi_mode, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   not64_mode, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_NOT64, BUSTER_X86_METADATA_COVERAGE_NOT64, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_NOT64, BUSTER_X86_METADATA_COVERAGE_NOT64, true,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));

        u32 not64_form_id = 0;
        String8 not64_mnemonic = {0};
        bool found_not64_only = false;
        char8 not64_mnemonic_buffer[128] = {0};
        for (u32 candidate_form_id = 0; candidate_form_id < buster_x86_metadata_form_count() && !found_not64_only;
             candidate_form_id += 1)
        {
            BusterX86MetadataForm candidate_form = {0};
            if (!buster_x86_metadata_form(candidate_form_id, &candidate_form) ||
                candidate_form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NOT64 ||
                !x86_64_metadata_test_string8(candidate_form.iclass, not64_mnemonic_buffer, sizeof(not64_mnemonic_buffer),
                                               &not64_mnemonic))
                continue;
            BusterX86MetadataCandidateRange candidate_range = buster_x86_metadata_lookup_mnemonic(not64_mnemonic);
            bool only_not64 = candidate_range.count > 0;
            for (u32 position = 0; position < candidate_range.count && only_not64; position += 1)
            {
                u32 range_form_id = 0;
                BusterX86MetadataForm range_form = {0};
                only_not64 = buster_x86_metadata_candidate_at(candidate_range, position, &range_form_id) &&
                             buster_x86_metadata_form(range_form_id, &range_form) &&
                             range_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64;
            }
            if (only_not64)
            {
                not64_form_id = candidate_form_id;
                found_not64_only = true;
            }
        }
        BusterX86MetadataOperandSignature not64_signatures[16] = {0};
        char8 not64_atom_buffers[16][128] = {0};
        char8 not64_width_buffers[16][128] = {0};
        char8 not64_feature_buffer[256] = {0};
        String8 not64_feature = {0};
        u32 not64_signature_count = 0;
        bool built_not64 = found_not64_only && x86_64_metadata_test_build_signatures(
                                                  not64_form_id, false, not64_signatures, not64_atom_buffers, not64_width_buffers,
                                                  &not64_feature, not64_feature_buffer, sizeof(not64_feature_buffer),
                                                  &not64_signature_count);
        String8 not64_features[1] = {not64_feature};
        u32 not64_ids[128] = {0};
        BusterX86MetadataResolveResult not64_rejected = {0};
        BusterX86MetadataResolveResult not64_inspected = {0};
        if (built_not64)
        {
            not64_rejected = buster_x86_metadata_resolve(
                (BusterX86MetadataResolveQuery){
                    .mnemonic = not64_mnemonic,
                    .operands = not64_signatures,
                    .operand_count = not64_signature_count,
                    .features = {.names = not64_features, .count = BUSTER_ARRAY_LENGTH(not64_features)},
                },
                not64_ids, BUSTER_ARRAY_LENGTH(not64_ids));
            not64_inspected = buster_x86_metadata_resolve(
                (BusterX86MetadataResolveQuery){
                    .mnemonic = not64_mnemonic,
                    .operands = not64_signatures,
                    .operand_count = not64_signature_count,
                    .features = {.names = not64_features, .count = BUSTER_ARRAY_LENGTH(not64_features)},
                    .include_not64 = true,
                },
                not64_ids, BUSTER_ARRAY_LENGTH(not64_ids));
        }
        BUSTER_TEST(arguments, built_not64 && not64_rejected.status == BUSTER_X86_METADATA_RESOLVE_EXECUTION_MODE_MISMATCH);
        BUSTER_TEST(arguments, built_not64 && not64_inspected.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   not64_inspected.candidate_count > 0);
    }

    {
        // These are caller-owned physical signatures.  No raw XED atom or
        // width token is copied from a selected form before resolution.
        String8 all_features[1] = {S8("*")};
        u16 gpr_widths[] = {
            BUSTER_X86_METADATA_PHYSICAL_WIDTH_8,
            BUSTER_X86_METADATA_PHYSICAL_WIDTH_16,
            BUSTER_X86_METADATA_PHYSICAL_WIDTH_32,
            BUSTER_X86_METADATA_PHYSICAL_WIDTH_64,
        };
        for (u32 width_index = 0; width_index < BUSTER_ARRAY_LENGTH(gpr_widths); width_index += 1)
        {
            BusterX86MetadataOperandSignature mov_operands[2] = {
                x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                         BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, gpr_widths[width_index]),
                x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                         BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, gpr_widths[width_index]),
            };
            u32 ids[256] = {0};
            BusterX86MetadataResolveResult resolved = x86_64_metadata_test_resolve_physical(
                S8("MOV"), mov_operands, BUSTER_ARRAY_LENGTH(mov_operands), all_features, BUSTER_ARRAY_LENGTH(all_features), 0, 0,
                0, 0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
            BUSTER_TEST(arguments, resolved.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && resolved.candidate_count > 0);
        }

        BusterX86MetadataOperandSignature accumulator_operand = x86_64_metadata_test_physical_signature(
            BUSTER_X86_METADATA_OPERAND_REGISTER, BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
            BUSTER_X86_METADATA_PHYSICAL_WIDTH_32);
        u32 accumulator_ids[64] = {0};
        BusterX86MetadataResolveResult accumulator = buster_x86_metadata_resolve(
            (BusterX86MetadataResolveQuery){
                .mnemonic = S8("VMFUNC"),
                .operands = &accumulator_operand,
                .operand_count = 1,
                .features = {.names = all_features, .count = BUSTER_ARRAY_LENGTH(all_features)},
                .include_implicit = true,
            },
            accumulator_ids, BUSTER_ARRAY_LENGTH(accumulator_ids));
        BUSTER_TEST(arguments, accumulator.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && accumulator.candidate_count > 0);

        bool found_fixed_special = false;
        bool fixed_special_is_special = false;
        for (u32 form_id = 0; form_id < buster_x86_metadata_form_count() && !found_fixed_special; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            for (u32 operand_index = 0; operand_index < form.operand_count && !found_fixed_special; operand_index += 1)
            {
                BusterX86MetadataOperand special_operand = {0};
                if (!buster_x86_metadata_operand(form_id, operand_index, &special_operand)) continue;
                bool fixed_special_atom = x86_64_metadata_test_string_equal(special_operand.atom, S8("XED_REG_RIP")) ||
                                           x86_64_metadata_test_string_equal(special_operand.atom, S8("XED_REG_X87STATUS"));
                if (fixed_special_atom)
                {
                    found_fixed_special = true;
                    fixed_special_is_special = special_operand.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
                }
            }
        }
        BUSTER_TEST(arguments, found_fixed_special && fixed_special_is_special);

        BusterX86MetadataOperandSignature xmm_operands[2] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_128),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_128),
        };
        String8 sse_features[1] = {S8("sse2")};
        u32 xmm_ids[128] = {0};
        BusterX86MetadataResolveResult xmm = x86_64_metadata_test_resolve_physical(
            S8("ADDPS"), xmm_operands, BUSTER_ARRAY_LENGTH(xmm_operands), sse_features, BUSTER_ARRAY_LENGTH(sse_features), 0, 0,
            0, 0, 0, false, false, xmm_ids, BUSTER_ARRAY_LENGTH(xmm_ids));
        BUSTER_TEST(arguments, xmm.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && xmm.candidate_count > 0);

        BusterX86MetadataOperandSignature ymm_operands[3] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_256),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_256),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_256),
        };
        String8 avx_features[1] = {S8("avx")};
        u32 ymm_ids[128] = {0};
        BusterX86MetadataResolveResult ymm = x86_64_metadata_test_resolve_physical(
            S8("VADDPS"), ymm_operands, BUSTER_ARRAY_LENGTH(ymm_operands), avx_features, BUSTER_ARRAY_LENGTH(avx_features), 0, 0,
            0, 0, 0, false, false, ymm_ids, BUSTER_ARRAY_LENGTH(ymm_ids));
        BUSTER_TEST(arguments, ymm.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && ymm.candidate_count > 0);

        BusterX86MetadataOperandSignature zmm_operands[4] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_512),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 0),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_512),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_512),
        };
        String8 avx512_features[1] = {S8("avx512f")};
        u32 zmm_ids[128] = {0};
        BusterX86MetadataResolveResult zmm = x86_64_metadata_test_resolve_physical(
            S8("VADDPS"), zmm_operands, BUSTER_ARRAY_LENGTH(zmm_operands), avx512_features,
            BUSTER_ARRAY_LENGTH(avx512_features), 0, 0, BUSTER_X86_METADATA_DECORATOR_MASK, 0, 0, false, false, zmm_ids,
            BUSTER_ARRAY_LENGTH(zmm_ids));
        BUSTER_TEST(arguments, zmm.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && zmm.candidate_count > 0);

        BusterX86MetadataOperandSignature immediate_operands[2] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_32),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_IMMEDIATE,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_IMMEDIATE, 0),
        };
        u32 immediate_ids[128] = {0};
        BusterX86MetadataResolveResult immediate = x86_64_metadata_test_resolve_physical(
            S8("MOV"), immediate_operands, BUSTER_ARRAY_LENGTH(immediate_operands), all_features,
            BUSTER_ARRAY_LENGTH(all_features), 0, 0, 0, 0, 0, false, false, immediate_ids, BUSTER_ARRAY_LENGTH(immediate_ids));
        BUSTER_TEST(arguments, immediate.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && immediate.candidate_count > 0);

        BusterX86MetadataOperandSignature relative_operand = x86_64_metadata_test_physical_signature(
            BUSTER_X86_METADATA_OPERAND_RELATIVE, BUSTER_X86_METADATA_PHYSICAL_CLASS_RELATIVE, 0);
        u32 relative_ids[128] = {0};
        BusterX86MetadataResolveResult relative = x86_64_metadata_test_resolve_physical(
            S8("JMP"), &relative_operand, 1, all_features, BUSTER_ARRAY_LENGTH(all_features), 0, 0, 0, 0, 0, false, false,
            relative_ids, BUSTER_ARRAY_LENGTH(relative_ids));
        BUSTER_TEST(arguments, relative.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && relative.candidate_count > 0);
    }

    {
        String8 all_features[1] = {S8("*")};
        BusterX86MetadataOperandSignature mov_memory_operands[2] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_64),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_MEMORY,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_64),
        };
        u32 ids[256] = {0};
        BusterX86MetadataResolveResult memory = x86_64_metadata_test_resolve_physical(
            S8("MOV"), mov_memory_operands, BUSTER_ARRAY_LENGTH(mov_memory_operands), all_features,
            BUSTER_ARRAY_LENGTH(all_features), BUSTER_X86_METADATA_FIELD_MEMORY, 0, 0, 0, 0, false, false, ids,
            BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, memory.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && memory.candidate_count > 0);
        BusterX86MetadataResolveResult memory_sib = x86_64_metadata_test_resolve_physical(
            S8("MOV"), mov_memory_operands, BUSTER_ARRAY_LENGTH(mov_memory_operands), all_features,
            BUSTER_ARRAY_LENGTH(all_features), BUSTER_X86_METADATA_FIELD_SIB, 0, 0, 0, 0, false, false, ids,
            BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, memory_sib.status == BUSTER_X86_METADATA_RESOLVE_ADDRESSING_FIELD_MISMATCH);
        BusterX86MetadataResolveResult memory_vsib = x86_64_metadata_test_resolve_physical(
            S8("MOV"), mov_memory_operands, BUSTER_ARRAY_LENGTH(mov_memory_operands), all_features,
            BUSTER_ARRAY_LENGTH(all_features), BUSTER_X86_METADATA_FIELD_VSIB, 0, 0, 0, 0, false, false, ids,
            BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, memory_vsib.status == BUSTER_X86_METADATA_RESOLVE_ADDRESSING_FIELD_MISMATCH);

        BusterX86MetadataOperandSignature tile_memory_operands[2] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_MEMORY,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_32),
        };
        String8 amx_features[1] = {S8("amx-tile")};
        BusterX86MetadataResolveResult sib = x86_64_metadata_test_resolve_physical(
            S8("TILELOADD"), tile_memory_operands, BUSTER_ARRAY_LENGTH(tile_memory_operands), amx_features,
            BUSTER_ARRAY_LENGTH(amx_features), BUSTER_X86_METADATA_FIELD_SIB, 0, 0, 0, 0, false, false, ids,
            BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, sib.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && sib.candidate_count > 0);
    }

    {
        // A register candidate reaches feature filtering while a same-shape
        // memory candidate is rejected by the addressing predicate.  The
        // final diagnostic must be feature-disabled, not address mismatch.
        BusterX86MetadataOperandSignature unconstrained[3] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_ANY,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_ANY, 0),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_ANY,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_ANY, 0),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_ANY,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_ANY, 0),
        };
        u32 ids[256] = {0};
        BusterX86MetadataResolveResult mixed = x86_64_metadata_test_resolve_physical(
            S8("VADDPS"), unconstrained, BUSTER_ARRAY_LENGTH(unconstrained), 0, 0, BUSTER_X86_METADATA_FIELD_REGISTER, 0, 0, 0,
            0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, mixed.status == BUSTER_X86_METADATA_RESOLVE_UNAVAILABLE_TARGET_FEATURE);
    }

    {
        BusterX86MetadataCandidateRange vadd_range = buster_x86_metadata_lookup_mnemonic(S8("VADDPS"));
        BusterX86MetadataForm broadcast_form = {0};
        u32 broadcast_id = 0;
        bool found_broadcast = x86_64_metadata_test_first_form_with_decorators(
            vadd_range, BUSTER_X86_METADATA_DECORATOR_BROADCAST, &broadcast_id, &broadcast_form);
        BusterX86MetadataOperandSignature signatures[16] = {0};
        char8 atom_buffers[16][128] = {0};
        char8 width_buffers[16][128] = {0};
        char8 feature_buffer[256] = {0};
        String8 feature = {0};
        u32 signature_count = 0;
        bool built = found_broadcast && x86_64_metadata_test_build_signatures(
                                        broadcast_id, false, signatures, atom_buffers, width_buffers, &feature, feature_buffer,
                                        sizeof(feature_buffer), &signature_count);
        String8 features[1] = {feature};
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult broadcast = {0};
        if (built)
        {
            broadcast = x86_64_metadata_test_resolve_physical(
                S8("VADDPS"), signatures, signature_count, features, BUSTER_ARRAY_LENGTH(features), 0, 0,
                BUSTER_X86_METADATA_DECORATOR_BROADCAST, 0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, built && broadcast.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && broadcast.candidate_count > 0);

        BusterX86MetadataCandidateRange vrnd_range = buster_x86_metadata_lookup_mnemonic(S8("VRNDSCALEPS"));
        BusterX86MetadataForm sae_form = {0};
        u32 sae_id = 0;
        bool found_sae = x86_64_metadata_test_first_form_with_decorators(vrnd_range, BUSTER_X86_METADATA_DECORATOR_SAE, &sae_id,
                                                                          &sae_form);
        signature_count = 0;
        feature = (String8){0};
        built = found_sae && x86_64_metadata_test_build_signatures(sae_id, false, signatures, atom_buffers, width_buffers, &feature,
                                                                    feature_buffer, sizeof(feature_buffer), &signature_count);
        features[0] = feature;
        BusterX86MetadataResolveResult sae_only = {0};
        if (built)
        {
            sae_only = x86_64_metadata_test_resolve_physical(
                S8("VRNDSCALEPS"), signatures, signature_count, features, BUSTER_ARRAY_LENGTH(features), 0, 0,
                BUSTER_X86_METADATA_DECORATOR_SAE, 0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, built && sae_only.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && sae_only.candidate_count > 0);
    }

    {
        BusterX86MetadataOperandSignature no_operands[1] = {x86_64_metadata_test_physical_signature(
            BUSTER_X86_METADATA_OPERAND_REGISTER, BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
            BUSTER_X86_METADATA_PHYSICAL_WIDTH_32)};
        String8 all_features[1] = {S8("*")};
        u32 ids[64] = {0};
        BusterX86MetadataResolveResult invalid_zeroing = x86_64_metadata_test_resolve_physical(
            S8("ADD"), no_operands, 1, all_features, BUSTER_ARRAY_LENGTH(all_features), 0, 0,
            BUSTER_X86_METADATA_DECORATOR_ZEROING, 0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, invalid_zeroing.status == BUSTER_X86_METADATA_RESOLVE_INVALID_INPUT);
        BusterX86MetadataResolveResult invalid_broadcast = x86_64_metadata_test_resolve_physical(
            S8("VADDPS"), no_operands, 1, all_features, BUSTER_ARRAY_LENGTH(all_features), 0, 0,
            BUSTER_X86_METADATA_DECORATOR_BROADCAST, 0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, invalid_broadcast.status == BUSTER_X86_METADATA_RESOLVE_INVALID_INPUT);
    }

    {
        // Canonical effective feature names are conjunctive for the AVX10
        // composite and do not fall back to the generic AVX512 extension.
        BusterX86MetadataCandidateRange range = buster_x86_metadata_lookup_iform(
            S8("VCVTBF42HF8_XMMhf8_MASKmskw_XMMbf4_AVX512"));
        u32 form_id = 0;
        BusterX86MetadataForm form = {0};
        bool found = x86_64_metadata_test_first_normalized_form(range, &form_id, &form);
        BusterX86MetadataOperandSignature signatures[16] = {0};
        char8 atom_buffers[16][128] = {0};
        char8 width_buffers[16][128] = {0};
        char8 feature_buffer[256] = {0};
        String8 raw_feature = {0};
        u32 signature_count = 0;
        bool built = found && x86_64_metadata_test_build_signatures(form_id, false, signatures, atom_buffers, width_buffers,
                                                                      &raw_feature, feature_buffer, sizeof(feature_buffer),
                                                                      &signature_count);
        String8 enabled_features[2] = {S8("avx10.2"), S8("avx10-v1-aux")};
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult enabled = {0};
        BusterX86MetadataResolveResult disabled = {0};
        if (built)
        {
            enabled = x86_64_metadata_test_resolve_physical(S8("VCVTBF42HF8"), signatures, signature_count, enabled_features,
                                                             BUSTER_ARRAY_LENGTH(enabled_features), 0, 0, form.decorator_flags,
                                                             0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
            String8 disabled_features[1] = {S8("avx10.2")};
            disabled = x86_64_metadata_test_resolve_physical(S8("VCVTBF42HF8"), signatures, signature_count, disabled_features,
                                                              BUSTER_ARRAY_LENGTH(disabled_features), 0, 0, form.decorator_flags,
                                                              0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, built && enabled.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && enabled.candidate_count > 0);
        BUSTER_TEST(arguments, built && disabled.status == BUSTER_X86_METADATA_RESOLVE_UNAVAILABLE_TARGET_FEATURE);

        BusterX86MetadataForm wide_form = {0};
        u32 wide_form_id = 0;
        bool found_wide = false;
        BusterX86MetadataCandidateRange wide_range = buster_x86_metadata_lookup_iform(
            S8("VCVTBF42HF8_ZMMhf8_MASKmskw_YMMbf4_AVX512"));
        for (u32 position = 0; position < wide_range.count && !found_wide; position += 1)
        {
            u32 candidate_id = 0;
            BusterX86MetadataForm candidate_form = {0};
            found_wide = buster_x86_metadata_candidate_at(wide_range, position, &candidate_id) &&
                         buster_x86_metadata_form(candidate_id, &candidate_form) &&
                         candidate_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                         x86_64_metadata_test_string_equal(candidate_form.isa_set, S8("AVX10_V2_AUX_512"));
            if (found_wide)
            {
                wide_form_id = candidate_id;
                wide_form = candidate_form;
            }
        }
        BusterX86MetadataOperandSignature wide_signatures[16] = {0};
        char8 wide_atom_buffers[16][128] = {0};
        char8 wide_width_buffers[16][128] = {0};
        char8 wide_feature_buffer[256] = {0};
        String8 wide_raw_feature = {0};
        u32 wide_signature_count = 0;
        bool built_wide = found_wide && x86_64_metadata_test_build_signatures(
                                             wide_form_id, false, wide_signatures, wide_atom_buffers, wide_width_buffers,
                                             &wide_raw_feature, wide_feature_buffer, sizeof(wide_feature_buffer),
                                             &wide_signature_count);
        String8 wide_features[3] = {S8("avx10.2"), S8("avx10-v1-aux"), S8("avx10-512")};
        u32 wide_ids[128] = {0};
        BusterX86MetadataResolveResult wide_enabled = {0};
        BusterX86MetadataResolveResult wide_disabled = {0};
        if (built_wide)
        {
            wide_enabled = x86_64_metadata_test_resolve_physical(S8("VCVTBF42HF8"), wide_signatures, wide_signature_count,
                                                                  wide_features, BUSTER_ARRAY_LENGTH(wide_features), 0, 0,
                                                                  wide_form.decorator_flags, 0, 0, false, false, wide_ids,
                                                                  BUSTER_ARRAY_LENGTH(wide_ids));
            String8 missing_wide_feature[2] = {S8("avx10.2"), S8("avx10-v1-aux")};
            wide_disabled = x86_64_metadata_test_resolve_physical(
                S8("VCVTBF42HF8"), wide_signatures, wide_signature_count, missing_wide_feature,
                BUSTER_ARRAY_LENGTH(missing_wide_feature), 0, 0, wide_form.decorator_flags, 0, 0, false, false, wide_ids,
                BUSTER_ARRAY_LENGTH(wide_ids));
        }
        BUSTER_TEST(arguments, built_wide && wide_enabled.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   wide_enabled.candidate_count > 0);
        BUSTER_TEST(arguments, built_wide && wide_disabled.status == BUSTER_X86_METADATA_RESOLVE_UNAVAILABLE_TARGET_FEATURE);
    }

    {
        BusterX86MetadataCandidateRange movrs_range = buster_x86_metadata_lookup_mnemonic(S8("VMOVRSB"));
        BusterX86MetadataForm movrs_form = {0};
        u32 movrs_id = 0;
        bool found_movrs = x86_64_metadata_test_first_normalized_form(movrs_range, &movrs_id, &movrs_form);
        BusterX86MetadataOperandSignature signatures[16] = {0};
        char8 atom_buffers[16][128] = {0};
        char8 width_buffers[16][128] = {0};
        char8 feature_buffer[256] = {0};
        String8 raw_feature = {0};
        u32 signature_count = 0;
        bool built = found_movrs && x86_64_metadata_test_build_signatures(movrs_id, false, signatures, atom_buffers, width_buffers,
                                                                           &raw_feature, feature_buffer, sizeof(feature_buffer),
                                                                           &signature_count);
        String8 features[2] = {S8("avx10.1"), S8("movrs")};
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult enabled = {0};
        BusterX86MetadataResolveResult disabled = {0};
        if (built)
        {
            enabled = x86_64_metadata_test_resolve_physical(S8("VMOVRSB"), signatures, signature_count, features,
                                                             BUSTER_ARRAY_LENGTH(features), 0, 0, movrs_form.decorator_flags, 0, 0,
                                                             false, false, ids, BUSTER_ARRAY_LENGTH(ids));
            String8 missing_feature[1] = {S8("avx10.1")};
            disabled = x86_64_metadata_test_resolve_physical(S8("VMOVRSB"), signatures, signature_count, missing_feature,
                                                              BUSTER_ARRAY_LENGTH(missing_feature), 0, 0, movrs_form.decorator_flags,
                                                              0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, built && enabled.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && enabled.candidate_count > 0);
        BUSTER_TEST(arguments, built && disabled.status == BUSTER_X86_METADATA_RESOLVE_UNAVAILABLE_TARGET_FEATURE);
    }

    {
        // The result reports missing APX/AMX attributes independently from
        // decorator bits, so an attribute diagnostic is never empty.
        BusterX86MetadataCandidateRange add_range = buster_x86_metadata_lookup_mnemonic(S8("ADD"));
        u32 add_id = 0;
        BusterX86MetadataForm add_form = {0};
        bool found_add = x86_64_metadata_test_first_form_with_extension(add_range, S8("BASE"), &add_id, &add_form);
        BusterX86MetadataOperandSignature signatures[16] = {0};
        char8 atom_buffers[16][128] = {0};
        char8 width_buffers[16][128] = {0};
        char8 feature_buffer[256] = {0};
        String8 feature = {0};
        u32 signature_count = 0;
        bool built = found_add && x86_64_metadata_test_build_signatures(add_id, false, signatures, atom_buffers, width_buffers,
                                                                         &feature, feature_buffer, sizeof(feature_buffer),
                                                                         &signature_count);
        String8 features[1] = {feature};
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult missing_apx = {0};
        BusterX86MetadataResolveResult missing_amx = {0};
        if (built)
        {
            missing_apx = x86_64_metadata_test_resolve_physical(S8("ADD"), signatures, signature_count, features,
                                                                 BUSTER_ARRAY_LENGTH(features), 0, 0, 0, BUSTER_X86_METADATA_APX,
                                                                 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
            missing_amx = x86_64_metadata_test_resolve_physical(S8("ADD"), signatures, signature_count, features,
                                                                 BUSTER_ARRAY_LENGTH(features), 0, 0, 0, 0,
                                                                 BUSTER_X86_METADATA_AMX_TILE_REGISTER, false, false, ids,
                                                                 BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, built && missing_apx.status == BUSTER_X86_METADATA_RESOLVE_UNSUPPORTED_DECORATOR &&
                                   missing_apx.unsupported_apx_flags != 0 && missing_apx.unsupported_decorator_flags == 0);
        BUSTER_TEST(arguments, built && missing_amx.status == BUSTER_X86_METADATA_RESOLVE_UNSUPPORTED_DECORATOR &&
                                   missing_amx.unsupported_amx_flags != 0 && missing_amx.unsupported_decorator_flags == 0);
    }

    {
        u32 known_class = 0;
        u32 known_width = 0;
        u32 known_both = 0;
        u32 conservative_unknown = 0;
        for (u32 form_id = 0; form_id < buster_x86_metadata_form_count(); form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
            {
                BusterX86MetadataOperand normalized_operand = {0};
                if (!buster_x86_metadata_operand(form_id, operand_index, &normalized_operand) || !normalized_operand.visible) continue;
                bool class_known = normalized_operand.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_NONE &&
                                   normalized_operand.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_UNKNOWN &&
                                   normalized_operand.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
                bool width_known = normalized_operand.physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN &&
                                   normalized_operand.physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_ANY;
                known_class += class_known;
                known_width += width_known;
                known_both += class_known && width_known;
                conservative_unknown += !class_known || !width_known;
            }
        }
        BUSTER_TEST(arguments, known_class > 0 && known_width > 0 && known_both > 0 && conservative_unknown > 0);
    }

    {
        // The audit is a complete capability ledger, not a best-effort
        // sample.  Capacity and row retrieval are both part of `complete`.
        u32 ledger_capacity = 11013;
        BusterX86MetadataCoverageLedgerEntry* ledger =
            arena_allocate(arguments->arena, BusterX86MetadataCoverageLedgerEntry, ledger_capacity);
        BusterX86MetadataCoverageAuditResult no_storage = buster_x86_metadata_coverage_audit(0, 0);
        BusterX86MetadataCoverageAuditResult short_storage = buster_x86_metadata_coverage_audit(ledger, 11012);
        BusterX86MetadataCoverageAuditResult audit = buster_x86_metadata_coverage_audit(ledger, ledger_capacity);
        String8 const cohort_tokens[] = {
            S8_INITIALIZER("ONE()"), S8_INITIALIZER("IGNORE66()"), S8_INITIALIZER("IMMUNE66()"),
            S8_INITIALIZER("LZCNT=1"), S8_INITIALIZER("TZCNT=1"), S8_INITIALIZER("CLDEMOTE=1"),
            S8_INITIALIZER("CET=1"), S8_INITIALIZER("PREFETCHIT=1"), S8_INITIALIZER("PREFETCHRST=1"),
        };
        u32 cohort_counts[BUSTER_ARRAY_LENGTH(cohort_tokens)] = {0};
        u32 cohort_all_counts[BUSTER_ARRAY_LENGTH(cohort_tokens)] = {0};
        u32 cohort_privileged[BUSTER_ARRAY_LENGTH(cohort_tokens)] = {0};
        u32 cohort_not64[BUSTER_ARRAY_LENGTH(cohort_tokens)] = {0};
        u32 cohort_capable[BUSTER_ARRAY_LENGTH(cohort_tokens)] = {0};
        u32 cohort_emitted[BUSTER_ARRAY_LENGTH(cohort_tokens)] = {0};
        // Both cohort sweeps below need the same per-form token answers, so
        // resolve each form's pattern against the nine tokens once.
        BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(cohort_tokens) <= 16);
        u16* cohort_masks = arena_allocate(arguments->arena, u16, audit.entry_count ? audit.entry_count : 1);
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            cohort_masks[form_id] = 0;
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            for (u32 cohort = 0; cohort < BUSTER_ARRAY_LENGTH(cohort_tokens); cohort += 1)
            {
                if (x86_64_metadata_test_pattern_has_token(form.pattern, cohort_tokens[cohort]))
                {
                    cohort_masks[form_id] |= (u16)(1u << cohort);
                }
            }
        }
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            for (u32 cohort = 0; cohort < BUSTER_ARRAY_LENGTH(cohort_tokens); cohort += 1)
            {
                if (cohort_masks[form_id] & (1u << cohort))
                {
                    cohort_all_counts[cohort] += 1;
                    cohort_counts[cohort] += form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED;
                    cohort_privileged[cohort] += form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED;
                    cohort_not64[cohort] += form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64;
                    cohort_capable[cohort] += ledger[form_id].encoder_capable;
                    cohort_emitted[cohort] += ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED;
                }
            }
        }
        static u32 const cohort_all_expected[] = {200, 105, 34, 2, 2, 1, 4, 2, 1};
        static u32 const cohort_normalized_expected[] = {200, 103, 26, 2, 2, 1, 4, 2, 1};
        static u32 const cohort_privileged_expected[] = {0, 1, 0, 0, 0, 0, 0, 0, 0};
        bool cohort_rows_consistent = true;
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            for (u32 cohort = 0; cohort < BUSTER_ARRAY_LENGTH(cohort_tokens); cohort += 1)
            {
                if (!(cohort_masks[form_id] & (1u << cohort))) continue;
                BusterX86MetadataCoverageLedgerEntry entry = ledger[form_id];
                bool row_consistent = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                                       entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED &&
                                       entry.blocker == BUSTER_X86_METADATA_BLOCKER_NONE && entry.encoder_capable;
                row_consistent |= form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED &&
                                  entry.disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED &&
                                  entry.blocker == BUSTER_X86_METADATA_BLOCKER_PRIVILEGED && entry.encoder_capable && entry.policy_excluded;
                row_consistent |= form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64 &&
                                  entry.disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED &&
                                  entry.blocker == BUSTER_X86_METADATA_BLOCKER_NOT64 && !entry.encoder_capable;
                cohort_rows_consistent &= row_consistent;
            }
        }
        bool cohort_counts_match = cohort_rows_consistent;
        for (u32 cohort = 0; cohort < BUSTER_ARRAY_LENGTH(cohort_tokens); cohort += 1)
        {
            cohort_counts_match &= cohort_all_counts[cohort] == cohort_all_expected[cohort];
            cohort_counts_match &= cohort_counts[cohort] == cohort_normalized_expected[cohort];
            cohort_counts_match &= cohort_privileged[cohort] == cohort_privileged_expected[cohort];
            cohort_counts_match &= cohort_all_counts[cohort] == cohort_counts[cohort] + cohort_privileged[cohort] + cohort_not64[cohort];
            cohort_counts_match &= cohort_capable[cohort] == cohort_counts[cohort] + cohort_privileged[cohort];
            cohort_counts_match &= cohort_emitted[cohort] == cohort_counts[cohort];
        }
        BUSTER_TEST(arguments, cohort_counts_match);
        BUSTER_TEST(arguments, !no_storage.complete && no_storage.required_entry_count == 11013 && no_storage.entry_count == 0);
        BUSTER_TEST(arguments, !short_storage.complete && short_storage.entry_count == 11012);
        BUSTER_TEST(arguments, audit.complete && !audit.duplicate_form_id && !audit.duplicate_stable_hash &&
                                   audit.entry_count == 11013 && audit.normalized_entry_count == 10636);
        BUSTER_TEST(arguments, audit.emitted_count == 9982 && audit.blocked_count == 1031 &&
                                   audit.disposition_counts[BUSTER_X86_METADATA_COVERAGE_EMITTED] == 9982 &&
                                   audit.disposition_counts[BUSTER_X86_METADATA_COVERAGE_BLOCKED] == 1031);
        BUSTER_TEST(arguments, audit.encoder_capable_count == 10090 && audit.policy_excluded_count == 377 &&
                                   audit.explicitly_unsupported_count == 268 && audit.schema_inexpressible_count == 654);

        u32 expected_families[BUSTER_X86_METADATA_ENCODER_COUNT] = {1812, 293, 5, 1549, 176, 6728, 49, 24};
        u32 expected_family_emitted[BUSTER_X86_METADATA_ENCODER_COUNT] = {1466, 156, 5, 1520, 176, 6586, 49, 24};
        u32 expected_family_blocked[BUSTER_X86_METADATA_ENCODER_COUNT] = {346, 137, 0, 29, 0, 142, 0, 0};
        bool family_counts_match = true;
        for (u32 family = 0; family < BUSTER_X86_METADATA_ENCODER_COUNT; family += 1)
        {
            family_counts_match &= audit.family_counts[family] == expected_families[family];
            family_counts_match &= audit.family_emitted_counts[family] == expected_family_emitted[family];
            family_counts_match &= audit.family_blocked_counts[family] == expected_family_blocked[family];
        }
        BUSTER_TEST(arguments, family_counts_match);

        u32 expected_blockers[BUSTER_X86_METADATA_COVERAGE_BLOCKER_COUNT] = {9982, 268, 108, 199, 0, 249, 5, 2, 48, 152, 0, 0};
        bool blocker_counts_match = true;
        for (u32 blocker = 0; blocker < BUSTER_X86_METADATA_COVERAGE_BLOCKER_COUNT; blocker += 1)
            blocker_counts_match &= audit.blocker_counts[blocker] == expected_blockers[blocker];
        BUSTER_TEST(arguments, blocker_counts_match);

        static struct
        {
            u32 form_id;
            u64 stable_hash;
        } const privileged_valid64_inventory[] = {
            {423, UINT64_C(0x000fdb86f1aadc81)},
            {424, UINT64_C(0x3d1ebefba8414734)},
            {425, UINT64_C(0x21718e05ae7ae52c)},
            {433, UINT64_C(0x72251dc6dc93950e)},
            {434, UINT64_C(0x2694fd3f416fc783)},
            {435, UINT64_C(0x83699f775c194bff)},
            {436, UINT64_C(0x28323239c95dbc87)},
            {454, UINT64_C(0x62b4a76cbfdcfa25)},
            {1750, UINT64_C(0xa97e8fa2c0259e4e)},
            {1847, UINT64_C(0x08ae34d52f01f12e)},
            {1848, UINT64_C(0x6ea84c686eb80660)},
            {1849, UINT64_C(0x2b74c486973d0549)},
            {2841, UINT64_C(0x6f4e95860ffd415c)},
            {2842, UINT64_C(0xa066f52336a3c8d4)},
            {2932, UINT64_C(0xe8cdbc59679f6759)},
            {2933, UINT64_C(0xba4853fea6c84638)},
            {7935, UINT64_C(0x40267c8c36d26c20)},
            {7944, UINT64_C(0x2fe685c7fcc2d261)},
            {7947, UINT64_C(0x9ae8ec3ad39cee8e)},
            {7948, UINT64_C(0x7ccabdbc15624f93)},
            {8014, UINT64_C(0x313df55a468d4c4e)},
            {8015, UINT64_C(0x0532aac23cd1abca)},
            {8016, UINT64_C(0x50a00ae85f53e21e)},
            {8017, UINT64_C(0x2b1e460fb338458e)},
            {8018, UINT64_C(0x2d03bc4e4a951fef)},
            {8099, UINT64_C(0xefd1dddbe29380f1)},
            {8100, UINT64_C(0xda6c941b1c643db9)},
            {8714, UINT64_C(0x9c48d051528adbbc)},
            {8818, UINT64_C(0xd727629e19927b1e)},
            {8819, UINT64_C(0xdaaccfb478023c56)},
            {8820, UINT64_C(0x2fee49937e0f70b1)},
            {8821, UINT64_C(0x8fb0189c52ba878f)},
            {8822, UINT64_C(0x33b06f287ee49f08)},
            {8824, UINT64_C(0x1aa8e61e5736a61e)},
            {8844, UINT64_C(0x6c8e58373bbce760)},
            {8888, UINT64_C(0xd0eff84140c1c18c)},
            {8889, UINT64_C(0xb79664c7df22e466)},
            {8890, UINT64_C(0xc7954cb1933ef918)},
            {8891, UINT64_C(0x740bd4f9301baeda)},
            {8892, UINT64_C(0x2fd845633b0bc38c)},
            {8893, UINT64_C(0xa47c62856f645e26)},
            {9064, UINT64_C(0x41dd5fc08f8ec534)},
            {9065, UINT64_C(0x72885d7dee189598)},
            {9066, UINT64_C(0xe36fed6c3bd872c9)},
            {9067, UINT64_C(0x2474767f901ed0fd)},
            {9496, UINT64_C(0x477a2d6c1d16e197)},
            {9497, UINT64_C(0x68d2140ab91e7c01)},
            {9498, UINT64_C(0x2052d7d6d17aaa2e)},
            {9499, UINT64_C(0x0773dda93b7427d0)},
            {9504, UINT64_C(0xc92bd45af3b599c1)},
            {9508, UINT64_C(0x97f38c8bd9dd0f7e)},
            {9509, UINT64_C(0xb5f3fc6194aba4ab)},
            {9521, UINT64_C(0x3c26e2370b8dfa23)},
            {9522, UINT64_C(0x985ae895a04329f5)},
            {9523, UINT64_C(0xb7776545562fdd93)},
            {9524, UINT64_C(0xf629709fd45ed2d1)},
            {9589, UINT64_C(0xae9065eb96d145da)},
            {9590, UINT64_C(0x8c09dad671924c24)},
            {9591, UINT64_C(0x0e896428f35aa669)},
            {9592, UINT64_C(0xdac2c95de0b3ab3f)},
            {9595, UINT64_C(0xac905f115efd43c3)},
            {9599, UINT64_C(0x3a6c3dca96c7359e)},
            {9600, UINT64_C(0x0af96989a90ec18c)},
            {9601, UINT64_C(0x5c8d272399e52dcb)},
            {9604, UINT64_C(0xcca8306a60fe6a09)},
            {9605, UINT64_C(0x04811f7a925fadbf)},
            {10069, UINT64_C(0x55e165834b72d02b)},
            {10082, UINT64_C(0x828ccf6ffb8c59b8)},
            {10083, UINT64_C(0x94dce232d97a5c8f)},
            {10084, UINT64_C(0xbea474e4102aa7f3)},
            {10122, UINT64_C(0x54cfc102161a412b)},
            {10124, UINT64_C(0xede5a7a43e0d5222)},
            {10126, UINT64_C(0x41f5fcac9b7a24e9)},
            {10128, UINT64_C(0xc5cc6c21a9fb6a08)},
            {10129, UINT64_C(0x78c5f4b02c063ce2)},
            {10131, UINT64_C(0x22e0ab5139918c10)},
            {10136, UINT64_C(0x9a91329cfd49cc15)},
            {10410, UINT64_C(0x87014095840a2c9f)},
            {10411, UINT64_C(0x47ccb4893dde9b19)},
            {10606, UINT64_C(0x2a17b4cffa7e84b7)},
            {10607, UINT64_C(0xa4b74c4baed2355b)},
            {10610, UINT64_C(0xff9b508fe1bccb9e)},
            {10611, UINT64_C(0x3acc002c3e56cd5d)},
            {10971, UINT64_C(0x74c3c5197ee56eef)},
            {10993, UINT64_C(0x98bc9422134ea928)},
            {10995, UINT64_C(0x37ad3945477649b6)},
            {11009, UINT64_C(0x9211e7f7e752f5d6)},
            {11010, UINT64_C(0xfa1cb7c603e5dca2)},
            {11011, UINT64_C(0x392090cc75f865d4)},
            {11012, UINT64_C(0xdd19095836024398)},
        };
        bool privileged_inventory_traceable = BUSTER_ARRAY_LENGTH(privileged_valid64_inventory) == 90;
        for (u32 inventory_index = 0; inventory_index < BUSTER_ARRAY_LENGTH(privileged_valid64_inventory); inventory_index += 1)
        {
            u32 form_id = privileged_valid64_inventory[inventory_index].form_id;
            BusterX86MetadataForm form = {0};
            bool retrieved = form_id < audit.entry_count && buster_x86_metadata_form(form_id, &form);
            bool valid64 = retrieved && buster_x86_metadata_test_execution_mode_matches(
                                           form.mode_flags, form.coverage_class, false, BUSTER_X86_METADATA_EXECUTION_MODE_64);
            BusterX86MetadataCoverageLedgerEntry entry = form_id < audit.entry_count ? ledger[form_id] : (BusterX86MetadataCoverageLedgerEntry){0};
            privileged_inventory_traceable &= retrieved && form.id == form_id &&
                                              form.stable_hash == privileged_valid64_inventory[inventory_index].stable_hash &&
                                              x86_64_metadata_test_string_equal(form.cpl, S8("0")) && valid64 &&
                                              form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED && entry.encoder_capable &&
                                              entry.policy_excluded && entry.blocker == BUSTER_X86_METADATA_BLOCKER_PRIVILEGED;
        }
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form) || !x86_64_metadata_test_string_equal(form.cpl, S8("0")) ||
                !buster_x86_metadata_test_execution_mode_matches(form.mode_flags, form.coverage_class, false,
                                                                  BUSTER_X86_METADATA_EXECUTION_MODE_64))
            {
                continue;
            }
            bool listed = false;
            for (u32 inventory_index = 0; inventory_index < BUSTER_ARRAY_LENGTH(privileged_valid64_inventory); inventory_index += 1)
                listed |= privileged_valid64_inventory[inventory_index].form_id == form_id;
            privileged_inventory_traceable &= listed;
        }
        BUSTER_TEST(arguments, privileged_inventory_traceable);

        u32 privileged_capable = 0;
        u32 privileged_blocked = 0;
        u32 not64_capable = 0;
        u32 privileged_total = 0;
        u32 privileged_valid64 = 0;
        u32 privileged_valid64_capable = 0;
        u32 privileged_valid64_blocked = 0;
        u32 privileged_not64 = 0;
        u32 apx_total = 0;
        u32 apx_emitted = 0;
        u32 apx_blocked = 0;
        u32 apx_scc_total = 0;
        u32 apx_scc_emitted = 0;
        u32 evex_r4_total = 0;
        u32 evex_r4_emitted = 0;
        u32 dfv_total = 0;
        u32 dfv_scc_total = 0;
        u32 emitted_bnd = 0;
        u32 emitted_control = 0;
        u32 emitted_debug = 0;
        u32 emitted_segment = 0;
        bool ledger_rows_consistent = true;
        bool dfv_scc_semantics_consistent = true;
        String8 const scc_tokens[] = {
            S8_INITIALIZER(" SCC0 "),  S8_INITIALIZER(" SCC1 "),  S8_INITIALIZER(" SCC2 "),  S8_INITIALIZER(" SCC3 "),
            S8_INITIALIZER(" SCC4 "),  S8_INITIALIZER(" SCC5 "),  S8_INITIALIZER(" SCC6 "),  S8_INITIALIZER(" SCC7 "),
            S8_INITIALIZER(" SCC8 "),  S8_INITIALIZER(" SCC9 "),  S8_INITIALIZER(" SCC10 "), S8_INITIALIZER(" SCC11 "),
            S8_INITIALIZER(" SCC12 "), S8_INITIALIZER(" SCC13 "), S8_INITIALIZER(" SCC14 "), S8_INITIALIZER(" SCC15 "),
        };
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataCoverageLedgerEntry entry = ledger[form_id];
            ledger_rows_consistent &= entry.form_id == form_id && entry.encoder_family < BUSTER_X86_METADATA_ENCODER_COUNT &&
                                     entry.disposition < BUSTER_X86_METADATA_COVERAGE_DISPOSITION_COUNT &&
                                     entry.blocker < BUSTER_X86_METADATA_COVERAGE_BLOCKER_COUNT;
            ledger_rows_consistent &= entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED
                                         ? entry.blocker == BUSTER_X86_METADATA_BLOCKER_NONE && entry.encoder_capable &&
                                               entry.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED
                                         : entry.blocker != BUSTER_X86_METADATA_BLOCKER_NONE;
            if (entry.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED)
                ledger_rows_consistent &= entry.encoder_capable == (entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED);
            BusterX86MetadataForm form = {0};
            if (buster_x86_metadata_form(form_id, &form))
            {
                bool privileged_policy = x86_64_metadata_test_string_equal(form.cpl, S8("0"));
                bool valid64 = buster_x86_metadata_test_execution_mode_matches(
                    form.mode_flags, form.coverage_class, false, BUSTER_X86_METADATA_EXECUTION_MODE_64);
                privileged_capable += form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED && valid64 && entry.encoder_capable;
                privileged_blocked += form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED && valid64 && !entry.encoder_capable;
                not64_capable += !valid64 && entry.encoder_capable;
                if (privileged_policy)
                {
                    privileged_total += 1;
                    if (valid64)
                    {
                        privileged_valid64 += 1;
                        privileged_valid64_capable += entry.encoder_capable;
                        privileged_valid64_blocked += !entry.encoder_capable;
                    }
                    else
                    {
                        privileged_not64 += 1;
                    }
                }
                if (form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED && (form.apx_flags & BUSTER_X86_METADATA_APX))
                {
                    apx_total += 1;
                    apx_emitted += entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED;
                    apx_blocked += entry.disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED;
                    if (x86_64_metadata_test_string_contains(form.pattern, S8("EVAPX_SCC")))
                    {
                        apx_scc_total += 1;
                        apx_scc_emitted += entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED;
                    }
                }
            }
            if (x86_64_metadata_test_string_contains(form.pattern, S8("EVEXR4_ONE()")))
            {
                evex_r4_total += 1;
                evex_r4_emitted += entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED;
            }
            bool form_requires_dfv = buster_x86_metadata_form_requires_dfv(form_id);
            bool pattern_has_evapx_scc = x86_64_metadata_test_string_contains(form.pattern, S8("EVAPX_SCC()"));
            u32 parsed_scc_count = 0;
            // Every scc token contains " SCC", so one probe clears the sixteen
            // token scans for the forms (nearly all) with no SCC atom at all.
            if (x86_64_metadata_test_string_contains(form.pattern, S8(" SCC")))
            {
                for (u32 scc_index = 0; scc_index < BUSTER_ARRAY_LENGTH(scc_tokens); scc_index += 1)
                    parsed_scc_count += x86_64_metadata_test_string_contains(form.pattern, scc_tokens[scc_index]);
            }
            bool parsed_scc = parsed_scc_count == 1;
            bool form_has_apx_scc = (form.apx_flags & BUSTER_X86_METADATA_APX_SCC) != 0;
            bool row_dfv_scc_coherent = form_requires_dfv == pattern_has_evapx_scc &&
                                        pattern_has_evapx_scc == parsed_scc && parsed_scc == form_has_apx_scc;
            dfv_total += form_requires_dfv;
            dfv_scc_total += form_requires_dfv && row_dfv_scc_coherent;
            dfv_scc_semantics_consistent &= row_dfv_scc_coherent;
            if (entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED && buster_x86_metadata_form(form_id, &form))
            {
                // Count visible physical classes only on emitted normalized
                // rows.  This proves which non-GPR register classes are
                // reachable from the current 8,428-form schema rather than
                // treating schema-blocked classes as public coverage.
                bool has_bnd = false;
                bool has_control = false;
                bool has_debug = false;
                bool has_segment = false;
                for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
                {
                    BusterX86MetadataOperand emitted_operand = {0};
                    if (!buster_x86_metadata_operand(form_id, operand_index, &emitted_operand) || !emitted_operand.visible)
                    {
                        continue;
                    }
                    has_bnd |= emitted_operand.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_BND;
                    has_control |= emitted_operand.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_CONTROL;
                    has_debug |= emitted_operand.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_DEBUG;
                    has_segment |= emitted_operand.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT;
                }
                emitted_bnd += has_bnd;
                emitted_control += has_control;
                emitted_debug += has_debug;
                emitted_segment += has_segment;
            }
        }
        BUSTER_TEST(arguments, ledger_rows_consistent);
        BUSTER_TEST(arguments, emitted_bnd == 0 && emitted_control == 0 && emitted_debug == 0 && emitted_segment == 4);
        BUSTER_TEST(arguments, privileged_capable == 90);
        BUSTER_TEST(arguments, privileged_blocked == 0);
        BUSTER_TEST(arguments, not64_capable == 18);
        BUSTER_TEST(arguments, privileged_total == 109 && privileged_valid64 == 90 && privileged_valid64_capable == 90 &&
                                   privileged_valid64_blocked == 0 && privileged_not64 == 19);
        BUSTER_TEST(arguments, apx_total == 2465);
        BUSTER_TEST(arguments, apx_emitted == 2414 && apx_blocked == 51);
        BUSTER_TEST(arguments, apx_scc_total == 640 && apx_scc_emitted == 640);
        BUSTER_TEST(arguments, evex_r4_total == 97 && evex_r4_emitted == 97);
        BUSTER_TEST(arguments, dfv_total == 640 && dfv_scc_total == 640 && dfv_scc_semantics_consistent);
        BUSTER_TEST(arguments, ledger[10069].blocker == BUSTER_X86_METADATA_BLOCKER_PRIVILEGED && ledger[10069].policy_excluded &&
                                   ledger[10069].encoder_capable);
        BusterX86MetadataForm xop_form = {0};
        BusterX86MetadataOperand xop_operand = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_form(326, &xop_form) && xop_form.prefix_kind == BUSTER_X86_METADATA_PREFIX_XOP &&
                                   xop_form.encoder_family == BUSTER_X86_METADATA_ENCODER_XOP &&
                                   buster_x86_metadata_operand(326, 0, &xop_operand) &&
                                   xop_operand.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR &&
                                   xop_operand.physical_width_flags == (BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 |
                                                                        BUSTER_X86_METADATA_PHYSICAL_WIDTH_64));
        BusterX86MetadataCoverage xop_coverage = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_coverage(326, &xop_coverage) &&
                                   xop_coverage.encoder_family == BUSTER_X86_METADATA_ENCODER_XOP);

        u32 branch_hint_count = 0;
        bool branch_hint_blocked = true;
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            if (form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                x86_64_metadata_test_string_contains(form.pattern, S8("BRANCH_HINT")))
            {
                branch_hint_count += 1;
                branch_hint_blocked &= ledger[form_id].blocker == BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
            }
        }
        BUSTER_TEST(arguments, branch_hint_count > 0 && branch_hint_blocked);
    }

    {
        // Privileged/system rows are directly encodable when the caller opts
        // into the policy class.  These exact forms cover the legacy prefix,
        // ModRM, address-size, and no-REX2 shapes represented by the 90-row
        // valid-64 inventory; public assembly tests below exercise routing.
        String8 invlpgb_feature[] = {S8("invlpgb")};
        String8 monitor_feature[] = {S8("monitor")};
        String8 invpcid_feature[] = {S8("invpcid")};
        String8 vmx_feature[] = {S8("vmx")};
        u8 output[32] = {0};
        BusterX86MetadataRelocation relocations[8] = {0};

        BusterX86MetadataPhysicalQuery invlpgb_addr32_query = x86_64_metadata_test_physical_query(
            S8("INVLPGB"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, invlpgb_feature, BUSTER_ARRAY_LENGTH(invlpgb_feature));
        invlpgb_addr32_query.include_privileged = true;
        invlpgb_addr32_query.address_size = 32;
        BusterX86MetadataEmitResult invlpgb_addr32 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = invlpgb_addr32_query, .form_id = 423, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, invlpgb_addr32.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && invlpgb_addr32.byte_count == 4 &&
                                   x86_64_metadata_test_bytes_equal(output, invlpgb_addr32.byte_count,
                                                                     (u8 const[]){0x67, 0x0f, 0x01, 0xfe}, 4));
        invlpgb_addr32_query.address_size = 64;
        BusterX86MetadataEmitResult invlpgb_addr32_mismatch = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = invlpgb_addr32_query, .form_id = 423, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, invlpgb_addr32_mismatch.status == BUSTER_X86_METADATA_ENCODE_ADDRESSING &&
                                   invlpgb_addr32_mismatch.byte_count == 0 && invlpgb_addr32_mismatch.relocation_count == 0);
        BusterX86MetadataPhysicalQuery invlpgb_addr64_query = invlpgb_addr32_query;
        BusterX86MetadataEmitResult invlpgb_addr64 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = invlpgb_addr64_query, .form_id = 424, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, invlpgb_addr64.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && invlpgb_addr64.byte_count == 3 &&
                                   x86_64_metadata_test_bytes_equal(output, invlpgb_addr64.byte_count, (u8 const[]){0x0f, 0x01, 0xfe}, 3));
        invlpgb_addr64_query.address_size = 32;
        BusterX86MetadataEmitResult invlpgb_addr64_mismatch = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = invlpgb_addr64_query, .form_id = 424, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, invlpgb_addr64_mismatch.status == BUSTER_X86_METADATA_ENCODE_ADDRESSING &&
                                   invlpgb_addr64_mismatch.byte_count == 0 && invlpgb_addr64_mismatch.relocation_count == 0);

        BusterX86MetadataPhysicalQuery monitor_addr64_query = x86_64_metadata_test_physical_query(
            S8("MONITOR"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, monitor_feature, BUSTER_ARRAY_LENGTH(monitor_feature));
        monitor_addr64_query.include_privileged = true;
        BusterX86MetadataEmitResult monitor_addr64 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = monitor_addr64_query, .form_id = 9599, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, monitor_addr64.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && monitor_addr64.byte_count == 3 &&
                                   x86_64_metadata_test_bytes_equal(output, monitor_addr64.byte_count, (u8 const[]){0x0f, 0x01, 0xc8}, 3));
        BusterX86MetadataPhysicalQuery monitor_addr32_query = monitor_addr64_query;
        monitor_addr32_query.address_size = 32;
        BusterX86MetadataEmitResult monitor_addr32 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = monitor_addr32_query, .form_id = 9600, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, monitor_addr32.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && monitor_addr32.byte_count == 4 &&
                                   x86_64_metadata_test_bytes_equal(output, monitor_addr32.byte_count,
                                                                     (u8 const[]){0x67, 0x0f, 0x01, 0xc8}, 4));

        String8 empty_features[] = {0};
        BusterX86MetadataPhysicalQuery wbinvd_query = x86_64_metadata_test_physical_query(
            S8("WBINVD"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, empty_features, 0);
        wbinvd_query.include_privileged = true;
        BusterX86MetadataEmitResult wbinvd = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = wbinvd_query, .form_id = 9064, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, wbinvd.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && wbinvd.byte_count == 2 &&
                                   x86_64_metadata_test_bytes_equal(output, wbinvd.byte_count, (u8 const[]){0x0f, 0x09}, 2));
        wbinvd_query.attributes.repne = true;
        BusterX86MetadataEmitResult repne_wbinvd = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = wbinvd_query, .form_id = 9065, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, repne_wbinvd.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && repne_wbinvd.byte_count == 3 &&
                                   x86_64_metadata_test_bytes_equal(output, repne_wbinvd.byte_count, (u8 const[]){0xf2, 0x0f, 0x09}, 3));
        wbinvd_query.attributes.repne = false;
        wbinvd_query.attributes.rep = true;
        BusterX86MetadataEmitResult rep_wbinvd = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = wbinvd_query, .form_id = 9065, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, rep_wbinvd.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION && rep_wbinvd.byte_count == 0 &&
                                   rep_wbinvd.relocation_count == 0);
        String8 wbnoinvd_feature[] = {S8("wbnoinvd")};
        BusterX86MetadataPhysicalQuery wbnoinvd_query = x86_64_metadata_test_physical_query(
            S8("WBNOINVD"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wbnoinvd_feature,
            BUSTER_ARRAY_LENGTH(wbnoinvd_feature));
        wbnoinvd_query.include_privileged = true;
        BusterX86MetadataEmitResult wbnoinvd = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = wbnoinvd_query, .form_id = 9066, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, wbnoinvd.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && wbnoinvd.byte_count == 3 &&
                                   x86_64_metadata_test_bytes_equal(output, wbnoinvd.byte_count, (u8 const[]){0xf3, 0x0f, 0x09}, 3));

        BusterX86MetadataPhysicalOperand gpr0 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
        BusterX86MetadataPhysicalOperand gpr1 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 64);
        BusterX86MetadataPhysicalOperand gpr2 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 64);
        BusterX86MetadataPhysicalOperand cr15 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_CONTROL, 15, 64);
        BusterX86MetadataPhysicalOperand dr15 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_DEBUG, 15, 64);
        BusterX86MetadataPhysicalOperand mov_cr_store[] = {cr15, gpr0};
        BusterX86MetadataPhysicalOperand mov_cr_load[] = {gpr0, cr15};
        BusterX86MetadataPhysicalOperand mov_dr_store[] = {dr15, gpr0};
        BusterX86MetadataPhysicalOperand mov_dr_load[] = {gpr0, dr15};
        u8 mov_cr_store_output[32] = {0};
        u8 mov_cr_load_output[32] = {0};
        u8 mov_dr_store_output[32] = {0};
        u8 mov_dr_load_output[32] = {0};
        BusterX86MetadataPhysicalQuery mov_cr_store_query = x86_64_metadata_test_physical_query(
            S8("MOV_CR"), mov_cr_store, BUSTER_ARRAY_LENGTH(mov_cr_store), (BusterX86MetadataPhysicalAttributes){0}, empty_features, 0);
        mov_cr_store_query.include_privileged = true;
        BusterX86MetadataPhysicalQuery mov_cr_load_query = mov_cr_store_query;
        mov_cr_load_query.mnemonic = S8("MOV_CR");
        mov_cr_load_query.operands = mov_cr_load;
        BusterX86MetadataPhysicalQuery mov_dr_store_query = mov_cr_store_query;
        mov_dr_store_query.mnemonic = S8("MOV_DR");
        mov_dr_store_query.operands = mov_dr_store;
        BusterX86MetadataPhysicalQuery mov_dr_load_query = mov_dr_store_query;
        mov_dr_load_query.operands = mov_dr_load;
        BusterX86MetadataEmitResult mov_cr_store_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = mov_cr_store_query, .form_id = 10122, .output = mov_cr_store_output, .output_capacity = sizeof(mov_cr_store_output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BusterX86MetadataEmitResult mov_cr_load_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = mov_cr_load_query, .form_id = 10124, .output = mov_cr_load_output, .output_capacity = sizeof(mov_cr_load_output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BusterX86MetadataEmitResult mov_dr_store_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = mov_dr_store_query, .form_id = 10126, .output = mov_dr_store_output, .output_capacity = sizeof(mov_dr_store_output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BusterX86MetadataEmitResult mov_dr_load_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = mov_dr_load_query, .form_id = 10128, .output = mov_dr_load_output, .output_capacity = sizeof(mov_dr_load_output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, mov_cr_store_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(mov_cr_store_output, mov_cr_store_result.byte_count,
                                                                     (u8 const[]){0x44, 0x0f, 0x22, 0xf8}, 4));
        BUSTER_TEST(arguments, mov_cr_load_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(mov_cr_load_output, mov_cr_load_result.byte_count,
                                                                     (u8 const[]){0x44, 0x0f, 0x20, 0xf8}, 4));
        BUSTER_TEST(arguments, mov_dr_store_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(mov_dr_store_output, mov_dr_store_result.byte_count,
                                                                     (u8 const[]){0x44, 0x0f, 0x23, 0xf8}, 4));
        BUSTER_TEST(arguments, mov_dr_load_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(mov_dr_load_output, mov_dr_load_result.byte_count,
                                                                     (u8 const[]){0x44, 0x0f, 0x21, 0xf8}, 4));

        BusterX86MetadataPhysicalOperand invpcid_operands[] = {
            gpr1, x86_64_metadata_test_physical_mem_base(0, 128, 0)};
        BusterX86MetadataPhysicalQuery invpcid_query = x86_64_metadata_test_physical_query(
            S8("INVPCID"), invpcid_operands, BUSTER_ARRAY_LENGTH(invpcid_operands), (BusterX86MetadataPhysicalAttributes){0},
            invpcid_feature, BUSTER_ARRAY_LENGTH(invpcid_feature));
        invpcid_query.include_privileged = true;
        BusterX86MetadataEmitResult invpcid = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = invpcid_query, .form_id = 8100, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, invpcid.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && invpcid.byte_count == 5 &&
                                   x86_64_metadata_test_bytes_equal(output, invpcid.byte_count,
                                                                     (u8 const[]){0x66, 0x0f, 0x38, 0x82, 0x08}, 5));

        BusterX86MetadataPhysicalOperand vmx_memory_operands[] = {gpr1, x86_64_metadata_test_physical_mem_base(0, 128, 0)};
        u32 vmx_memory_form_ids[] = {10993, 10995};
        u8 vmx_memory_opcodes[] = {0x80, 0x81};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(vmx_memory_form_ids); index += 1)
        {
            BusterX86MetadataPhysicalQuery query = x86_64_metadata_test_physical_query(
                index == 0 ? S8("INVEPT") : S8("INVVPID"), vmx_memory_operands, BUSTER_ARRAY_LENGTH(vmx_memory_operands),
                (BusterX86MetadataPhysicalAttributes){0}, vmx_feature, BUSTER_ARRAY_LENGTH(vmx_feature));
            query.include_privileged = true;
            BusterX86MetadataEmitResult vmx_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = query, .form_id = vmx_memory_form_ids[index], .output = output, .output_capacity = sizeof(output),
                .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
            u8 expected[] = {0x66, 0x0f, 0x38, vmx_memory_opcodes[index], 0x08};
            BUSTER_TEST(arguments, vmx_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && vmx_result.byte_count == sizeof(expected) &&
                                       x86_64_metadata_test_bytes_equal(output, vmx_result.byte_count, expected, sizeof(expected)));
        }

        BusterX86MetadataPhysicalOperand vmread_memory_operands[] = {
            x86_64_metadata_test_physical_mem_base(0, 64, 0), gpr1};
        BusterX86MetadataPhysicalOperand vmwrite_memory_operands[] = {
            gpr1, x86_64_metadata_test_physical_mem_base(0, 64, 0)};
        BusterX86MetadataPhysicalOperand vmread_register_operands[] = {gpr2, gpr1};
        BusterX86MetadataPhysicalOperand vmwrite_register_operands[] = {gpr1, gpr2};
        u32 vmx_data_form_ids[] = {10606, 10607, 10610, 10611};
        BusterX86MetadataPhysicalOperand const* vmx_data_operands[] = {
            vmread_memory_operands, vmread_register_operands, vmwrite_memory_operands, vmwrite_register_operands};
        u8 vmx_data_opcodes[] = {0x78, 0x78, 0x79, 0x79};
        u8 vmx_data_modrms[] = {0x08, 0xca, 0x08, 0xca};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(vmx_data_form_ids); index += 1)
        {
            BusterX86MetadataPhysicalQuery query = x86_64_metadata_test_physical_query(
                index < 2 ? S8("VMREAD") : S8("VMWRITE"), vmx_data_operands[index], 2,
                (BusterX86MetadataPhysicalAttributes){0}, vmx_feature, BUSTER_ARRAY_LENGTH(vmx_feature));
            query.include_privileged = true;
            BusterX86MetadataEmitResult vmx_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = query, .form_id = vmx_data_form_ids[index], .output = output, .output_capacity = sizeof(output),
                .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
            u8 expected[] = {0x0f, vmx_data_opcodes[index], vmx_data_modrms[index]};
            BUSTER_TEST(arguments, vmx_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && vmx_result.byte_count == sizeof(expected) &&
                                       x86_64_metadata_test_bytes_equal(output, vmx_result.byte_count, expected, sizeof(expected)));
        }

        // APX system forms use EVEX for EGPR extension bits, but their XED
        // fixed-width schemas keep W clear.  Keep every reachable mapping
        // byte-exact here so the generic APX width heuristic cannot regress
        // one ISA set while fixing another.
        BusterX86MetadataPhysicalOperand gpr16 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 64);
        String8 apx_enqcmd_features[] = {S8("apx"), S8("enqcmd")};
        String8 apx_invpcid_features[] = {S8("apx"), S8("invpcid")};
        String8 apx_msr_imm_features[] = {S8("apx"), S8("msr-imm")};
        String8 apx_vmx_features[] = {S8("apx"), S8("vmx")};
        String8 apx_movdir64b_features[] = {S8("apx"), S8("movdir64b")};
        BusterX86MetadataPhysicalOperand apx_descriptor_memory = x86_64_metadata_test_physical_mem_base(0, 128, 0);
        BusterX86MetadataPhysicalOperand apx_enqcmd_memory = x86_64_metadata_test_physical_mem_base(0, 32, 0);
        BusterX86MetadataPhysicalOperand apx_memory_operands[] = {gpr16, apx_descriptor_memory};
        BusterX86MetadataPhysicalOperand apx_enqcmd_operands[] = {gpr16, apx_enqcmd_memory};
        BusterX86MetadataPhysicalQuery apx_enqcmd_query = x86_64_metadata_test_physical_query(
            S8("ENQCMDS"), apx_enqcmd_operands, BUSTER_ARRAY_LENGTH(apx_enqcmd_operands),
            (BusterX86MetadataPhysicalAttributes){0}, apx_enqcmd_features, BUSTER_ARRAY_LENGTH(apx_enqcmd_features));
        apx_enqcmd_query.include_privileged = true;
        BusterX86MetadataEmitResult apx_enqcmd = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = apx_enqcmd_query, .form_id = 1750, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, apx_enqcmd.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && apx_enqcmd.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(output, apx_enqcmd.byte_count,
                                                                     (u8 const[]){0x62, 0xe4, 0x7e, 0x08, 0xf8, 0x00}, 6));
        BusterX86MetadataPhysicalQuery apx_enqcmd_user_query = apx_enqcmd_query;
        apx_enqcmd_user_query.mnemonic = S8("ENQCMD");
        apx_enqcmd_user_query.include_privileged = false;
        BusterX86MetadataEmitResult apx_enqcmd_user = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = apx_enqcmd_user_query, .form_id = 1749, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, apx_enqcmd_user.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && apx_enqcmd_user.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(output, apx_enqcmd_user.byte_count,
                                                                     (u8 const[]){0x62, 0xe4, 0x7f, 0x08, 0xf8, 0x00}, 6));

        BusterX86MetadataPhysicalQuery apx_invpcid_query = x86_64_metadata_test_physical_query(
            S8("INVPCID"), apx_memory_operands, BUSTER_ARRAY_LENGTH(apx_memory_operands),
            (BusterX86MetadataPhysicalAttributes){0}, apx_invpcid_features, BUSTER_ARRAY_LENGTH(apx_invpcid_features));
        apx_invpcid_query.include_privileged = true;
        BusterX86MetadataEmitResult apx_invpcid = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = apx_invpcid_query, .form_id = 1848, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, apx_invpcid.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && apx_invpcid.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(output, apx_invpcid.byte_count,
                                                                     (u8 const[]){0x62, 0xe4, 0x7e, 0x08, 0xf2, 0x00}, 6));

        BusterX86MetadataEmitResult apx_invept = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = (BusterX86MetadataPhysicalQuery){
                .mnemonic = S8("INVEPT"), .operands = apx_memory_operands, .operand_count = BUSTER_ARRAY_LENGTH(apx_memory_operands),
                .features = {.names = apx_vmx_features, .count = BUSTER_ARRAY_LENGTH(apx_vmx_features)},
                .address_size = 64, .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64, .include_privileged = true},
            .form_id = 1847, .output = output, .output_capacity = sizeof(output), .relocations = relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, apx_invept.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && apx_invept.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(output, apx_invept.byte_count,
                                                                     (u8 const[]){0x62, 0xe4, 0x7e, 0x08, 0xf0, 0x00}, 6));
        BusterX86MetadataEmitResult apx_invvpid = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = (BusterX86MetadataPhysicalQuery){
                .mnemonic = S8("INVVPID"), .operands = apx_memory_operands, .operand_count = BUSTER_ARRAY_LENGTH(apx_memory_operands),
                .features = {.names = apx_vmx_features, .count = BUSTER_ARRAY_LENGTH(apx_vmx_features)},
                .address_size = 64, .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64, .include_privileged = true},
            .form_id = 1849, .output = output, .output_capacity = sizeof(output), .relocations = relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, apx_invvpid.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && apx_invvpid.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(output, apx_invvpid.byte_count,
                                                                     (u8 const[]){0x62, 0xe4, 0x7e, 0x08, 0xf1, 0x00}, 6));

        BusterX86MetadataPhysicalOperand rdmsr_apx_operands[] = {gpr16, x86_64_metadata_test_physical_imm(0x1234, 32)};
        BusterX86MetadataPhysicalOperand wrmsrns_apx_operands[] = {x86_64_metadata_test_physical_imm(0x1234, 32), gpr16};
        BusterX86MetadataPhysicalQuery rdmsr_apx_query = x86_64_metadata_test_physical_query(
            S8("RDMSR"), rdmsr_apx_operands, BUSTER_ARRAY_LENGTH(rdmsr_apx_operands), (BusterX86MetadataPhysicalAttributes){0},
            apx_msr_imm_features, BUSTER_ARRAY_LENGTH(apx_msr_imm_features));
        rdmsr_apx_query.include_privileged = true;
        BusterX86MetadataEmitResult rdmsr_apx = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = rdmsr_apx_query, .form_id = 2932, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, rdmsr_apx.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && rdmsr_apx.byte_count == 10 &&
                                   x86_64_metadata_test_bytes_equal(output, rdmsr_apx.byte_count,
                                                                     (u8 const[]){0x62, 0xff, 0x7f, 0x08, 0xf6, 0xc0, 0x34, 0x12, 0x00, 0x00}, 10));
        BusterX86MetadataPhysicalQuery wrmsrns_apx_query = rdmsr_apx_query;
        wrmsrns_apx_query.mnemonic = S8("WRMSRNS");
        wrmsrns_apx_query.operands = wrmsrns_apx_operands;
        BusterX86MetadataEmitResult wrmsrns_apx = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = wrmsrns_apx_query, .form_id = 2933, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, wrmsrns_apx.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && wrmsrns_apx.byte_count == 10 &&
                                   x86_64_metadata_test_bytes_equal(output, wrmsrns_apx.byte_count,
                                                                     (u8 const[]){0x62, 0xff, 0x7e, 0x08, 0xf6, 0xc0, 0x34, 0x12, 0x00, 0x00}, 10));

        BusterX86MetadataPhysicalQuery apx_movdir64b_query = apx_enqcmd_query;
        apx_movdir64b_query.mnemonic = S8("MOVDIR64B");
        apx_movdir64b_query.features.names = apx_movdir64b_features;
        apx_movdir64b_query.features.count = BUSTER_ARRAY_LENGTH(apx_movdir64b_features);
        apx_movdir64b_query.include_privileged = false;
        BusterX86MetadataEmitResult apx_movdir64b = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = apx_movdir64b_query, .form_id = 1886, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, apx_movdir64b.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && apx_movdir64b.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(output, apx_movdir64b.byte_count,
                                                                     (u8 const[]){0x62, 0xe4, 0x7d, 0x08, 0xf8, 0x00}, 6));

        BusterX86MetadataPhysicalQuery wrmsr_query = x86_64_metadata_test_physical_query(
            S8("WRMSR"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, empty_features, 0);
        wrmsr_query.include_privileged = true;
        BusterX86MetadataPhysicalQuery rdmsr_query = wrmsr_query;
        rdmsr_query.mnemonic = S8("RDMSR");
        u8 wrmsr_output[32] = {0};
        u8 rdmsr_output[32] = {0};
        BusterX86MetadataEmitResult wrmsr = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = wrmsr_query, .form_id = 10129, .output = wrmsr_output, .output_capacity = sizeof(wrmsr_output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BusterX86MetadataEmitResult rdmsr = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = rdmsr_query, .form_id = 10131, .output = rdmsr_output, .output_capacity = sizeof(rdmsr_output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, wrmsr.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && wrmsr.byte_count == 2 &&
                                   x86_64_metadata_test_bytes_equal(wrmsr_output, wrmsr.byte_count, (u8 const[]){0x0f, 0x30}, 2));
        BUSTER_TEST(arguments, rdmsr.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && rdmsr.byte_count == 2 &&
                                   x86_64_metadata_test_bytes_equal(rdmsr_output, rdmsr.byte_count, (u8 const[]){0x0f, 0x32}, 2));
    }

    {
        // Selection keeps arity and physical binding diagnostics distinct.
        // Three operands of the wrong class are not an operand-count error.
        String8 wildcard[1] = {S8("*")};
        BusterX86MetadataPhysicalOperand wrong_count_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
        };
        BusterX86MetadataPhysicalOperand wrong_class_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 64),
        };
        BusterX86MetadataSelectResult wrong_count = buster_x86_metadata_select_form(
            x86_64_metadata_test_physical_query(S8("VADDPS"), wrong_count_operands, BUSTER_ARRAY_LENGTH(wrong_count_operands),
                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard)));
        BusterX86MetadataSelectResult wrong_class = buster_x86_metadata_select_form(
            x86_64_metadata_test_physical_query(S8("VADDPS"), wrong_class_operands, BUSTER_ARRAY_LENGTH(wrong_class_operands),
                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard)));
        BUSTER_TEST(arguments, wrong_count.status == BUSTER_X86_METADATA_ENCODE_WRONG_OPERAND_COUNT &&
                                   wrong_count.required_feature.length == 0);
        BUSTER_TEST(arguments, wrong_class.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH &&
                                   wrong_class.diagnostic_operand == 0 && wrong_class.required_feature.length == 0);
    }

    {
        // PREFETCHIT0/1 are 64-bit RIP-relative-only forms.  Keep this
        // invariant in the typed selector and emitter, not only in assembly
        // source parsing, so direct metadata callers cannot encode a base
        // register form.
        String8 wildcard[1] = {S8("*")};
        String8 prefetchit_mnemonics[] = {S8("PREFETCHIT0"), S8("PREFETCHIT1")};
        u8 prefetchit_modrm[] = {0x3d, 0x35};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(prefetchit_mnemonics); index += 1)
        {
            BusterX86MetadataPhysicalOperand rip_operand =
                x86_64_metadata_test_physical_mem_rip(S8("prefetchit_direct_external"), 0, 8);
            BusterX86MetadataPhysicalOperand base_operand = x86_64_metadata_test_physical_mem_base(0, 8, 0);
            BusterX86MetadataPhysicalQuery rip_query = x86_64_metadata_test_physical_query(
                prefetchit_mnemonics[index], &rip_operand, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                BUSTER_ARRAY_LENGTH(wildcard));
            BusterX86MetadataPhysicalQuery base_query = rip_query;
            base_query.operands = &base_operand;
            BusterX86MetadataSelectResult rip_selection = buster_x86_metadata_select_form(rip_query);
            BusterX86MetadataSelectResult base_selection = buster_x86_metadata_select_form(base_query);
            u8 output[16] = {0};
            BusterX86MetadataRelocation relocations[1] = {0};
            BusterX86MetadataEmitResult rip_emit = x86_64_metadata_test_emit_form(
                prefetchit_mnemonics[index], rip_selection.form_id, &rip_operand, 1,
                (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), output,
                BUSTER_ARRAY_LENGTH(output), relocations, BUSTER_ARRAY_LENGTH(relocations));
            BusterX86MetadataEmitResult base_emit = x86_64_metadata_test_emit_form(
                prefetchit_mnemonics[index], rip_selection.form_id, &base_operand, 1,
                (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), output,
                BUSTER_ARRAY_LENGTH(output), relocations, BUSTER_ARRAY_LENGTH(relocations));
            BUSTER_TEST(arguments, rip_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                       rip_selection.selected_byte_count == 7 &&
                                       base_selection.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
            BUSTER_TEST(arguments, rip_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && rip_emit.byte_count == 7 &&
                                       rip_emit.relocation_count == 1 && output[0] == 0x0f && output[1] == 0x18 &&
                                       output[2] == prefetchit_modrm[index] && output[3] == 0 && output[4] == 0 &&
                                       output[5] == 0 && output[6] == 0 && relocations[0].offset == 3 &&
                                       relocations[0].width == 4 && relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC32 &&
                                       relocations[0].addend == -4);
            BUSTER_TEST(arguments, base_emit.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH &&
                                       base_emit.byte_count == 0 && base_emit.relocation_count == 0);
        }
    }

    {
        // Permanent byte oracles cover the legacy/REX, REX2/APX, VEX, XOP,
        // EVEX, AMX, and system families.  The operands deliberately use the
        // public physical identity rather than metadata signatures.
        String8 wildcard[1] = {S8("*")};
        BusterX86MetadataPhysicalOperand mov_imm64[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_imm((s64)UINT64_C(0x1122334455667788), 64),
        };
        u8 mov_imm64_bytes[] = {0x48, 0xb8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("MOV"), 10018, mov_imm64, 2, (BusterX86MetadataPhysicalAttributes){0},
                                                                 wildcard, BUSTER_ARRAY_LENGTH(wildcard), mov_imm64_bytes,
                                                                 BUSTER_ARRAY_LENGTH(mov_imm64_bytes)));
        BusterX86MetadataPhysicalOperand mov_imm64_max[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_imm_u64(UINT64_MAX, 64),
        };
        u8 mov_imm64_max_bytes[] = {0x48, 0xb8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("MOV"), 10018, mov_imm64_max, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), mov_imm64_max_bytes,
                                                                 BUSTER_ARRAY_LENGTH(mov_imm64_max_bytes)));
        BusterX86MetadataPhysicalOperand mov_imm16[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 16),
            x86_64_metadata_test_physical_imm(0x1234, 16),
        };
        u8 mov_imm16_bytes[] = {0x66, 0xb8, 0x34, 0x12};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("MOV"), 10018, mov_imm16, 2, (BusterX86MetadataPhysicalAttributes){0},
                                                                 wildcard, BUSTER_ARRAY_LENGTH(wildcard), mov_imm16_bytes,
                                                                 BUSTER_ARRAY_LENGTH(mov_imm16_bytes)));
        BusterX86MetadataPhysicalOperand mov_imm16_overflow[2] = {
            mov_imm16[0], x86_64_metadata_test_physical_imm(0x10000, 16),
        };
        BusterX86MetadataEmitResult mov_imm16_range = x86_64_metadata_test_emit_form(
            S8("MOV"), 10018, mov_imm16_overflow, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, mov_imm16_range.status == BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE &&
                                   mov_imm16_range.diagnostic_value == 0x10000);

        BusterX86MetadataPhysicalOperand push_r16 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 64);
        BusterX86MetadataPhysicalOperand push_r31 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 31, 64);
        u8 push_r16_bytes[] = {0xd5, 0x18, 0x50};
        u8 push_r31_bytes[] = {0xd5, 0x19, 0x57};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSHP"), 2946, &push_r16, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), push_r16_bytes,
                                                                 BUSTER_ARRAY_LENGTH(push_r16_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSHP"), 2946, &push_r31, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), push_r31_bytes,
                                                                 BUSTER_ARRAY_LENGTH(push_r31_bytes)));
        BusterX86MetadataPhysicalQuery pushp_query = x86_64_metadata_test_physical_query(
            S8("PUSHP"), &push_r16, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult pushp_selection = buster_x86_metadata_select_form(pushp_query);
        u8 pushp_selected_bytes[8] = {0};
        BusterX86MetadataEmitResult pushp_selected = x86_64_metadata_test_emit_form(
            S8("PUSHP"), pushp_selection.form_id, &push_r16, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), pushp_selected_bytes, BUSTER_ARRAY_LENGTH(pushp_selected_bytes), 0, 0);
        BUSTER_TEST(arguments, pushp_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && pushp_selection.form_id == 2946 &&
                                   pushp_selection.selected_byte_count == 3 && pushp_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(pushp_selected_bytes, pushp_selected.byte_count,
                                                                     push_r16_bytes, BUSTER_ARRAY_LENGTH(push_r16_bytes)));
        BusterX86MetadataPhysicalOperand push16 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 16);
        BusterX86MetadataPhysicalQuery push16_query = x86_64_metadata_test_physical_query(
            S8("PUSH"), &push16, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult push16_selection = buster_x86_metadata_select_form(push16_query);
        u8 push16_selected_bytes[8] = {0};
        BusterX86MetadataEmitResult push16_selected = x86_64_metadata_test_emit_form(
            S8("PUSH"), push16_selection.form_id, &push16, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), push16_selected_bytes, BUSTER_ARRAY_LENGTH(push16_selected_bytes), 0, 0);
        BUSTER_TEST(arguments, push16_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && push16_selection.selected_byte_count == 4 &&
                                   push16_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(push16_selected_bytes, push16_selected.byte_count,
                                                                     (u8[]){0x66, 0xd5, 0x10, 0x50}, 4));
        BusterX86MetadataPhysicalAttributes rex2_nf = {.no_flags = true};
        BusterX86MetadataEmitResult rex2_nf_result = x86_64_metadata_test_emit_form(
            S8("PUSHP"), 2946, &push_r16, 1, rex2_nf, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[8]){0}, 8, 0, 0);
        BUSTER_TEST(arguments, rex2_nf_result.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION);

        BusterX86MetadataPhysicalOperand rex2_map_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 17, 64),
        };
        String8 apx_features[1] = {S8("apx")};
        BusterX86MetadataPhysicalQuery imul_rex2_query = x86_64_metadata_test_physical_query(
            S8("IMUL"), rex2_map_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, apx_features,
            BUSTER_ARRAY_LENGTH(apx_features));
        BusterX86MetadataPhysicalQuery cmovo_rex2_query = imul_rex2_query;
        cmovo_rex2_query.mnemonic = S8("CMOVO");
        BusterX86MetadataSelectResult imul_rex2_selection = buster_x86_metadata_select_form(imul_rex2_query);
        BusterX86MetadataSelectResult cmovo_rex2_selection = buster_x86_metadata_select_form(cmovo_rex2_query);
        u8 imul_rex2_bytes[8] = {0};
        u8 cmovo_rex2_bytes[8] = {0};
        BusterX86MetadataEmitResult imul_rex2_result = x86_64_metadata_test_emit_form(
            S8("IMUL"), imul_rex2_selection.form_id, rex2_map_operands, 2, (BusterX86MetadataPhysicalAttributes){0},
            apx_features, BUSTER_ARRAY_LENGTH(apx_features), imul_rex2_bytes, BUSTER_ARRAY_LENGTH(imul_rex2_bytes), 0, 0);
        BusterX86MetadataEmitResult cmovo_rex2_result = x86_64_metadata_test_emit_form(
            S8("CMOVO"), cmovo_rex2_selection.form_id, rex2_map_operands, 2, (BusterX86MetadataPhysicalAttributes){0},
            apx_features, BUSTER_ARRAY_LENGTH(apx_features), cmovo_rex2_bytes, BUSTER_ARRAY_LENGTH(cmovo_rex2_bytes), 0, 0);
        BUSTER_TEST(arguments, imul_rex2_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   imul_rex2_selection.selected_byte_count == 4 &&
                                   imul_rex2_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && imul_rex2_result.byte_count == 4 &&
                                   x86_64_metadata_test_bytes_equal(imul_rex2_bytes, imul_rex2_result.byte_count,
                                                                    (u8[]){0xd5, 0xd8, 0xaf, 0xc1}, 4));
        BUSTER_TEST(arguments, cmovo_rex2_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   cmovo_rex2_selection.selected_byte_count == 4 &&
                                   cmovo_rex2_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && cmovo_rex2_result.byte_count == 4 &&
                                   x86_64_metadata_test_bytes_equal(cmovo_rex2_bytes, cmovo_rex2_result.byte_count,
                                                                    (u8[]){0xd5, 0xd8, 0x40, 0xc1}, 4));

        BusterX86MetadataPhysicalOperand vaddps_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 2, 128),
        };
        u8 vaddps_bytes[] = {0xc5, 0xf0, 0x58, 0xc2};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 3053, vaddps_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), vaddps_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vaddps_bytes)));

        BusterX86MetadataPhysicalOperand xchg_ecx =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 32);
        BusterX86MetadataPhysicalOperand xchg_rcx =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 64);
        BusterX86MetadataPhysicalOperand xchg_r8 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 8, 64);
        u8 xchg_ecx_bytes[] = {0x91};
        u8 xchg_rcx_bytes[] = {0x48, 0x91};
        u8 xchg_r8_bytes[] = {0x49, 0x90};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("XCHG"), 9855, &xchg_ecx, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), xchg_ecx_bytes,
                                                                 BUSTER_ARRAY_LENGTH(xchg_ecx_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("XCHG"), 9855, &xchg_rcx, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), xchg_rcx_bytes,
                                                                 BUSTER_ARRAY_LENGTH(xchg_rcx_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("XCHG"), 9855, &xchg_r8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), xchg_r8_bytes,
                                                                 BUSTER_ARRAY_LENGTH(xchg_r8_bytes)));
        BusterX86MetadataPhysicalOperand xchg_rax =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
        BusterX86MetadataEmitResult xchg_rax_result = x86_64_metadata_test_emit_form(
            S8("XCHG"), 9855, &xchg_rax, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[8]){0}, 8, 0, 0);
        BUSTER_TEST(arguments, xchg_rax_result.status == BUSTER_X86_METADATA_ENCODE_REGISTER_ENCODING);

        BusterX86MetadataPhysicalOperand xchg_rex2_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 17, 64),
        };
        String8 xchg_apx_features[1] = {S8("apx")};
        BusterX86MetadataPhysicalQuery xchg_rex2_query = x86_64_metadata_test_physical_query(
            S8("XCHG"), xchg_rex2_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, xchg_apx_features,
            BUSTER_ARRAY_LENGTH(xchg_apx_features));
        BusterX86MetadataSelectResult xchg_rex2_selection = buster_x86_metadata_select_form(xchg_rex2_query);
        u8 xchg_rex2_selected_bytes[8] = {0};
        BusterX86MetadataEmitResult xchg_rex2_selected = x86_64_metadata_test_emit_form(
            S8("XCHG"), xchg_rex2_selection.form_id, xchg_rex2_operands, 2,
            (BusterX86MetadataPhysicalAttributes){0}, xchg_apx_features, BUSTER_ARRAY_LENGTH(xchg_apx_features),
            xchg_rex2_selected_bytes, BUSTER_ARRAY_LENGTH(xchg_rex2_selected_bytes), 0, 0);
        BUSTER_TEST(arguments, xchg_rex2_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   xchg_rex2_selection.selected_byte_count == 4 &&
                                   xchg_rex2_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(xchg_rex2_selected_bytes, xchg_rex2_selected.byte_count,
                                                                    (u8[]){0xd5, 0x58, 0x87, 0xc1}, 4));

        BusterX86MetadataPhysicalOperand bswap_eax =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32);
        BusterX86MetadataPhysicalOperand bswap_rax =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
        BusterX86MetadataPhysicalOperand bswap_r8d =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 8, 32);
        BusterX86MetadataPhysicalOperand bswap_r8 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 8, 64);
        BusterX86MetadataPhysicalOperand bswap_r16 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 64);
        u8 bswap_eax_bytes[] = {0x0f, 0xc8};
        u8 bswap_rax_bytes[] = {0x48, 0x0f, 0xc8};
        u8 bswap_r8d_bytes[] = {0x41, 0x0f, 0xc8};
        u8 bswap_r8_bytes[] = {0x49, 0x0f, 0xc8};
        u8 bswap_r16_bytes[] = {0xd5, 0x98, 0xc8};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSWAP"), 10688, &bswap_eax, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bswap_eax_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bswap_eax_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSWAP"), 10688, &bswap_rax, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bswap_rax_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bswap_rax_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSWAP"), 10688, &bswap_r8d, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bswap_r8d_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bswap_r8d_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSWAP"), 10688, &bswap_r8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bswap_r8_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bswap_r8_bytes)));
        BusterX86MetadataPhysicalQuery bswap_r16_query = x86_64_metadata_test_physical_query(
            S8("BSWAP"), &bswap_r16, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult bswap_r16_selection = buster_x86_metadata_select_form(bswap_r16_query);
        u8 bswap_r16_selected_bytes[8] = {0};
        BusterX86MetadataEmitResult bswap_r16_selected = x86_64_metadata_test_emit_form(
            S8("BSWAP"), bswap_r16_selection.form_id, &bswap_r16, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), bswap_r16_selected_bytes, BUSTER_ARRAY_LENGTH(bswap_r16_selected_bytes), 0, 0);
        BUSTER_TEST(arguments, bswap_r16_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   bswap_r16_selection.selected_byte_count == 3 &&
                                   bswap_r16_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(bswap_r16_selected_bytes, bswap_r16_selected.byte_count,
                                                                    bswap_r16_bytes, BUSTER_ARRAY_LENGTH(bswap_r16_bytes)));

        u8 ud0_short_bytes[] = {0x0f, 0xff};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("UD0"), 10412, 0, 0,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), ud0_short_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ud0_short_bytes)));

        BusterX86MetadataPhysicalOperand bextr_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_mem_base(3, 64, 0),
            x86_64_metadata_test_physical_imm(5, 32),
        };
        u8 bextr_bytes[] = {0x8f, 0xea, 0xf8, 0x10, 0x03, 0x05, 0x00, 0x00, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BEXTR_XOP"), 326, bextr_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bextr_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bextr_bytes)));

        BusterX86MetadataPhysicalOperand vmov_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
        };
        u8 vmov_plain_bytes[] = {0x62, 0xf1, 0x7d, 0x08, 0x6f, 0xc1};
        BusterX86MetadataPhysicalAttributes masked = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK, .has_mask_register = true, .mask_register = 1,
        };
        BusterX86MetadataPhysicalAttributes zeroed = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK | BUSTER_X86_METADATA_DECORATOR_ZEROING,
            .has_mask_register = true, .mask_register = 1, .zeroing = true,
        };
        u8 vmov_masked_bytes[] = {0x62, 0xf1, 0x7d, 0x09, 0x6f, 0xc1};
        u8 vmov_zeroed_bytes[] = {0x62, 0xf1, 0x7d, 0x89, 0x6f, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VMOVDQA32"), 5583, vmov_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), vmov_plain_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vmov_plain_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VMOVDQA32"), 5583, vmov_operands, 2, masked, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), vmov_masked_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vmov_masked_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VMOVDQA32"), 5583, vmov_operands, 2, zeroed, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), vmov_zeroed_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vmov_zeroed_bytes)));
        BusterX86MetadataPhysicalOperand vmov_explicit_mask_operands[3] = {
            vmov_operands[0], x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 2, 64), vmov_operands[1],
        };
        BusterX86MetadataEmitResult vmov_mask_mismatch = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5583, vmov_explicit_mask_operands, 3, masked, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
            (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, vmov_mask_mismatch.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
        BusterX86MetadataPhysicalAttributes explicit_mask = masked;
        explicit_mask.mask_register = 2;
        u8 vmov_explicit_mask_bytes[] = {0x62, 0xf1, 0x7d, 0x0a, 0x6f, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VMOVDQA32"), 5583, vmov_explicit_mask_operands, 3,
                                                                 explicit_mask, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
                                                                 vmov_explicit_mask_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vmov_explicit_mask_bytes)));
        BusterX86MetadataEmitResult zero_k0 = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5583, vmov_operands, 2,
            (BusterX86MetadataPhysicalAttributes){.decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK | BUSTER_X86_METADATA_DECORATOR_ZEROING,
                                                   .zeroing = true},
            wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, zero_k0.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand round_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
        };
        BusterX86MetadataPhysicalAttributes rounding = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_ROUNDING,
            .rounding_mode = BUSTER_X86_METADATA_ROUNDING_DOWN,
        };
        u8 rounding_bytes[] = {0x62, 0xf1, 0xff, 0x38, 0x2f, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCOMXSD"), 4276, round_operands, 2, rounding, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), rounding_bytes,
                                                                 BUSTER_ARRAY_LENGTH(rounding_bytes)));
        u8 fixed_round_len128_plain_bytes[] = {0x62, 0xf1, 0xff, 0x08, 0x2f, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCOMXSD"), 4275, round_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), fixed_round_len128_plain_bytes,
                                                                 BUSTER_ARRAY_LENGTH(fixed_round_len128_plain_bytes)));

        BusterX86MetadataPhysicalOperand fixed_round_len512_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 2, 512),
        };
        BusterX86MetadataPhysicalAttributes fixed_round_len512_rounding = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_ROUNDING,
            .rounding_mode = BUSTER_X86_METADATA_ROUNDING_NEAREST,
        };
        u8 fixed_round_len512_plain_bytes[] = {0x62, 0xf5, 0x74, 0x48, 0x58, 0xc2};
        u8 fixed_round_len512_rounding_bytes[] = {0x62, 0xf5, 0x74, 0x18, 0x58, 0xc2};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPH"), 4479, fixed_round_len512_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), fixed_round_len512_plain_bytes,
                                                                 BUSTER_ARRAY_LENGTH(fixed_round_len512_plain_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPH"), 4480, fixed_round_len512_operands, 3,
                                                                 fixed_round_len512_rounding, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), fixed_round_len512_rounding_bytes,
                                                                 BUSTER_ARRAY_LENGTH(fixed_round_len512_rounding_bytes)));
        BusterX86MetadataPhysicalOperand fixed_round_len512_wrong_width[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 0, 256),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 1, 256),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 2, 256),
        };
        BusterX86MetadataEmitResult fixed_round_len512_wrong_width_result = x86_64_metadata_test_emit_form(
            S8("VADDPH"), 4480, fixed_round_len512_wrong_width, 3, fixed_round_len512_rounding, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, fixed_round_len512_wrong_width_result.status != BUSTER_X86_METADATA_ENCODE_SUCCESS);

        BusterX86MetadataPhysicalOperand apx_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 3, 64),
        };
        u8 apx_nf0_bytes[] = {0x62, 0xf4, 0xfc, 0x08, 0x01, 0xd8};
        u8 apx_nf1_bytes[] = {0x62, 0xf4, 0xfc, 0x0c, 0x01, 0xd8};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("ADD"), 559, apx_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), apx_nf0_bytes,
                                                                 BUSTER_ARRAY_LENGTH(apx_nf0_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("ADD"), 561, apx_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){.no_flags = true}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), apx_nf1_bytes,
                                                                 BUSTER_ARRAY_LENGTH(apx_nf1_bytes)));
        BusterX86MetadataPhysicalQuery no_flags_query = x86_64_metadata_test_physical_query(
            S8("ADD"), apx_operands, 2, (BusterX86MetadataPhysicalAttributes){.no_flags = true}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult no_flags_select = buster_x86_metadata_select_form(no_flags_query);
        BusterX86MetadataForm no_flags_form = {0};
        BUSTER_TEST(arguments, no_flags_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && no_flags_select.form_id == 561 &&
                                   buster_x86_metadata_form(no_flags_select.form_id, &no_flags_form) &&
                                   (no_flags_form.apx_flags & BUSTER_X86_METADATA_APX_NF));
        BusterX86MetadataEmitResult no_flags_classic = x86_64_metadata_test_emit_form(
            S8("ADD"), 3053, vaddps_operands, 3, (BusterX86MetadataPhysicalAttributes){.no_flags = true}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, no_flags_classic.status != BUSTER_X86_METADATA_ENCODE_SUCCESS);

        BusterX86MetadataPhysicalOperand ccmp_byte_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 8),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 14, 8),
        };
        BusterX86MetadataPhysicalAttributes ccmp_dfv0 = {.has_dfv = true, .dfv = 0};
        BusterX86MetadataPhysicalAttributes ccmp_dfv2 = {.has_dfv = true, .dfv = 2};
        BusterX86MetadataPhysicalAttributes ccmp_dfv15 = {.has_dfv = true, .dfv = 15};
        u8 ccmpb_dfv0_bytes[] = {0x62, 0x74, 0x04, 0x02, 0x38, 0xf2};
        u8 ccmpb_dfv2_bytes[] = {0x62, 0x74, 0x14, 0x02, 0x38, 0xf2};
        u8 ccmpb_dfv15_bytes[] = {0x62, 0x74, 0x7c, 0x02, 0x38, 0xf2};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CCMPB"), 779, ccmp_byte_operands, 2,
                                                                 ccmp_dfv0, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), ccmpb_dfv0_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ccmpb_dfv0_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CCMPB"), 779, ccmp_byte_operands, 2,
                                                                 ccmp_dfv2, wildcard, BUSTER_ARRAY_LENGTH(wildcard), ccmpb_dfv2_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ccmpb_dfv2_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CCMPB"), 779, ccmp_byte_operands, 2,
                                                                 ccmp_dfv15, wildcard, BUSTER_ARRAY_LENGTH(wildcard), ccmpb_dfv15_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ccmpb_dfv15_bytes)));

        BusterX86MetadataPhysicalOperand ccmp_qword_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 30, 64),
        };
        u8 ccmpl_bytes[] = {0x62, 0x64, 0x84, 0x0c, 0x39, 0xf2};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CCMPL"), 851, ccmp_qword_operands, 2,
                                                                 ccmp_dfv0, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), ccmpl_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ccmpl_bytes)));

        BusterX86MetadataPhysicalOperand ctest_qword_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 14, 64),
        };
        u8 ctestz_bytes[] = {0x62, 0x74, 0x84, 0x04, 0x85, 0xf2};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CTESTZ"), 1697, ctest_qword_operands, 2,
                                                                 ccmp_dfv0, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), ctestz_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ctestz_bytes)));

        BusterX86MetadataEmitResult ccmp_missing_dfv = x86_64_metadata_test_emit_form(
            S8("CCMPB"), 779, ccmp_byte_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult ccmp_invalid_dfv = x86_64_metadata_test_emit_form(
            S8("CCMPB"), 779, ccmp_byte_operands, 2,
            (BusterX86MetadataPhysicalAttributes){.has_dfv = true, .dfv = 16}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult ccmp_nonzero_without_dfv = x86_64_metadata_test_emit_form(
            S8("CCMPB"), 779, ccmp_byte_operands, 2,
            (BusterX86MetadataPhysicalAttributes){.dfv = 1}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult ordinary_form_with_dfv = x86_64_metadata_test_emit_form(
            S8("ADD"), 559, apx_operands, 2, ccmp_dfv0, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, ccmp_missing_dfv.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   ccmp_invalid_dfv.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   ccmp_nonzero_without_dfv.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   ordinary_form_with_dfv.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        u32 rol_one_form_id = UINT32_MAX;
        BusterX86MetadataForm rol_one_form = {0};
        bool rol_one_contract = x86_64_metadata_test_iform_id(S8("ROL_GPR8i8_ONE_APX"), &rol_one_form_id) &&
                                buster_x86_metadata_form(rol_one_form_id, &rol_one_form) &&
                                x86_64_metadata_test_pattern_has_token(rol_one_form.pattern, S8("ONE()"));
        u32 rol_one_implicit_immediate_count = 0;
        bool rol_one_visible_immediate = false;
        if (rol_one_contract)
        {
            for (u32 operand_index = 0; operand_index < rol_one_form.operand_count; operand_index += 1)
            {
                BusterX86MetadataOperand rol_one_metadata_operand = {0};
                if (!buster_x86_metadata_operand(rol_one_form_id, operand_index, &rol_one_metadata_operand))
                {
                    rol_one_contract = false;
                    break;
                }
                if (rol_one_metadata_operand.kind == BUSTER_X86_METADATA_OPERAND_IMMEDIATE &&
                    (rol_one_metadata_operand.access & BUSTER_X86_METADATA_ACCESS_IMPLICIT))
                    rol_one_implicit_immediate_count += 1;
                rol_one_visible_immediate |= rol_one_metadata_operand.kind == BUSTER_X86_METADATA_OPERAND_IMMEDIATE &&
                                             rol_one_metadata_operand.visible;
            }
        }
        BUSTER_TEST(arguments, rol_one_contract && rol_one_implicit_immediate_count == 1 && !rol_one_visible_immediate);
        BusterX86MetadataPhysicalOperand rol_one_operand =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 8);
        // This normalized iform is the EVEX APX row; the public handwritten
        // APX path separately uses the shorter REX2 spelling.
        u8 rol_one_bytes[] = {0x62, 0xfc, 0x7c, 0x08, 0xd0, 0xc0};
        BUSTER_TEST(arguments, rol_one_contract && x86_64_metadata_test_emit_exact(S8("ROL"), rol_one_form_id, &rol_one_operand, 1,
                                                                                      (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                      BUSTER_ARRAY_LENGTH(wildcard), rol_one_bytes,
                                                                                      BUSTER_ARRAY_LENGTH(rol_one_bytes)));

        u32 lzcnt_form_id = UINT32_MAX;
        u32 tzcnt_form_id = UINT32_MAX;
        u32 movss_ignore66_form_id = UINT32_MAX;
        u32 cmpxchg8b_immune66_form_id = UINT32_MAX;
        BusterX86MetadataForm lzcnt_form = {0};
        BusterX86MetadataForm tzcnt_form = {0};
        BusterX86MetadataForm movss_ignore66_form = {0};
        BusterX86MetadataForm cmpxchg8b_immune66_form = {0};
        bool lzcnt_token_form = x86_64_metadata_test_find_token_form(
            S8("LZCNT"), S8("LZCNT=1"), BUSTER_X86_METADATA_OPERAND_REGISTER, &lzcnt_form_id, &lzcnt_form);
        bool tzcnt_token_form = x86_64_metadata_test_find_token_form(
            S8("TZCNT"), S8("TZCNT=1"), BUSTER_X86_METADATA_OPERAND_REGISTER, &tzcnt_form_id, &tzcnt_form);
        bool movss_ignore66_token_form = x86_64_metadata_test_find_token_form(
            S8("MOVSS"), S8("IGNORE66()"), BUSTER_X86_METADATA_OPERAND_REGISTER, &movss_ignore66_form_id,
            &movss_ignore66_form);
        bool cmpxchg8b_immune66_token_form = x86_64_metadata_test_find_token_form(
            S8("CMPXCHG8B"), S8("IMMUNE66()"), BUSTER_X86_METADATA_OPERAND_MEMORY, &cmpxchg8b_immune66_form_id,
            &cmpxchg8b_immune66_form);
        BusterX86MetadataPhysicalOperand lzcnt_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 32),
        };
        BusterX86MetadataPhysicalOperand tzcnt_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 3, 32),
        };
        BusterX86MetadataPhysicalOperand movss_ignore66_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
        };
        BusterX86MetadataPhysicalOperand cmpxchg8b_immune66_operand = x86_64_metadata_test_physical_mem_base(0, 64, 0);
        u8 lzcnt_metadata_bytes[] = {0xf3, 0x0f, 0xbd, 0xca};
        u8 tzcnt_metadata_bytes[] = {0xf3, 0x0f, 0xbc, 0xc3};
        u8 movss_ignore66_bytes[] = {0xf3, 0x0f, 0x10, 0xc1};
        u8 cmpxchg8b_immune66_bytes[] = {0x0f, 0xc7, 0x08};
        BUSTER_TEST(arguments, lzcnt_token_form && x86_64_metadata_test_emit_exact(S8("LZCNT"), lzcnt_form_id, lzcnt_operands, 2,
                                                                                       (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                       BUSTER_ARRAY_LENGTH(wildcard), lzcnt_metadata_bytes,
                                                                                       BUSTER_ARRAY_LENGTH(lzcnt_metadata_bytes)));
        BUSTER_TEST(arguments, tzcnt_token_form && x86_64_metadata_test_emit_exact(S8("TZCNT"), tzcnt_form_id, tzcnt_operands, 2,
                                                                                       (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                       BUSTER_ARRAY_LENGTH(wildcard), tzcnt_metadata_bytes,
                                                                                       BUSTER_ARRAY_LENGTH(tzcnt_metadata_bytes)));
        BUSTER_TEST(arguments, movss_ignore66_token_form && x86_64_metadata_test_emit_exact(
                                   S8("MOVSS"), movss_ignore66_form_id, movss_ignore66_operands, 2,
                                   (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
                                   movss_ignore66_bytes, BUSTER_ARRAY_LENGTH(movss_ignore66_bytes)));
        BUSTER_TEST(arguments, cmpxchg8b_immune66_token_form && x86_64_metadata_test_emit_exact(
                                   S8("CMPXCHG8B"), cmpxchg8b_immune66_form_id, &cmpxchg8b_immune66_operand, 1,
                                   (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
                                   cmpxchg8b_immune66_bytes, BUSTER_ARRAY_LENGTH(cmpxchg8b_immune66_bytes)));

        u32 endbr64_form_id = UINT32_MAX;
        u32 cldemote_form_id = UINT32_MAX;
        u32 prefetchit0_form_id = UINT32_MAX;
        u32 prefetchit1_form_id = UINT32_MAX;
        u32 prefetchrst2_form_id = UINT32_MAX;
        bool selector_forms = x86_64_metadata_test_iform_id(S8("ENDBR64"), &endbr64_form_id) &&
                              x86_64_metadata_test_iform_id(S8("CLDEMOTE_MEMu8"), &cldemote_form_id) &&
                              x86_64_metadata_test_iform_id(S8("PREFETCHIT0_MEMu8"), &prefetchit0_form_id) &&
                              x86_64_metadata_test_iform_id(S8("PREFETCHIT1_MEMu8"), &prefetchit1_form_id) &&
                              x86_64_metadata_test_iform_id(S8("PREFETCHRST2_MEMu8"), &prefetchrst2_form_id);
        BusterX86MetadataPhysicalOperand selector_memory = x86_64_metadata_test_physical_mem_base(0, 8, 0);
        BusterX86MetadataPhysicalOperand prefetchit_memory = x86_64_metadata_test_physical_mem_rip((String8){0}, 0, 8);
        prefetchit_memory.memory.has_symbol = false;
        u8 endbr64_bytes[] = {0xf3, 0x0f, 0x1e, 0xfa};
        u8 cldemote_bytes[] = {0x0f, 0x1c, 0x00};
        u8 prefetchit0_bytes[] = {0x0f, 0x18, 0x3d, 0x00, 0x00, 0x00, 0x00};
        u8 prefetchit1_bytes[] = {0x0f, 0x18, 0x35, 0x00, 0x00, 0x00, 0x00};
        u8 prefetchrst2_bytes[] = {0x0f, 0x18, 0x20};
        BUSTER_TEST(arguments, selector_forms && x86_64_metadata_test_emit_exact(S8("ENDBR64"), endbr64_form_id, 0, 0,
                                                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                    BUSTER_ARRAY_LENGTH(wildcard), endbr64_bytes,
                                                                                    BUSTER_ARRAY_LENGTH(endbr64_bytes)));
        BUSTER_TEST(arguments, selector_forms && x86_64_metadata_test_emit_exact(S8("CLDEMOTE"), cldemote_form_id, &selector_memory, 1,
                                                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                    BUSTER_ARRAY_LENGTH(wildcard), cldemote_bytes,
                                                                                    BUSTER_ARRAY_LENGTH(cldemote_bytes)));
        BUSTER_TEST(arguments, selector_forms && x86_64_metadata_test_emit_exact(S8("PREFETCHIT0"), prefetchit0_form_id, &prefetchit_memory, 1,
                                                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                    BUSTER_ARRAY_LENGTH(wildcard), prefetchit0_bytes,
                                                                                    BUSTER_ARRAY_LENGTH(prefetchit0_bytes)));
        BUSTER_TEST(arguments, selector_forms && x86_64_metadata_test_emit_exact(S8("PREFETCHIT1"), prefetchit1_form_id, &prefetchit_memory, 1,
                                                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                    BUSTER_ARRAY_LENGTH(wildcard), prefetchit1_bytes,
                                                                                    BUSTER_ARRAY_LENGTH(prefetchit1_bytes)));
        BUSTER_TEST(arguments, selector_forms && x86_64_metadata_test_emit_exact(S8("PREFETCHRST2"), prefetchrst2_form_id, &selector_memory, 1,
                                                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                    BUSTER_ARRAY_LENGTH(wildcard), prefetchrst2_bytes,
                                                                                    BUSTER_ARRAY_LENGTH(prefetchrst2_bytes)));

        BusterX86MetadataEmitResult ccmp_wrong_operand = x86_64_metadata_test_emit_form(
            S8("CCMPB"), 779,
            (BusterX86MetadataPhysicalOperand[2]){
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 2, 128),
                ccmp_byte_operands[1],
            },
            2, ccmp_dfv0, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, ccmp_wrong_operand.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
        BusterX86MetadataEmitResult ccmp_decorator = x86_64_metadata_test_emit_form(
            S8("CCMPB"), 779, ccmp_byte_operands, 2,
            (BusterX86MetadataPhysicalAttributes){
                .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK,
                .has_mask_register = true,
                .mask_register = 1,
                .has_dfv = true,
                .dfv = 0,
            },
            wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, ccmp_decorator.status == BUSTER_X86_METADATA_ENCODE_DECORATOR);
        String8 no_apx_features[1] = {S8("avx512f")};
        BusterX86MetadataEmitResult ccmp_feature = x86_64_metadata_test_emit_form(
            S8("CCMPB"), 779, ccmp_byte_operands, 2, ccmp_dfv0, no_apx_features,
            BUSTER_ARRAY_LENGTH(no_apx_features), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, ccmp_feature.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE);

        BusterX86MetadataPhysicalOperand kadd_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 1, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 2, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 3, 64),
        };
        u8 kadd_bytes[] = {0xc5, 0xec, 0x4a, 0xcb};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("KADDW"), 6874, kadd_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), kadd_bytes,
                                                                 BUSTER_ARRAY_LENGTH(kadd_bytes)));

        BusterX86MetadataPhysicalOperand tile_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM, 0, 1024),
            x86_64_metadata_test_physical_mem_base(3, 32, 0),
        };
        u8 tile_bytes[] = {0xc4, 0xe2, 0x7b, 0x4b, 0x04, 0x23};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("TILELOADD"), 483, tile_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), tile_bytes,
                                                                 BUSTER_ARRAY_LENGTH(tile_bytes)));

        // These exact byte oracles exercise EVEX memory extension, APX width
        // selection, VSIB V' routing, tuple displacement compression, and
        // the XOP/APX families without relying on a host assembler.
        BusterX86MetadataPhysicalOperand push16_form =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 16);
        u8 push16_form_bytes[] = {0x66, 0xd5, 0x10, 0x50};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSH"), 9723, &push16_form, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), push16_form_bytes,
                                                                 BUSTER_ARRAY_LENGTH(push16_form_bytes)));
        BusterX86MetadataEmitResult rex2_without_apx = x86_64_metadata_test_emit_form(
            S8("PUSH"), 9723, &push16_form, 1, (BusterX86MetadataPhysicalAttributes){0}, 0, 0, (u8[1]){0}, 0, 0, 0);
        String8 apx_feature[1] = {S8("apx")};
        BUSTER_TEST(arguments, rex2_without_apx.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   x86_64_metadata_test_emit_exact(S8("PUSH"), 9723, &push16_form, 1,
                                                                    (BusterX86MetadataPhysicalAttributes){0}, apx_feature,
                                                                    BUSTER_ARRAY_LENGTH(apx_feature), push16_form_bytes,
                                                                    BUSTER_ARRAY_LENGTH(push16_form_bytes)));

        BusterX86MetadataPhysicalOperand vaddps_memory_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
            x86_64_metadata_test_physical_mem_base(16, 32, 0),
        };
        u8 vaddps_r16_bytes[] = {0x62, 0xf9, 0x74, 0x48, 0x58, 0x00};
        String8 avx512_only[1] = {S8("avx512f")};
        String8 avx512_apx[2] = {S8("avx512f"), S8("apx")};
        BusterX86MetadataEmitResult vaddps_r16_without_apx = x86_64_metadata_test_emit_form(
            S8("VADDPS"), 6940, vaddps_memory_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, avx512_only,
            BUSTER_ARRAY_LENGTH(avx512_only), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, vaddps_r16_without_apx.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   x86_64_metadata_test_emit_exact(S8("VADDPS"), 6940, vaddps_memory_operands, 3,
                                                                    (BusterX86MetadataPhysicalAttributes){0}, avx512_apx,
                                                                    BUSTER_ARRAY_LENGTH(avx512_apx), vaddps_r16_bytes,
                                                                    BUSTER_ARRAY_LENGTH(vaddps_r16_bytes)));

        BusterX86MetadataPhysicalOperand vaddps_complex_memory = x86_64_metadata_test_physical_mem_base(24, 32, 64);
        vaddps_complex_memory.memory.has_index = true;
        vaddps_complex_memory.memory.index = (BusterX86MetadataPhysicalRegister){
            .index = 25, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR};
        vaddps_complex_memory.memory.scale = 4;
        vaddps_complex_memory.memory.has_displacement = true;
        BusterX86MetadataPhysicalOperand vaddps_complex_operands[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], vaddps_complex_memory};
        u8 vaddps_complex_bytes[] = {0x62, 0x99, 0x70, 0x48, 0x58, 0x44, 0x88, 0x01};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 6940, vaddps_complex_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, avx512_apx,
                                                                 BUSTER_ARRAY_LENGTH(avx512_apx), vaddps_complex_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vaddps_complex_bytes)));
        BusterX86MetadataPhysicalOperand vaddps_index_memory = x86_64_metadata_test_physical_mem_index(25, 4, 32, 64);
        vaddps_index_memory.memory.has_displacement = true;
        BusterX86MetadataPhysicalOperand vaddps_index_operands[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], vaddps_index_memory};
        u8 vaddps_index_bytes[] = {0x62, 0xb1, 0x70, 0x48, 0x58, 0x04, 0x8d, 0x40, 0x00, 0x00, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 6940, vaddps_index_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, avx512_apx,
                                                                 BUSTER_ARRAY_LENGTH(avx512_apx), vaddps_index_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vaddps_index_bytes)));
        BusterX86MetadataPhysicalOperand vaddps_broadcast_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
            x86_64_metadata_test_physical_mem_base(0, 32, 0),
        };
        BusterX86MetadataPhysicalAttributes broadcast_attributes = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_BROADCAST,
            .broadcast_elements = 16,
        };
        u8 vaddps_broadcast_bytes[] = {0x62, 0xf1, 0x74, 0x58, 0x58, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 6940, vaddps_broadcast_operands, 3,
                                                                 broadcast_attributes, avx512_only,
                                                                 BUSTER_ARRAY_LENGTH(avx512_only), vaddps_broadcast_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vaddps_broadcast_bytes)));
        BusterX86MetadataPhysicalAttributes invalid_broadcast_attributes = broadcast_attributes;
        invalid_broadcast_attributes.broadcast_elements = 7;
        BusterX86MetadataEmitResult invalid_broadcast_count = x86_64_metadata_test_emit_form(
            S8("VADDPS"), 6940, vaddps_broadcast_operands, 3, invalid_broadcast_attributes, avx512_only,
            BUSTER_ARRAY_LENGTH(avx512_only), (u8[8]){0}, 8, 0, 0);
        BUSTER_TEST(arguments, invalid_broadcast_count.status == BUSTER_X86_METADATA_ENCODE_DECORATOR);

        BusterX86MetadataPhysicalOperand tile_memory_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM, 0, 1024),
            x86_64_metadata_test_physical_mem_base(16, 32, 0),
        };
        u8 tile_r16_bytes[] = {0x62, 0xfa, 0x7f, 0x08, 0x4b, 0x04, 0x20};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("TILELOADD"), 490, tile_memory_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), tile_r16_bytes,
                                                                 BUSTER_ARRAY_LENGTH(tile_r16_bytes)));
        BusterX86MetadataPhysicalOperand tile_complex_memory = x86_64_metadata_test_physical_mem_base(24, 32, 64);
        tile_complex_memory.memory.has_index = true;
        tile_complex_memory.memory.index = (BusterX86MetadataPhysicalRegister){
            .index = 25, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR};
        tile_complex_memory.memory.scale = 4;
        tile_complex_memory.memory.has_displacement = true;
        BusterX86MetadataPhysicalOperand tile_complex_operands[2] = {tile_memory_operands[0], tile_complex_memory};
        u8 tile_complex_bytes[] = {0x62, 0x9a, 0x7b, 0x08, 0x4b, 0x44, 0x88, 0x40};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("TILELOADD"), 490, tile_complex_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), tile_complex_bytes,
                                                                 BUSTER_ARRAY_LENGTH(tile_complex_bytes)));

        BusterX86MetadataPhysicalOperand high_vvvv[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 16, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
        };
        u8 high_vvvv_bytes[] = {0x62, 0xf1, 0x7c, 0x40, 0x58, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 6938, high_vvvv, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), high_vvvv_bytes,
                                                                 BUSTER_ARRAY_LENGTH(high_vvvv_bytes)));

        BusterX86MetadataPhysicalOperand apx_add64[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_imm(1, 64),
        };
        u8 apx_add64_bytes[] = {0x62, 0xf4, 0xfc, 0x08, 0x81, 0xc0, 0x01, 0x00, 0x00, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("ADD"), 607, apx_add64, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), apx_add64_bytes,
                                                                 BUSTER_ARRAY_LENGTH(apx_add64_bytes)));
        BusterX86MetadataPhysicalOperand apx_add16[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 16),
            x86_64_metadata_test_physical_imm(1, 16),
        };
        u8 apx_add16_bytes[] = {0x62, 0xf4, 0x7d, 0x08, 0x81, 0xc0, 0x01, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("ADD"), 611, apx_add16, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), apx_add16_bytes,
                                                                 BUSTER_ARRAY_LENGTH(apx_add16_bytes)));
        BusterX86MetadataPhysicalOperand andn_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 32),
        };
        u8 andn_bytes[] = {0x62, 0xf2, 0x7c, 0x08, 0xf2, 0xc2};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("ANDN"), 731, andn_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), andn_bytes,
                                                                 BUSTER_ARRAY_LENGTH(andn_bytes)));

        BusterX86MetadataPhysicalOperand fma4_operands[4] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 2, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 3, 128),
        };
        u8 fma4_bytes[] = {0xc4, 0xe3, 0x71, 0x68, 0xc2, 0x30};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VFMADDPS"), 64, fma4_operands, 4,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), fma4_bytes,
                                                                 BUSTER_ARRAY_LENGTH(fma4_bytes)));
        BusterX86MetadataEmitResult fma4_missing_selector = x86_64_metadata_test_emit_form(
            S8("VFMADDPS"), 64, fma4_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, fma4_missing_selector.status != BUSTER_X86_METADATA_ENCODE_SUCCESS);

        BusterX86MetadataPhysicalOperand gather_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 0, 256),
            x86_64_metadata_test_physical_mem_base(0, 64, 0),
        };
        gather_operands[1].memory.vsib = true;
        gather_operands[1].memory.has_index = true;
        gather_operands[1].memory.index = (BusterX86MetadataPhysicalRegister){
            .index = 1, .width = 256, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM};
        BusterX86MetadataPhysicalAttributes gather_attributes = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK, .has_mask_register = true, .mask_register = 1,
        };
        u8 gather_bytes[] = {0x62, 0xf2, 0xfd, 0x29, 0x92, 0x04, 0x08};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VGATHERDPD"), 5508, gather_operands, 2, gather_attributes,
                                                                 wildcard, BUSTER_ARRAY_LENGTH(wildcard), gather_bytes,
                                                                 BUSTER_ARRAY_LENGTH(gather_bytes)));
        gather_operands[1].memory.index.index = 4;
        u8 gather_index4_bytes[] = {0x62, 0xf2, 0xfd, 0x29, 0x92, 0x04, 0x20};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VGATHERDPD"), 5508, gather_operands, 2, gather_attributes,
                                                                 wildcard, BUSTER_ARRAY_LENGTH(wildcard), gather_index4_bytes,
                                                                 BUSTER_ARRAY_LENGTH(gather_index4_bytes)));
        gather_operands[1].memory.index.index = 17;
        u8 gather_high_bytes[] = {0x62, 0xf2, 0xfd, 0x21, 0x92, 0x04, 0x08};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VGATHERDPD"), 5508, gather_operands, 2, gather_attributes,
                                                                 wildcard, BUSTER_ARRAY_LENGTH(wildcard), gather_high_bytes,
                                                                 BUSTER_ARRAY_LENGTH(gather_high_bytes)));
        BusterX86MetadataPhysicalOperand ordinary_vsib = vaddps_memory_operands[2];
        ordinary_vsib.memory.vsib = true;
        ordinary_vsib.memory.has_index = true;
        ordinary_vsib.memory.index = (BusterX86MetadataPhysicalRegister){
            .index = 1, .width = 256, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM};
        BusterX86MetadataPhysicalOperand ordinary_vsib_operands[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], ordinary_vsib};
        BusterX86MetadataEmitResult ordinary_vsib_result = x86_64_metadata_test_emit_form(
            S8("VADDPS"), 6940, ordinary_vsib_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, ordinary_vsib_result.status == BUSTER_X86_METADATA_ENCODE_ADDRESSING);
        ordinary_vsib.memory.has_index = false;
        BusterX86MetadataPhysicalOperand vsib_without_index_operands[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], ordinary_vsib};
        BusterX86MetadataEmitResult vsib_without_index = x86_64_metadata_test_emit_form(
            S8("VADDPS"), 6940, vsib_without_index_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, vsib_without_index.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand tuple_disp1[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], x86_64_metadata_test_physical_mem_base(0, 32, 1)};
        BusterX86MetadataPhysicalOperand tuple_disp64[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], x86_64_metadata_test_physical_mem_base(0, 32, 64)};
        u8 tuple_disp1_bytes[] = {0x62, 0xf1, 0x74, 0x48, 0x58, 0x80, 0x01, 0x00, 0x00, 0x00};
        u8 tuple_disp64_bytes[] = {0x62, 0xf1, 0x74, 0x48, 0x58, 0x40, 0x01};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 6940, tuple_disp1, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), tuple_disp1_bytes,
                                                                 BUSTER_ARRAY_LENGTH(tuple_disp1_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 6940, tuple_disp64, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), tuple_disp64_bytes,
                                                                 BUSTER_ARRAY_LENGTH(tuple_disp64_bytes)));
        BusterX86MetadataPhysicalOperand tuple_symbol_memory = x86_64_metadata_test_physical_mem_base(0, 32, 0);
        tuple_symbol_memory.memory.symbol = S8("data");
        tuple_symbol_memory.memory.addend = 7;
        tuple_symbol_memory.memory.has_symbol = true;
        BusterX86MetadataPhysicalOperand tuple_symbol_operands[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], tuple_symbol_memory};
        u8 tuple_symbol_bytes[32] = {0};
        BusterX86MetadataRelocation tuple_symbol_relocations[2] = {0};
        BusterX86MetadataEmitResult tuple_symbol = x86_64_metadata_test_emit_form(
            S8("VADDPS"), 6940, tuple_symbol_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), tuple_symbol_bytes, BUSTER_ARRAY_LENGTH(tuple_symbol_bytes), tuple_symbol_relocations,
            BUSTER_ARRAY_LENGTH(tuple_symbol_relocations));
        BUSTER_TEST(arguments, tuple_symbol.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && tuple_symbol.byte_count == 10 &&
                                   tuple_symbol.relocation_count == 1 && tuple_symbol_relocations[0].offset == 6 &&
                                   tuple_symbol_relocations[0].width == 4 &&
                                   tuple_symbol_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_SIGN_EXTENDED &&
                                   tuple_symbol_relocations[0].addend == 7);

        BusterX86MetadataPhysicalOperand addr32_symbol_memory = {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
            .width = 8,
            .memory = {.address_size = 32, .scale = 1, .symbol = S8("data"), .has_symbol = true},
        };
        BusterX86MetadataPhysicalOperand addr32_symbol_operands[2] = {
            addr32_symbol_memory, x86_64_metadata_test_physical_imm(1, 8)};
        BusterX86MetadataPhysicalQuery addr32_symbol_query = x86_64_metadata_test_physical_query(
            S8("MOV"), addr32_symbol_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        addr32_symbol_query.address_size = 32;
        BusterX86MetadataRelocation addr32_symbol_relocations[2] = {0};
        BusterX86MetadataEmitResult addr32_symbol_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = addr32_symbol_query, .form_id = 9532, .output = (u8[32]){0},
                                         .output_capacity = 32, .relocations = addr32_symbol_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(addr32_symbol_relocations)});
        BusterX86MetadataPhysicalOperand addr64_symbol_memory = addr32_symbol_memory;
        addr64_symbol_memory.memory.address_size = 64;
        BusterX86MetadataPhysicalOperand addr64_symbol_operands[2] = {
            addr64_symbol_memory, x86_64_metadata_test_physical_imm(1, 8)};
        BusterX86MetadataPhysicalQuery addr64_symbol_query = x86_64_metadata_test_physical_query(
            S8("MOV"), addr64_symbol_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataRelocation addr64_symbol_relocations[2] = {0};
        BusterX86MetadataEmitResult addr64_symbol_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = addr64_symbol_query, .form_id = 9532, .output = (u8[32]){0},
                                         .output_capacity = 32, .relocations = addr64_symbol_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(addr64_symbol_relocations)});
        BUSTER_TEST(arguments, addr32_symbol_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   addr32_symbol_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_ZERO_EXTENDED &&
                                   addr64_symbol_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   addr64_symbol_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_SIGN_EXTENDED);
        BusterX86MetadataPhysicalOperand overflowing_memory = addr64_symbol_memory;
        overflowing_memory.memory.addend = INT64_MAX;
        overflowing_memory.memory.displacement = 1;
        BusterX86MetadataPhysicalOperand overflowing_operands[2] = {
            overflowing_memory, x86_64_metadata_test_physical_imm(1, 8)};
        BusterX86MetadataRelocation overflowing_relocations[2] = {0};
        BusterX86MetadataEmitResult overflowing_addend = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532, overflowing_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[32]){0}, 32, overflowing_relocations,
            BUSTER_ARRAY_LENGTH(overflowing_relocations));
        BUSTER_TEST(arguments, overflowing_addend.status == BUSTER_X86_METADATA_ENCODE_DISPLACEMENT_RANGE &&
                                   overflowing_addend.diagnostic_value == INT64_MAX);

        BusterX86MetadataPhysicalOperand addr32_memory = {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
            .width = 8,
            .memory = {.address_size = 32, .scale = 1, .displacement = (s64)UINT32_C(0xfffffff0), .has_displacement = true},
        };
        BusterX86MetadataPhysicalOperand addr32_operands[2] = {
            addr32_memory, x86_64_metadata_test_physical_imm(1, 8)};
        BusterX86MetadataPhysicalQuery addr32_query = x86_64_metadata_test_physical_query(
            S8("MOV"), addr32_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        addr32_query.address_size = 32;
        u8 addr32_bytes[32] = {0};
        BusterX86MetadataEmitResult addr32_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = addr32_query, .form_id = 9532, .output = addr32_bytes,
                                         .output_capacity = BUSTER_ARRAY_LENGTH(addr32_bytes)});
        BUSTER_TEST(arguments, addr32_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && addr32_emit.byte_count == 9 &&
                                   x86_64_metadata_test_bytes_equal(addr32_bytes, addr32_emit.byte_count,
                                                                     (u8[]){0x67, 0xc6, 0x04, 0x25, 0xf0, 0xff, 0xff, 0xff, 0x01}, 9));

        BusterX86MetadataForm evv_form = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_form(463, &evv_form) &&
                                   evv_form.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX);


        BusterX86MetadataPhysicalQuery system_query = x86_64_metadata_test_physical_query(
            S8("HLT"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        u8 system_bytes[32] = {0};
        BusterX86MetadataEmitResult system_default = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = system_query, .form_id = 10069, .output = system_bytes, .output_capacity = sizeof(system_bytes)});
        system_query.include_privileged = true;
        BusterX86MetadataEmitResult system_opt_in = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = system_query, .form_id = 10069, .output = system_bytes, .output_capacity = sizeof(system_bytes)});
        BUSTER_TEST(arguments, system_default.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   system_default.required_feature.length == 0);
        BUSTER_TEST(arguments, system_opt_in.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && system_opt_in.byte_count == 1 &&
                                   system_bytes[0] == 0xf4 && system_opt_in.required_feature.length == 0);
    }

    {
        String8 wildcard[1] = {S8("*")};
        String8 svm_features[1] = {S8("svm")};
        // Fixed REG/RM fields and fixed implicit registers are emitted from
        // the schema even when no dynamic binding owns that field.
        u8 vmmcall_bytes[8] = {0};
        BusterX86MetadataEmitResult vmmcall = x86_64_metadata_test_emit_form(
            S8("VMMCALL"), 448, 0, 0, (BusterX86MetadataPhysicalAttributes){0}, svm_features, BUSTER_ARRAY_LENGTH(svm_features),
            vmmcall_bytes, sizeof(vmmcall_bytes), 0, 0);
        BUSTER_TEST(arguments, vmmcall.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && vmmcall.byte_count == 3 &&
                                   vmmcall_bytes[0] == 0x0f && vmmcall_bytes[1] == 0x01 && vmmcall_bytes[2] == 0xd9);
        BusterX86MetadataPhysicalOperand div_operand =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
        u8 div_bytes[] = {0x48, 0xf7, 0xf0};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("DIV"), 9468, &div_operand, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), div_bytes,
                                                                 BUSTER_ARRAY_LENGTH(div_bytes)));

        BusterX86MetadataPhysicalOperand fixed_al_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8),
            x86_64_metadata_test_physical_imm(1, 8),
        };
        BusterX86MetadataPhysicalQuery fixed_al_query = x86_64_metadata_test_physical_query(
            S8("ADD"), fixed_al_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        fixed_al_query.include_implicit = true;
        u8 fixed_bytes[32] = {0};
        BusterX86MetadataEmitResult fixed_al = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = fixed_al_query, .form_id = 9625, .output = fixed_bytes, .output_capacity = sizeof(fixed_bytes)});
        fixed_al_operands[0] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 3, 8);
        BusterX86MetadataEmitResult fixed_bl = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = fixed_al_query, .form_id = 9625, .output = fixed_bytes, .output_capacity = sizeof(fixed_bytes)});
        BUSTER_TEST(arguments, fixed_al.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && fixed_al.byte_count == 2 && fixed_bytes[0] == 0x04 &&
                                   fixed_bytes[1] == 0x01);
        BUSTER_TEST(arguments, fixed_bl.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);

        BusterX86MetadataPhysicalOperand low_byte_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 4, 8),
            x86_64_metadata_test_physical_imm(1, 8),
        };
        u8 low_byte_rex[] = {0x40, 0xb4, 0x01};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("MOV"), 10017, low_byte_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), low_byte_rex,
                                                                 BUSTER_ARRAY_LENGTH(low_byte_rex)));
        low_byte_operands[0].reg.high_byte = true;
        u8 high_byte_no_rex[] = {0xb4, 0x01};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("MOV"), 10017, low_byte_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), high_byte_no_rex,
                                                                 BUSTER_ARRAY_LENGTH(high_byte_no_rex)));
        BusterX86MetadataPhysicalOperand high_byte_apx[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 4, 8),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 8, 8),
        };
        high_byte_apx[0].reg.high_byte = true;
        BusterX86MetadataEmitResult high_byte_rex = x86_64_metadata_test_emit_form(
            S8("ADD"), 547, high_byte_apx, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, high_byte_rex.status == BUSTER_X86_METADATA_ENCODE_HIGH_BYTE_WITH_REX);
    }

    {
        String8 wildcard[1] = {S8("*")};
        // Relocations are format-neutral and use the final instruction end
        // as P for every PC-relative width, including trailing immediates.
        BusterX86MetadataPhysicalOperand branch_literal = x86_64_metadata_test_physical_relative(0, 8);
        BusterX86MetadataPhysicalQuery branch_literal_query = x86_64_metadata_test_physical_query(
            S8("JMP"), &branch_literal, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult branch_literal_select = buster_x86_metadata_select_form(branch_literal_query);
        u8 branch_literal_bytes[32] = {0};
        BusterX86MetadataEmitResult branch_literal_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = branch_literal_query, .form_id = branch_literal_select.form_id,
                                         .output = branch_literal_bytes, .output_capacity = sizeof(branch_literal_bytes)});
        BUSTER_TEST(arguments, branch_literal_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   branch_literal_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && branch_literal_emit.byte_count == 2 &&
                                   branch_literal_emit.relocation_count == 0 && branch_literal_bytes[0] == 0xeb && branch_literal_bytes[1] == 0);

        BusterX86MetadataPhysicalOperand branch = {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE, .width = 8, .symbol = S8("target"), .addend = 7,
            .has_symbol = true,
        };
        BusterX86MetadataPhysicalQuery branch_query = x86_64_metadata_test_physical_query(
            S8("JMP"), &branch, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult branch_select = buster_x86_metadata_select_form(branch_query);
        u8 branch_bytes[32] = {0};
        BusterX86MetadataRelocation branch_relocations[2] = {0};
        BusterX86MetadataEmitResult branch_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = branch_query, .form_id = branch_select.form_id, .output = branch_bytes,
                                         .output_capacity = sizeof(branch_bytes), .relocations = branch_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(branch_relocations)});
        BUSTER_TEST(arguments, branch_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && branch_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   branch_emit.byte_count == 2 && branch_emit.relocation_count == 1 && branch_relocations[0].offset == 1 &&
                                   branch_relocations[0].width == 1 && branch_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC8 &&
                                   branch_relocations[0].addend == 6);

        BusterX86MetadataPhysicalOperand branch32 = {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE, .width = 32, .symbol = S8("target"),
            .addend = 0, .has_symbol = true,
        };
        BusterX86MetadataPhysicalQuery branch32_query = x86_64_metadata_test_physical_query(
            S8("JMP"), &branch32, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult branch32_select = buster_x86_metadata_select_form(branch32_query);
        u8 branch32_bytes[32] = {0};
        BusterX86MetadataRelocation branch32_relocations[2] = {0};
        BusterX86MetadataEmitResult branch32_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = branch32_query, .form_id = branch32_select.form_id,
                                         .output = branch32_bytes, .output_capacity = sizeof(branch32_bytes),
                                         .relocations = branch32_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(branch32_relocations)});
        BUSTER_TEST(arguments, branch32_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && branch32_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   branch32_emit.byte_count == 5 && branch32_relocations[0].offset == 1 &&
                                   branch32_relocations[0].width == 4 && branch32_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC32 &&
                                   branch32_relocations[0].addend == -4);
        BusterX86MetadataPhysicalOperand branch_auto = x86_64_metadata_test_physical_relative(0, 0);
        BusterX86MetadataPhysicalQuery branch_auto_query = x86_64_metadata_test_physical_query(
            S8("JMP"), &branch_auto, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult branch_auto_select = buster_x86_metadata_select_form(branch_auto_query);
        BUSTER_TEST(arguments, branch_auto_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   branch_auto_select.selected_byte_count == 2);
        BusterX86MetadataEmitResult branch_direct_width_mismatch = x86_64_metadata_test_emit_form(
            S8("JMP"), branch_literal_select.form_id, &branch32, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[32]){0}, 32, 0, 0);
        BUSTER_TEST(arguments, branch_direct_width_mismatch.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);

        BusterX86MetadataPhysicalOperand rip_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_mem_rip(S8("data"), 3, 64),
        };
        BusterX86MetadataPhysicalQuery rip_query = x86_64_metadata_test_physical_query(
            S8("MOV"), rip_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult rip_select = buster_x86_metadata_select_form(rip_query);
        u8 rip_bytes[32] = {0};
        BusterX86MetadataRelocation rip_relocations[2] = {0};
        BusterX86MetadataEmitResult rip_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = rip_query, .form_id = rip_select.form_id, .output = rip_bytes,
                                         .output_capacity = sizeof(rip_bytes), .relocations = rip_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(rip_relocations)});
        BUSTER_TEST(arguments, rip_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && rip_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   rip_emit.byte_count == 7 && rip_emit.relocation_count == 1 && rip_relocations[0].offset == 3 &&
                                   rip_relocations[0].width == 4 && rip_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC32 &&
                                   rip_relocations[0].addend == -1);

        BusterX86MetadataPhysicalOperand trailing_operands[2] = {
            x86_64_metadata_test_physical_mem_rip(S8("data"), 3, 64),
            x86_64_metadata_test_physical_imm(127, 8),
        };
        BusterX86MetadataPhysicalQuery trailing_query = x86_64_metadata_test_physical_query(
            S8("ADD"), trailing_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult trailing_select = buster_x86_metadata_select_form(trailing_query);
        u8 trailing_bytes[32] = {0};
        BusterX86MetadataRelocation trailing_relocations[2] = {0};
        BusterX86MetadataEmitResult trailing_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = trailing_query, .form_id = trailing_select.form_id, .output = trailing_bytes,
                                         .output_capacity = sizeof(trailing_bytes), .relocations = trailing_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(trailing_relocations)});
        BUSTER_TEST(arguments, trailing_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && trailing_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   trailing_emit.relocation_count == 1 && trailing_relocations[0].offset == 2 &&
                                   trailing_relocations[0].width == 4 && trailing_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC32 &&
                                   trailing_relocations[0].addend == -2);

        BusterX86MetadataPhysicalOperand absolute = x86_64_metadata_test_physical_absolute(S8("target"), 64);
        BusterX86MetadataPhysicalQuery absolute_query = x86_64_metadata_test_physical_query(
            S8("JMPABS"), &absolute, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        u8 absolute_bytes[32] = {0};
        BusterX86MetadataRelocation absolute_relocations[2] = {0};
        BusterX86MetadataEmitResult absolute_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = absolute_query, .form_id = 2931, .output = absolute_bytes,
                                         .output_capacity = sizeof(absolute_bytes), .relocations = absolute_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(absolute_relocations)});
        BUSTER_TEST(arguments, absolute_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && absolute_emit.byte_count == 11 &&
                                   absolute_emit.relocation_count == 1 && absolute_relocations[0].offset == 3 &&
                                   absolute_relocations[0].width == 8 && absolute_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_ABSOLUTE64 &&
                                   absolute_relocations[0].addend == 0);
        BusterX86MetadataPhysicalOperand absolute32 = x86_64_metadata_test_physical_absolute(S8("target"), 32);
        BusterX86MetadataEmitResult absolute_width_mismatch = x86_64_metadata_test_emit_form(
            S8("JMPABS"), 2931, &absolute32, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[32]){0}, 32, 0, 0);
        BUSTER_TEST(arguments, absolute_width_mismatch.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
    }

    {
        String8 wildcard[1] = {S8("*")};
        String8 avx512_features[2] = {S8("avx512f"), S8("avx512vl")};
        String8 avx512_apx_features[3] = {S8("avx512f"), S8("avx512vl"), S8("apx")};
        BusterX86MetadataPhysicalOperand vector16_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 16, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 17, 128),
        };
        u8 vector16_bytes[] = {0x62, 0xa1, 0x7d, 0x08, 0x6f, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VMOVDQA32"), 5583, vector16_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, avx512_features,
                                                                 BUSTER_ARRAY_LENGTH(avx512_features), vector16_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vector16_bytes)));

        BusterX86MetadataPhysicalOperand egpr_memory_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_mem_base(16, 32, 0),
        };
        BusterX86MetadataEmitResult egpr_without_apx = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5584, egpr_memory_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, avx512_features,
            BUSTER_ARRAY_LENGTH(avx512_features), (u8[1]){0}, 0, 0, 0);
        u8 egpr_bytes[32] = {0};
        BusterX86MetadataEmitResult egpr_with_apx = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5584, egpr_memory_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, avx512_apx_features,
            BUSTER_ARRAY_LENGTH(avx512_apx_features), egpr_bytes, sizeof(egpr_bytes), 0, 0);
        BUSTER_TEST(arguments, egpr_without_apx.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE);
        BUSTER_TEST(arguments, egpr_with_apx.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && egpr_with_apx.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(egpr_bytes, egpr_with_apx.byte_count,
                                                                     (u8[]){0x62, 0xf9, 0x7d, 0x08, 0x6f, 0x00}, 6));
        egpr_memory_operands[1] = x86_64_metadata_test_physical_mem_base(31, 32, 0);
        BusterX86MetadataEmitResult egpr31_without_apx = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5584, egpr_memory_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, avx512_features,
            BUSTER_ARRAY_LENGTH(avx512_features), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, egpr31_without_apx.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE);

        BusterX86MetadataPhysicalOperand legacy_base16[2] = {
            x86_64_metadata_test_physical_mem_base(16, 8, 0), x86_64_metadata_test_physical_imm(1, 8),
        };
        u8 legacy_egpr_bytes[8] = {0};
        BusterX86MetadataEmitResult legacy_egpr = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532, legacy_base16, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), legacy_egpr_bytes, BUSTER_ARRAY_LENGTH(legacy_egpr_bytes), 0, 0);
        BusterX86MetadataEmitResult legacy_egpr_without_apx = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532, legacy_base16, 2, (BusterX86MetadataPhysicalAttributes){0}, 0, 0, (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, legacy_egpr_without_apx.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   x86_64_metadata_test_string_equal(legacy_egpr_without_apx.required_feature, S8("APX_F")));
        BUSTER_TEST(arguments, legacy_egpr.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && legacy_egpr.byte_count == 5 &&
                                   x86_64_metadata_test_bytes_equal(legacy_egpr_bytes, legacy_egpr.byte_count,
                                                                     (u8[]){0xd5, 0x10, 0xc6, 0x00, 0x01}, 5));
        BusterX86MetadataPhysicalOperand malformed_base_width = x86_64_metadata_test_physical_mem_base(3, 8, 0);
        malformed_base_width.memory.base.width = 8;
        BusterX86MetadataEmitResult malformed_base_width_result = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532,
            (BusterX86MetadataPhysicalOperand[]){malformed_base_width, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        malformed_base_width.memory.base.width = 64;
        malformed_base_width.memory.base.high_byte = true;
        BusterX86MetadataEmitResult malformed_base_high_byte = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532,
            (BusterX86MetadataPhysicalOperand[]){malformed_base_width, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataPhysicalOperand malformed_index_width = x86_64_metadata_test_physical_mem_index(1, 2, 8, 0);
        malformed_index_width.memory.index.width = 8;
        BusterX86MetadataEmitResult malformed_index_width_result = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532,
            (BusterX86MetadataPhysicalOperand[]){malformed_index_width, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        malformed_index_width.memory.index.width = 64;
        malformed_index_width.memory.index.high_byte = true;
        BusterX86MetadataEmitResult malformed_index_high_byte = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532,
            (BusterX86MetadataPhysicalOperand[]){malformed_index_width, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, malformed_base_width_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   malformed_base_high_byte.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   malformed_index_width_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   malformed_index_high_byte.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand base_less = x86_64_metadata_test_physical_mem_index(1, 4, 32, 16);
        BusterX86MetadataPhysicalOperand base_less_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32), base_less,
        };
        BusterX86MetadataPhysicalQuery base_less_query = x86_64_metadata_test_physical_query(
            S8("MOV"), base_less_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult base_less_select = buster_x86_metadata_select_form(base_less_query);
        u8 base_less_bytes[32] = {0};
        BusterX86MetadataEmitResult base_less_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = base_less_query, .form_id = base_less_select.form_id,
                                         .output = base_less_bytes, .output_capacity = sizeof(base_less_bytes)});
        BUSTER_TEST(arguments, base_less_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(base_less_bytes, base_less_emit.byte_count,
                                                                     (u8[]){0x8b, 0x04, 0x8d, 0x10, 0x00, 0x00, 0x00}, 7));

        BusterX86MetadataPhysicalOperand invalid_index = x86_64_metadata_test_physical_mem_index(4, 2, 8, 0);
        invalid_index.memory.has_base = true;
        invalid_index.memory.base = (BusterX86MetadataPhysicalRegister){.index = 3, .width = 64,
                                                                          .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR};
        BusterX86MetadataPhysicalOperand invalid_index_operands[2] = {invalid_index, x86_64_metadata_test_physical_imm(1, 8)};
        BusterX86MetadataEmitResult invalid_sib = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532, invalid_index_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, invalid_sib.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand overflow_memory = x86_64_metadata_test_physical_mem_index(1, 1, 32, INT64_C(0x100000000));
        BusterX86MetadataPhysicalOperand overflow_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128), overflow_memory,
        };
        BusterX86MetadataEmitResult overflow_emit = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5584, overflow_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataPhysicalQuery overflow_query = x86_64_metadata_test_physical_query(
            S8("VMOVDQA32"), overflow_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult overflow_select = buster_x86_metadata_select_form(overflow_query);
        BUSTER_TEST(arguments, overflow_emit.status == BUSTER_X86_METADATA_ENCODE_DISPLACEMENT_RANGE &&
                                   overflow_emit.diagnostic_value == INT64_C(0x100000000) &&
                                   overflow_select.status == BUSTER_X86_METADATA_ENCODE_DISPLACEMENT_RANGE &&
                                   overflow_select.diagnostic_value == INT64_C(0x100000000));

        BusterX86MetadataPhysicalOperand mismatch_memory = x86_64_metadata_test_physical_mem_base(3, 32, 0);
        BusterX86MetadataPhysicalOperand mismatch_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128), mismatch_memory,
        };
        BusterX86MetadataPhysicalQuery mismatch_query = x86_64_metadata_test_physical_query(
            S8("VMOVDQA32"), mismatch_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        mismatch_query.address_size = 32;
        BusterX86MetadataEmitResult address_size_mismatch = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = mismatch_query, .form_id = 5584, .output = (u8[32]){0}, .output_capacity = 32});
        mismatch_memory.memory.has_segment = true;
        mismatch_memory.memory.segment = BUSTER_X86_METADATA_SEGMENT_NONE;
        mismatch_operands[1] = mismatch_memory;
        BusterX86MetadataPhysicalQuery bad_segment_query = x86_64_metadata_test_physical_query(
            S8("VMOVDQA32"), mismatch_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataEmitResult bad_segment = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = bad_segment_query, .form_id = 5584, .output = (u8[32]){0}, .output_capacity = 32});
        BUSTER_TEST(arguments, address_size_mismatch.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   bad_segment.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        mismatch_memory.memory.has_segment = false;
        mismatch_memory.memory.segment = BUSTER_X86_METADATA_SEGMENT_DS;
        mismatch_operands[1] = mismatch_memory;
        bad_segment_query.operands = mismatch_operands;
        bad_segment = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = bad_segment_query, .form_id = 5584, .output = (u8[32]){0}, .output_capacity = 32});
        BUSTER_TEST(arguments, bad_segment.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand vsib_memory = x86_64_metadata_test_physical_mem_base(3, 32, 0);
        vsib_memory.memory.vsib = true;
        BusterX86MetadataPhysicalOperand vsib_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128), vsib_memory,
        };
        BusterX86MetadataEmitResult ordinary_vsib = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5584, vsib_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, ordinary_vsib.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);
    }

    {
        String8 wildcard[1] = {S8("*")};
        BusterX86MetadataPhysicalQuery decorator_query = x86_64_metadata_test_physical_query(
            S8("VADDPS"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataPhysicalAttributes mask_flag_only = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK,
        };
        BusterX86MetadataPhysicalAttributes mask_payload_only = {
            .mask_register = 1,
            .has_mask_register = true,
        };
        BusterX86MetadataPhysicalAttributes broadcast_flag_only = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_BROADCAST,
        };
        BusterX86MetadataPhysicalAttributes broadcast_payload_only = {.broadcast_elements = 16};
        BusterX86MetadataPhysicalAttributes rounding_flag_only = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_ROUNDING,
        };
        BusterX86MetadataPhysicalAttributes rounding_payload_only = {.rounding_mode = BUSTER_X86_METADATA_ROUNDING_DOWN};
        BusterX86MetadataPhysicalAttributes sae_flag_only = {.decorator_flags = BUSTER_X86_METADATA_DECORATOR_SAE};
        BusterX86MetadataPhysicalAttributes sae_payload_only = {.sae = true};
        BusterX86MetadataPhysicalQuery coherent_mask_query = decorator_query;
        coherent_mask_query.attributes = (BusterX86MetadataPhysicalAttributes){
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK,
            .has_mask_register = true, .mask_register = 1,
        };
        BusterX86MetadataPhysicalQuery coherent_broadcast_query = decorator_query;
        coherent_broadcast_query.attributes = (BusterX86MetadataPhysicalAttributes){
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_BROADCAST, .broadcast_elements = 16,
        };
        BusterX86MetadataPhysicalOperand coherent_broadcast_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
            x86_64_metadata_test_physical_mem_base(0, 32, 0),
        };
        coherent_broadcast_query.operands = coherent_broadcast_operands;
        coherent_broadcast_query.operand_count = BUSTER_ARRAY_LENGTH(coherent_broadcast_operands);
        BusterX86MetadataPhysicalQuery coherent_rounding_query = decorator_query;
        coherent_rounding_query.attributes = (BusterX86MetadataPhysicalAttributes){
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_ROUNDING, .rounding_mode = BUSTER_X86_METADATA_ROUNDING_DOWN,
        };
        BusterX86MetadataPhysicalQuery coherent_sae_query = decorator_query;
        coherent_sae_query.attributes = (BusterX86MetadataPhysicalAttributes){
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_SAE, .sae = true,
        };
        BusterX86MetadataSelectResult coherent_mask = buster_x86_metadata_select_form(coherent_mask_query);
        BusterX86MetadataSelectResult coherent_broadcast = buster_x86_metadata_select_form(coherent_broadcast_query);
        BusterX86MetadataSelectResult coherent_rounding = buster_x86_metadata_select_form(coherent_rounding_query);
        BusterX86MetadataSelectResult coherent_sae = buster_x86_metadata_select_form(coherent_sae_query);
        BusterX86MetadataPhysicalQuery duplicate_query = decorator_query;
        duplicate_query.attributes = mask_flag_only;
        BusterX86MetadataSelectResult mask_flag_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = mask_payload_only;
        BusterX86MetadataSelectResult mask_payload_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = broadcast_flag_only;
        BusterX86MetadataSelectResult broadcast_flag_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = broadcast_payload_only;
        BusterX86MetadataSelectResult broadcast_payload_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = rounding_flag_only;
        BusterX86MetadataSelectResult rounding_flag_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = rounding_payload_only;
        BusterX86MetadataSelectResult rounding_payload_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = sae_flag_only;
        BusterX86MetadataSelectResult sae_flag_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = sae_payload_only;
        BusterX86MetadataSelectResult sae_payload_only_result = buster_x86_metadata_select_form(duplicate_query);
        BUSTER_TEST(arguments, coherent_mask.status != BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   coherent_broadcast.status != BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   coherent_rounding.status != BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   coherent_sae.status != BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   mask_flag_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   mask_payload_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   broadcast_flag_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   broadcast_payload_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   rounding_flag_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   rounding_payload_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   sae_flag_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   sae_payload_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand immediate = x86_64_metadata_test_physical_imm(1, 8);
        immediate.has_symbol = true;
        immediate.symbol = S8("target");
        BusterX86MetadataEmitResult both_immediate_states = x86_64_metadata_test_emit_form(
            S8("MOV"), 10017, (BusterX86MetadataPhysicalOperand[]){
                              x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8), immediate},
            2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataPhysicalOperand neither_immediate_states = x86_64_metadata_test_physical_imm(0, 8);
        neither_immediate_states.has_value = false;
        BusterX86MetadataEmitResult neither_immediate = x86_64_metadata_test_emit_form(
            S8("MOV"), 10017, (BusterX86MetadataPhysicalOperand[]){
                              x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8), neither_immediate_states},
            2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, both_immediate_states.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   neither_immediate.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand malformed_register = x86_64_metadata_test_physical_reg(
            BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8);
        malformed_register.reg.high_byte = true;
        BusterX86MetadataEmitResult malformed_high_byte = x86_64_metadata_test_emit_form(
            S8("MOV"), 10017, (BusterX86MetadataPhysicalOperand[]){malformed_register, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        malformed_register = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8);
        malformed_register.reg.width = 16;
        BusterX86MetadataEmitResult mismatched_register_width = x86_64_metadata_test_emit_form(
            S8("MOV"), 10017, (BusterX86MetadataPhysicalOperand[]){malformed_register, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataPhysicalOperand malformed_symbol = x86_64_metadata_test_physical_imm(1, 8);
        malformed_symbol.symbol = S8("target");
        malformed_symbol.symbol.length = 0;
        BusterX86MetadataEmitResult malformed_symbol_result = x86_64_metadata_test_emit_form(
            S8("MOV"), 10017, (BusterX86MetadataPhysicalOperand[]){x86_64_metadata_test_physical_reg(
                                                                        BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8),
                                                                    malformed_symbol},
            2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataPhysicalOperand scaled_without_index = x86_64_metadata_test_physical_mem_base(3, 8, 0);
        scaled_without_index.memory.scale = 4;
        BusterX86MetadataEmitResult scaled_without_index_result = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532, (BusterX86MetadataPhysicalOperand[]){scaled_without_index, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, malformed_high_byte.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   mismatched_register_width.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   malformed_symbol_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   scaled_without_index_result.status == BUSTER_X86_METADATA_ENCODE_ADDRESSING);

        BUSTER_TEST(arguments, buster_x86_metadata_instruction_length_status(15) == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   buster_x86_metadata_instruction_length_status(16) ==
                                       BUSTER_X86_METADATA_ENCODE_INSTRUCTION_LENGTH);

        BusterX86MetadataPhysicalAttributes rep_conflict = {.rep = true, .repne = true};
        BusterX86MetadataEmitResult rep_conflict_result = x86_64_metadata_test_emit_form(
            S8("VMMCALL"), 448, 0, 0, rep_conflict, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult lock_register = x86_64_metadata_test_emit_form(
            S8("NOT"), 9459, &(BusterX86MetadataPhysicalOperand){
                                  .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER, .width = 64,
                                  .reg = {.index = 0, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR}},
            1, (BusterX86MetadataPhysicalAttributes){.lock = true}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, rep_conflict_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   lock_register.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION);

        BusterX86MetadataEmitResult wrong_mnemonic = x86_64_metadata_test_emit_form(
            S8("ADD"), 10018, (BusterX86MetadataPhysicalOperand[]){
                              x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
                              x86_64_metadata_test_physical_imm(1, 64)},
            2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, wrong_mnemonic.status == BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM);

        BusterX86MetadataPhysicalQuery output_query = x86_64_metadata_test_physical_query(
            S8("MOV"), (BusterX86MetadataPhysicalOperand[]){
                           x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
                           x86_64_metadata_test_physical_imm(1, 64)},
            2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataEmitResult output_short = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = output_query, .form_id = 10018, .output = 0, .output_capacity = 0});
        BUSTER_TEST(arguments, output_short.status == BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY && output_short.required_byte_count == 10);

    }
    return result;
}
#endif
