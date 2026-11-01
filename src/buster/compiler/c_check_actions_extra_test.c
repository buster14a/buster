// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Continued #133 action/literal coverage. Include after c_checker_test.c's helpers and c_check_data_test.c.
// Original expected bytes and semantic mutations are checked independently with Clang, not only with the twins.
#ifndef BUSTER_C_CHECK_ACTIONS_EXTRA_TEST_INCLUDED
#define BUSTER_C_CHECK_ACTIONS_EXTRA_TEST_INCLUDED

typedef struct CheckExtraStringFixture CheckExtraStringFixture;
struct CheckExtraStringFixture
{
    const char* parts[3];
    uint32_t count;
    TypeId element;
    uint8_t bytes[32];
    uint32_t length;
};

static void check_test_string_literals(CheckTest* test, const char* clang)
{
    const CheckExtraStringFixture fixtures[] = {
        { .parts = { "\"\"" }, .count = 1, .element = BUSTER_TYPE_CHAR, .bytes = { 0 }, .length = 1 },
        { .parts = { "\"ab\"", "\"c\"" }, .count = 2, .element = BUSTER_TYPE_CHAR, .bytes = { 'a', 'b', 'c', 0 }, .length = 4 },
        { .parts = { "\"a\\0b\"", "\"\\xff\"" }, .count = 2, .element = BUSTER_TYPE_CHAR, .bytes = { 'a', 0, 'b', 255, 0 }, .length = 5 },
        { .parts = { "\"\\u00e9\\U0001f600\"" }, .count = 1, .element = BUSTER_TYPE_CHAR, .bytes = { 0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80, 0 }, .length = 7 },
        { .parts = { "u8\"é😀\"" }, .count = 1, .element = BUSTER_TYPE_CHAR, .bytes = { 0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80, 0 }, .length = 7 },
        { .parts = { "u\"\\u00e9\\U0001f600\"" }, .count = 1, .element = BUSTER_TYPE_USHORT, .bytes = { 0xe9, 0, 0x3d, 0xd8, 0, 0xde, 0, 0 }, .length = 8 },
        { .parts = { "U\"\\U0001f600\"" }, .count = 1, .element = BUSTER_TYPE_UINT, .bytes = { 0, 0xf6, 1, 0, 0, 0, 0, 0 }, .length = 8 },
        { .parts = { "L\"\\u20ac\"", "\"!\"" }, .count = 2, .element = BUSTER_TYPE_INT, .bytes = { 0xac, 0x20, 0, 0, '!', 0, 0, 0, 0, 0, 0, 0 }, .length = 12 },
        { .parts = { "\"\\x1234\"", "L\"\"" }, .count = 2, .element = BUSTER_TYPE_INT, .bytes = { 0x34, 0x12, 0, 0, 0, 0, 0, 0 }, .length = 8 },
        { .parts = { "u\"\\xd800\"" }, .count = 1, .element = BUSTER_TYPE_USHORT, .bytes = { 0, 0xd8, 0, 0 }, .length = 4 },
        { .parts = { "U\"\\xffffffff\"" }, .count = 1, .element = BUSTER_TYPE_UINT, .bytes = { 255, 255, 255, 255, 0, 0, 0, 0 }, .length = 8 },
        { .parts = { "R\"tag(a\\n\nb)tag\"" }, .count = 1, .element = BUSTER_TYPE_CHAR, .bytes = { 'a', '\\', 'n', '\n', 'b', 0 }, .length = 6 },
        { .parts = { "uR\"(😀)\"", "\"'\\\"\\\\\"" }, .count = 2, .element = BUSTER_TYPE_USHORT, .bytes = { 0x3d, 0xd8, 0, 0xde, '\'', 0, '"', 0, '\\', 0, 0, 0 }, .length = 12 },
        { .parts = { "\"\\1012\\0\"", "\"0\"" }, .count = 2, .element = BUSTER_TYPE_CHAR, .bytes = { 'A', '2', 0, '0', 0 }, .length = 5 },
        { .parts = { "\"\\e[0m\"" }, .count = 1, .element = BUSTER_TYPE_CHAR, .bytes = { 27, '[', '0', 'm', 0 }, .length = 5 },
    };
    Arena arena = arena_reserve(65536);
    char directory[] = "/tmp/buster-checker-strings-XXXXXX";
    const bool directory_ready = mkdtemp(directory) != NULL;
    char source_path[160];
    char object_path[160];
    char output_path[160];
    char error_path[160];
    snprintf(source_path, sizeof(source_path), "%s/oracle.c", directory);
    snprintf(object_path, sizeof(object_path), "%s/oracle.o", directory);
    snprintf(output_path, sizeof(output_path), "%s/out", directory);
    snprintf(error_path, sizeof(error_path), "%s/err", directory);
    FILE* source = directory_ready ? fopen(source_path, "w") : NULL;
    bool written = source != NULL;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        CheckStringPart parts[3];
        char spelling[1024] = { 0 };
        uint32_t used = 0;
        for (uint32_t p = 0; p < fixtures[i].count; p += 1)
        {
            parts[p] = (CheckStringPart){ .bytes = (const uint8_t*)fixtures[i].parts[p], .length = (uint32_t)strlen(fixtures[i].parts[p]) };
            const int added = snprintf(spelling + used, sizeof(spelling) - used, "%s ", fixtures[i].parts[p]);
            written = written && added >= 0 && (uint32_t)added < sizeof(spelling) - used;
            used += added >= 0 && (uint32_t)added < sizeof(spelling) - used ? (uint32_t)added : 0;
        }
        for (uint32_t mode = 0; mode < 2; mode += 1)
        {
            const CheckStringLiteral value = check_literal_strings(parts, fixtures[i].count, mode != 0, &arena);
            const TypeId element = mode != 0 && i == 4 ? BUSTER_TYPE_UCHAR : fixtures[i].element;
            const uint32_t width = element == BUSTER_TYPE_USHORT ? 2 : element == BUSTER_TYPE_UINT || element == BUSTER_TYPE_INT ? 4 : 1;
            char message[160];
            snprintf(message, sizeof(message), "decoded string %u mode %u has independent type/bytes/terminator", i, mode);
            check_test_expect(test, value.valid && !value.resource && value.element_type == element && value.byte_count == fixtures[i].length && value.unit_count == fixtures[i].length / width && memcmp(value.bytes, fixtures[i].bytes, fixtures[i].length) == 0, message);
        }
        if (written)
        {
            written = fprintf(source, "const __typeof__((%s)[0]) string_%u[] = %s;\n", spelling, i, spelling) > 0;
            if (i == 4)
            {
                written = fprintf(source, "#if __STDC_VERSION__ >= 202311L\n_Static_assert(__builtin_types_compatible_p(__typeof__((%s)[0]), unsigned char), \"C23 UTF8 element\");\n#else\n_Static_assert(__builtin_types_compatible_p(__typeof__((%s)[0]), char), \"old UTF8 element\");\n#endif\n", spelling, spelling) > 0 && written;
            }
            else
            {
                const char* type_name = fixtures[i].element == BUSTER_TYPE_CHAR ? "char" : fixtures[i].element == BUSTER_TYPE_USHORT ? "unsigned short" : fixtures[i].element == BUSTER_TYPE_UINT ? "unsigned int" : "int";
                written = fprintf(source, "_Static_assert(__builtin_types_compatible_p(__typeof__((%s)[0]), %s), \"string element\");\n", spelling, type_name) > 0 && written;
            }
        }
    }
    if (source != NULL)
    {
        written = fclose(source) == 0 && written;
    }
    char* arguments[] = {
        (char*)clang,
        "-std=gnu17",
        "-c",
        source_path,
        "-o",
        object_path,
        NULL,
    };
    ObjectView view;
    char error[256];
    for (uint32_t mode = 0; mode < 2; mode += 1)
    {
        arguments[1] = mode == 0 ? "-std=gnu17" : "-std=gnu23";
        bool oracle = written && object_test_run(arguments, output_path, error_path) == 0;
        if (oracle)
        {
            const String8 file = object_test_read_file(&arena, object_path);
            oracle = file.pointer != NULL && object_view_open(&view, (const uint8_t*)file.pointer, file.length, true, error, sizeof(error));
        }
        check_test_expect(test, oracle, "independent Clang string object oracle runs in both language modes");
        for (uint32_t i = 0; oracle && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
        {
            char name[32];
            snprintf(name, sizeof(name), "string_%u", i);
            const uint64_t symbol = check_data_test_find_symbol(&view, name);
            check_test_expect(test, check_data_test_symbol_bytes(&view, symbol, fixtures[i].bytes, fixtures[i].length, BUSTER_LINK_CLASS_RODATA), "decoded string bytes match Clang's independently emitted array");
        }
    }
    if (directory_ready)
    {
        unlink(source_path);
        unlink(object_path);
        unlink(output_path);
        unlink(error_path);
        rmdir(directory);
    }
    const char* invalid[] = {
        "\"\\x\"",
        "\"\\x100\"",
        "u\"\\x10000\"",
        "\"\\uD800\"",
        "U\"\\U00110000\"",
        "\"unterminated",
        "\"a\"b\"",
        "R\"bad delimiter(x)bad delimiter\"",
        "R\"12345678901234567(x)12345678901234567\"",
        "R\"x(a)x\"b)x\"",
        "R\"x(a)y\"",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        const CheckStringPart part = { .bytes = (const uint8_t*)invalid[i], .length = (uint32_t)strlen(invalid[i]) };
        const uint64_t mark = arena.length;
        const CheckStringLiteral value = check_literal_strings(&part, 1, false, &arena);
        check_test_expect(test, !value.valid && !value.resource && value.failed_part == 0 && value.bytes == NULL && arena.length == mark, "malformed string fails without partial arena publication");
    }
    const CheckStringPart mixed[] = {
        { .bytes = (const uint8_t*)"u\"x\"", .length = 4 },
        { .bytes = (const uint8_t*)"U\"y\"", .length = 4 },
    };
    const CheckStringLiteral mismatch = check_literal_strings(mixed, 2, false, &arena);
    check_test_expect(test, !mismatch.valid && mismatch.failed_part == 1, "distinct nonordinary string prefixes cannot concatenate");
    const uint8_t invalid_zero[] = { '"', '\\', 0, '"' };
    const CheckStringPart zero = { .bytes = invalid_zero, .length = sizeof(invalid_zero) };
    check_test_expect(test, !check_literal_strings(&zero, 1, false, &arena).valid, "escaped physical NUL stays within the bounded escape table");
    _Alignas(16) uint8_t buffer[64];
    const CheckStringPart small = { .bytes = (const uint8_t*)"U\"a\"", .length = 4 };
    for (uint64_t capacity = 3; capacity < 12; capacity += 1)
    {
        memset(buffer, 0xa5, sizeof(buffer));
        Arena bounded = { .base = buffer, .length = 3, .capacity = capacity };
        const CheckStringLiteral value = check_literal_strings(&small, 1, false, &bounded);
        check_test_expect(test, !value.valid && value.resource && value.bytes == NULL && bounded.length == 3 && check_test_graph_canary(buffer, 0, sizeof(buffer)), "every partial string output capacity fails atomically");
    }
    memset(buffer, 0xa5, sizeof(buffer));
    Arena exact = { .base = buffer, .length = 3, .capacity = 12 };
    const CheckStringLiteral exact_string = check_literal_strings(&small, 1, false, &exact);
    check_test_expect(test, exact_string.valid && exact.length == 12 && exact_string.bytes == buffer + 4 && check_test_graph_canary(buffer, 0, 4) && check_test_graph_canary(buffer, 12, sizeof(buffer)), "exact string capacity includes alignment and writes only its target bytes");
    for (uint64_t capacity = 3; capacity < 7; capacity += 1)
    {
        Arena bounded = { .base = buffer, .length = 3, .capacity = capacity };
        CheckLiteral real = { 0 };
        check_literal_number((const uint8_t*)"1.0", 3, &bounded, &real);
        CheckLiteral integer = { 0 };
        check_literal_number((const uint8_t*)"17", 2, &bounded, &integer);
        check_test_expect(test, !real.valid && real.resource && integer.valid && !integer.resource && bounded.length == 3, "floating spelling allocation failure is a resource result, distinct from invalid literal syntax");
    }
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    check_test_expect(test, guarded, "string guarded-tail allocation");
    for (uint32_t length = 0; guarded && length <= 33; length += 1)
    {
        uint8_t* bytes = mapping + page - length;
        memset(bytes, 'a', length);
        if (length >= 2)
        {
            bytes[0] = '"';
            bytes[length - 1] = '"';
        }
        const CheckStringPart part = { .bytes = bytes, .length = length };
        const CheckStringLiteral value = check_literal_strings(&part, 1, false, &arena);
        check_test_expect(test, value.valid == (length >= 2) && (!value.valid || value.unit_count == length - 1), "unrounded string input ends at a protected page");
    }
    for (uint32_t count = 0; guarded && count <= 33; count += 1)
    {
        CheckStringPart* parts = (CheckStringPart*)(mapping + page - count * sizeof(CheckStringPart));
        for (uint32_t i = 0; i < count; i += 1)
        {
            parts[i] = (CheckStringPart){ .bytes = (const uint8_t*)"\"a\"", .length = 3 };
        }
        const CheckStringLiteral value = check_literal_strings(parts, count, false, &arena);
        check_test_expect(test, value.valid == (count != 0) && (!value.valid || value.unit_count == count + 1), "literal-part batches end at a protected page for counts zero through 33");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
    arena_release(&arena);
}

static void check_test_static_selection(CheckTest* test, TypeTable* table)
{
    CheckValue inputs[] = {
        check_test_integer(BUSTER_TYPE_INT, 7),
        { .type = BUSTER_TYPE_INT, .category = CHECK_LVALUE, .flags = BUSTER_CHECK_REGISTER },
        { .type = type_array(table, BUSTER_TYPE_INT, 3), .category = CHECK_LVALUE, .flags = BUSTER_CHECK_STATIC_OBJECT, .object = 19 },
        { .type = type_function(table, BUSTER_TYPE_INT, NULL, 0, BUSTER_TYPE_FLAG_PROTOTYPE), .category = CHECK_FUNCTION, .flags = BUSTER_CHECK_STATIC_OBJECT, .object = 23 },
        { .type = BUSTER_TYPE_UINT, .category = CHECK_LVALUE, .flags = BUSTER_CHECK_BIT_FIELD, .bit_width = 3 },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(inputs); i += 1)
    {
        const CheckAction action = { .op = CHECK_OP_SELECT, .operands = { inputs[i] } };
        const CheckActionResult result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && check_test_value_equal(&result.value, &inputs[i]) && result.conversions[0].flags == 0 && result.conversions[0].type == inputs[i].type, "static selection preserves category, qualifiers, array/function identity and bit-field metadata");
        for (uint32_t effect = 0; effect < 2; effect += 1)
        {
            CheckAction statement = { .op = CHECK_OP_STATEMENT_VALUE, .operands = { inputs[i] }, .member = effect != 0 ? BUSTER_CHECK_SIDE_EFFECT : 0 };
            if (i == 2)
            {
                statement.operands[0].flags &= ~BUSTER_CHECK_REGISTER;
            }
            const CheckActionResult value = check_test_action(test, table, statement);
            const TypeId expected = i == 2 ? type_pointer(table, BUSTER_TYPE_INT) : i == 3 ? type_pointer(table, inputs[i].type) : inputs[i].type;
            check_test_expect(test, value.diagnostic == CHECK_DIAG_NONE && value.value.type == expected && value.value.category == CHECK_RVALUE &&
                                   (value.value.flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER)) == 0 &&
                                   ((value.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0) == (effect != 0),
                              "configured Clang statement-expression results apply final conversions and preserve preceding region effects");
        }
    }
}

