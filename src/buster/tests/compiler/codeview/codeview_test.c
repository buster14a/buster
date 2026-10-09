#include <buster/tests/compiler/codeview/codeview_test.h>
#if BUSTER_INCLUDE_TESTS


BUSTER_GLOBAL_LOCAL u16 codeview_test_u16(u8 const* bytes)
{
    u16 value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

BUSTER_GLOBAL_LOCAL u32 codeview_test_u32(u8 const* bytes)
{
    u32 value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codeview_test_global_linkage(UnitTestArguments* arguments)
{
    enum {TEST_LDATA32 = 0x110c, TEST_GDATA32 = 0x110d};
    UnitTestResult result = {0};
    String8 path = S8("linkage.c");
    DebugType type = {.kind = DEBUG_TYPE_BASE, .name = S8("int"), .size = 4};
    DebugVariable variables[] = {
        {.name = S8("static_hidden"), .linkage_name = S8("static_hidden"), .type = 0,
         .symbol = {.value = 4}, .kind = DEBUG_VARIABLE_GLOBAL, .is_internal = true},
        {.name = S8("public_data"), .linkage_name = S8("public_data"), .type = 0,
         .symbol = {.value = 5}, .kind = DEBUG_VARIABLE_GLOBAL},
    };
    DebugModel model = {.types = &type, .type_count = 1, .variables = variables, .variable_count = BUSTER_ARRAY_LENGTH(variables), .valid = true};
    CodeviewResult built = codeview_build(arguments->arena, (CodeviewInput){.model = &model, .file_paths = &path, .file_count = 1,
        .producer = S8("buster"), .machine = CODEVIEW_MACHINE_X64});
    bool valid = built.valid && built.symbols.length >= 4 && codeview_test_u32(built.symbols.pointer) == CODEVIEW_TEST_SIGNATURE_C13;
    u32 local_count = 0;
    u32 public_count = 0;
    u64 subsection = 4;
    while (valid && subsection + 8 <= built.symbols.length)
    {
        u32 kind = codeview_test_u32(built.symbols.pointer + subsection);
        u32 length = codeview_test_u32(built.symbols.pointer + subsection + 4);
        u64 payload = subsection + 8;
        valid = length <= built.symbols.length - payload;
        u64 cursor = payload;
        while (valid && kind == CODEVIEW_TEST_SYMBOLS && cursor + 4 <= payload + length)
        {
            u16 record_length = codeview_test_u16(built.symbols.pointer + cursor);
            u16 record_kind = codeview_test_u16(built.symbols.pointer + cursor + 2);
            valid = record_length >= 2 && (u64)record_length + 2 <= payload + length - cursor;
            if (valid && (record_kind == TEST_LDATA32 || record_kind == TEST_GDATA32))
            {
                u64 name = cursor + 14;
                u64 end = cursor + 2 + record_length;
                valid = name < end;
                if (valid)
                {
                    u64 name_end = name;
                    while (name_end < end && built.symbols.pointer[name_end]) name_end += 1;
                    String8 spelling = {.pointer = (char8*)built.symbols.pointer + name, .length = name_end - name};
                    local_count += record_kind == TEST_LDATA32 && string_equal(spelling, S8("static_hidden")) && name_end < end;
                    public_count += record_kind == TEST_GDATA32 && string_equal(spelling, S8("public_data")) && name_end < end;
                }
            }
            cursor += (u64)record_length + 2;
        }
        valid = valid && (kind != CODEVIEW_TEST_SYMBOLS || cursor == payload + length);
        subsection = payload + ((length + 3) & ~3u);
    }
    BUSTER_TEST(arguments, valid && subsection == built.symbols.length);
    BUSTER_TEST(arguments, local_count == 1 && public_count == 1);
    BUSTER_TEST(arguments, built.relocation_count == 4);
    return result;
}

// Decode the produced stream independently, following LF_INDEX rather than
// assuming field lists are contiguous or are emitted in primary-type order.
// Resolve a forward aggregate by its scoped unique name, independently of
// record order. This also exercises the consumer rule used by PDB readers.
BUSTER_GLOBAL_LOCAL u64 codeview_test_complete_aggregate(ByteSlice types, u64 forward)
{
    u64 result = 0;
    if (forward + 26 <= types.length)
    {
        u16 kind = codeview_test_u16(types.pointer + forward + 2);
        u64 name_offset = forward + (kind == 0x1505 ? 26 : 18);
        u64 forward_end = forward + 2 + codeview_test_u16(types.pointer + forward);
        u64 unique = name_offset;
        while (unique < forward_end && types.pointer[unique])
        {
            unique += 1;
        }
        unique += 1;
        u64 unique_end = unique;
        while (unique_end < forward_end && types.pointer[unique_end])
        {
            unique_end += 1;
        }
        for (u64 cursor = 4; unique_end < forward_end && cursor + 4 <= types.length;)
        {
            u64 size = 2 + (u64)codeview_test_u16(types.pointer + cursor);
            if (size < 4 || size > types.length - cursor)
            {
                break;
            }
            if (codeview_test_u16(types.pointer + cursor + 2) == kind && size >= (kind == 0x1505 ? 26u : 18u) &&
                !(codeview_test_u16(types.pointer + cursor + 6) & 0x80))
            {
                u64 candidate = cursor + (kind == 0x1505 ? 26 : 18);
                while (candidate < cursor + size && types.pointer[candidate])
                {
                    candidate += 1;
                }
                candidate += 1;
                u64 length = unique_end - unique;
                if (candidate < cursor + size && length < cursor + size - candidate &&
                    !memcmp(types.pointer + candidate, types.pointer + unique, length) && !types.pointer[candidate + length])
                {
                    result = cursor;
                    break;
                }
            }
            cursor += size;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codeview_test_recursive_aggregates(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 path = S8("recursive.c");
    DebugTypeField next[] = {{.name = S8("next"), .type = 1}};
    DebugTypeField previous[] = {{.name = S8("previous"), .type = 1}};
    DebugType types[] = {
        {.kind = DEBUG_TYPE_STRUCT, .name = S8("Node"), .size = 8, .fields = next, .field_count = 1},
        {.kind = DEBUG_TYPE_POINTER, .element_type = 0, .size = 8},
        {.kind = DEBUG_TYPE_UNION, .name = S8("Node"), .size = 8, .fields = previous, .field_count = 1},
        {.kind = DEBUG_TYPE_STRUCT, .size = 8, .fields = next, .field_count = 1},
        {.kind = DEBUG_TYPE_STRUCT, .size = 8, .fields = next, .field_count = 1},
        {.kind = DEBUG_TYPE_STRUCT, .name = S8("Node"), .size = 8, .fields = next, .field_count = 1},
    };
    DebugModel model = {.types = types, .type_count = BUSTER_ARRAY_LENGTH(types), .valid = true};
    for (u32 architecture = 0; architecture < 2; architecture += 1)
    {
        CodeviewResult built = codeview_build(arguments->arena, (CodeviewInput){
            .model = &model, .file_paths = &path, .file_count = 1,
            .machine = architecture ? CODEVIEW_MACHINE_ARM64 : CODEVIEW_MACHINE_X64});
        BUSTER_TEST(arguments, built.valid);
        u64 cursor = 4;
        u64 complete[6] = {0};
        for (u32 index = 0; built.valid && index < BUSTER_ARRAY_LENGTH(types) && cursor + 4 <= built.types.length; index += 1)
        {
            u8* record = built.types.pointer + cursor;
            u64 size = 2 + (u64)codeview_test_u16(record);
            BUSTER_TEST(arguments, size >= 4 && size <= built.types.length - cursor);
            if (size < 4 || size > built.types.length - cursor)
            {
                break;
            }
            if (index == 1)
            {
                // The self pointer names the original canonical aggregate ID.
                BUSTER_TEST(arguments, codeview_test_u16(record + 2) == 0x1002 && codeview_test_u32(record + 4) == 0x1000);
                BUSTER_TEST(arguments, (codeview_test_u32(record + 8) & 0x1f) == 0x0c &&
                    ((codeview_test_u32(record + 8) >> 13) & 0x3f) == 8);
            }
            else
            {
                BUSTER_TEST(arguments, size >= 26 && codeview_test_u16(record + 6) == 0x0380 &&
                    !codeview_test_u16(record + 4) && !codeview_test_u32(record + 8));
                complete[index] = codeview_test_complete_aggregate(built.types, cursor);
                BUSTER_TEST(arguments, complete[index] > cursor);
                if (complete[index])
                {
                    u8* full = built.types.pointer + complete[index];
                    BUSTER_TEST(arguments, codeview_test_u16(full + 6) == 0x0300 &&
                        codeview_test_u16(full + 4) == 1 && codeview_test_u32(full + 8) >= 0x1006);
                }
            }
            cursor += size;
        }
        // Empty friendly names remain different types, as do shadowed tags.
        BUSTER_TEST(arguments, complete[0] && complete[2] && complete[3] && complete[4] &&
            complete[0] != complete[2] && complete[3] != complete[4] && complete[5] && complete[0] != complete[5]);
        // Every field list references only canonical declarations/pointers;
        // complete records are not reachable from those declarations, so the
        // recursive source graph has no record-reference cycle.
        u32 lists = 0;
        for (u64 offset = cursor; built.valid && offset + 4 <= built.types.length;)
        {
            u64 size = 2 + (u64)codeview_test_u16(built.types.pointer + offset);
            if (size < 4 || size > built.types.length - offset)
            {
                break;
            }
            if (codeview_test_u16(built.types.pointer + offset + 2) == 0x1203)
            {
                BUSTER_TEST(arguments, size >= 18 && codeview_test_u16(built.types.pointer + offset + 4) == 0x150d &&
                    codeview_test_u32(built.types.pointer + offset + 8) == 0x1001);
                lists += 1;
            }
            offset += size;
        }
        BUSTER_TEST(arguments, lists == 5);
    }
    u64 unsupported_pointer_sizes[] = {0, 64, UINT64_MAX};
    for (u32 size_index = 0; size_index < BUSTER_ARRAY_LENGTH(unsupported_pointer_sizes); size_index += 1)
    {
        types[1].size = unsupported_pointer_sizes[size_index];
        CodeviewResult refused_pointer = codeview_build(arguments->arena, (CodeviewInput){
            .model = &model, .file_paths = &path, .file_count = 1, .machine = CODEVIEW_MACHINE_X64});
        BUSTER_TEST(arguments, !refused_pointer.valid && !refused_pointer.symbols.length && !refused_pointer.types.length);
    }
    types[1].size = 8;
    String8 reserved[] = {S8_INITIALIZER("__unnamed"), S8_INITIALIZER("<unnamed-tag>"),
                          S8_INITIALIZER("scope::__unnamed"), S8_INITIALIZER("scope::<unnamed-tag>")};
    for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(reserved); name_index += 1)
    {
        types[2].name = reserved[name_index];
        CodeviewResult refused = codeview_build(arguments->arena, (CodeviewInput){
            .model = &model, .file_paths = &path, .file_count = 1, .machine = CODEVIEW_MACHINE_X64});
        BUSTER_TEST(arguments, !refused.valid && refused.unsupported_type && refused.unsupported_type_id == 2);
        BUSTER_TEST(arguments, !refused.symbols.length && !refused.types.length);
    }
    types[2].name = S8("scope__unnamed");
    CodeviewResult neighbor = codeview_build(arguments->arena, (CodeviewInput){
        .model = &model, .file_paths = &path, .file_count = 1, .machine = CODEVIEW_MACHINE_X64});
    BUSTER_TEST(arguments, neighbor.valid && !neighbor.unsupported_type);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codeview_test_large_types(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    enum { MEMBER_COUNT = 4096, TYPE_BASE = 0x1000, MAX_RECORD = 0xff00 };
    String8 path = S8("large.c");
    DebugTypeField* fields = arena_allocate(arguments->arena, DebugTypeField, MEMBER_COUNT);
    DebugEnumMember* members = arena_allocate(arguments->arena, DebugEnumMember, MEMBER_COUNT);
    for (u32 index = 0; index < MEMBER_COUNT; index += 1)
    {
        String8 name = string_format(arguments->arena, S8("member_with_a_deliberately_long_debug_name_{u32}"), index);
        fields[index] = (DebugTypeField){.name = name, .type = 0, .offset = index * 4};
        members[index] = (DebugEnumMember){.name = name, .value = index};
    }
    DebugTypeId parameters[] = {0};
    DebugType types[] = {
        {.kind = DEBUG_TYPE_BASE, .name = S8("int"), .size = 4, .is_signed = true},
        // An argument list preceding a field list used to invalidate the
        // precomputed auxiliary indices even without any continuation records.
        {.kind = DEBUG_TYPE_FUNCTION, .return_type = 0, .parameter_types = parameters, .parameter_count = 1},
        {.kind = DEBUG_TYPE_STRUCT, .name = S8("Large"), .size = MEMBER_COUNT * 4, .fields = fields, .field_count = MEMBER_COUNT},
        {.kind = DEBUG_TYPE_ENUM, .name = S8("Values"), .size = 4, .enum_members = members, .enum_member_count = MEMBER_COUNT},
        {.kind = DEBUG_TYPE_UNION, .name = S8("Choice"), .size = 4, .fields = fields, .field_count = 1},
    };
    DebugModel model = {.types = types, .type_count = BUSTER_ARRAY_LENGTH(types), .valid = true};
    CodeviewInput input = {.model = &model, .file_paths = &path, .file_count = 1, .machine = CODEVIEW_MACHINE_X64};
    CodeviewResult built = codeview_build(arguments->arena, input);
    BUSTER_TEST(arguments, built.valid);
    u64 offsets[64] = {0};
    u32 record_count = 0;
    u64 cursor = 4;
    while (built.valid && cursor + 4 <= built.types.length && record_count < BUSTER_ARRAY_LENGTH(offsets))
    {
        u32 size = (u32)codeview_test_u16(built.types.pointer + cursor) + 2;
        BUSTER_TEST(arguments, size >= 4 && size <= MAX_RECORD && !(size & 3));
        if (size < 4 || size > built.types.length - cursor)
        {
            break;
        }
        offsets[record_count++] = cursor;
        cursor += size;
    }
    BUSTER_TEST(arguments, built.valid && cursor == built.types.length && record_count >= 12);
    if (built.valid && cursor == built.types.length && record_count >= 12)
    {
        u32 arguments_index = codeview_test_u32(built.types.pointer + offsets[1] + 12);
        BUSTER_TEST(arguments, arguments_index >= TYPE_BASE && arguments_index - TYPE_BASE < record_count);
        if (arguments_index >= TYPE_BASE && arguments_index - TYPE_BASE < record_count)
        {
            u8* record = built.types.pointer + offsets[arguments_index - TYPE_BASE];
            BUSTER_TEST(arguments, codeview_test_u16(record + 2) == 0x1201 && codeview_test_u32(record + 8) == TYPE_BASE);
        }
        for (u32 type_index = 2; type_index <= 4; type_index += 1)
        {
            bool enumeration = type_index == 3;
            u64 aggregate = enumeration ? offsets[type_index] : codeview_test_complete_aggregate(built.types, offsets[type_index]);
            BUSTER_TEST(arguments, aggregate != 0);
            u32 field_index = aggregate ? codeview_test_u32(built.types.pointer + aggregate + (enumeration ? 12 : 8)) : 0;
            u32 seen = 0;
            u32 links = 0;
            bool valid = true;
            while (field_index && valid && links < record_count)
            {
                valid = field_index >= TYPE_BASE && field_index - TYPE_BASE < record_count;
                BUSTER_TEST(arguments, valid);
                if (!valid)
                {
                    break;
                }
                u64 offset = offsets[field_index - TYPE_BASE];
                u64 end = offset + 2 + codeview_test_u16(built.types.pointer + offset);
                BUSTER_TEST(arguments, codeview_test_u16(built.types.pointer + offset + 2) == 0x1203);
                cursor = offset + 4;
                u32 next = 0;
                while (cursor < end && valid)
                {
                    u8* record = built.types.pointer + cursor;
                    u16 leaf = codeview_test_u16(record);
                    if (leaf == 0x1404)
                    {
                        valid = end - cursor == 8 && !codeview_test_u16(record + 2);
                        next = codeview_test_u32(record + 4);
                        valid &= next < field_index && next >= TYPE_BASE;
                        cursor += 8;
                    }
                    else
                    {
                        u32 prefix = enumeration ? 10 : 14;
                        valid = leaf == (enumeration ? 0x1502 : 0x150d) && seen < MEMBER_COUNT && end - cursor > prefix;
                        if (!valid)
                        {
                            break;
                        }
                        // LF_ULONG is 0x8004 (cvinfo.h; LLVM CodeViewTypes.def);
                        // 0x8003 is the signed LF_LONG (#1440).
                        BUSTER_TEST(arguments, codeview_test_u16(record + prefix - 6) == 0x8004);
                        BUSTER_TEST(arguments, codeview_test_u32(record + prefix - 4) == seen * (enumeration ? 1u : 4u));
                        if (!enumeration)
                        {
                            BUSTER_TEST(arguments, codeview_test_u32(record + 4) == TYPE_BASE);
                        }
                        String8 expected = fields[seen].name;
                        valid = expected.length + 1 <= end - cursor - prefix;
                        if (!valid)
                        {
                            break;
                        }
                        BUSTER_TEST(arguments, !memcmp(record + prefix, expected.pointer, expected.length) && !record[prefix + expected.length]);
                        cursor += prefix + expected.length + 1;
                        u32 padding = (u32)((4 - (cursor & 3)) & 3);
                        for (u32 pad = padding; pad && cursor < end; pad -= 1)
                        {
                            BUSTER_TEST(arguments, built.types.pointer[cursor++] == 0xf0 + pad);
                        }
                        seen += 1;
                    }
                }
                BUSTER_TEST(arguments, valid && cursor == end);
                field_index = next;
                links += next != 0;
            }
            BUSTER_TEST(arguments, !field_index && valid && seen == (type_index == 4 ? 1u : MEMBER_COUNT));
            BUSTER_TEST(arguments, type_index == 4 || links >= 2);
        }
        // LF_UNION has no derived/vshape words: the numeric size starts at +12.
        BUSTER_TEST(arguments, codeview_test_u16(built.types.pointer + offsets[4] + 12) == 0x8004);
    }

    // Exact reserved-continuation boundary, then one byte over. A single member
    // cannot itself be continued; do not wrap the length or loop on overflow.
    char8* large_name = arena_allocate(arguments->arena, char8, MAX_RECORD);
    memset(large_name, 'x', MAX_RECORD);
    fields[0].name = (String8){.pointer = large_name, .length = MAX_RECORD - 4 - 8 - 15};
    types[2].field_count = 1;
    model.type_count = 3;
    BUSTER_TEST(arguments, codeview_build(arguments->arena, input).valid);
    fields[0].name.length += 1;
    BUSTER_TEST(arguments, !codeview_build(arguments->arena, input).valid);
    fields[0].name = S8("field");
    types[2].name = (String8){.pointer = large_name, .length = MAX_RECORD};
    BUSTER_TEST(arguments, !codeview_build(arguments->arena, input).valid);
    return result;
}

// Floating-point base types are classified by DebugType::is_float, not by
// spelling (#2737).  Expected values are the CodeView simple types from
// cvinfo.h: T_REAL32 0x40, T_REAL64 0x41, T_REAL80 0x42, T_REAL128 0x43,
// T_REAL16 0x46, T_INT4 0x74.
BUSTER_GLOBAL_LOCAL UnitTestResult codeview_test_float_base_types(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 path = S8("float.c");
    DebugType types[] = {
        {.kind = DEBUG_TYPE_BASE, .name = S8("float"), .size = 4, .bit_width = 32, .is_float = true},
        {.kind = DEBUG_TYPE_BASE, .name = S8("double"), .size = 8, .bit_width = 64, .is_float = true},
        {.kind = DEBUG_TYPE_BASE, .name = S8("long double"), .size = 16, .bit_width = 80, .is_float = true},
        {.kind = DEBUG_TYPE_BASE, .name = S8("_Float128"), .size = 16, .bit_width = 128, .is_float = true},
        {.kind = DEBUG_TYPE_BASE, .name = S8("_Float16"), .size = 2, .bit_width = 16, .is_float = true},
        {.kind = DEBUG_TYPE_BASE, .name = S8("fixed_int"), .size = 4, .bit_width = 32, .is_signed = true},
    };
    u32 expected[] = {0x0040, 0x0041, 0x0042, 0x0043, 0x0046, 0x0074};
    DebugModel model = {.types = types, .type_count = BUSTER_ARRAY_LENGTH(types), .valid = true};
    CodeviewResult built = codeview_build(arguments->arena, (CodeviewInput){.model = &model, .file_paths = &path, .file_count = 1,
        .machine = CODEVIEW_MACHINE_X64});
    BUSTER_TEST(arguments, built.valid);
    u64 cursor = 4;
    for (u32 index = 0; built.valid && index < BUSTER_ARRAY_LENGTH(types); index += 1)
    {
        bool in_range = cursor + 12 <= built.types.length;
        BUSTER_TEST(arguments, in_range);
        if (!in_range)
        {
            break;
        }
        u8* record = built.types.pointer + cursor;
        BUSTER_TEST(arguments, codeview_test_u16(record + 2) == 0x1001 && codeview_test_u32(record + 4) == expected[index]);
        cursor += (u64)codeview_test_u16(record) + 2;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codeview_test_scope_growth(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u64 previous = 0;
    for (u32 count = 256; count <= 1024; count *= 4)
    {
        String8 path = S8("scopes.c");
        DebugType type = {.kind = DEBUG_TYPE_FUNCTION, .return_type = DEBUG_ID_INVALID};
        DebugScope* scopes = arena_allocate(arguments->arena, DebugScope, count * 3);
        DebugFunction* functions = arena_allocate(arguments->arena, DebugFunction, count);
        DwarfFunction* legacy = arena_allocate(arguments->arena, DwarfFunction, count);
        for (u32 index = 0; index < count; index += 1)
        {
            scopes[index] = (DebugScope){.kind = DEBUG_SCOPE_FUNCTION, .parent = DEBUG_SCOPE_INVALID, .start = index * 8, .end = index * 8 + 8};
            scopes[count + index] = (DebugScope){.kind = DEBUG_SCOPE_LEXICAL, .parent = index, .start = index * 8 + 1, .end = index * 8 + 7};
            scopes[count * 2 + index] = (DebugScope){.kind = DEBUG_SCOPE_LEXICAL, .parent = count + index, .start = index * 8 + 2, .end = index * 8 + 6};
            functions[index] = (DebugFunction){.name = S8("f"), .scope = index, .type = 0, .code_offset = index * 8, .code_size = 8};
            legacy[index] = (DwarfFunction){.name = S8("f"), .code_offset = index * 8, .code_size = 8, .line = 1};
        }
        DebugModel model = {.types = &type, .type_count = 1, .scopes = scopes, .scope_count = count * 3,
                            .functions = functions, .function_count = count, .valid = true};
        u64 start = arguments->arena->position;
        CodeviewResult built = codeview_build(arguments->arena, (CodeviewInput){.model = &model, .file_paths = &path, .file_count = 1,
            .functions = legacy, .function_count = count, .machine = CODEVIEW_MACHINE_X64});
        u64 allocated = arguments->arena->position - start;
        BUSTER_TEST(arguments, built.valid);
        // Includes output buffers, relocations, types and traversal scratch.
        BUSTER_TEST(arguments, allocated < (u64)count * 1024 + 16384);
        BUSTER_TEST(arguments, !previous || allocated < previous * 6);
        if (arguments->show)
        {
            arguments->show(arguments, S8("CODEVIEW_SCOPE_ALLOCATION functions={u32} scopes={u32} bytes={u64}\n"), count, count * 3, allocated);
        }
        previous = allocated;
    }
    return result;
}


// COFF object producers leave scope pointers as zero placeholders.  Linkers
// rebuild them only after concatenating the DEBUG_S_SYMBOLS payloads into the
// final module stream, where subsection headers and line data no longer exist.
BUSTER_GLOBAL_LOCAL UnitTestResult codeview_test_object_scope_placeholders(UnitTestArguments* arguments, CodeviewResult built,
                                                                           u32 expected_subsections, u32 expected_procedures,
                                                                           u32 expected_blocks, u32 expected_inlines)
{
    UnitTestResult result = {0};
    enum
    {
        TEST_S_END = 0x0006,
        TEST_S_BLOCK32 = 0x1103,
        TEST_S_GPROC32 = 0x1110,
        TEST_S_INLINESITE = 0x114d,
        TEST_S_INLINESITE_END = 0x114e,
    };
    bool valid = built.valid && built.symbols.length >= 4 && codeview_test_u32(built.symbols.pointer) == CODEVIEW_TEST_SIGNATURE_C13;
    u32 subsection_count = 0;
    u32 procedures = 0;
    u32 blocks = 0;
    u32 inlines = 0;
    u64 subsection_offset = 4;
    while (valid && subsection_offset < built.symbols.length)
    {
        if (!BUSTER_REQUIRE(arguments, subsection_offset + 8 <= built.symbols.length))
        {
            valid = false;
            break;
        }
        u32 subsection_kind = codeview_test_u32(built.symbols.pointer + subsection_offset);
        u32 subsection_length = codeview_test_u32(built.symbols.pointer + subsection_offset + 4);
        u64 payload = subsection_offset + 8;
        if (!BUSTER_REQUIRE(arguments, subsection_length <= built.symbols.length - payload))
        {
            valid = false;
            break;
        }
        if (subsection_kind == CODEVIEW_TEST_SYMBOLS)
        {
            subsection_count += 1;
            u64 record_offset = payload;
            u64 record_end = payload + subsection_length;
            u32 stack_capacity = subsection_length / 4 + 1;
            u16* closing_kinds = arena_allocate(arguments->arena, u16, stack_capacity);
            u32 depth = 0;
            while (record_offset < record_end)
            {
                if (!BUSTER_REQUIRE(arguments, record_offset + 4 <= record_end))
                {
                    valid = false;
                    break;
                }
                u16 length = codeview_test_u16(built.symbols.pointer + record_offset);
                u16 kind = codeview_test_u16(built.symbols.pointer + record_offset + 2);
                u64 record_size = (u64)length + 2;
                if (!BUSTER_REQUIRE(arguments, length >= 2 && !(record_size & 3) && record_size <= record_end - record_offset))
                {
                    valid = false;
                    break;
                }
                bool opening = kind == TEST_S_GPROC32 || kind == TEST_S_BLOCK32 || kind == TEST_S_INLINESITE;
                if (opening)
                {
                    u32 minimum_size = kind == TEST_S_GPROC32 ? 40 : kind == TEST_S_BLOCK32 ? 24 : 16;
                    if (!BUSTER_REQUIRE(arguments, record_size >= minimum_size && depth < stack_capacity))
                    {
                        valid = false;
                        break;
                    }
                    BUSTER_TEST(arguments, codeview_test_u32(built.symbols.pointer + record_offset + 4) == 0);
                    BUSTER_TEST(arguments, codeview_test_u32(built.symbols.pointer + record_offset + 8) == 0);
                    closing_kinds[depth++] = kind == TEST_S_INLINESITE ? TEST_S_INLINESITE_END : TEST_S_END;
                    procedures += kind == TEST_S_GPROC32;
                    blocks += kind == TEST_S_BLOCK32;
                    inlines += kind == TEST_S_INLINESITE;
                }
                else if (kind == TEST_S_END || kind == TEST_S_INLINESITE_END)
                {
                    if (!BUSTER_REQUIRE(arguments, depth && closing_kinds[depth - 1] == kind))
                    {
                        valid = false;
                        break;
                    }
                    depth -= 1;
                }
                record_offset += record_size;
            }
            BUSTER_TEST(arguments, valid && record_offset == record_end && !depth);
        }
        subsection_offset = payload + (((u64)subsection_length + 3) & ~(u64)3);
    }
    BUSTER_TEST(arguments, valid && subsection_offset == built.symbols.length);
    BUSTER_TEST(arguments, subsection_count == expected_subsections);
    BUSTER_TEST(arguments, procedures == expected_procedures);
    BUSTER_TEST(arguments, blocks == expected_blocks);
    BUSTER_TEST(arguments, inlines == expected_inlines);
    return result;
}

// Bit-field members and array sizes against CodeView's published encodings
// (cvinfo.h; LLVM's CodeViewTypes.def), not against codeview.c's names
// (#1440): a bit-field member's type is an LF_BITFIELD (0x1205) naming the
// declared type, width and position inside the unit LF_MEMBER's offset names,
// and LF_ARRAY's numeric field is the size in bytes, as an LF_ULONG (0x8004).
BUSTER_GLOBAL_LOCAL UnitTestResult codeview_test_bit_fields_and_arrays(UnitTestArguments* arguments)
{
    enum { TYPE_BASE = 0x1000, LEAF_ARRAY = 0x1503, LEAF_BITFIELD = 0x1205, LEAF_FIELDLIST = 0x1203, LEAF_MEMBER = 0x150d, LEAF_ULONG = 0x8004 };
    UnitTestResult result = {0};
    String8 path = S8("geometry.c");
    DebugTypeField fields[] = {
        {.name = S8("a"), .type = 0, .offset = 0},
        {.name = S8("b"), .type = 0, .offset = 4, .bit_offset = 3, .bit_width = 13, .is_bit_field = true},
    };
    DebugType types[] = {
        {.kind = DEBUG_TYPE_BASE, .name = S8("unsigned int"), .size = 4},
        {.kind = DEBUG_TYPE_ARRAY, .name = S8(""), .element_type = 0, .element_count = 7, .size = 28},
        {.kind = DEBUG_TYPE_STRUCT, .name = S8("geometry"), .size = 8, .fields = fields, .field_count = BUSTER_ARRAY_LENGTH(fields)},
    };
    DebugModel model = {.types = types, .type_count = BUSTER_ARRAY_LENGTH(types), .valid = true};
    CodeviewResult built = codeview_build(arguments->arena, (CodeviewInput){.model = &model, .file_paths = &path, .file_count = 1,
                                                                             .machine = CODEVIEW_MACHINE_X64});
    if (BUSTER_REQUIRE(arguments, built.valid))
    {
        u32 bit_field_index = 0;
        bool bit_field_valid = false;
        bool array_valid = false;
        u32 member_b_type = 0;
        u32 record_index = 0;
        for (u64 cursor = 4; cursor + 4 <= built.types.length; record_index += 1)
        {
            u8 const* record = built.types.pointer + cursor;
            u64 size = (u64)codeview_test_u16(record) + 2;
            u16 leaf = codeview_test_u16(record + 2);
            if (leaf == LEAF_BITFIELD && size >= 10)
            {
                bit_field_index = TYPE_BASE + record_index;
                bit_field_valid = codeview_test_u32(record + 4) == TYPE_BASE && record[8] == 13 && record[9] == 3;
            }
            else if (leaf == LEAF_ARRAY && size >= 18)
            {
                array_valid = codeview_test_u32(record + 4) == TYPE_BASE && codeview_test_u16(record + 12) == LEAF_ULONG &&
                              codeview_test_u32(record + 14) == 28;
            }
            else if (leaf == LEAF_FIELDLIST && size >= 4 + 2 * 16)
            {
                // Two members, each LF_MEMBER, attributes, type, LF_ULONG
                // offset, one-letter name and NUL, padded to four bytes.
                u8 const* second = record + 4 + 16;
                if (codeview_test_u16(second) == LEAF_MEMBER && second[14] == 'b')
                {
                    member_b_type = codeview_test_u32(second + 4);
                    BUSTER_TEST(arguments, codeview_test_u16(second + 8) == LEAF_ULONG && codeview_test_u32(second + 10) == 4);
                }
            }
            cursor += size;
        }
        BUSTER_TEST(arguments, bit_field_valid && array_valid);
        BUSTER_TEST(arguments, bit_field_index && member_b_type == bit_field_index);
    }
    return result;
}

UnitTestResult codeview_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = codeview_test_large_types(arguments);
    UnitTestResult recursive = codeview_test_recursive_aggregates(arguments);
    result.test_count += recursive.test_count;
    result.succeeded_test_count += recursive.succeeded_test_count;
    UnitTestResult linkage = codeview_test_global_linkage(arguments);
    result.test_count += linkage.test_count;
    result.succeeded_test_count += linkage.succeeded_test_count;
    UnitTestResult geometry = codeview_test_bit_fields_and_arrays(arguments);
    result.test_count += geometry.test_count;
    result.succeeded_test_count += geometry.succeeded_test_count;
    UnitTestResult floats = codeview_test_float_base_types(arguments);
    result.test_count += floats.test_count;
    result.succeeded_test_count += floats.succeeded_test_count;
    UnitTestResult growth = codeview_test_scope_growth(arguments);
    result.test_count += growth.test_count;
    result.succeeded_test_count += growth.succeeded_test_count;
    String8 files[] = {
        S8_INITIALIZER("main.c"),
        S8_INITIALIZER("helper.h"),
    };
    DwarfFunction functions[] = {
        {
            .name = S8_INITIALIZER("main"),
            .code_offset = 0,
            .code_size = 32,
            .file = 0,
            .line = 3,
        },
        {
            .name = S8_INITIALIZER("helper"),
            .code_offset = 32,
            .code_size = 48,
            .file = 1,
            .line = 10,
        },
    };
    DwarfLineEntry lines[] = {
        {.code_offset = 0, .file = 0, .line = 3, .column = 1},
        {.code_offset = 8, .file = 0, .line = 4, .column = 5},
        {.code_offset = 32, .file = 1, .line = 10, .column = 1},
        {.code_offset = 48, .file = 1, .line = 12, .column = 9},
    };
    CodeviewInput input = {
        .producer = S8("buster"),
        .file_paths = files,
        .functions = functions,
        .lines = lines,
        .file_count = BUSTER_ARRAY_LENGTH(files),
        .function_count = BUSTER_ARRAY_LENGTH(functions),
        .line_count = BUSTER_ARRAY_LENGTH(lines),
        .machine = CODEVIEW_MACHINE_X64,
    };
    CodeviewResult built = codeview_build(arguments->arena, input);
    BUSTER_TEST(arguments, built.valid);
    BUSTER_TEST(arguments, built.types.length == 4);
    BUSTER_TEST(arguments, built.symbols.length > 16);
    BUSTER_TEST(arguments, built.relocation_count == 4 * BUSTER_ARRAY_LENGTH(functions));
    UnitTestResult basic_scope_links = codeview_test_object_scope_placeholders(arguments, built, 3, 2, 0, 0);
    result.test_count += basic_scope_links.test_count;
    result.succeeded_test_count += basic_scope_links.succeeded_test_count;
    // A failed build leaves nothing walkable, and a subsection that overruns the
    // buffer stops the walk; both cases skip the structural tallies below rather
    // than leaving the function early.
    bool walkable = built.valid;
    u32 symbol_subsections = 0;
    u32 line_subsections = 0;
    u32 checksum_subsections = 0;
    u32 string_subsections = 0;
    u32 checked_lines = 0;
    if (walkable)
    {
        u32 signature;
        memcpy(&signature, built.symbols.pointer, sizeof(signature));
        BUSTER_TEST(arguments, signature == CODEVIEW_TEST_SIGNATURE_C13);
        for (u32 relocation_index = 0; relocation_index < built.relocation_count; relocation_index += 1)
        {
            CodeviewRelocation relocation = built.relocations[relocation_index];
            u64 width = relocation.kind == CODEVIEW_RELOCATION_SECREL32 ? 4 : 2;
            BUSTER_TEST(arguments, relocation.offset + width <= built.symbols.length);
            BUSTER_TEST(arguments, relocation.function < BUSTER_ARRAY_LENGTH(functions));
        }
        // Walk the subsections and check the payload structure.
        u64 offset = 4;
        while (offset + 8 <= built.symbols.length && walkable)
        {
            u32 kind;
            u32 length;
            memcpy(&kind, built.symbols.pointer + offset, sizeof(kind));
            memcpy(&length, built.symbols.pointer + offset + 4, sizeof(length));
            u64 payload = offset + 8;
            walkable = payload + length <= built.symbols.length;
            BUSTER_TEST(arguments, walkable);
            if (walkable)
            {
                symbol_subsections += kind == CODEVIEW_TEST_SYMBOLS;
                checksum_subsections += kind == CODEVIEW_TEST_FILECHKSMS;
                string_subsections += kind == CODEVIEW_TEST_STRINGTABLE;
                if (kind == CODEVIEW_TEST_LINES && length >= 12 + 12 + 8)
                {
                    line_subsections += 1;
                    u32 contribution_size;
                    u32 file_id;
                    u32 line_count;
                    u32 first_line;
                    memcpy(&contribution_size, built.symbols.pointer + payload + 8, sizeof(contribution_size));
                    memcpy(&file_id, built.symbols.pointer + payload + 12, sizeof(file_id));
                    memcpy(&line_count, built.symbols.pointer + payload + 16, sizeof(line_count));
                    memcpy(&first_line, built.symbols.pointer + payload + 24 + 4, sizeof(first_line));
                    DwarfFunction* function = functions + (line_subsections - 1);
                    BUSTER_TEST(arguments, contribution_size == function->code_size);
                    BUSTER_TEST(arguments, file_id == function->file * 8);
                    BUSTER_TEST(arguments, line_count >= 1);
                    BUSTER_TEST(arguments, (first_line & CODEVIEW_TEST_LINE_NUMBER_MASK) == function->line);
                    BUSTER_TEST(arguments, first_line & CODEVIEW_TEST_LINE_STATEMENT);
                    checked_lines += 1;
                }
                if (kind == CODEVIEW_TEST_STRINGTABLE)
                {
                    BUSTER_TEST(arguments, length >= 1 + files[0].length + 1 + files[1].length + 1);
                    BUSTER_TEST(arguments, built.symbols.pointer[payload] == 0);
                    BUSTER_TEST(arguments, memcmp(built.symbols.pointer + payload + 1, files[0].pointer, files[0].length) == 0);
                }
                offset = payload + ((length + 3) & ~3u);
            }
        }
    }

    if (walkable)
    {
        BUSTER_TEST(arguments, symbol_subsections == 1 + BUSTER_ARRAY_LENGTH(functions));
        BUSTER_TEST(arguments, line_subsections == BUSTER_ARRAY_LENGTH(functions));
        BUSTER_TEST(arguments, checksum_subsections == 1 && string_subsections == 1);
        BUSTER_TEST(arguments, checked_lines == BUSTER_ARRAY_LENGTH(functions));
    }
    // Invalid input: an out-of-range file index must be rejected.
    DwarfLineEntry invalid_line = {.code_offset = 0, .file = 9, .line = 1, .column = 1};
    CodeviewInput invalid = input;
    invalid.lines = &invalid_line;
    invalid.line_count = 1;
    BUSTER_TEST(arguments, !codeview_build(arguments->arena, invalid).valid);

    // The format backend also accepts the neutral model directly.  Keep a
    // register-to-frame transition, a parameter, and a synthetic inline site
    // here so the C13 records cannot silently regress to line-only output.
    DebugTypeId model_parameter_types[] = {0};
    DebugType model_types[] = {
        {
            .name = S8("int"),
            .kind = DEBUG_TYPE_BASE,
            .size = 4,
            .alignment = 4,
            .bit_width = 32,
            .is_signed = true,
        },
        {
            .name = S8("model_function"),
            .kind = DEBUG_TYPE_FUNCTION,
            .return_type = 0,
            .parameter_types = model_parameter_types,
            .parameter_count = 1,
        },
    };
    DebugLocationPiece model_pieces[] = {
        {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -24, .value_offset = 0, .size = 4},
        {.kind = DEBUG_LOCATION_REGISTER, .reg = DEBUG_REGISTER_X86_RAX, .value_offset = 4, .size = 4},
    };
    DebugLocationRange model_locations[] = {
        {.start = 64, .end = 76, .location = {.kind = DEBUG_LOCATION_REGISTER, .reg = DEBUG_REGISTER_X86_RAX}},
        {.start = 76, .end = 88, .location = {.kind = DEBUG_LOCATION_PIECEWISE, .pieces = model_pieces, .piece_count = BUSTER_ARRAY_LENGTH(model_pieces)}},
        {.start = 88, .end = 96, .location = {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -16}},
    };
    DebugVariableId model_variable_ids[] = {0};
    DebugVariable model_variables[] = {
        {
            .name = S8("value"),
            .type = 0,
            .declaration = {.source = 0, .line = 2, .column = 5},
            .locations = model_locations,
            .location_count = BUSTER_ARRAY_LENGTH(model_locations),
            .local = {.value = 0},
            .kind = DEBUG_VARIABLE_PARAMETER,
        },
    };
    DebugScope model_scopes[] = {
        {
            .parent = DEBUG_SCOPE_INVALID,
            .kind = DEBUG_SCOPE_FUNCTION,
            .start = 64,
            .end = 96,
        },
        {
            .parent = 0,
            .kind = DEBUG_SCOPE_LEXICAL,
            .start = 68,
            .end = 92,
        },
        {
            .parent = 1,
            .kind = DEBUG_SCOPE_LEXICAL,
            .start = 72,
            .end = 88,
            // Promotion-like location transitions sit inside both lexical
            // scopes, exercising relocations around every defrange record.
            .variables = model_variable_ids,
            .variable_count = BUSTER_ARRAY_LENGTH(model_variable_ids),
        },
    };
    DebugFunction model_functions[] = {
        {
            .name = S8("model_function"),
            .declaration = {.source = 0, .line = 1, .column = 1},
            .symbol = {.value = 0},
            .type = 1,
            .scope = 0,
            .code_offset = 64,
            .code_size = 32,
        },
    };
    DebugInlineSite model_inline_sites[] = {
        {
            .function = model_functions,
            .call_site = {.source = 0, .line = 8, .column = 9},
            .start = 68,
            .end = 84,
            .has_ranges = true,
        },
    };
    DebugModel model = {
        .source_paths = files,
        .types = model_types,
        .functions = model_functions,
        .scopes = model_scopes,
        .variables = model_variables,
        .inline_sites = model_inline_sites,
        .source_count = BUSTER_ARRAY_LENGTH(files),
        .type_count = BUSTER_ARRAY_LENGTH(model_types),
        .function_count = BUSTER_ARRAY_LENGTH(model_functions),
        .scope_count = BUSTER_ARRAY_LENGTH(model_scopes),
        .variable_count = BUSTER_ARRAY_LENGTH(model_variables),
        .inline_site_count = BUSTER_ARRAY_LENGTH(model_inline_sites),
        .valid = true,
    };
    DwarfFunction model_function = {
        .name = S8("model_function"),
        .code_offset = 64,
        .code_size = 32,
        .file = 0,
        .line = 1,
    };
    DwarfLineEntry model_line = {.code_offset = 64, .file = 0, .line = 1, .column = 1};
    CodeviewResult model_built = codeview_build(arguments->arena, (CodeviewInput){
                                                                     .model = &model,
                                                                     .producer = S8("buster"),
                                                                     .file_paths = files,
                                                                     .functions = &model_function,
                                                                     .lines = &model_line,
                                                                     .file_count = BUSTER_ARRAY_LENGTH(files),
                                                                     .function_count = 1,
                                                                     .line_count = 1,
                                                                     .machine = CODEVIEW_MACHINE_X64,
                                                                 });
    BUSTER_TEST(arguments, model_built.valid && model_built.types.length > 4);
    BUSTER_TEST(arguments, model_built.relocation_count == 16);
    u32 nonzero_addends = 0;
    for (u32 relocation_index = 0; relocation_index + 1 < model_built.relocation_count; relocation_index += 2)
    {
        CodeviewRelocation address = model_built.relocations[relocation_index];
        CodeviewRelocation section = model_built.relocations[relocation_index + 1];
        BUSTER_TEST(arguments, address.kind == CODEVIEW_RELOCATION_SECREL32 && section.kind == CODEVIEW_RELOCATION_SECTION16);
        BUSTER_TEST(arguments, address.function == 0 && section.function == 0 && section.offset == address.offset + 4);
        BUSTER_TEST(arguments, address.offset + 6 <= model_built.symbols.length);
        if (address.offset + 6 <= model_built.symbols.length)
        {
            BUSTER_TEST(arguments, codeview_test_u32(model_built.symbols.pointer + address.offset) <= 24);
            // The relocation carries the in-place COFF addend explicitly, so
            // a linker that overwrites the field keeps the range start (#2736).
            BUSTER_TEST(arguments, address.addend == codeview_test_u32(model_built.symbols.pointer + address.offset));
            nonzero_addends += address.addend != 0;
            BUSTER_TEST(arguments, codeview_test_u16(model_built.symbols.pointer + section.offset) == 0);
        }
    }
    BUSTER_TEST(arguments, nonzero_addends != 0);
    // A model containing only locals must not create a zero-length
    // DEBUG_S_SYMBOLS subsection: MSVC link.exe rejects that stream.
    UnitTestResult model_scope_links = codeview_test_object_scope_placeholders(arguments, model_built, 2, 1, 2, 1);
    result.test_count += model_scope_links.test_count;
    result.succeeded_test_count += model_scope_links.succeeded_test_count;
    bool found_local = false;
    bool found_procedure_type = false;
    bool found_register = false;
    bool found_frame = false;
    bool found_subfield = false;
    bool found_inline = false;
    u64 model_symbol_offset = 4;
    while (model_symbol_offset + 8 <= model_built.symbols.length)
    {
        u32 subsection_kind = 0;
        u32 subsection_length = 0;
        memcpy(&subsection_kind, model_built.symbols.pointer + model_symbol_offset, sizeof(subsection_kind));
        memcpy(&subsection_length, model_built.symbols.pointer + model_symbol_offset + 4, sizeof(subsection_length));
        u64 payload = model_symbol_offset + 8;
        if (payload + subsection_length > model_built.symbols.length)
        {
            break;
        }
        if (subsection_kind == CODEVIEW_TEST_SYMBOLS)
        {
            u64 record_offset = payload;
            u64 record_end = payload + subsection_length;
            while (record_offset + 4 <= record_end)
            {
                u16 record_length = 0;
                u16 record_kind = 0;
                memcpy(&record_length, model_built.symbols.pointer + record_offset, sizeof(record_length));
                memcpy(&record_kind, model_built.symbols.pointer + record_offset + 2, sizeof(record_kind));
                if (record_length < 2 || record_offset + 2 + record_length > record_end)
                {
                    break;
                }
                if (record_kind == CODEVIEW_TEST_S_GPROC32 && record_offset + 32 <= record_end)
                {
                    u32 type_index = 0;
                    memcpy(&type_index, model_built.symbols.pointer + record_offset + 28, sizeof(type_index));
                    found_procedure_type |= type_index == 0x1001u;
                }
                found_local |= record_kind == CODEVIEW_TEST_S_LOCAL;
                found_register |= record_kind == CODEVIEW_TEST_S_DEFRANGE_REGISTER;
                found_frame |= record_kind == CODEVIEW_TEST_S_DEFRANGE_FRAMEPOINTER_REL;
                found_subfield |= record_kind == CODEVIEW_TEST_S_DEFRANGE_SUBFIELD || record_kind == CODEVIEW_TEST_S_DEFRANGE_SUBFIELD_REGISTER;
                found_inline |= record_kind == CODEVIEW_TEST_S_INLINESITE;
                record_offset += 2 + record_length;
            }
        }
        model_symbol_offset = payload + ((subsection_length + 3) & ~3u);
    }
    BUSTER_TEST(arguments, found_procedure_type && found_local && found_register && found_frame && found_subfield && found_inline);

    DebugVariable global_variable = {
        .name = S8("global_value"),
        .linkage_name = S8("global_value"),
        .symbol = {.value = 7},
        .type = 0,
        .kind = DEBUG_VARIABLE_GLOBAL,
    };
    DebugModel globals_only = model;
    globals_only.functions = 0;
    globals_only.scopes = 0;
    globals_only.variables = &global_variable;
    globals_only.function_count = 0;
    globals_only.scope_count = 0;
    globals_only.variable_count = 1;
    globals_only.inline_sites = 0;
    globals_only.inline_site_count = 0;
    CodeviewResult globals_built = codeview_build(arguments->arena, (CodeviewInput){
                                                                      .model = &globals_only,
                                                                      .producer = S8("buster"),
                                                                      .file_paths = files,
                                                                      .file_count = BUSTER_ARRAY_LENGTH(files),
                                                                      .machine = CODEVIEW_MACHINE_X64,
                                                                  });
    BUSTER_TEST(arguments, globals_built.valid && globals_built.relocation_count == 2);
    // Both named relocations carry the variable's program symbol, which the
    // object writer resolves without rehashing the linkage name.
    for (u32 index = 0; globals_built.valid && index < globals_built.relocation_count; index += 1)
    {
        BUSTER_TEST(arguments, string_equal(globals_built.relocations[index].symbol_name, S8("global_value")) &&
                                   globals_built.relocations[index].symbol.value == 7);
    }
    return result;
}
#endif
