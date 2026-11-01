// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source fixtures: the installed Clang checks acceptance and static layout assertions. The direct tests
// check retained metadata, source ranges, arena exhaustion and scalar/vector parser equivalence independently of
// the surrounding declaration integration. Include after c_checker_test.c's CheckTest and oracle helpers.

#define BUSTER_CHECK_ATTRIBUTE_TEST_IMPLEMENTATION 1

typedef struct CheckAttributeFixture CheckAttributeFixture;
struct CheckAttributeFixture
{
    const char* name;
    const char* source;
    uint32_t count;
    CheckInputAttributeKind last_kind;
    CheckInputAttributeError error;
    const char* error_word;
    TypeId base;
    bool is_typedef;
    uint64_t size;
    uint32_t alignment;
    uint8_t declared_alignment;
    bool packed;
    bool rejected;
    int64_t values[8];
    const char* text;
    CheckInputCallingConvention calling_convention;
    TypeId function_result;
    TypeId function_parameters[4];
    uint32_t function_count;
    uint8_t pointer_parameters;
    bool pointer_result;
    bool ignored;
    bool nonvariadic;
};

static void check_test_attribute_storage(CheckTest* test)
{
    Arena storage = arena_reserve(UINT64_C(1) << 17);
    CheckInputReport reports[8];
    CheckInput input = { .reports = reports, .report_capacity = BUSTER_ARRAY_LENGTH(reports) };
    CheckInputReader reader = { .output = &input, .arena = &storage };
    CheckInputAttributes common = { 0 };
    CheckInputAttributes suffix = { 0 };
    CheckInputAttributes merged = { 0 };
    bool valid = storage.base != 0;
    for (uint32_t i = 0; valid && i < 65; i += 1)
    {
        const CheckInputAttribute attribute = { .first = i, .end = i + 1, .kind = CHECK_ATTRIBUTE_UNUSED };
        valid = check_input_attribute_append(&reader, &common, attribute);
    }
    check_test_expect(test, valid && common.count == 65 && common.capacity == 128, "attribute capacity growth retains every record");
    for (uint32_t i = 0; valid && i < common.count; i += 1)
    {
        check_test_expect(test, common.records[i].first == i && common.records[i].end == i + 1, "attribute growth preserves source order");
    }
    common.packed = true;
    common.alignment_log2 = 2;
    suffix.alignment_log2 = 5;
    suffix.required_alignment_log2 = 4;
    const CheckInputAttribute calling = { .first = 70, .end = 71, .kind = CHECK_ATTRIBUTE_CALLING_CONVENTION, .value = CHECK_CALL_MICROSOFT };
    valid = valid && check_input_attribute_append(&reader, &suffix, calling);
    suffix.calling_convention = CHECK_CALL_MICROSOFT;
    valid = valid && check_input_attribute_merge(&reader, &common, &suffix, &merged);
    check_test_expect(test, valid && merged.count == 66 && merged.packed && merged.alignment_log2 == 5 && merged.required_alignment_log2 == 4 &&
                      merged.calling_convention == CHECK_CALL_MICROSOFT && common.count == 65 && merged.records != common.records,
                      "common and postfix attribute merge retains layout and calling ABI");
    common.calling_convention = CHECK_CALL_SYSV;
    check_test_expect(test, !check_input_attribute_merge(&reader, &common, &suffix, &merged) && merged.error == CHECK_ATTRIBUTE_ERROR_CONFLICT &&
                      merged.error_first == 70 && merged.error_end == 71, "cross-position ABI conflicts retain the suffix's exact range");
    Arena exhausted = { 0 };
    reader.arena = &exhausted;
    CheckInputAttributes unavailable = { 0 };
    check_test_expect(test, !check_input_attribute_append(&reader, &unavailable, calling) && input.resource && unavailable.count == 0 &&
                      unavailable.error == CHECK_ATTRIBUTE_ERROR_RESOURCE && unavailable.error_first == 70,
                      "attribute allocation exhaustion fails explicitly without publishing a record");
    arena_release(&storage);
}

static void check_test_attribute_arguments(CheckTest* test)
{
    enum { ARGUMENTS = 8192, TOKENS_PER_ARGUMENT = 8 };
    Arena tokens = arena_reserve(UINT64_C(1) << 20);
    Arena storage = arena_reserve(UINT64_C(1) << 20);
    Parser parser = { 0 };
    parser.tokens.kinds = arena_push_array(&tokens, ARGUMENTS * TOKENS_PER_ARGUMENT, 1, 1);
    parser.tree.parents = arena_push_array(&tokens, ARGUMENTS * TOKENS_PER_ARGUMENT, sizeof(uint32_t), 4);
    const bool ready = parser.tokens.kinds != 0 && parser.tree.parents != 0 && storage.base != 0;
    check_test_expect(test, ready, "attribute boundary fixture storage");
    if (ready)
    {
        const uint8_t opens[] = { PARSE_KIND_L_PAREN, PARSE_KIND_L_SQUARE, PARSE_KIND_L_BRACE };
        const uint8_t closes[] = { PARSE_KIND_R_PAREN, PARSE_KIND_R_SQUARE, PARSE_KIND_R_BRACE };
        for (uint32_t a = 0; a < ARGUMENTS; a += 1)
        {
            const uint32_t first = a * TOKENS_PER_ARGUMENT;
            const uint8_t kinds[] = { opens[a % 3], PARSE_KIND_L_PAREN, PARSE_KIND_NUMBER, PARSE_KIND_COMMA,
                                      PARSE_KIND_NUMBER, PARSE_KIND_R_PAREN, closes[a % 3], PARSE_KIND_COMMA };
            memcpy(parser.tokens.kinds + first, kinds, sizeof(kinds));
            parser.tree.parents[first] = first + 6;
            parser.tree.parents[first + 1] = first + 5;
        }
        const uint32_t counts[] = { 0, 1, 2, 15, 16, 17, 63, 64, 65, 255, 256, 257, ARGUMENTS };
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(counts); c += 1)
        {
            const uint32_t count = counts[c];
            storage.length = 0;
            CheckInputReport reports[4];
            CheckInput input = { .reports = reports, .report_capacity = BUSTER_ARRAY_LENGTH(reports) };
            CheckInputReader reader = { .output = &input, .parser = &parser, .arena = &storage };
            CheckInputAttribute attribute =
            {
                .argument_end = count != 0 ? count * TOKENS_PER_ARGUMENT - 1 : 0,
                .kind = CHECK_ATTRIBUTE_NONNULL,
                .schema = CHECK_ATTRIBUTE_INTEGER_LIST,
            };
            CheckInputAttributes attributes = { .records = &attribute, .count = 1 };
            const bool valid = check_input_attribute_validate(&reader, &attributes, &attribute, 0, 255);
            check_test_expect(test, valid && attribute.argument_count == count && !input.resource &&
                              storage.length == (count > 1 ? (uint64_t)(count - 1) * sizeof(uint32_t) : 0) &&
                              (attribute.argument_ends != 0) == (count > 1),
                              "unlimited attribute list stores one u32 per separator and allocates nothing for zero/single arguments");
            for (uint32_t a = count; valid && a != 0; a -= 1)
            {
                uint32_t first = UINT32_MAX;
                uint32_t end = UINT32_MAX;
                check_test_expect(test, check_input_attribute_argument(&attribute, a - 1, &first, &end) &&
                                  first == (a - 1) * TOKENS_PER_ARGUMENT && end == first + 7,
                                  "reverse argument access retains exact spans and skips commas in all bracket groups");
            }
            uint32_t first = 0;
            uint32_t end = 0;
            check_test_expect(test, !check_input_attribute_argument(&attribute, count, &first, &end) &&
                              !check_input_attribute_argument(&attribute, UINT32_MAX, &first, &end),
                              "out-of-range argument access is bounded");
            if (valid && count > 1)
            {
                CheckInputAttributes copied = { 0 };
                check_test_expect(test, check_input_attribute_copy(&reader, &attributes, &copied) && copied.records != &attribute &&
                                  copied.records[0].argument_ends == attribute.argument_ends,
                                  "attribute copies share immutable arena-owned boundaries");
                check_test_expect(test, !check_input_attribute_argument_value(&reader, &attributes, 0, count - 1, 0) &&
                                  attributes.error == CHECK_ATTRIBUTE_ERROR_INTEGER && attributes.error_first == (count - 1) * TOKENS_PER_ARGUMENT &&
                                  attributes.error_end == count * TOKENS_PER_ARGUMENT - 1,
                                  "late invalid numeric argument preserves its exact full grouped range");
                check_input_attribute_argument_value(&reader, &attributes, 0, 0, 0);
                check_test_expect(test, attributes.error_first == (count - 1) * TOKENS_PER_ARGUMENT,
                                  "later attribute failures preserve the first diagnostic");
            }
        }
        storage.length = 0;
        storage.capacity = sizeof(uint32_t) - 1;
        CheckInputReport report;
        CheckInput input = { .reports = &report, .report_capacity = 1 };
        CheckInputReader reader = { .output = &input, .parser = &parser, .arena = &storage };
        CheckInputAttribute attribute = { .argument_end = 15, .end = 16, .schema = CHECK_ATTRIBUTE_INTEGER_LIST };
        CheckInputAttributes attributes = { 0 };
        check_test_expect(test, !check_input_attribute_validate(&reader, &attributes, &attribute, 0, 255) && input.resource &&
                          attributes.error == CHECK_ATTRIBUTE_ERROR_RESOURCE && attributes.error_first == 0 && attributes.error_end == 16 &&
                          attributes.count == 0 && storage.length == 0,
                          "boundary table exhaustion reports the original attribute range without publishing a record");
        storage.capacity = UINT64_C(1) << 20;
    }
    arena_release(&storage);
    arena_release(&tokens);
}