static void check_test_record_casts(CheckTest* test, TypeTable* table)
{
    const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 133501);
    const TypeId other = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 133502);
    const TypeId variant = type_tag(table, BUSTER_TYPE_KIND_UNION, 133503);
    const TypeMember member = { .name = 133504, .type = BUSTER_TYPE_INT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
    const bool complete = type_complete_record(table, record, &member, 1, (TypeRecordAttributes){ 0 }) &&
                          type_complete_record(table, other, &member, 1, (TypeRecordAttributes){ 0 }) &&
                          type_complete_record(table, variant, &member, 1, (TypeRecordAttributes){ 0 });
    check_test_expect(test, complete, "record-cast distinct compatible-layout type construction");
    const TypeId records[] = { record, variant };
    for (uint32_t i = 0; complete && i < BUSTER_ARRAY_LENGTH(records); i += 1)
    {
        CheckAction cast = {
            .op = CHECK_OP_CAST,
            .operands = { { .type = records[i] | BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE, .category = CHECK_LVALUE } },
            .type = records[i],
        };
        const CheckActionResult strict = check_test_action(test, table, cast);
        check_test_expect(test, strict.diagnostic == CHECK_DIAG_NONE && strict.value.type == records[i],
                          "a same-record cast is GNU's extension, which clang has outside GNU modes too");
        cast.extensions = BUSTER_CHECK_EXTENSION_GNU;
        const CheckActionResult gnu = check_test_action(test, table, cast);
        check_test_expect(test, gnu.diagnostic == CHECK_DIAG_NONE && gnu.value.type == records[i] && gnu.value.category == CHECK_RVALUE &&
                               gnu.value.constant_kind == CHECK_CONSTANT_NONE && (gnu.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                               (gnu.conversions[0].flags & (BUSTER_CHECK_CONVERT_LVALUE | BUSTER_CHECK_CONVERT_CAST)) ==
                               (BUSTER_CHECK_CONVERT_LVALUE | BUSTER_CHECK_CONVERT_CAST),
                          "GNU same-record cast performs the volatile read once and produces an unqualified rvalue");
        cast.operands[0].type = other;
        const CheckActionResult incompatible = check_test_action(test, table, cast);
        check_test_expect(test, incompatible.diagnostic == CHECK_DIAG_INCOMPATIBLE,
                          "GNU record cast rejects a distinct record despite identical member layout");
    }
}

static void check_test_mixed_void_conditional(CheckTest* test, TypeTable* table)
{
    for (uint32_t truth = 0; truth < 2; truth += 1)
    {
        for (uint32_t reverse = 0; reverse < 2; reverse += 1)
        {
            CheckAction action = {
                .op = CHECK_OP_CONDITIONAL,
                .operands = { check_test_integer(BUSTER_TYPE_INT, truth) },
            };
            action.operands[1 + reverse] = (CheckValue){ .type = BUSTER_TYPE_VOID, .category = CHECK_RVALUE };
            action.operands[2 - reverse] = (CheckValue){
                .type = BUSTER_TYPE_INT,
                .category = CHECK_RVALUE,
                .flags = BUSTER_CHECK_SIDE_EFFECT,
                .fault = CHECK_FAULT_DIVIDE_BY_ZERO,
                .fault_node = 17,
            };
            const CheckActionResult strict = check_test_action(test, table, action);
            check_test_expect(test, strict.diagnostic == CHECK_DIAG_NONE && strict.value.type == BUSTER_TYPE_VOID,
                              "a conditional with one void operand is void outside GNU modes too");
            action.extensions = BUSTER_CHECK_EXTENSION_GNU;
            const CheckActionResult gnu = check_test_action(test, table, action);
            const bool selected_integer = truth == reverse;
            check_test_expect(test, gnu.diagnostic == CHECK_DIAG_NONE && gnu.value.type == BUSTER_TYPE_VOID && gnu.value.category == CHECK_RVALUE &&
                                   gnu.value.constant_kind == CHECK_CONSTANT_NONE &&
                                   ((gnu.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0) == selected_integer &&
                                   (gnu.value.fault != CHECK_FAULT_NONE) == selected_integer &&
                                   gnu.conversions[1].type == BUSTER_TYPE_VOID && gnu.conversions[2].type == BUSTER_TYPE_VOID,
                              "GNU mixed-void conditional yields void and retains only the evaluated arm's effects/faults");
        }
    }
}

static void check_test_action_histories(CheckTest* test, TypeTable* table)
{
    const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 7101);
    const TypeMember member = { .name = 7102, .type = type_array(table, BUSTER_TYPE_INT, 3), .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
    check_test_expect(test, type_complete_record(table, record, &member, 1, (TypeRecordAttributes){ 0 }), "source-history record completion");
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 7103);
    check_test_expect(test, type_enum_complete(table, enumeration, BUSTER_TYPE_UINT, 0), "source-history enum completion");
    const TypeId pointer = type_pointer(table, record);
    const TypeId function = type_function(table, record, NULL, 0, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId with_argument = type_function(table, BUSTER_TYPE_VOID, &record, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const CheckValue object = { .type = record, .category = CHECK_LVALUE, .flags = BUSTER_CHECK_STATIC_OBJECT, .object = 29, .constant_kind = CHECK_CONSTANT_ADDRESS };
    const CheckValue address = { .type = pointer, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_STATIC_OBJECT, .object = 29, .constant_kind = CHECK_CONSTANT_ADDRESS };
    Arena storage = arena_reserve(65536);
    Arena output = arena_reserve(65536);
    Arena scratch = arena_reserve(65536);
    uint32_t* completions = BUSTER_ARENA_PUSH_ARRAY(&storage, uint32_t, table->tags.count);
    const bool ready = completions != NULL;
    check_test_expect(test, ready, "source-history completion storage");
    if (ready)
    {
        memset(completions, 0, (uint64_t)table->tags.count * sizeof(*completions));
        completions[table->types.index[type_index(record)]] = 20;
        completions[table->types.index[type_index(enumeration)]] = 20;
        CheckConversion argument_conversion;
        CheckAction requiring[] = {
            { .op = CHECK_OP_SIZEOF, .type = record },
            { .op = CHECK_OP_ALIGNOF, .type = record },
            { .op = CHECK_OP_SIZEOF, .type = type_aligned(table, type_array(table, record, 2), 5) },
            { .op = CHECK_OP_MEMBER, .operands = { object }, .member = member.name },
            { .op = CHECK_OP_ARROW, .operands = { address }, .member = member.name },
            { .op = CHECK_OP_ADD, .operands = { address, check_test_integer(BUSTER_TYPE_INT, 1) } },
            { .op = CHECK_OP_SUBTRACT, .operands = { address, address } },
            { .op = CHECK_OP_CONVERT, .operands = { object }, .type = record },
            { .op = CHECK_OP_ASSIGN, .operands = { object, object } },
            { .op = CHECK_OP_CALL, .operands = { { .type = function, .category = CHECK_FUNCTION } } },
            { .op = CHECK_OP_CALL, .operands = { { .type = with_argument, .category = CHECK_FUNCTION } }, .arguments = &object, .argument_count = 1, .argument_conversions = &argument_conversion },
            { .op = CHECK_OP_CAST, .operands = { check_test_integer(BUSTER_TYPE_INT, 0) }, .type = enumeration },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(requiring); i += 1)
        {
            requiring[i].tag_completions = completions;
            requiring[i].tag_completion_count = table->tags.count;
            requiring[i].source_position = 19;
            CheckActionResult result = check_test_action(test, table, requiring[i]);
            check_test_expect(test, result.diagnostic != CHECK_DIAG_NONE, "later tag completion cannot validate an earlier operation needing completeness");
            requiring[i].source_position = 20;
            result = check_test_action(test, table, requiring[i]);
            check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE, "the same operation is valid at the completion cutoff");
        }
        CheckAction opaque[] = {
            { .op = CHECK_OP_ADDRESS, .operands = { object } },
            { .op = CHECK_OP_DEREFERENCE, .operands = { address } },
            { .op = CHECK_OP_EQUAL, .operands = { address, check_test_integer(BUSTER_TYPE_INT, 0) } },
            { .op = CHECK_OP_CONVERT, .operands = { address }, .type = pointer },
            { .op = CHECK_OP_SIZEOF, .type = pointer },
            { .op = CHECK_OP_CONDITIONAL, .operands = { check_test_integer(BUSTER_TYPE_INT, 1), address, check_test_integer(BUSTER_TYPE_INT, 0) } },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(opaque); i += 1)
        {
            opaque[i].tag_completions = completions;
            opaque[i].tag_completion_count = table->tags.count;
            opaque[i].source_position = 19;
            const CheckActionResult result = check_test_action(test, table, opaque[i]);
            check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE, "opaque pointers, incomplete designators and their addresses stay valid");
        }
        CheckAction action = { .op = CHECK_OP_ARROW, .operands = { address }, .member = member.name };
        const CheckActionResult field = check_test_action(test, table, action);
        check_test_expect(test, field.diagnostic == CHECK_DIAG_NONE && (field.value.flags & BUSTER_CHECK_BIT_FIELD) == 0, "ordinary array member is not a zero-width bit-field");
        action = (CheckAction){ .op = CHECK_OP_SIZEOF, .operands = { field.value } };
        const CheckActionResult size = check_test_action(test, table, action);
        check_test_expect(test, size.diagnostic == CHECK_DIAG_NONE && size.value.integer == 12, "sizeof ordinary array member uses full array size");
        action.op = CHECK_OP_ADDRESS;
        const CheckActionResult member_address = check_test_action(test, table, action);
        check_test_expect(test, member_address.diagnostic == CHECK_DIAG_NONE && type_element(table, member_address.value.type) == member.type, "address of an ordinary array member preserves the array target");
        CheckGraph graph;
        const bool graph_ready = check_graph_initialize(&graph, &storage, 3, 0);
        check_test_expect(test, graph_ready, "source-history graph storage");
        if (graph_ready)
        {
            graph.tag_completions = completions;
            graph.tag_completion_count = table->tags.count;
            const uint32_t leaf = check_test_leaf(&graph, 4, object);
            const uint32_t early = check_graph_add(&graph, CHECK_OP_SIZEOF, 5, leaf, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            const uint32_t late = check_graph_add(&graph, CHECK_OP_SIZEOF, 6, leaf, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            graph.source_positions[leaf] = 10;
            graph.source_positions[early] = 19;
            graph.source_positions[late] = 20;
            for (uint32_t path = 0; path < 2; path += 1)
            {
                const CheckOptions options = { .scalar = path == 0 };
                CheckStream stream;
                const bool checked = check_graph_run(table, &graph, &output, &scratch, &options, &stream);
                char message[192];
                snprintf(message, sizeof(message), "source cutoffs path %u checked %u reports %u early %u late %u size %u", path, checked, stream.report_count, checked ? stream.diagnostics[early] : 0, checked ? stream.diagnostics[late] : 0, checked ? (uint32_t)check_stream_value(&stream, late).integer : 0);
                check_test_expect(test, checked && stream.report_count == 1 && stream.diagnostics[early] == CHECK_DIAG_INCOMPLETE && stream.diagnostics[late] == CHECK_DIAG_NONE && check_stream_value(&stream, late).integer == 12, message);
                arena_pop_to(&output, 0);
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
}

static void check_test_gnu_constant_contexts(CheckTest* test, TypeTable* table)
{
    Arena storage = arena_reserve(65536);
    Arena output = arena_reserve(65536);
    Arena scratch = arena_reserve(65536);
    for (uint32_t form = 0; form < 9; form += 1)
    {
        CheckGraph graph;
        const bool ready = check_graph_initialize(&graph, &storage, 8, 0);
        check_test_expect(test, ready, "GNU constant fixture storage");
        if (ready)
        {
            const uint32_t zero = check_test_leaf(&graph, 1, check_test_integer(BUSTER_TYPE_INT, 0));
            const uint32_t one = check_test_leaf(&graph, 2, check_test_integer(BUSTER_TYPE_INT, 1));
            const uint32_t real = check_test_leaf(&graph, 3, check_test_float(BUSTER_TYPE_DOUBLE, 1));
            const uint32_t effect = check_test_leaf(&graph, 4, (CheckValue){ .type = BUSTER_TYPE_INT, .flags = BUSTER_CHECK_SIDE_EFFECT });
            uint32_t root;
            if (form == 0)
            {
                root = check_graph_add(&graph, CHECK_OP_COMMA, 5, one, one, BUSTER_CHECK_NONE);
            }
            else if (form == 1)
            {
                root = check_graph_add(&graph, CHECK_OP_EQUAL, 5, real, real, BUSTER_CHECK_NONE);
            }
            else if (form == 2 || form == 5)
            {
                root = check_graph_add(&graph, form == 2 ? CHECK_OP_LOGICAL_OR : CHECK_OP_LOGICAL_AND, 5, one, effect, BUSTER_CHECK_NONE);
            }
            else if (form == 3 || form == 4 || form == 6)
            {
                root = check_graph_add(&graph, CHECK_OP_CONDITIONAL, 5, form == 4 ? zero : one, form == 3 ? one : effect, form == 3 ? effect : one);
            }
            else
            {
                const uint32_t invalid = check_graph_add(&graph, form == 7 ? CHECK_OP_REMAINDER : CHECK_OP_DIVIDE, 5, form == 7 ? real : one, zero, BUSTER_CHECK_NONE);
                root = form == 7 ? check_graph_add(&graph, CHECK_OP_LOGICAL_OR, 6, one, invalid, BUSTER_CHECK_NONE) : invalid;
            }
            for (uint32_t mode = 0; mode < 2; mode += 1)
            {
                graph.contexts[root] = BUSTER_CHECK_CONTEXT_ASSERT | (mode != 0 ? BUSTER_CHECK_CONTEXT_GNU_CONSTANT : 0);
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    const CheckOptions options = { .scalar = path == 0 };
                    CheckStream stream;
                    const bool checked = check_graph_run(table, &graph, &output, &scratch, &options, &stream);
                    const bool accept = mode != 0 && form < 5;
                    check_test_expect(test, checked && (stream.report_count == 0) == accept && (!accept || (check_stream_flags(&stream, root) & (BUSTER_CHECK_ICE | BUSTER_CHECK_SIDE_EFFECT)) == 0), "GNU folded acceptance neither inflates strict ICE nor suppresses evaluated effects/type errors/faults");
                    arena_pop_to(&output, 0);
                }
            }
        }
        arena_pop_to(&storage, 0);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
}

static void check_test_gnu_actions(CheckTest* test, TypeTable* table)
{
    const TypeId function = type_function(table, BUSTER_TYPE_INT, NULL, 0, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId types[] = { BUSTER_TYPE_VOID, function };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(types); i += 1)
    {
        for (uint32_t operation = 0; operation < 2; operation += 1)
        {
            CheckAction action = { .op = operation == 0 ? CHECK_OP_SIZEOF : CHECK_OP_ALIGNOF, .type = types[i] };
            CheckActionResult result = check_test_action(test, table, action);
            check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.integer == (i == 1 && operation == 1 ? 4 : 1),
                              "void/function layout extensions apply outside GNU modes too");
            action.extensions = BUSTER_CHECK_EXTENSION_GNU;
            result = check_test_action(test, table, action);
            check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_INTEGER && (result.value.flags & BUSTER_CHECK_ICE) != 0 && result.value.integer == (i == 1 && operation == 1 ? 4 : 1), "explicit GNU void/function target layout matches Clang");
        }
        const TypeId pointer = type_pointer(table, types[i]);
        const CheckValue base = { .type = pointer, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_STATIC_OBJECT, .constant_kind = CHECK_CONSTANT_ADDRESS, .object = 31, .offset = 8 };
        CheckAction action = { .op = CHECK_OP_ADD, .operands = { base, check_test_integer(BUSTER_TYPE_INT, 3) } };
        CheckActionResult result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.offset == 11, "void/function pointer arithmetic applies outside GNU modes too");
        action.extensions = BUSTER_CHECK_EXTENSION_GNU;
        result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == pointer && result.value.object == 31 && result.value.offset == 11, "GNU void/function pointer addition uses a one-byte scale");
        action.op = CHECK_OP_SUBTRACT;
        action.operands[0] = result.value;
        action.operands[1] = base;
        result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == BUSTER_TYPE_LONG && result.value.integer == 3, "GNU pointer subtraction uses the same explicit byte scale");
        action.op = CHECK_OP_SUBSCRIPT;
        action.operands[0] = base;
        action.operands[1] = check_test_integer(BUSTER_TYPE_INT, 3);
        result = check_test_action(test, table, action);
        check_test_expect(test, i == 0 ? result.diagnostic == CHECK_DIAG_NONE && result.value.type == BUSTER_TYPE_VOID && result.value.category == CHECK_LVALUE : result.diagnostic == CHECK_DIAG_INCOMPLETE, "GNU void subscripts remain lvalues while function-pointer subscripts are still invalid");
        action = (CheckAction){ .op = CHECK_OP_CAST, .operands = { { .type = BUSTER_TYPE_VOID, .category = CHECK_LVALUE } }, .type = BUSTER_TYPE_VOID, .extensions = BUSTER_CHECK_EXTENSION_GNU };
        result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == BUSTER_TYPE_VOID, "GNU discarded void indirection can be explicitly converted to void");
    }
    const CheckLiteral escape = check_literal_character((const uint8_t*)"'\\e'", 4, false, false);
    check_test_expect(test, escape.valid && escape.integer == 27, "GNU escape character uses target ESC code 27");
    const TypeId void_pointer = type_pointer(table, BUSTER_TYPE_VOID);
    const TypeId function_pointer = type_pointer(table, function);
    const TypeId destinations[] = { void_pointer, function_pointer };
    for (uint32_t direction = 0; direction < 2; direction += 1)
    {
        const CheckValue address = { .type = destinations[direction ^ 1], .constant_kind = CHECK_CONSTANT_ADDRESS, .object = 37, .flags = BUSTER_CHECK_STATIC_OBJECT };
        CheckAction action = { .op = CHECK_OP_CONVERT, .operands = { address }, .type = destinations[direction] };
        CheckActionResult result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.warnings == 0 && result.value.object == 37,
                          "void/function pointer implicit conversion is clang's pedantic extension in every mode");
        action.extensions = BUSTER_CHECK_EXTENSION_GNU;
        result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == destinations[direction] && result.value.object == 37, "explicit GNU mode accepts void/function pointer interchange preserving provenance");
    }
    CheckAction action = { .op = CHECK_OP_SUBTRACT, .operands = { check_test_float(BUSTER_TYPE_DOUBLE, 1), check_test_float(BUSTER_TYPE_DOUBLE, 1) }, .extensions = BUSTER_CHECK_EXTENSION_GNU };
    CheckActionResult result = check_test_action(test, table, action);
    action = (CheckAction){ .op = CHECK_OP_CAST, .operands = { result.value }, .type = BUSTER_TYPE_INT, .extensions = BUSTER_CHECK_EXTENSION_GNU };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_INTEGER && result.value.integer == 0 && (result.value.flags & BUSTER_CHECK_ICE) == 0, "GNU arithmetic folding keeps non-ICE zero distinct from a null pointer constant");
    action = (CheckAction){ .op = CHECK_OP_CONVERT, .operands = { result.value }, .type = void_pointer, .extensions = BUSTER_CHECK_EXTENSION_GNU };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_INCOMPATIBLE, "GNU known zero from floating arithmetic still cannot initialize a pointer implicitly");
}

static void check_test_pointer_integer_casts(CheckTest* test, TypeTable* table, const char* clang)
{
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_VOID);
    const TypeId integers[] = {
        BUSTER_TYPE_SCHAR, BUSTER_TYPE_UCHAR, BUSTER_TYPE_INT, BUSTER_TYPE_LONG,
        BUSTER_TYPE_ULONG, BUSTER_TYPE_INT128, BUSTER_TYPE_UINT128, BUSTER_TYPE_BOOL,
    };
    const uint64_t bits = UINT64_C(0x8000000100000001);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(integers); i += 1)
    {
        const CheckAction action = {
            .op = CHECK_OP_CAST,
            .operands = { { .type = pointer, .constant_kind = CHECK_CONSTANT_ADDRESS, .offset = (int64_t)bits } },
            .type = integers[i],
        };
        const CheckActionResult result = check_test_action(test, table, action);
        const unsigned __int128 expected = i < 3 || i == 7 ? 1 : bits;
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_INTEGER &&
                               result.value.integer == expected && (result.value.flags & BUSTER_CHECK_ICE) == 0,
                          "absolute pointer casts retain target bits without becoming strict integer constant expressions");
    }
    const CheckValue symbolic = {
        .type = pointer,
        .flags = BUSTER_CHECK_STATIC_OBJECT,
        .constant_kind = CHECK_CONSTANT_ADDRESS,
        .object = 191,
        .offset = 4,
    };
    CheckValue absolute = check_test_integer(BUSTER_TYPE_ULONG, bits);
    absolute.flags = 0;
    CheckAction action = { .op = CHECK_OP_CAST, .operands = { absolute }, .type = pointer };
    CheckActionResult result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_ADDRESS &&
                           result.value.object == 0 && (uint64_t)result.value.offset == bits &&
                           (result.value.flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_NULL_POINTER)) == 0,
                      "known integer-to-pointer casts preserve absolute roundtrips without inventing null-pointer-constant status");
    action = (CheckAction){ .op = CHECK_OP_CAST, .operands = { symbolic }, .type = BUSTER_TYPE_ULONG };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_ADDRESS &&
                           result.value.object == 191 && result.value.offset == 4 && (result.value.flags & BUSTER_CHECK_ICE) == 0,
                      "pointer-width integer casts preserve an ABS64 symbolic address");
    const CheckValue address_integer = result.value;
    for (uint32_t operation = 0; operation < 4; operation += 1)
    {
        action = (CheckAction){ .op = operation == 2 ? CHECK_OP_SUBTRACT : operation == 3 ? CHECK_OP_POSITIVE : CHECK_OP_ADD };
        action.operands[operation == 1 ? 1 : 0] = address_integer;
        action.operands[operation == 1 ? 0 : 1] = check_test_integer(BUSTER_TYPE_INT, 3);
        result = check_test_action(test, table, action);
        const int64_t expected = operation == 2 ? 1 : operation == 3 ? 4 : 7;
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_ADDRESS &&
                               result.value.object == 191 && result.value.offset == expected && (result.value.flags & BUSTER_CHECK_ICE) == 0,
                          "symbolic integer addition/subtraction has byte addends and one retained object coefficient");
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(integers); i += 1)
    {
        action = (CheckAction){ .op = CHECK_OP_CAST, .operands = { symbolic }, .type = integers[i] };
        result = check_test_action(test, table, action);
        const bool relocatable = i == 3 || i == 4;
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == (relocatable ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_NONE),
                          "symbolic integer casts require exactly the supported pointer-width relocation");
    }
    const TypeId null_targets[] = { BUSTER_TYPE_INT, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_BOOL, pointer, BUSTER_TYPE_VOID, BUSTER_TYPE_NULLPTR };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(null_targets); i += 1)
    {
        action = (CheckAction){
            .op = CHECK_OP_CAST,
            .operands = { { .type = BUSTER_TYPE_NULLPTR, .constant_kind = CHECK_CONSTANT_ADDRESS } },
            .type = null_targets[i],
        };
        result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == (i < 2 ? CHECK_DIAG_INCOMPATIBLE : CHECK_DIAG_NONE),
                          "C23 nullptr only casts to bool, pointer, nullptr_t or void");
    }
    const char* sources[] = {
        "_Static_assert((unsigned long)(void*)17==17,\"absolute\");\n",
        "_Static_assert((unsigned)(void*)0x100000001UL==1,\"narrow\");\n",
        "_Static_assert((__int128)(void*)0xffffffffffffffffUL==(__int128)0xffffffffffffffffUL,\"wide\");\n",
        "_Static_assert((long)(void*)0xffffffffffffffffUL==-1,\"signed\");\n",
        "_Static_assert((void*)(unsigned long)(void*)17==(void*)17,\"roundtrip\");\n",
        "_Static_assert((unsigned long)(void*)17==18,\"mutation\");\n",
        "int object; unsigned long symbolic=(unsigned long)&object+3;\n",
        "int integer=(int)nullptr;\n",
        "double floating=(double)nullptr;\n",
        "_Bool boolean=(_Bool)nullptr;void *pointer=(void*)nullptr;void f(void){(void)nullptr;}\n",
    };
    const bool rejected[] = { false, false, false, false, false, true, false, true, true, false };
    char directory[] = "/tmp/buster-checker-casts-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(sources)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(sources)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(sources)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = ready && parse_test_write(paths[i], sources[i], strlen(sources[i]));
    }
    const char* mode = "-std=gnu23";
    const bool checked = ready && parse_test_clang(clang, &mode, 1, pointers, BUSTER_ARRAY_LENGTH(sources), outcomes);
    check_test_expect(test, checked, "independent pointer/nullptr cast oracle runs");
    for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        check_test_expect(test, outcomes[i].rejected == rejected[i], "Clang agrees with pointer/nullptr cast semantic mutations");
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        unlink(paths[i]);
    }
    rmdir(directory);
}