static void check_test_attribute_abis(CheckTest* test)
{
    CheckTestTypes types = { 0 };
    Arena scratch = arena_reserve(65536);
    CheckInputReport reports[8];
    CheckInput input = { .reports = reports, .report_capacity = BUSTER_ARRAY_LENGTH(reports) };
    CheckInputReader reader = { .output = &input, .types = &types.table, .scratch = &scratch };
    const bool ready = check_test_types_open(&types) && scratch.base != 0;
    check_test_expect(test, ready, "attribute ABI type and scratch storage initializes");
    if (ready)
    {
        const CheckInputCallingConvention conventions[] = {
            CHECK_CALL_SYSV, CHECK_CALL_MICROSOFT, CHECK_CALL_VECTOR, CHECK_CALL_REG,
            CHECK_CALL_PRESERVE_MOST, CHECK_CALL_PRESERVE_ALL, CHECK_CALL_PRESERVE_NONE,
        };
        const TypeId integer = BUSTER_TYPE_INT;
        const TypeId function = type_function(&types.table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId pointer = type_qualify(&types.table, type_pointer(&types.table, function), BUSTER_TYPE_CONST);
        const TypeId array = type_array(&types.table, pointer, 7);
        const TypeId aligned = type_aligned(&types.table, array, 5);
        const TypeId wrapped = type_qualify(&types.table, type_pointer(&types.table, aligned), BUSTER_TYPE_VOLATILE);
        uint8_t* prefix = arena_push(&scratch, 31, 1);
        check_test_expect(test, prefix != 0, "ABI wrapper scratch prefix allocates");
        if (prefix != 0)
        {
            memset(prefix, 0xa5, 31);
        }
        for (uint32_t abi = 0; abi < BUSTER_ARRAY_LENGTH(conventions); abi += 1)
        {
            CheckInputAttribute record = { .first = 70, .end = 74, .kind = CHECK_ATTRIBUTE_CALLING_CONVENTION, .value = conventions[abi] };
            CheckInputAttributes attributes = { .records = &record, .count = 1, .calling_convention = conventions[abi] };
            const TypeId changed = type_function_abi(&types.table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE, (TypeFunctionABI)abi);
            const TypeId changed_pointer = type_qualify(&types.table, type_pointer(&types.table, changed), BUSTER_TYPE_CONST);
            const TypeId changed_array = type_array(&types.table, changed_pointer, 7);
            const TypeId changed_aligned = type_aligned(&types.table, changed_array, 5);
            const TypeId expected = type_qualify(&types.table, type_pointer(&types.table, changed_aligned), BUSTER_TYPE_VOLATILE);
            const TypeId result = check_input_attribute_abi(&reader, &attributes, wrapped);
            check_test_expect(test, result == expected && attributes.error == CHECK_ATTRIBUTE_ERROR_NONE && scratch.length == 31 &&
                                   prefix != 0 && prefix[30] == 0xa5,
                              "explicit ABI application retains all pointer qualifiers, array extent and typedef alignment");
            check_test_expect(test, check_input_attribute_abi(&reader, &attributes, result) == expected && scratch.length == 31,
                              "reapplying the same explicit ABI preserves the exact wrapped type");
            for (uint8_t origin = 0; origin <= TYPE_FUNCTION_ABI_COUNT; origin += 1)
            {
                input.report_count = 0;
                attributes.error = CHECK_ATTRIBUTE_ERROR_NONE;
                const uint32_t type_count = types.table.types.count;
                const uint8_t actual = check_input_attribute_abi_origin(&reader, &attributes, origin);
                const bool matches = origin == 0 || origin == abi + 1;
                check_test_expect(test, actual == (matches ? abi + 1 : origin) &&
                                       attributes.error == (matches ? CHECK_ATTRIBUTE_ERROR_NONE : CHECK_ATTRIBUTE_ERROR_CONFLICT) &&
                                       input.report_count == (matches ? 0 : 1) &&
                                       (matches || (attributes.error_first == 70 && attributes.error_end == 74 && reports[0].token == 70)) &&
                                       types.table.types.count == type_count && scratch.length == 31,
                                  "spelling ABI provenance distinguishes explicit SysV from implicit default without changing canonical types");
            }
        }
        CheckInputAttribute record = { .first = 80, .end = 84, .kind = CHECK_ATTRIBUTE_CALLING_CONVENTION, .value = CHECK_CALL_MICROSOFT };
        CheckInputAttributes attributes = { .records = &record, .count = 1, .calling_convention = CHECK_CALL_MICROSOFT };
        const uint64_t capacity = scratch.capacity;
        uint32_t full = 0;
        uint32_t complete = 0;
        for (uint64_t room = 0; room <= 20; room += 1)
        {
            input.report_count = 0;
            input.resource = false;
            attributes.error = CHECK_ATTRIBUTE_ERROR_NONE;
            scratch.capacity = 31 + room;
            const TypeId result = check_input_attribute_abi(&reader, &attributes, wrapped);
            full += input.resource;
            complete += result != 0;
            check_test_expect(test, (result != 0 || (input.resource && attributes.error == CHECK_ATTRIBUTE_ERROR_RESOURCE &&
                                                    attributes.error_first == 80 && attributes.error_end == 84)) && scratch.length == 31 &&
                                   prefix != 0 && prefix[30] == 0xa5,
                              "each bounded wrapper scratch capacity applies the complete ABI or reports exact-range exhaustion and rolls back");
        }
        check_test_expect(test, full != 0 && complete != 0, "ABI wrapper scratch controls exercise both exhaustion and success");
        scratch.capacity = capacity;
        input.report_count = 0;
        input.resource = false;
        attributes.error = CHECK_ATTRIBUTE_ERROR_NONE;
        check_test_expect(test, check_input_attribute_abi(&reader, &attributes, integer) == integer && record.ignored && input.report_count == 0,
                          "calling convention on a scalar remains deliberately ignored metadata");
        attributes.calling_convention = CHECK_CALL_IGNORED_X86_32;
        check_test_expect(test, check_input_attribute_abi_origin(&reader, &attributes, TYPE_FUNCTION_ABI_MICROSOFT + 1) == TYPE_FUNCTION_ABI_MICROSOFT + 1 &&
                               check_input_attribute_abi(&reader, &attributes, wrapped) == wrapped,
                          "ignored x86-32 convention does not replace a retained ABI or its origin");
        check_test_expect(test, check_input_attribute_abi_origin(&reader, 0, TYPE_FUNCTION_ABI_SYSV + 1) == TYPE_FUNCTION_ABI_SYSV + 1 &&
                               check_input_attribute_abi(&reader, 0, wrapped) == wrapped,
                          "absent attributes preserve explicit-default provenance and the full type");
    }
    check_test_types_close(&types);
    arena_release(&scratch);
}

static void check_test_attribute_fixture(CheckTest* test, const CheckAttributeFixture* fixture)
{
    for (uint32_t variant = 0; variant < 2; variant += 1)
    {
        InternTable identifiers = { 0 };
        Preprocessor pp = { 0 };
        Parser parser = { 0 };
        CheckTestTypes types = { 0 };
        Arena storage = arena_reserve(UINT64_C(1) << 20);
        Arena scratch = arena_reserve(UINT64_C(1) << 20);
        const PPOptions options = { .language = { .standard = 2023, .gnu = true }, .no_standard_includes = true };
        const ParseOptions parse_options = { .scalar = variant == 0 };
        bool ready = storage.base != 0 && scratch.base != 0 && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                     parse_initialize(&parser, &parse_options, options.language) && check_test_types_open(&types);
        ready = ready && pp_run_source(&pp, "attribute-test.c", (const uint8_t*)fixture->source, strlen(fixture->source)) && parse_translation_unit(&parser, &pp);
        CheckInputReport reports[16];
        CheckInput input = { .reports = reports, .report_capacity = BUSTER_ARRAY_LENGTH(reports) };
        CheckInputReader reader =
        {
            .output = &input,
            .parser = &parser,
            .pp = &pp,
            .types = &types.table,
            .arena = &storage,
            .scratch = &scratch,
        };
        reader.ignored = ready ? arena_push_array(&storage, parser.tokens.count + 1, 1, 1) : 0;
        ready &= reader.ignored != 0;
        if (ready)
        {
            memset(reader.ignored, 0, parser.tokens.count + 1);
        }
        char description[256];
        snprintf(description, sizeof(description), "%s: attribute fixture setup", fixture->name);
        check_test_expect(test, ready, description);
        CheckInputAttributes attributes = { 0 };
        bool valid = ready;
        for (uint32_t token = 0; valid && token < parser.tokens.count;)
        {
            if (check_input_attribute_starts(&parser, token, parser.tokens.count))
            {
                const uint32_t first = token;
                valid = check_input_attributes(&reader, &token, parser.tokens.count, &attributes);
                if (valid)
                {
                    check_test_expect(test, token > first, "attribute scanner must make progress");
                    for (uint32_t i = first; i < token; i += 1)
                    {
                        check_test_expect(test, reader.ignored[i], "attribute tokens must not leak into expression checking");
                    }
                }
            }
            else
            {
                const uint32_t before = token;
                valid = check_input_attributes(&reader, &token, parser.tokens.count, &attributes);
                check_test_expect(test, token == before, "ordinary tokens must not be consumed as attributes");
                token += 1;
            }
        }
        uint32_t numeric = 0;
        for (uint32_t i = 0; valid && i < attributes.count; i += 1)
        {
            CheckInputAttribute* attribute = &attributes.records[i];
            const bool integers = attribute->schema == CHECK_ATTRIBUTE_INTEGER || attribute->schema == CHECK_ATTRIBUTE_INTEGER_LIST ||
                                  attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS;
            for (uint32_t argument = 0; valid && integers && argument < attribute->argument_count; argument += 1)
            {
                if (!(argument == 0 && attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS))
                {
                    uint32_t first = 0;
                    uint32_t end = 0;
                    check_test_expect(test, check_input_attribute_argument(attribute, argument, &first, &end) && first < end,
                                      "numeric attribute argument retains its exact source range");
                    valid = numeric < BUSTER_ARRAY_LENGTH(fixture->values) && check_input_attribute_argument_value(&reader, &attributes, i, argument, fixture->values[numeric]);
                    numeric += 1;
                }
            }
        }
        TypeId result = fixture->base;
        if (valid && fixture->function_result != 0)
        {
            TypeId parameters[4];
            for (uint32_t i = 0; i < fixture->function_count; i += 1)
            {
                parameters[i] = (fixture->pointer_parameters & (1u << i)) != 0 ? type_pointer(&types.table, fixture->function_parameters[i]) : fixture->function_parameters[i];
            }
            const TypeId return_type = fixture->pointer_result ? type_pointer(&types.table, fixture->function_result) : fixture->function_result;
            const uint8_t flags = BUSTER_TYPE_FLAG_PROTOTYPE | (fixture->nonvariadic ? 0 : BUSTER_TYPE_FLAG_VARIADIC);
            result = type_function(&types.table, return_type, parameters, fixture->function_count, flags);
            valid = result != 0;
        }
        if (valid && result != 0)
        {
            result = check_input_attribute_type(&reader, &attributes, result, fixture->is_typedef);
            valid = result != 0 && check_input_attribute_function(&reader, &attributes, result);
        }
        snprintf(description, sizeof(description), "%s: exact attribute diagnosis", fixture->name);
        check_test_expect(test, ready && attributes.error == fixture->error && valid == (fixture->error == CHECK_ATTRIBUTE_ERROR_NONE), description);
        if (ready && fixture->error_word != 0)
        {
            const uint8_t* bytes = 0;
            uint32_t length = 0;
            bool spelled = attributes.error_first < parser.tokens.count;
            uint8_t buffer[64];
            if (spelled)
            {
                // The authored offending names/numbers/punctuators above fit this buffer. Identifier spellings
                // are returned from the intern table; pp_spelling writes at most 24 bytes for generated integers.
                const uint32_t source = parser.tokens.sources[attributes.error_first];
                length = pp_spelling(&pp, pp.output.kinds[source], pp.output.values[source], pp.output.locations[source], buffer, &bytes);
                spelled = length < sizeof(buffer);
            }
            snprintf(description, sizeof(description), "%s: attribute diagnosis points at the offending token", fixture->name);
            check_test_expect(test, spelled && length == strlen(fixture->error_word) && memcmp(bytes, fixture->error_word, length) == 0, description);
            check_test_expect(test, attributes.error_first < attributes.error_end && attributes.error_end <= parser.tokens.count,
                              "attribute diagnosis has a bounded nonempty source range");
        }
        if (valid)
        {
            snprintf(description, sizeof(description), "%s: retained attribute metadata", fixture->name);
            check_test_expect(test, attributes.count == fixture->count && (attributes.count == 0 || attributes.records[attributes.count - 1].kind == fixture->last_kind) &&
                              attributes.packed == fixture->packed && attributes.alignment_log2 == fixture->declared_alignment &&
                              attributes.calling_convention == fixture->calling_convention &&
                              (attributes.count == 0 || attributes.records[attributes.count - 1].ignored == fixture->ignored), description);
            if (fixture->size != 0)
            {
                check_test_expect(test, result != 0 && type_size(&types.table, result) == fixture->size && type_alignment(&types.table, result) == fixture->alignment,
                                  "attribute type agrees with independent Clang layout assertions");
            }
            if (fixture->text != 0)
            {
                check_test_expect(test, attributes.count != 0 && check_input_attribute_equal(attributes.records[attributes.count - 1].text, fixture->text),
                                  "linkage attribute retains the decoded payload");
            }
            CheckInputAttributes copied = { 0 };
            check_test_expect(test, check_input_attribute_copy(&reader, &attributes, &copied) && copied.count == attributes.count &&
                              (attributes.count == 0 || copied.records != attributes.records), "declarator attribute copies isolate writable record storage");
        }
        check_test_types_close(&types);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
        arena_release(&scratch);
        arena_release(&storage);
    }
}

// The attributes a declaration adds after its entity's definition: the checker warns at each one the definition doesn't
// carry and marks it ignored, and a later declaration's repeat of one the definition carries warns nowhere. Both paths,
// and the warnings' records, with the definition each one's note names (#888).
static void check_test_attribute_late(CheckTest* test)
{
    static const char source[] =
        "static int f(void) { return 1; }\n"
        "static int f(void) __attribute__((used));\n"
        "static int g(void) __attribute__((used));\n"
        "static int g(void) { return 2; }\n"
        "static int g(void) __attribute__((used));\n"
        "static int h(void) { return 3; }\n"
        "static int h(void) __attribute__((gnu_inline));\n";
    for (uint32_t path = 0; path < 2; path += 1)
    {
        InternTable identifiers = { 0 };
        Preprocessor pp = { 0 };
        Parser parser = { 0 };
        CheckTestTypes types = { 0 };
        Arena storage = arena_reserve(UINT64_C(1) << 22);
        Arena scratch = arena_reserve(UINT64_C(1) << 22);
        const PPOptions options = { .language = { .standard = 2023, .gnu = true }, .no_standard_includes = true };
        const ParseOptions parse_options = { .scalar = path == 0 };
        bool ready = storage.base != 0 && scratch.base != 0 && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                     parse_initialize(&parser, &parse_options, options.language) && check_test_types_open(&types);
        ready = ready && pp_run_source(&pp, "attribute-late.c", (const uint8_t*)source, sizeof(source) - 1) && parse_translation_unit(&parser, &pp) &&
                parser.errors == 0;
        CheckInput input = { 0 };
        const bool checked = ready && check_input(&input, &parser, &pp, &types.table, &storage, &scratch);
        // f's used and h's gnu_inline are new to their definitions; g's second used repeats what its first declaration
        // gave the definition, and h's message is clang's gnu_inline one, which carries no note.
        uint32_t ignored = 0;
        uint32_t kept = 0;
        for (uint32_t token = 0; checked && token < input.token_count; token += 1)
        {
            const CheckInputAttributes* attributes = input.attributes[token];
            for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + i;
                if (check_input_attribute_late_kind(attribute))
                {
                    ignored += attribute->ignored ? 1 : 0;
                    kept += attribute->ignored ? 0 : 1;
                }
            }
        }
        check_test_expect(test, checked && input.report_count == 0, "late attribute fixture checks");
        check_test_expect(test, input.attribute_warning_count == 2 && input.attribute_warnings != NULL && !input.attribute_warnings[0].missing_inline &&
                               input.attribute_warnings[1].missing_inline,
                          "late attributes warn at each new one, and a gnu_inline without inline with its own message");
        check_test_expect(test, ignored == 3 && kept == 1, "a late attribute is ignored, and one the definition carries is kept");
        check_test_expect(test, input.attribute_warnings == NULL || (input.attribute_warnings[0].token < input.attribute_warnings[1].token &&
                               input.attribute_warnings[0].definition < input.attribute_warnings[1].definition),
                          "late attributes' warnings and their definitions are in source order");
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&storage);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
}