static void check_test_qualifier_warnings(CheckTest* test, TypeTable* table)
{
    const TypeId plain = type_pointer(table, BUSTER_TYPE_INT);
    const TypeId qualified = type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST));
    const CheckValue source = { .type = qualified };
    CheckAction action = { .op = CHECK_OP_CONVERT, .operands = { source }, .type = plain };
    CheckActionResult result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.warnings == CHECK_WARNING_DISCARDED_QUALIFIERS && result.value.type == plain,
                      "qualifier discard outside GNU modes warns, as clang does");
    action.extensions = BUSTER_CHECK_EXTENSION_GNU;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.warnings == CHECK_WARNING_DISCARDED_QUALIFIERS && result.value.type == plain,
                      "GNU qualifier discard preserves warning metadata while accepting the compatible target");
    const TypeId function = type_function(table, BUSTER_TYPE_VOID, &plain, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    CheckConversion conversion;
    action = (CheckAction){
        .op = CHECK_OP_CALL,
        .operands = { { .type = function, .category = CHECK_FUNCTION } },
        .arguments = &source,
        .argument_count = 1,
        .argument_conversions = &conversion,
        .extensions = BUSTER_CHECK_EXTENSION_GNU,
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.warnings == CHECK_WARNING_DISCARDED_QUALIFIERS && conversion.type == plain,
                      "call argument conversions retain GNU qualifier warning metadata");
    Arena storage = arena_reserve(65536);
    Arena output = arena_reserve(65536);
    Arena scratch = arena_reserve(65536);
    CheckGraph graph;
    const bool ready = storage.base != NULL && output.base != NULL && scratch.base != NULL && check_graph_initialize(&graph, &storage, 2, 0);
    check_test_expect(test, ready, "qualifier-warning graph allocation");
    if (ready)
    {
        graph.extensions = BUSTER_CHECK_EXTENSION_GNU;
        const uint32_t leaf = check_test_leaf(&graph, 10, source);
        const uint32_t node = check_graph_add(&graph, CHECK_OP_CONVERT, 11, leaf, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        graph.types[node] = plain;
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const CheckOptions options = { .scalar = path == 0 };
            CheckStream stream;
            const bool checked = check_graph_run(table, &graph, &output, &scratch, &options, &stream);
            check_test_expect(test, checked && stream.report_count == 0 && stream.warnings[leaf] == 0 && stream.warnings[node] == CHECK_WARNING_DISCARDED_QUALIFIERS,
                              "graph warning columns are exact and do not inflate rejection report counts");
            arena_pop_to(&output, 0);
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
}

static void check_test_extra_semantic_oracles(CheckTest* test, const char* clang)
{
    const char* sources[] = {
        "_Static_assert((1, 1), \"folded comma\");\n",
        "_Static_assert(1.0 == 1.0, \"folded floating comparison\");\n",
        "int f(void); _Static_assert(1 || f(), \"unselected call\");\n",
        "int f(void); _Static_assert(1 ? 1 : f(), \"unselected conditional arm\");\n",
        "int f(void); _Static_assert(0 ? f() : 1, \"other unselected arm\");\n",
        "int f(void); _Static_assert(1 && f(), \"selected call\");\n",
        "int f(void); _Static_assert(1 ? f() : 1, \"selected conditional call\");\n",
        "_Static_assert(1 || (1.0 % 0), \"unselected type error\");\n",
        "_Static_assert(1 / 0, \"selected fault\");\n",
        "struct S; extern struct S s; struct S *p = &s; int f(void){ return p == 0; } struct S{int a[3];};\n",
        "struct S; int f(struct S *p){return p->a[0];} struct S{int a[3];};\n",
        "struct S{int a[3];}; int f(struct S *p){return p->a[0];}\n",
        "struct S; unsigned long n = sizeof(struct S); struct S{int a[3];};\n",
        "struct S{int a[3];}; unsigned long n = sizeof(struct S);\n",
        "struct S; struct S f(void); void g(void){f();} struct S{int x;};\n",
        "struct S{int x;}; struct S f(void); void g(void){f();}\n",
        "enum E; void f(void){(enum E)0;} enum E{ZERO};\n",
        "enum E{ZERO}; void f(void){(enum E)0;}\n",
        "int x; int *p = &_Generic(0, int: x, default: 0);\n",
        "int x; int *p = &_Generic(0, int: 0, default: x);\n",
        "int x; int *p = &__builtin_choose_expr(1, x, 0);\n",
        "int x; int *p = &__builtin_choose_expr(0, x, 0);\n",
        "struct S{int a[3];}; _Static_assert(sizeof(((struct S*)0)->a) == 12, \"ordinary member\");\n",
        "struct S{int a[3];}; _Static_assert(sizeof(((struct S*)0)->a) == 4, \"mutated member size\");\n",
        "unsigned short s[] = u\"x\" U\"y\";\n",
        "char s[] = \"\\x100\";\n",
        "char s[] = \"\\uD800\";\n",
        "_Static_assert(sizeof(void) == 1 && __alignof__(void) == 1 && sizeof(int(void)) == 1 && __alignof__(int(void)) == 4, \"GNU layout\");\n",
        "_Static_assert(sizeof(void) == 4, \"mutated GNU layout\");\n",
        "char a[8]; _Static_assert((char*)((void*)a + 3) - a == 3, \"GNU byte scale\");\n",
        "char a[8]; _Static_assert((char*)((void*)a + 3) - a == 4, \"mutated GNU byte scale\");\n",
        "int f(void); _Static_assert(sizeof(f[1]) == 1, \"GNU function designator\");\n",
        "_Static_assert('\\e' == 27, \"GNU escape\");\n",
        "int f(void); _Static_assert(sizeof(*(f + 1)) == 1, \"GNU function arithmetic\");\n",
        "int f(void); void *v = f; int (*p)(void) = (void*)1;\n",
        "int *p = (int)(1.0 - 1.0);\n",
        "int *p = (0, 0);\n",
        "void f(void){int x;({x;})=3;}\n",
        "void f(void){int x;int*p=&({x;});}\n",
        "void f(void){_Static_assert(({1;}),\"pure statement\");}\n",
        "int x;void f(void){_Static_assert(({x=2;1;}),\"effect statement\");}\n",
        "void f(int i){void*p;p=i?0:(const void*)0;}\n",
        "struct S{int x;};struct S f(const struct S x){return (struct S)x;}\n",
        "union U{int x;double y;};union U f(union U x){return (union U)x;}\n",
        "struct S{int x;};struct T{int x;};struct S f(struct T x){return (struct S)x;}\n",
        "struct S{int x;};void f(struct S x){(struct S)x=x;}\n",
        "struct S{int x;};void f(struct S x){struct S*p=&((struct S)x);}\n",
        "void f(int n){(n?1:(void)0);(n?(void)0:1);}\n",
        "void f(int n){int x=(n?1:(void)0);}\n",
        "struct S{int x;};void f(int n,struct S s){(n?s:(void)0);}\n",
        "void f(int n){int a[n];_Static_assert(sizeof a>0,\"runtime size\");}\n",
        "void f(int n){int(*a[3])[n];_Static_assert(sizeof a==24,\"fixed pointer array\");}\n",
        "void f(int n){int(*a[3])[n];_Static_assert(sizeof a==12,\"mutated pointer array\");}\n",
        "void f(int n){int a[3][n];_Static_assert(sizeof a==0,\"nested runtime size\");}\n",
    };
    const bool rejected[] = {
        false, false, false, false, false, true, true, true, true,
        false, true, false, true, false, true, false, true, false,
        false, true, false, true, false, true, true, true, true,
        false, true, false, true, true, false, false, false, true, true,
        true, true, false, true, false,
        false, false, true, true, true,
        false, true, false,
        true, false, true, true,
    };
    char directory[] = "/tmp/buster-checker-extra-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(sources)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(sources)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(sources)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = ready && parse_test_write(paths[i], sources[i], strlen(sources[i]));
    }
    const char* modes[] = { "-std=gnu17", "-std=c17", "-std=gnu23" };
    for (uint32_t mode = 0; ready && mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
    {
        const bool checked = parse_test_clang(clang, &modes[mode], 1, pointers, BUSTER_ARRAY_LENGTH(sources), outcomes);
        check_test_expect(test, checked, "independent semantic history/selection/GNU oracle runs");
        for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
        {
            char message[160];
            snprintf(message, sizeof(message), "independent semantic mutation %u agrees in %s", i, modes[mode]);
            check_test_expect(test, outcomes[i].rejected == rejected[i], message);
        }
    }
    check_test_expect(test, ready, "semantic oracle source creation");
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        unlink(paths[i]);
    }
    rmdir(directory);
}

static void check_test_vla_actions(CheckTest* test, TypeTable* table)
{
    const TypeId variable = type_variable_array(table, BUSTER_TYPE_INT, 133801);
    const TypeId nested = type_array(table, variable, 3);
    const TypeId pointer = type_pointer(table, variable);
    const TypeId pointer_array = type_array(table, pointer, 3);
    const TypeId zero = type_array(table, BUSTER_TYPE_INT, 0);
    check_test_expect(test, variable != 0 && nested != 0 && pointer != 0 && pointer_array != 0 && zero != 0,
                      "VLA action test type construction");
    const TypeId dynamic[] = { variable, nested };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(dynamic); i += 1)
    {
        const CheckAction size = { .op = CHECK_OP_SIZEOF, .type = dynamic[i] };
        const CheckActionResult unsupported = check_test_action(test, table, size);
        check_test_expect(test, unsupported.diagnostic == CHECK_DIAG_UNSUPPORTED && unsupported.value.constant_kind == CHECK_CONSTANT_NONE,
                          "ordinary sizeof cannot invent zero for a runtime-sized array chain");
        const CheckAction runtime = {
            .op = CHECK_OP_SIZEOF_VLA,
            .type = dynamic[i],
            .operands = { { .type = BUSTER_TYPE_ULONG, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = 12,
                            .flags = BUSTER_CHECK_ICE | BUSTER_CHECK_SIDE_EFFECT } },
        };
        const CheckActionResult sized = check_test_action(test, table, runtime);
        check_test_expect(test, sized.diagnostic == CHECK_DIAG_NONE && sized.value.type == BUSTER_TYPE_ULONG &&
                               sized.value.constant_kind == CHECK_CONSTANT_NONE && (sized.value.flags & BUSTER_CHECK_ICE) == 0 &&
                               (sized.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0,
                          "explicit VLA size retains runtime evaluation and cannot become an integer constant expression");
        const CheckAction offset = {
            .op = CHECK_OP_ADD,
            .operands = { { .type = type_pointer(table, dynamic[i]), .constant_kind = CHECK_CONSTANT_ADDRESS,
                            .flags = BUSTER_CHECK_STATIC_OBJECT, .object = 133802, .offset = 8 },
                          check_test_integer(BUSTER_TYPE_INT, 1) },
        };
        const CheckActionResult moved = check_test_action(test, table, offset);
        check_test_expect(test, moved.diagnostic == CHECK_DIAG_NONE && moved.value.type == offset.operands[0].type &&
                               moved.value.constant_kind == CHECK_CONSTANT_NONE,
                          "pointer arithmetic with a VLA stride remains runtime instead of folding a zero-byte step");
    }
    const CheckAction saved = { .op = CHECK_OP_VLA_EXTENT, .member = 1 };
    const CheckActionResult extent = check_test_action(test, table, saved);
    check_test_expect(test, extent.diagnostic == CHECK_DIAG_NONE && extent.value.type == BUSTER_TYPE_ULONG &&
                           extent.value.constant_kind == CHECK_CONSTANT_NONE && extent.value.flags == 0,
                      "saved VLA extent reads are nonconstant and do not repeat the original bound effects");
    const CheckAction captured = {
        .op = CHECK_OP_VLA_BOUND,
        .member = 1,
        .operands = { { .type = BUSTER_TYPE_INT, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = 3,
                        .flags = BUSTER_CHECK_ICE | BUSTER_CHECK_SIDE_EFFECT } },
    };
    const CheckActionResult bound = check_test_action(test, table, captured);
    check_test_expect(test, bound.diagnostic == CHECK_DIAG_NONE && bound.value.type == BUSTER_TYPE_ULONG &&
                           bound.value.constant_kind == CHECK_CONSTANT_NONE && (bound.value.flags & BUSTER_CHECK_ICE) == 0 &&
                           (bound.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 && bound.conversions[0].type == BUSTER_TYPE_ULONG,
                      "VLA bound capture converts once while retaining source effects");
    const CheckAction static_size = { .op = CHECK_OP_SIZEOF, .type = pointer_array };
    const CheckActionResult fixed = check_test_action(test, table, static_size);
    check_test_expect(test, fixed.diagnostic == CHECK_DIAG_NONE && fixed.value.constant_kind == CHECK_CONSTANT_INTEGER &&
                           fixed.value.integer == 24 && (fixed.value.flags & BUSTER_CHECK_ICE) != 0,
                      "an array of pointers to VLA still has a fixed pointer-sized layout");
    const CheckAction zero_offset = {
        .op = CHECK_OP_ADD,
        .operands = { { .type = type_pointer(table, zero), .constant_kind = CHECK_CONSTANT_ADDRESS,
                        .flags = BUSTER_CHECK_STATIC_OBJECT, .object = 133803, .offset = 8 },
                      check_test_integer(BUSTER_TYPE_INT, 1) },
        .extensions = BUSTER_CHECK_EXTENSION_GNU,
    };
    const CheckActionResult unchanged = check_test_action(test, table, zero_offset);
    check_test_expect(test, unchanged.diagnostic == CHECK_DIAG_NONE && unchanged.value.constant_kind == CHECK_CONSTANT_ADDRESS &&
                           unchanged.value.object == 133803 && unchanged.value.offset == 8,
                      "GNU fixed zero-length arrays keep their genuine zero-byte pointer stride");
}

static void check_test_void_return_conversion(CheckTest* test, TypeTable* table)
{
    CheckAction action = { .op = CHECK_OP_CONVERT, .type = BUSTER_TYPE_VOID,
                           .operands = { { .type = BUSTER_TYPE_VOID, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_SIDE_EFFECT } } };
    const CheckActionResult strict = check_test_action(test, table, action);
    action.extensions = BUSTER_CHECK_EXTENSION_GNU;
    const CheckActionResult accepted = check_test_action(test, table, action);
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, 1);
    const CheckActionResult integer = check_test_action(test, table, action);
    check_test_expect(test, strict.diagnostic == CHECK_DIAG_NONE && strict.value.type == BUSTER_TYPE_VOID && accepted.diagnostic == CHECK_DIAG_NONE &&
                           accepted.value.type == BUSTER_TYPE_VOID && (accepted.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                           integer.diagnostic == CHECK_DIAG_INCOMPATIBLE,
                      "return conversion accepts exact void expressions in every mode, preserving effects and rejecting integer returns");
}

static __attribute__((noinline)) void check_test_actions_extra(CheckTest* test, TypeTable* table, const char* clang)
{
    check_test_void_return_conversion(test, table);
    check_test_string_literals(test, clang);
    check_test_static_selection(test, table);
    check_test_record_casts(test, table);
    check_test_mixed_void_conditional(test, table);
    check_test_action_histories(test, table);
    check_test_gnu_constant_contexts(test, table);
    check_test_gnu_actions(test, table);
    check_test_pointer_integer_casts(test, table, clang);
    check_test_qualifier_warnings(test, table);
    check_test_vla_actions(test, table);
    check_test_extra_semantic_oracles(test, clang);
}

#endif