static __attribute__((noinline)) void check_test_attributes(CheckTest* test, const char* clang)
{
    check_test_attribute_storage(test);
    check_test_attribute_arguments(test);
    check_test_attribute_abis(test);
    check_test_attribute_late(test);
    const CheckAttributeFixture fixtures[] =
    {
        {
            .name = "fallthrough object declaration subject",
            .source = "int x __attribute__((fallthrough));\n",
            .error = CHECK_ATTRIBUTE_ERROR_SUBJECT,
            .error_word = "fallthrough",
            .base = BUSTER_TYPE_INT,
            .rejected = true,
        },
        {
            .name = "fallthrough function declaration subject",
            .source = "int f(void) __attribute__((fallthrough));\n",
            .error = CHECK_ATTRIBUTE_ERROR_SUBJECT,
            .error_word = "fallthrough",
            .function_result = BUSTER_TYPE_INT,
            .nonvariadic = true,
            .rejected = true,
        },
        {
            .name = "fallthrough null statement metadata",
            .source = "int f(int x){switch(x){case 0:__attribute__((fallthrough));case 1:return x;default:return 0;}}\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_FALLTHROUGH,
        },
        {
            .name = "empty GNU attributes",
            .source = "int x __attribute__((,,));\n",
        },
        {
            .name = "glibc advisory function attributes",
            .source = "extern int f(const char*, ...) __attribute__((__nothrow__, __leaf__, __format__(__printf__,1,2), __nonnull__(1), __warn_unused_result__));\n",
            .count = 5,
            .last_kind = CHECK_ATTRIBUTE_WARN_UNUSED_RESULT,
            .values = { 1, 2, 1 },
        },
        {
            .name = "allocation contract metadata",
            .source = "extern void *f(unsigned long,unsigned long) __attribute__((malloc, alloc_size(1,2), alloc_align(1), returns_nonnull));\n",
            .count = 4,
            .last_kind = CHECK_ATTRIBUTE_RETURNS_NONNULL,
            .values = { 1, 2, 1 },
        },
        {
            .name = "packed aligned record metadata",
            .source = "struct __attribute__((packed, aligned(2))) S { char c; int x; }; _Static_assert(sizeof(struct S)==6,\"layout\");\n",
            .count = 2,
            .last_kind = CHECK_ATTRIBUTE_ALIGNED,
            .declared_alignment = 2,
            .packed = true,
            .values = { 2 },
        },
        {
            .name = "typedef lowered alignment",
            .source = "typedef int I __attribute__((aligned(1))); _Static_assert(sizeof(I)==4 && _Alignof(I)==1,\"alignment\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_ALIGNED,
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .size = 4,
            .alignment = 1,
            .declared_alignment = 1,
            .values = { 1 },
        },
        {
            .name = "default maximum alignment",
            .source = "typedef int I __attribute__((aligned)); _Static_assert(_Alignof(I)==16,\"alignment\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_ALIGNED,
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .size = 4,
            .alignment = 16,
            .declared_alignment = 5,
        },
        {
            .name = "object alignment is not type alignment",
            .source = "int x __attribute__((aligned(32))); _Static_assert(__alignof__(x)==32 && _Alignof(int)==4,\"object\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_ALIGNED,
            .base = BUSTER_TYPE_INT,
            .size = 4,
            .alignment = 4,
            .declared_alignment = 6,
            .values = { 32 },
        },
        {
            .name = "standard required alignment",
            .source = "_Alignas(16) int x;\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_ALIGNAS,
            .base = BUSTER_TYPE_INT,
            .declared_alignment = 5,
            .values = { 16 },
        },
        {
            .name = "standard zero alignment",
            .source = "alignas(0) int x;\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_ALIGNAS,
            .base = BUSTER_TYPE_INT,
        },
        {
            .name = "GNU zero alignment is invalid",
            .source = "int x __attribute__((aligned(0)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ALIGNMENT,
            .error_word = "0",
            .base = BUSTER_TYPE_INT,
            .rejected = true,
        },
        {
            .name = "GNU vector type",
            .source = "typedef int V __attribute__((vector_size(16))); _Static_assert(sizeof(V)==16 && _Alignof(V)==16,\"vector\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_VECTOR_SIZE,
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .size = 16,
            .alignment = 16,
            .values = { 16 },
        },
        {
            .name = "Clang vector type",
            .source = "typedef float V __attribute__((ext_vector_type(3))); _Static_assert(sizeof(V)==16 && _Alignof(V)==16,\"vector\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_EXT_VECTOR_TYPE,
            .base = BUSTER_TYPE_FLOAT,
            .is_typedef = true,
            .size = 16,
            .alignment = 16,
            .values = { 3 },
        },
        {
            .name = "aligned vector typedef",
            .source = "typedef int V __attribute__((vector_size(16), aligned(1))); _Static_assert(sizeof(V)==16 && _Alignof(V)==1,\"vector\");\n",
            .count = 2,
            .last_kind = CHECK_ATTRIBUTE_ALIGNED,
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .size = 16,
            .alignment = 1,
            .declared_alignment = 1,
            .values = { 16, 1 },
        },
        {
            .name = "glibc integer machine mode",
            .source = "typedef unsigned int I __attribute__((__mode__(__DI__))); _Static_assert(sizeof(I)==8 && _Alignof(I)==8,\"mode\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_MODE,
            .base = BUSTER_TYPE_UINT,
            .is_typedef = true,
            .size = 8,
            .alignment = 8,
        },
        {
            .name = "byte integer machine mode",
            .source = "typedef int I __attribute__((mode(byte))); _Static_assert(sizeof(I)==1 && _Alignof(I)==1,\"mode\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_MODE,
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .size = 1,
            .alignment = 1,
        },
        {
            .name = "floating machine mode",
            .source = "typedef float F __attribute__((mode(DF))); _Static_assert(sizeof(F)==8 && _Alignof(F)==8,\"mode\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_MODE,
            .base = BUSTER_TYPE_FLOAT,
            .is_typedef = true,
            .size = 8,
            .alignment = 8,
        },
        {
            .name = "glibc complex quad machine mode",
            .source = "typedef _Complex float C __attribute__((__mode__(__TC__))); _Static_assert(sizeof(C)==32 && _Alignof(C)==16,\"complex\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_MODE,
            .base = BUSTER_TYPE_COMPLEX_FLOAT,
            .is_typedef = true,
            .size = 32,
            .alignment = 16,
        },
        {
            .name = "complex double machine mode",
            .source = "typedef _Complex float C __attribute__((mode(DC))); _Static_assert(sizeof(C)==16 && _Alignof(C)==8,\"complex\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_MODE,
            .base = BUSTER_TYPE_COMPLEX_FLOAT,
            .is_typedef = true,
            .size = 16,
            .alignment = 8,
        },
        {
            .name = "complex base with scalar machine mode",
            .source = "typedef _Complex float C __attribute__((mode(DF))); _Static_assert(sizeof(C)==8 && _Alignof(C)==8,\"scalar\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_MODE,
            .base = BUSTER_TYPE_COMPLEX_FLOAT,
            .is_typedef = true,
            .size = 8,
            .alignment = 8,
        },
        {
            .name = "asm symbol concatenation and escape",
            .source = "extern int f(void) __asm__(\"sym\" \"bol_\\x78\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_ASM_LABEL,
            .text = "symbol_x",
        },
        {
            .name = "GNU integer vector machine mode",
            .source = "typedef int V __attribute__((mode(V4SI))); _Static_assert(sizeof(V)==16 && _Alignof(V)==16,\"vector mode\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_MODE,
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .size = 16,
            .alignment = 16,
        },
        {
            .name = "GNU floating vector machine mode",
            .source = "typedef double V __attribute__((mode(V2DF))); _Static_assert(sizeof(V)==16 && _Alignof(V)==16,\"vector mode\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_MODE,
            .base = BUSTER_TYPE_DOUBLE,
            .is_typedef = true,
            .size = 16,
            .alignment = 16,
        },
        {
            .name = "GNU vector machine mode requires power of two",
            .source = "typedef int V __attribute__((mode(V3SI)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_MODE,
            .error_word = "mode",
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .rejected = true,
        },
        {
            .name = "weak hidden symbol",
            .source = "extern int x __attribute__((weak, visibility(\"hidden\")));\n",
            .count = 2,
            .last_kind = CHECK_ATTRIBUTE_VISIBILITY,
            .text = "hidden",
        },
        {
            .name = "unrecognized visibility is deliberately ignored",
            .source = "extern int x __attribute__((visibility(\"invented\")));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_VISIBILITY,
            .text = "invented",
            .ignored = true,
        },
        {
            .name = "symbol alias metadata",
            .source = "int target; extern int x __attribute__((alias(\"target\")));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_ALIAS,
            .text = "target",
        },
        {
            .name = "WebAssembly export spelling retained without native symbol renaming",
            .source = "__attribute__((export_name(\"entry_point\"))) void entry(void) {}\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_EXPORT_NAME,
            .text = "entry_point",
            .ignored = true,
        },
        {
            .name = "scoped WebAssembly export spelling retained on native output",
            .source = "[[clang::export_name(\"entry_point\")]] void entry(void) {}\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_EXPORT_NAME,
            .text = "entry_point",
            .ignored = true,
        },
        {
            .name = "section metadata",
            .source = "int x __attribute__((section(\".data.private\"), used, retain));\n",
            .count = 3,
            .last_kind = CHECK_ATTRIBUTE_RETAIN,
        },
        {
            .name = "TLS model metadata",
            .source = "__thread int x __attribute__((tls_model(\"initial-exec\")));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_TLS_MODEL,
            .text = "initial-exec",
        },
        {
            .name = "standard attribute list",
            .source = "[[maybe_unused, deprecated(\"old\")]] int x;\n",
            .count = 2,
            .last_kind = CHECK_ATTRIBUTE_DEPRECATED,
            .text = "old",
        },
        {
            .name = "standard GNU spelling",
            .source = "typedef int I [[gnu::aligned(16)]]; _Static_assert(_Alignof(I)==16,\"alignment\");\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_ALIGNED,
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .size = 4,
            .alignment = 16,
            .declared_alignment = 5,
            .values = { 16 },
        },
        {
            .name = "noreturn keyword",
            .source = "_Noreturn void f(void);\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_NORETURN,
        },
        {
            .name = "intrinsic nodebug metadata",
            .source = "static inline void f(void) __attribute__((__nodebug__));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_NODEBUG,
        },
        {
            .name = "nodebug rejects arguments",
            .source = "void f(void) __attribute__((nodebug(1)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "nodebug",
            .rejected = true,
        },
        {
            .name = "overloadable prefix metadata",
            .source = "__attribute__((overloadable)) int f(int);\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_OVERLOADABLE,
        },
        {
            .name = "overloadable suffix metadata",
            .source = "int f(int) __attribute__((__overloadable__));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_OVERLOADABLE,
        },
        {
            .name = "overloadable rejects arguments",
            .source = "int f(int) __attribute__((overloadable(1)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "overloadable",
            .rejected = true,
        },
        {
            .name = "scoped overloadable metadata",
            .source = "[[clang::overloadable]] int f(int);\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_OVERLOADABLE,
        },
        {
            .name = "cdecl keyword",
            .source = "void __cdecl f(void);\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_CALLING_CONVENTION,
            .calling_convention = CHECK_CALL_SYSV,
        },
        {
            .name = "Microsoft ABI metadata",
            .source = "void f(void) __attribute__((ms_abi));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_CALLING_CONVENTION,
            .calling_convention = CHECK_CALL_MICROSOFT,
        },
        {
            .name = "nullability qualifier metadata",
            .source = "int *_Nullable p;\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_NULLABILITY,
        },
        {
            .name = "constructor priority metadata",
            .source = "void f(void) __attribute__((constructor(200)));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_CONSTRUCTOR,
            .values = { 200 },
        },
        {
            .name = "constructor full unsigned priority",
            .source = "void f(void) __attribute__((constructor(-1)));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_CONSTRUCTOR,
            .values = { -1 },
        },
        {
            .name = "register argument metadata",
            .source = "void f(void) __attribute__((regparm(3)));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_REGPARM,
            .values = { 3 },
        },
        {
            .name = "format parameters checked against function type",
            .source = "int f(const char*,...) __attribute__((format(printf,1,2)));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_FORMAT,
            .values = { 1, 2 },
            .function_result = BUSTER_TYPE_INT,
            .function_parameters = { BUSTER_TYPE_CHAR },
            .function_count = 1,
            .pointer_parameters = 1,
        },
        {
            .name = "format non-string parameter rejected",
            .source = "int f(int,...) __attribute__((format(printf,1,2)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_PARAMETER,
            .error_word = "1",
            .rejected = true,
            .values = { 1, 2 },
            .function_result = BUSTER_TYPE_INT,
            .function_parameters = { BUSTER_TYPE_INT },
            .function_count = 1,
        },
        {
            .name = "nonnull parameter index rejected",
            .source = "int f(char*) __attribute__((nonnull(2)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_PARAMETER,
            .error_word = "2",
            .rejected = true,
            .values = { 2 },
            .function_result = BUSTER_TYPE_INT,
            .function_parameters = { BUSTER_TYPE_CHAR },
            .function_count = 1,
            .pointer_parameters = 1,
            .nonvariadic = true,
        },
        {
            .name = "nonnull variadic positions retained",
            .source = "int f(char*,...) __attribute__((nonnull(2,99)));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_NONNULL,
            .values = { 2, 99 },
            .function_result = BUSTER_TYPE_INT,
            .function_parameters = { BUSTER_TYPE_CHAR },
            .function_count = 1,
            .pointer_parameters = 1,
        },
        {
            .name = "nonnull non-pointer warning disposition",
            .source = "int f(int,...) __attribute__((nonnull(1)));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_NONNULL,
            .values = { 1 },
            .function_result = BUSTER_TYPE_INT,
            .function_parameters = { BUSTER_TYPE_INT },
            .function_count = 1,
            .ignored = true,
        },
        {
            .name = "allocation integer parameter checked",
            .source = "void *f(int,...) __attribute__((alloc_size(1)));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_ALLOC_SIZE,
            .values = { 1 },
            .function_result = BUSTER_TYPE_VOID,
            .function_parameters = { BUSTER_TYPE_INT },
            .function_count = 1,
            .pointer_result = true,
        },
        {
            .name = "allocation floating parameter rejected",
            .source = "void *f(float,...) __attribute__((alloc_size(1)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_PARAMETER,
            .error_word = "1",
            .rejected = true,
            .values = { 1 },
            .function_result = BUSTER_TYPE_VOID,
            .function_parameters = { BUSTER_TYPE_FLOAT },
            .function_count = 1,
            .pointer_result = true,
        },
        {
            .name = "allocation non-pointer return warning disposition",
            .source = "int f(int,...) __attribute__((alloc_size(1)));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_ALLOC_SIZE,
            .values = { 1 },
            .function_result = BUSTER_TYPE_INT,
            .function_parameters = { BUSTER_TYPE_INT },
            .function_count = 1,
            .ignored = true,
        },
        {
            .name = "format_arg return string checked",
            .source = "char *f(char*,...) __attribute__((format_arg(1)));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_FORMAT_ARG,
            .values = { 1 },
            .function_result = BUSTER_TYPE_CHAR,
            .function_parameters = { BUSTER_TYPE_CHAR },
            .function_count = 1,
            .pointer_result = true,
            .pointer_parameters = 1,
        },
        {
            .name = "format_arg non-string return rejected",
            .source = "int f(char*,...) __attribute__((format_arg(1)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_PARAMETER,
            .error_word = "1",
            .rejected = true,
            .values = { 1 },
            .function_result = BUSTER_TYPE_INT,
            .function_parameters = { BUSTER_TYPE_CHAR },
            .function_count = 1,
            .pointer_parameters = 1,
        },
        {
            .name = "malformed packed argument",
            .source = "struct __attribute__((packed(1))) S { int x; };\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "packed",
            .rejected = true,
        },
        {
            .name = "invalid aligned argument count",
            .source = "int x __attribute__((aligned(2,4)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "aligned",
            .rejected = true,
        },
        {
            .name = "invalid alignment value",
            .source = "int x __attribute__((aligned(3)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ALIGNMENT,
            .error_word = "3",
            .rejected = true,
            .values = { 3 },
        },
        {
            .name = "negative alignment value",
            .source = "int x __attribute__((aligned(-8)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ALIGNMENT,
            .error_word = "-",
            .rejected = true,
            .values = { -8 },
        },
        {
            .name = "_Alignas cannot reduce alignment",
            .source = "_Alignas(1) int x;\n",
            .error = CHECK_ATTRIBUTE_ERROR_ALIGNMENT,
            .error_word = "_Alignas",
            .base = BUSTER_TYPE_INT,
            .rejected = true,
            .values = { 1 },
        },
        {
            .name = "_Alignas cannot annotate typedef",
            .source = "_Alignas(16) typedef int I;\n",
            .error = CHECK_ATTRIBUTE_ERROR_SUBJECT,
            .error_word = "_Alignas",
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .rejected = true,
            .values = { 16 },
        },
        {
            .name = "invalid vector element count",
            .source = "typedef int V __attribute__((vector_size(3)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_VECTOR,
            .error_word = "vector_size",
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .rejected = true,
            .values = { 3 },
        },
        {
            .name = "zero vector element count",
            .source = "typedef int V __attribute__((vector_size(0)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_VECTOR,
            .error_word = "0",
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .rejected = true,
        },
        {
            .name = "invalid machine mode spelling",
            .source = "typedef int I __attribute__((mode(NO_SUCH_MODE)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_MODE,
            .error_word = "mode",
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .rejected = true,
        },
        {
            .name = "wrong machine mode class",
            .source = "typedef float I __attribute__((mode(SI)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_MODE,
            .error_word = "mode",
            .base = BUSTER_TYPE_FLOAT,
            .is_typedef = true,
            .rejected = true,
        },
        {
            .name = "invalid string attribute",
            .source = "int x __attribute__((section(3)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_STRING,
            .error_word = "section",
            .rejected = true,
        },
        {
            .name = "invalid TLS model",
            .source = "__thread int x __attribute__((tls_model(\"invented\")));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "tls_model",
            .rejected = true,
        },
        {
            .name = "conflicting calling conventions",
            .source = "void f(void) __attribute__((ms_abi, sysv_abi));\n",
            .error = CHECK_ATTRIBUTE_ERROR_CONFLICT,
            .error_word = "sysv_abi",
            .rejected = true,
        },
        {
            .name = "invalid regparm count",
            .source = "void f(void) __attribute__((regparm(7)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_INTEGER,
            .error_word = "7",
            .rejected = true,
            .values = { 7 },
        },
        {
            .name = "align_value on a pointer parameter, as clang 23's <movdirintrin.h> has it",
            .source = "void f(void *p __attribute__((align_value(64))), const void *q);\n"
                      "typedef double *aligned_double __attribute__((align_value(1ull << 40)));\n",
            .count = 2,
            .last_kind = CHECK_ATTRIBUTE_ALIGN_VALUE,
            .values = { 64, INT64_C(1) << 40 },
        },
        {
            .name = "align_value of a value other than a power of two",
            .source = "void f(void *p __attribute__((align_value(48))));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ALIGNMENT,
            .error_word = "48",
            .rejected = true,
            .values = { 48 },
        },
        {
            .name = "unknown attribute is explicit",
            .source = "int x __attribute__((invented_semantics(3)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_UNKNOWN,
            .error_word = "invented_semantics",
        },
        {
            .name = "unknown C23 namespace is explicit",
            .source = "[[invented::aligned(16)]] int x;\n",
            .error = CHECK_ATTRIBUTE_ERROR_UNKNOWN,
            .error_word = "aligned",
        },
        {
            .name = "unscoped GNU layout name is not ISO packed",
            .source = "struct [[packed]] S { int x; };\n",
            .error = CHECK_ATTRIBUTE_ERROR_UNKNOWN,
            .error_word = "packed",
        },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-attribute-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[FILE_COUNT][160];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu23" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "attribute Clang oracle could not run");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        char description[192];
        snprintf(description, sizeof(description), "%s: original attribute fixture differs from Clang", fixtures[i].name);
        check_test_expect(test, outcomes[i].rejected == fixtures[i].rejected, description);
        check_test_attribute_fixture(test, &fixtures[i]);
    }
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (directory[0] != 0)
    {
        rmdir(directory);
    }
}

static void check_test_attribute_lists(CheckTest* test, const char* clang)
{
    const struct
    {
        const char* name;
        const char* prefix;
        const char* item;
        const char* suffix;
        uint32_t count;
        bool rejected;
    } fixtures[] =
    {
        { "8192 repeated nonnull indices", "void f(void*) __attribute__((nonnull(", "1", ")));\n", 8192, false },
        { "grouped attribute expressions with brackets", "void f(void*) __attribute__((nonnull(", "((sizeof(int[2]) == 2*sizeof(int)))", ")));\n", 257, false },
        { "long concatenated string attribute list", "void f(void*) __attribute__((no_sanitize(", "\"ad\" \"dress\"", ")));\n", 257, false },
        { "raw constant-reader attribute list", "struct S {int x;}; enum { N=sizeof(struct __attribute__((nonnull(", "1", "))) S) }; _Static_assert(N==sizeof(struct S),\"tag attributes\");\n", 257, false },
        { "late zero nonnull index", "void f(void*) __attribute__((nonnull(", "1", ",0)));\n", 257, true },
        { "late nonnull parameter past prototype", "void f(void*) __attribute__((nonnull(", "1", ",2)));\n", 257, true },
        { "late nonconstant nonnull argument", "int n; void f(void*) __attribute__((nonnull(\n", "1", ",\nn)));\n", 257, true },
        { "late non-string attribute argument", "void f(void*) __attribute__((no_sanitize(", "\"address\"", ",3)));\n", 257, true },
        { "attribute trailing comma", "void f(void*) __attribute__((nonnull(", "1", ",)));\n", 257, true },
        { "attribute empty middle argument", "void f(void*) __attribute__((nonnull(", "1", ",,1)));\n", 257, true },
        { "attribute leading comma", "void f(void*) __attribute__((nonnull(,", "1", ")));\n", 257, true },
    };
    char directory[] = "/tmp/buster-attribute-list-XXXXXX";
    const bool directory_ready = mkdtemp(directory) != 0;
    check_test_expect(test, directory_ready, "attribute list oracle directory");
    for (uint32_t i = 0; directory_ready && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[32768];
        uint32_t length = (uint32_t)snprintf(source, sizeof(source), "%s", fixtures[i].prefix);
        for (uint32_t a = 0; a < fixtures[i].count; a += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "%s%s", a != 0 ? "," : "", fixtures[i].item);
        }
        snprintf(source + length, sizeof(source) - length, "%s", fixtures[i].suffix);
        char path[160];
        snprintf(path, sizeof(path), "%s/%u.c", directory, i);
        char* paths[] = { path };
        ParseTestClang outcome;
        const char* arguments[] = { "-std=gnu17" };
        const bool ready = parse_test_write(path, source, strlen(source)) &&
                           parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths, 1, &outcome);
        check_test_expect(test, ready, "long attribute list Clang oracle");
        if (ready)
        {
            check_test_expect(test, outcome.rejected == fixtures[i].rejected, fixtures[i].name);
            const CheckSourceFixture fixture = { .name = fixtures[i].name, .source = source, .rejected = fixtures[i].rejected };
            check_test_source(test, &fixture, path, &outcome);
        }
        unlink(path);
    }
    if (directory_ready)
    {
        rmdir(directory);
    }
}

static __attribute__((noinline)) void check_test_attribute_sources(CheckTest* test, const char* clang)
{
    check_test_attribute_lists(test, clang);
    const CheckSourceFixture fixtures[] =
    {
        {
            "GNU fallthrough null statement",
            "int f(int x){switch(x){case 0:x++;__attribute__((fallthrough));case 1:return x;default:return 0;}}\n",
            false,
            false,
        },
        {
            "standard fallthrough null statement",
            "int f(int x){switch(x){case 0:x++;[[fallthrough]];case 1:return x;default:return 0;}}\n",
            false,
            false,
        },
        {
            "GNU scoped standard fallthrough null statement",
            "int f(int x){switch(x){case 0:x++;[[gnu::fallthrough]];case 1:return x;default:return 0;}}\n",
            false,
            false,
        },
        {
            "fallthrough through a closing compound statement",
            "int f(int x){switch(x){case 0:{x++;__attribute__((fallthrough));}case 1:return x;default:return 0;}}\n",
            false,
            false,
        },
        {
            "fallthrough outside switch rejected",
            "int f(void){\n__attribute__((fallthrough));\nreturn 0;}\n",
            true,
            false,
        },
        {
            "standard fallthrough outside switch rejected",
            "int f(void){\n[[fallthrough]];\nreturn 0;}\n",
            true,
            false,
        },
        {
            "fallthrough placement checking is disabled by the oracle warning profile",
            "int f(int x){switch(x){case 0:\n__attribute__((fallthrough));\nreturn 1;case 1:return x;default:return 0;}}\n",
            false,
            false,
        },
        {
            "outer-switch fallthrough placement is deferred without warning analysis",
            "int f(int x){switch(x){case 0:switch(x){case 0:\n__attribute__((fallthrough));\n}case 1:return x;default:return 0;}}\n",
            false,
            false,
        },
        {
            "end-of-switch fallthrough placement is deferred without warning analysis",
            "int f(int x){switch(x){case 0:\n__attribute__((fallthrough));\n}return x;}\n",
            false,
            false,
        },
        {
            "empty attributed null statements outside switch",
            "int f(int x){__attribute__(());[[]];return x;}\n",
            false,
            false,
        },
        {
            "fallthrough cannot annotate an object declaration",
            "int x __attribute__((fallthrough));\n",
            true,
            false,
        },
        {
            "fallthrough cannot annotate a function declaration",
            "int f(void) __attribute__((fallthrough));\n",
            true,
            false,
        },
        {
            "fallthrough cannot annotate a tag declaration",
            "struct __attribute__((fallthrough)) S { int x; };\n",
            true,
            false,
        },
        {
            "leading GNU attribute declaration",
            "__attribute__((aligned(16))) int x;\n",
            false,
            false,
        },
        {
            "leading standard attribute declaration",
            "[[maybe_unused]] int x;\n",
            false,
            false,
        },
        {
            "leading numeric alignment declaration",
            "_Alignas(16) int x;\n",
            false,
            false,
        },
        {
            "glibc function attribute integration",
            "int f(const char*,...) __attribute__((nothrow,leaf,format(printf,1,2),nonnull(1))); int g(void){return f(\"%d\",3);}\n",
            false,
            false,
        },
        {
            "integer mode type reaches sizeof",
            "typedef unsigned int U __attribute__((mode(DI))); _Static_assert(sizeof(U)==8,\"mode\");\n",
            false,
            false,
        },
        {
            "glibc complex mode reaches sizeof",
            "typedef _Complex float C __attribute__((mode(TC))); _Static_assert(sizeof(C)==32,\"complex\");\n",
            false,
            false,
        },
        {
            "vector type reaches sizeof",
            "typedef int V __attribute__((vector_size(16))); _Static_assert(sizeof(V)==16 && _Alignof(V)==16,\"vector\");\n",
            false,
            false,
        },
        {
            "typedef alignment reaches alignof",
            "typedef int I __attribute__((aligned(1))); _Static_assert(sizeof(I)==4 && _Alignof(I)==1,\"alignment\");\n",
            false,
            false,
        },
        {
            "specifier alignment applies to completed typedef",
            "typedef __attribute__((aligned(32))) int *P; _Static_assert(sizeof(P)==8 && _Alignof(P)==32,\"pointer\");\n",
            false,
            false,
        },
        {
            "packed record layout integration",
            "struct __attribute__((packed)) S { char c; int i; }; _Static_assert(sizeof(struct S)==5 && _Alignof(struct S)==1,\"packed\");\n",
            false,
            false,
        },
        {
            "postfix packed record layout integration",
            "struct S { char c; int i; } __attribute__((packed)); _Static_assert(sizeof(struct S)==5,\"packed\");\n",
            false,
            false,
        },
        {
            "aligned packed member integration",
            "struct S { char c; int i __attribute__((packed,aligned(2))); }; _Static_assert(sizeof(struct S)==6,\"member\");\n",
            false,
            false,
        },
        {
            "multiple declarators retain separate attributes",
            "typedef int A __attribute__((aligned(16))), B __attribute__((aligned(2))); _Static_assert(_Alignof(A)==16 && _Alignof(B)==2,\"separate\");\n",
            false,
            false,
        },
        {
            "asm weak visibility declaration integration",
            "extern int target __asm__(\"actual_target\") __attribute__((weak,visibility(\"hidden\"))); int f(void){return target;}\n",
            false,
            false,
        },
        {
            "GNU attribute ICE integration",
            "typedef int V __attribute__((vector_size(2*sizeof(int)),aligned(1<<4))); _Static_assert(sizeof(V)==8 && _Alignof(V)==16,\"ICE\");\n",
            false,
            false,
        },
        {
            "standard attribute ICE integration",
            "typedef int I [[gnu::aligned(1<<4)]]; _Static_assert(_Alignof(I)==16,\"ICE\");\n",
            false,
            false,
        },
        {
            "alignment type-name integration",
            "_Alignas(long double) int x;\n",
            false,
            false,
        },
        {
            "invalid attribute alignment diagnosed",
            "int x __attribute__((aligned(3)));\n",
            true,
            false,
        },
        {
            "invalid standard alignment diagnosed",
            "_Alignas(1) int x;\n",
            true,
            false,
        },
        {
            "zero alignment cannot annotate typedef",
            "_Alignas(0) typedef int I;\n",
            true,
            false,
        },
        {
            "zero alignment cannot annotate function",
            "_Alignas(0) void f(void);\n",
            true,
            false,
        },
        {
            "invalid nonnull index diagnosed",
            "int f(char*) __attribute__((nonnull(2)));\n",
            true,
            false,
        },
        {
            "postfix vector pointer rejected",
            "typedef int *P __attribute__((vector_size(16)));\n",
            true,
            false,
        },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-attribute-source-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[FILE_COUNT][160];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "attribute source Clang oracle could not run");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        char description[192];
        snprintf(description, sizeof(description), "%s: original source fixture differs from Clang", fixtures[i].name);
        check_test_expect(test, outcomes[i].rejected == fixtures[i].rejected, description);
        check_test_source(test, &fixtures[i], paths[i], &outcomes[i]);
    }
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (directory[0] != 0)
    {
        rmdir(directory);
    }
}
