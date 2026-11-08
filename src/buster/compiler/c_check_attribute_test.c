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
    // The attribute's reading or its function type's pass gives `error` at `error_word` and recovers from it (#1447,
    // check_input_attribute_recover): the attributes stay valid with no declaration error, the report is the unit's one
    // recovered report, and the attribute is dropped (left out of the list, or ignored).
    bool recovered;
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
    check_test_expect(test, check_input_attribute_merge(&reader, &common, &suffix, &merged) && merged.error == CHECK_ATTRIBUTE_ERROR_NONE &&
                      merged.calling_convention == CHECK_CALL_SYSV && merged.count == 66 && merged.records[65].first == 70,
                      "a cross-position convention merges after the common one's, for the function they meet to check (#147)");
    Arena exhausted = { 0 };
    reader.arena = &exhausted;
    CheckInputAttributes unavailable = { 0 };
    check_test_expect(test, !check_input_attribute_append(&reader, &unavailable, calling) && input.resource && unavailable.count == 0 &&
                      unavailable.error == CHECK_ATTRIBUTE_ERROR_RESOURCE && unavailable.error_first == 70,
                      "attribute allocation exhaustion fails explicitly without publishing a record");
    arena_release(&storage);
}

// A large object.member[0][0]... tree exercises the location walk with no parser-derived columns: the only edges
// are parent links. Its syntax names member, but a checked outer subscript converts its base and names object.
static void check_test_attribute_expression_location(CheckTest* test)
{
    enum { DEPTH = 32768, TOKENS = 3 + DEPTH * 3 };
    Arena storage = arena_reserve(UINT64_C(1) << 20);
    Parser parser = { 0 };
    parser.tokens.count = TOKENS;
    parser.tree.parents = BUSTER_ARENA_PUSH_ARRAY(&storage, uint32_t, TOKENS);
    parser.tree.roles = BUSTER_ARENA_PUSH_ARRAY(&storage, uint8_t, TOKENS);
    const bool ready = parser.tree.parents != NULL && parser.tree.roles != NULL;
    check_test_expect(test, ready, "deep expression location fixture storage");
    if (ready)
    {
        parser.tree.parents[0] = 1;
        parser.tree.parents[1] = 5;
        parser.tree.parents[2] = 1;
        parser.tree.roles[0] = PARSE_ROLE_OPERAND;
        parser.tree.roles[1] = PARSE_ROLE_MEMBER_OPERATOR;
        parser.tree.roles[2] = PARSE_ROLE_MEMBER;
        for (uint32_t i = 0; i < DEPTH; i += 1)
        {
            const uint32_t open = 3 + i * 3;
            const uint32_t close = open + 2;
            parser.tree.parents[open] = close;
            parser.tree.parents[open + 1] = close;
            parser.tree.parents[close] = i + 1 < DEPTH ? close + 3 : BUSTER_PARSE_ROOT;
            parser.tree.roles[open] = PARSE_ROLE_OPEN_SUBSCRIPT;
            parser.tree.roles[open + 1] = PARSE_ROLE_OPERAND;
            parser.tree.roles[close] = PARSE_ROLE_CLOSE_SUBSCRIPT;
        }
        check_test_expect(test, check_input_expression_location(&parser, 0, TOKENS) == 2,
                          "deep subscript syntax finds the member base's location using parent links");
        CheckTestTypes types = { 0 };
        const bool opened = check_test_types_open(&types);
        check_test_expect(test, opened, "deep expression location fixture types");
        if (opened)
        {
            const TypeId pointer = type_pointer(&types.table, BUSTER_TYPE_INT);
            CheckOp ops[] = { CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_SUBSCRIPT };
            uint32_t operand0[] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, 0 };
            uint32_t operand1[] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, 1 };
            CheckGraph graph = { .count = 3, .ops = ops, .operand0 = operand0, .operand1 = operand1 };
            CheckValue values[] = { { .type = pointer, .category = CHECK_LVALUE }, { .type = BUSTER_TYPE_INT }, { .type = BUSTER_TYPE_INT } };
            CheckStream stream = { .graph = &graph, .count = 3, .dense_values = values };
            check_test_expect(test, pointer != 0 && check_warning_expression_location(&parser, &types.table, &stream, 2, 0, TOKENS) == 0,
                              "deep checked subscripts report the converted base's beginning with one parent walk");
        }
        check_test_types_close(&types);
    }
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
                // (Any integer is a nonnull index here, which the function's check bounds: a negative one of another
                // integer list's attribute fails, a malloc's index among them.)
                attribute.kind = CHECK_ATTRIBUTE_MALLOC;
                check_test_expect(test, !check_input_attribute_argument_value(&reader, &attributes, 0, count - 1, -1) &&
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
            CHECK_CALL_PRESERVE_MOST, CHECK_CALL_PRESERVE_ALL, CHECK_CALL_PRESERVE_NONE, CHECK_CALL_NO_CALLER_SAVED,
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
                // no_caller_saved_registers with System V's convention is one type; with another, refused by name (#147).
                const uint8_t every = TYPE_FUNCTION_ABI_NO_CALLER_SAVED + 1;
                const bool bit = origin != 0 && (origin == every) != (abi == TYPE_FUNCTION_ABI_NO_CALLER_SAVED);
                const bool paired = bit && (origin == TYPE_FUNCTION_ABI_SYSV + 1 || abi == TYPE_FUNCTION_ABI_SYSV);
                const bool kept = matches || paired;
                check_test_expect(test, actual == (paired ? every : matches ? abi + 1 : origin) &&
                                       attributes.error == (kept ? CHECK_ATTRIBUTE_ERROR_NONE : bit ? CHECK_ATTRIBUTE_ERROR_UNKNOWN : CHECK_ATTRIBUTE_ERROR_CONFLICT) &&
                                       input.report_count == (kept ? 0 : 1) &&
                                       (kept || (attributes.error_first == 70 && attributes.error_end == 74 && reports[0].token == 70)) &&
                                       types.table.types.count == type_count && scratch.length == 31,
                                  "spelling ABI provenance distinguishes explicit SysV from implicit default without changing canonical types");
            }
        }
        CheckInputAttribute pair[2] =
        {
            { .first = 60, .end = 61, .kind = CHECK_ATTRIBUTE_CALLING_CONVENTION, .value = CHECK_CALL_SYSV },
            { .first = 70, .end = 74, .kind = CHECK_ATTRIBUTE_CALLING_CONVENTION, .value = CHECK_CALL_MICROSOFT },
        };
        CheckInputAttributes conflicting = { .records = pair, .count = 2, .calling_convention = CHECK_CALL_SYSV };
        input.report_count = 0;
        check_test_expect(test, check_input_attribute_abi(&reader, &conflicting, integer) == integer && conflicting.error == CHECK_ATTRIBUTE_ERROR_NONE &&
                               input.report_count == 0,
                          "conflicting conventions on no function are no error, as clang's (#147)");
        check_test_expect(test, check_input_attribute_abi(&reader, &conflicting, wrapped) == 0 && conflicting.error == CHECK_ATTRIBUTE_ERROR_CONFLICT &&
                               conflicting.error_first == 70 && conflicting.error_end == 74 && input.report_count == 1 && reports[0].token == 70 &&
                               reports[0].detail == (CHECK_CALL_MICROSOFT | CHECK_CALL_SYSV << 8),
                          "the second of conflicting conventions on a function is clang's error, naming both (#147)");
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
            result = check_input_attribute_type(&reader, &attributes, result, fixture->is_typedef, false);
            valid = result != 0 && check_input_attribute_function(&reader, &attributes, result, BUSTER_CHECK_NONE);
            // Calling conventions meet the function, which checks that they agree (#147).
            result = valid ? check_input_attribute_abi(&reader, &attributes, result) : 0;
            valid = result != 0;
        }
        snprintf(description, sizeof(description), "%s: exact attribute diagnosis", fixture->name);
        const bool recovered = fixture->recovered && valid && attributes.error == CHECK_ATTRIBUTE_ERROR_NONE && reader.output->recovered_count == 1 &&
                               reader.output->report_count == 0 && (attributes.count == 0 || attributes.records[attributes.count - 1].ignored);
        check_test_expect(test, ready && (fixture->recovered ? recovered : attributes.error == fixture->error && valid == (fixture->error == CHECK_ATTRIBUTE_ERROR_NONE)),
                          description);
        if (ready && fixture->error_word != 0)
        {
            const uint8_t* bytes = 0;
            uint32_t length = 0;
            // A recovered error's place is its report's.
            const uint32_t place = recovered ? reader.output->recovered_reports[0].token : attributes.error_first;
            bool spelled = place < parser.tokens.count;
            uint8_t buffer[64];
            if (spelled)
            {
                // The authored offending names/numbers/punctuators above fit this buffer. Identifier spellings
                // are returned from the intern table; pp_spelling writes at most 24 bytes for generated integers.
                const uint32_t source = parser.tokens.sources[place];
                length = pp_spelling(&pp, pp.output.kinds[source], pp.output.values[source], pp.output.locations[source], buffer, &bytes);
                spelled = length < sizeof(buffer);
            }
            snprintf(description, sizeof(description), "%s: attribute diagnosis points at the offending token", fixture->name);
            check_test_expect(test, spelled && length == strlen(fixture->error_word) && memcmp(bytes, fixture->error_word, length) == 0, description);
            check_test_expect(test, recovered || (attributes.error_first < attributes.error_end && attributes.error_end <= parser.tokens.count),
                              "attribute diagnosis has a bounded nonempty source range");
        }
        if (valid && !fixture->recovered)
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

// The vocabulary of attributes clang knows (#147, c_check_attribute_clang.inc): each list in its names' byte order without
// repeats, which its binary search needs; the spellings within the 64 bytes a lookup builds; and each list's ends and a name
// between them found, a name between two of them not, and a scoped C23 spelling found under each of its scopes' spellings.
static void check_test_attribute_vocabulary(CheckTest* test)
{
    const CheckInputClangAttribute* const lists[4] = { check_input_clang_gnu_attributes, check_input_clang_c23_attributes, check_input_clang_spellings,
                                                       check_input_clang_scopes };
    const uint32_t counts[4] = { BUSTER_ARRAY_LENGTH(check_input_clang_gnu_attributes), BUSTER_ARRAY_LENGTH(check_input_clang_c23_attributes),
                                 BUSTER_ARRAY_LENGTH(check_input_clang_spellings), BUSTER_ARRAY_LENGTH(check_input_clang_scopes) };
    for (uint32_t standard = 0; standard < 4; standard += 1)
    {
        const CheckInputClangAttribute* list = lists[standard];
        const uint32_t count = counts[standard];
        uint32_t unordered = 0;
        uint32_t found = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t shorter = i != 0 && list[i - 1].length < list[i].length ? list[i - 1].length : list[i].length;
            const int order = i != 0 ? memcmp(list[i - 1].name, list[i].name, shorter) : -1;
            unordered += order > 0 || (order == 0 && list[i - 1].length >= list[i].length) || list[i].length >= 64 ? 1u : 0u;
            found += check_input_attribute_listed(list, count, (String8){ (char*)list[i].name, list[i].length }) ? 1u : 0u;
        }
        check_test_expect(test, (count > 256 || (standard == 3 && count == 9)) && unordered == 0 && found == count,
                          "attribute vocabulary: ordered, bounded and each name found");
    }
    // The subject rows (c_check_attribute_subjects.inc): each list in its spellings' byte order, without repeats.
    for (uint32_t standard = 0; standard < 2; standard += 1)
    {
        const CheckInputSubjectRow* list = standard != 0 ? check_input_subject_c23 : check_input_subject_gnu;
        const uint32_t count = standard != 0 ? BUSTER_ARRAY_LENGTH(check_input_subject_c23) : BUSTER_ARRAY_LENGTH(check_input_subject_gnu);
        uint32_t unordered = 0;
        uint32_t found = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t shorter = i != 0 && list[i - 1].length < list[i].length ? list[i - 1].length : list[i].length;
            const int order = i != 0 ? memcmp(list[i - 1].spelling, list[i].spelling, shorter) : -1;
            unordered += order > 0 || (order == 0 && list[i - 1].length >= list[i].length) ? 1u : 0u;
            found += check_input_subject_find(list, count, (String8){ (char*)list[i].spelling, list[i].length }) == list + i ? 1u : 0u;
        }
        check_test_expect(test, count > 200 && unordered == 0 && found == count, "attribute subjects: rows ordered and each found");
    }
    check_test_expect(test, BUSTER_ARRAY_LENGTH(check_input_subject_gnu) == 304 && BUSTER_ARRAY_LENGTH(check_input_subject_c23) == 225,
                      "attribute subjects: clang 23.1.5's 304 GNU spellings and 225 C23 ones with subjects");
    // The arguments rows (#147): clang 23.1.5's parser switches' 160 names, in byte order, each found by its search; a
    // name of none is none.
    {
        uint32_t unordered = 0;
        uint32_t found = 0;
        const uint32_t count = BUSTER_ARRAY_LENGTH(check_input_arguments_rows);
        for (uint32_t i = 0; i < count; i += 1)
        {
            unordered += i != 0 && strcmp(check_input_arguments_rows[i - 1].name, check_input_arguments_rows[i].name) >= 0 ? 1u : 0u;
            const String8 name = { (char*)check_input_arguments_rows[i].name, strlen(check_input_arguments_rows[i].name) };
            found += check_input_arguments_row(name) == check_input_arguments_rows + i ? 1u : 0u;
        }
        const CheckInputArgumentsRow* format = check_input_arguments_row((String8){ "format", 6 });
        const CheckInputArgumentsRow* deprecated = check_input_arguments_row((String8){ "deprecated", 10 });
        const CheckInputArgumentsRow* typed = check_input_arguments_row((String8){ "preferred_type", 14 });
        const CheckInputArgumentsRow* enable_if = check_input_arguments_row((String8){ "enable_if", 9 });
        check_test_expect(test, count == 160 && unordered == 0 && found == count && check_input_arguments_row((String8){ "aligned", 7 }) == NULL &&
                          check_input_arguments_row((String8){ "", 0 }) == NULL && format != NULL && format->flags == BUSTER_CHECK_ARGUMENTS_IDENTIFIER &&
                          deprecated != NULL && deprecated->strings == 3 && typed != NULL && typed->flags == BUSTER_CHECK_ARGUMENTS_TYPE &&
                          enable_if != NULL && enable_if->flags == BUSTER_CHECK_ARGUMENTS_FUNCTION_SCOPE && enable_if->strings == 2,
                          "attribute arguments: clang 23.1.5's 160 names, ordered, each found, with their lists and string places");
    }
    // The facts rows (#147): each spelling clang has for x86-64 Linux, as its vocabulary has them, in byte order.
    for (uint32_t standard = 0; standard < 2; standard += 1)
    {
        const CheckInputFactRow* list = standard != 0 ? check_input_fact_c23 : check_input_fact_gnu;
        const uint32_t count = standard != 0 ? BUSTER_ARRAY_LENGTH(check_input_fact_c23) : BUSTER_ARRAY_LENGTH(check_input_fact_gnu);
        uint32_t unordered = 0;
        uint32_t found = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t shorter = i != 0 && list[i - 1].length < list[i].length ? list[i - 1].length : list[i].length;
            const int order = i != 0 ? memcmp(list[i - 1].spelling, list[i].spelling, shorter) : -1;
            unordered += order > 0 || (order == 0 && list[i - 1].length >= list[i].length) || list[i].minimum > list[i].maximum ? 1u : 0u;
            found += check_input_fact_find(list, count, (String8){ (char*)list[i].spelling, list[i].length }) == list + i ? 1u : 0u;
        }
        check_test_expect(test, unordered == 0 && found == count, "attribute facts: rows ordered, bounded and each found");
    }
    const CheckInputFactRow* cold_fact = check_input_fact_row((String8){ "", 0 }, (String8){ "cold", 4 }, false, false);
    const CheckInputFactRow* nonnull_fact = check_input_fact_row((String8){ "", 0 }, (String8){ "nonnull", 7 }, false, false);
    const CheckInputFactRow* noderef_fact = check_input_fact_row((String8){ "_Clang", 6 }, (String8){ "noderef", 7 }, true, true);
    const CheckInputFactRow* vector_fact = check_input_fact_row((String8){ "__gnu__", 7 }, (String8){ "vector_size", 11 }, true, true);
    const CheckInputFactRow* bounded_fact = check_input_fact_row((String8){ "", 0 }, (String8){ "bounded", 7 }, false, false);
    check_test_expect(test, BUSTER_ARRAY_LENGTH(check_input_fact_gnu) == 366 && BUSTER_ARRAY_LENGTH(check_input_fact_c23) == 283 && cold_fact != NULL &&
                          cold_fact->flags == (BUSTER_CHECK_FACT_GCC | BUSTER_CHECK_FACT_SIMPLE) && nonnull_fact != NULL &&
                          (nonnull_fact->flags & BUSTER_CHECK_FACT_VARIADIC) != 0 && nonnull_fact->maximum == UINT32_MAX && noderef_fact != NULL &&
                          noderef_fact->flags == BUSTER_CHECK_FACT_TYPE && vector_fact != NULL && (vector_fact->flags & BUSTER_CHECK_FACT_TYPE) != 0 &&
                          vector_fact->minimum == 1 && vector_fact->maximum == 1 && bounded_fact != NULL && (bounded_fact->flags & BUSTER_CHECK_FACT_IGNORED) != 0 &&
                          check_input_fact_row((String8){ "clang", 5 }, (String8){ "cold", 4 }, true, true) == NULL,
                      "attribute facts: clang 23.1.5's 366 GNU and 283 C23 spellings; cold's, nonnull's, clang::noderef's, gnu::vector_size's and bounded's");
    // C's attributes aren't another language's (`(!LangOpts.CPlusPlus)` holds in C), and CUDA's are.
    const CheckInputFactRow* counted_fact = check_input_fact_row((String8){ "", 0 }, (String8){ "counted_by", 10 }, false, false);
    const CheckInputFactRow* device_fact = check_input_fact_row((String8){ "", 0 }, (String8){ "device", 6 }, false, false);
    check_test_expect(test, counted_fact != NULL && (counted_fact->flags & BUSTER_CHECK_FACT_NOT_C) == 0 && device_fact != NULL &&
                          (device_fact->flags & BUSTER_CHECK_FACT_NOT_C) != 0,
                      "attribute facts: counted_by is C's, device another language's");
    // The mutual exclusions (#147): in byte order, both ways for hot and cold.
    uint32_t exclusions_unordered = 0;
    for (uint32_t i = 1; i < BUSTER_ARRAY_LENGTH(check_input_exclusions); i += 1)
    {
        const int order = strcmp(check_input_exclusions[i - 1].spelling, check_input_exclusions[i].spelling);
        exclusions_unordered += order > 0 || (order == 0 && strcmp(check_input_exclusions[i - 1].excluded, check_input_exclusions[i].excluded) >= 0) ? 1u : 0u;
    }
    bool hot_cold = false;
    bool cold_hot = false;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_input_exclusions); i += 1)
    {
        hot_cold |= strcmp(check_input_exclusions[i].spelling, "hot") == 0 && strcmp(check_input_exclusions[i].excluded, "cold") == 0;
        cold_hot |= strcmp(check_input_exclusions[i].spelling, "cold") == 0 && strcmp(check_input_exclusions[i].excluded, "hot") == 0;
    }
    check_test_expect(test, BUSTER_ARRAY_LENGTH(check_input_exclusions) == 44 && exclusions_unordered == 0 && hot_cold && cold_hot,
                      "attribute exclusions: clang 23.1.5's 44 pairs, ordered, hot's and cold's");
    const CheckInputSubjectRow* cold = check_input_subject_row((String8){ "", 0 }, (String8){ "cold", 4 }, false, false);
    const CheckInputSubjectRow* scoped = check_input_subject_row((String8){ "_Clang", 6 }, (String8){ "nomerge", 7 }, true, true);
    check_test_expect(test, cold != NULL && cold->subjects == BUSTER_CHECK_SUBJECT_FUNCTION && !cold->error && scoped != NULL && scoped->error &&
                          check_input_subject_row((String8){ "", 0 }, (String8){ "aligned", 7 }, false, false) == NULL,
                      "attribute subjects: cold's, clang::nomerge's under _Clang, and none for aligned, whose handler checks it");
    // The handlers' own rows: GNU's spelling and gnu::'s (__gnu__ too), and none under another scope or none.
    const CheckInputSubjectRow* sentinel = check_input_subject_row((String8){ "", 0 }, (String8){ "sentinel", 8 }, false, false);
    const CheckInputSubjectRow* transparent = check_input_subject_row((String8){ "__gnu__", 7 }, (String8){ "transparent_union", 17 }, true, true);
    check_test_expect(test, sentinel != NULL && (sentinel->subjects & BUSTER_CHECK_SUBJECT_FUNCTION_POINTER_VARIABLE) != 0 && transparent != NULL &&
                          (transparent->subjects & BUSTER_CHECK_SUBJECT_UNION_TYPEDEF) != 0 &&
                          check_input_subject_row((String8){ "clang", 5 }, (String8){ "transparent_union", 17 }, true, true) == NULL &&
                          check_input_subject_row((String8){ "", 0 }, (String8){ "transparent_union", 17 }, false, true) == NULL,
                      "attribute subjects: sentinel's and transparent_union's rows, which their handlers check, by their spellings alone");
    // weak_import's (#147): GNU's spelling and clang::'s (_Clang too), and none under gnu::.
    const CheckInputSubjectRow* weak_import = check_input_subject_row((String8){ "_Clang", 6 }, (String8){ "weak_import", 11 }, true, true);
    check_test_expect(test, weak_import != NULL && weak_import->subjects == (BUSTER_CHECK_SUBJECT_FUNCTION | BUSTER_CHECK_SUBJECT_VARIABLE) &&
                          check_input_subject_row((String8){ "", 0 }, (String8){ "weak_import", 11 }, false, false) != NULL &&
                          check_input_subject_row((String8){ "gnu", 3 }, (String8){ "weak_import", 11 }, true, true) == NULL,
                      "attribute subjects: weak_import's row, which its handler checks, by its spellings alone");
    TypeTestTable storage;
    const bool ready = type_test_table_open(&storage, 4096);
    check_test_expect(test, ready, "attribute subjects: nonstring's type table opens");
    if (ready)
    {
        // nonstring's types (handleNonStringAttr): a pointer to a character type or an array of one through arrays, qualified
        // or not, and nothing else.
        TypeTable* table = &storage.table;
        const TypeId char_array = type_array(table, BUSTER_TYPE_CHAR, 4);
        const TypeId fits[] = {
            char_array,
            type_array(table, type_array(table, BUSTER_TYPE_SCHAR, 2), 3),
            type_pointer(table, type_qualify(table, BUSTER_TYPE_CHAR, BUSTER_TYPE_CONST)),
            type_pointer(table, BUSTER_TYPE_UCHAR),
            type_incomplete_array(table, BUSTER_TYPE_CHAR),
        };
        const TypeId misfits[] = {
            BUSTER_TYPE_CHAR,
            BUSTER_TYPE_INT,
            type_array(table, BUSTER_TYPE_INT, 2),
            type_array(table, BUSTER_TYPE_BOOL, 2),
            type_pointer(table, type_pointer(table, BUSTER_TYPE_CHAR)),
            type_pointer(table, char_array),
        };
        bool all = true;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fits); i += 1)
        {
            all &= check_input_nonstring_type(table, fits[i]);
        }
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(misfits); i += 1)
        {
            all &= !check_input_nonstring_type(table, misfits[i]);
        }
        check_test_expect(test, all, "attribute subjects: nonstring takes character arrays and pointers to characters alone");
        type_test_table_close(&storage);
    }
    check_test_expect(test, BUSTER_ARRAY_LENGTH(check_input_clang_gnu_attributes) == 366 && BUSTER_ARRAY_LENGTH(check_input_clang_c23_attributes) == 283 &&
                          BUSTER_ARRAY_LENGTH(check_input_clang_spellings) == 539 && BUSTER_ARRAY_LENGTH(check_input_clang_scopes) == 9,
                      "attribute vocabulary: clang 23.1.5's 366 GNU names, 283 C23 spellings, and its list's 539 spellings and 9 scopes");
    static const struct
    {
        const char* scope;
        const char* name;
        bool scoped;
        bool standard;
        bool known;
    } cases[] = {
        { "", "aligned", false, false, true },
        { "", "NSObject", false, false, true },
        { "", "__const", false, false, true },
        { "", "externally_visible", false, false, false },
        { "", "alignedx", false, false, false },
        { "", "", false, false, false },
        { "", "deprecated", false, true, true },
        { "", "unsequenced", false, true, false },
        { "", "aligned", false, true, false },
        { "gnu", "aligned", true, true, true },
        { "__gnu__", "aligned", true, true, false },
        { "clang", "noderef", true, true, true },
        { "_Clang", "noderef", true, true, true },
        { "gnu", "noderef", true, true, false },
        { "invented", "aligned", true, true, false },
        { "gnu", "invented", true, true, false },
        { "gnu", "an_attribute_name_of_sixty_bytes_which_no_lookup_can_build_x", true, true, false },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        const String8 scope = { (char*)cases[i].scope, strlen(cases[i].scope) };
        const String8 name = { (char*)cases[i].name, strlen(cases[i].name) };
        char description[160];
        snprintf(description, sizeof(description), "attribute vocabulary: %s%s%s (%s) is %s", cases[i].scope, cases[i].scoped ? "::" : "", cases[i].name,
                 cases[i].standard ? "C23" : "GNU", cases[i].known ? "known" : "unknown");
        check_test_expect(test, check_input_attribute_clang_knows(scope, name, cases[i].scoped, cases[i].standard) == cases[i].known, description);
    }
}

static __attribute__((noinline)) void check_test_attributes(CheckTest* test, const char* clang)
{
    check_test_attribute_storage(test);
    check_test_attribute_vocabulary(test);
    check_test_attribute_expression_location(test);
    check_test_attribute_arguments(test);
    check_test_attribute_abis(test);
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
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "aligned",
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
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
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
        // WebAssembly's export_name, which clang doesn't know for x86-64 and ignores with -Wunknown-attributes' warning (#147).
        {
            .name = "WebAssembly export spelling ignored on x86-64",
            .source = "__attribute__((export_name(\"entry_point\"))) void entry(void) {}\n",
        },
        {
            .name = "scoped WebAssembly export spelling ignored on x86-64",
            .source = "[[clang::export_name(\"entry_point\")]] void entry(void) {}\n",
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
            // Its count's error is its subject's check's, after the subject (#147, check_input_attribute_subjects): kept as
            // a generic attribute, which its handler doesn't apply.
            .name = "nodebug keeps its argument count for its subject",
            .source = "void f(void) __attribute__((nodebug(1)));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_UNKNOWN,
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
            // Its count's error is its subject's check's, after the subject (#147, check_input_attribute_subjects): kept as
            // a generic attribute, which its handler doesn't apply.
            .name = "overloadable keeps its argument count for its subject",
            .source = "int f(int) __attribute__((overloadable(1)));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_UNKNOWN,
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
            .recovered = true,
            .source = "int f(int,...) __attribute__((format(printf,1,2)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "format",
            .rejected = true,
            .values = { 1, 2 },
            .function_result = BUSTER_TYPE_INT,
            .function_parameters = { BUSTER_TYPE_INT },
            .function_count = 1,
        },
        {
            .name = "nonnull parameter index rejected",
            .recovered = true,
            .source = "int f(char*) __attribute__((nonnull(2)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_PARAMETER,
            // clang's "parameter 1 is out of bounds" is at the attribute's name.
            .error_word = "nonnull",
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
            .recovered = true,
            .source = "void *f(float,...) __attribute__((alloc_size(1)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
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
            .recovered = true,
            .source = "int f(char*,...) __attribute__((format_arg(1)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "format_arg",
            .rejected = true,
            .values = { 1 },
            .function_result = BUSTER_TYPE_INT,
            .function_parameters = { BUSTER_TYPE_CHAR },
            .function_count = 1,
            .pointer_parameters = 1,
        },
        {
            .name = "malformed packed argument",
            .recovered = true,
            .source = "struct __attribute__((packed(1))) S { int x; };\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "packed",
            .rejected = true,
        },
        {
            // An argument count other than clang's waits for the subject's check too (#147), its arguments unread.
            .name = "aligned keeps its argument count for its subject",
            .source = "int x __attribute__((aligned(2,4)));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_UNKNOWN,
            .rejected = true,
        },
        {
            .name = "invalid alignment value",
            .source = "int x __attribute__((aligned(3)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "aligned",
            .rejected = true,
            .values = { 3 },
        },
        {
            .name = "negative alignment value",
            .source = "int x __attribute__((aligned(-8)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "aligned",
            .rejected = true,
            .values = { -8 },
        },
        {
            .name = "_Alignas cannot reduce alignment",
            .source = "_Alignas(1) int x;\n",
            // clang's "requested alignment is less than minimum alignment" (check_input_alignas_error, #147).
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "_Alignas",
            .base = BUSTER_TYPE_INT,
            .rejected = true,
            .values = { 1 },
        },
        {
            .name = "_Alignas cannot annotate typedef",
            .source = "_Alignas(16) typedef int I;\n",
            // clang's "'_Alignas' attribute only applies to variables and fields" (check_input_alignas_error, #147).
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
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
            // clang's "zero vector size" is at the attribute (check_input_vector_size).
            .error_word = "vector_size",
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .rejected = true,
        },
        {
            .name = "invalid machine mode spelling",
            .source = "typedef int I __attribute__((mode(NO_SUCH_MODE)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "mode",
            .base = BUSTER_TYPE_INT,
            .is_typedef = true,
            .rejected = true,
        },
        {
            .name = "wrong machine mode class",
            .source = "typedef float I __attribute__((mode(SI)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "mode",
            .base = BUSTER_TYPE_FLOAT,
            .is_typedef = true,
            .rejected = true,
        },
        {
            .name = "invalid string attribute",
            .recovered = true,
            .source = "int x __attribute__((section(3)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "3",
            .rejected = true,
        },
        {
            .name = "invalid TLS model",
            .source = "__thread int x __attribute__((tls_model(\"invented\")));\n",
            // Read, its model's error kept for after its subject's check (CheckInputAttribute.deferred, #147), which the
            // driver test compares with clang's.
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_TLS_MODEL,
            .rejected = true,
        },
        {
            .name = "conflicting calling conventions",
            .source = "void f(void) __attribute__((ms_abi, sysv_abi));\n",
            .error = CHECK_ATTRIBUTE_ERROR_CONFLICT,
            .error_word = "sysv_abi",
            .rejected = true,
            .function_result = BUSTER_TYPE_VOID,
            .nonvariadic = true,
        },
        {
            .name = "invalid regparm count",
            .source = "void f(void) __attribute__((regparm(7)));\n",
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "regparm",
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
            .error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
            .error_word = "align_value",
            .rejected = true,
            .values = { 48 },
        },
        // An attribute clang doesn't know it ignores with a warning (#147): none of it is kept.
        {
            .name = "an attribute clang doesn't know is ignored",
            .source = "int x __attribute__((invented_semantics(3)));\n",
        },
        {
            .name = "an unknown C23 namespace's attribute is ignored",
            .source = "[[invented::aligned(16)]] int x;\n",
        },
        {
            .name = "unscoped GNU layout name is not ISO packed, and ignored",
            .source = "struct [[packed]] S { int x; };\n",
        },
        {
            .name = "a GNU name clang doesn't know is ignored beside one it does",
            .source = "int x __attribute__((externally_visible, aligned(8)));\n",
            .count = 1,
            .last_kind = CHECK_ATTRIBUTE_ALIGNED,
            .base = BUSTER_TYPE_INT,
            .size = 4,
            .alignment = 4,
            .declared_alignment = 4,
            .values = { 8 },
        },
        {
            .name = "C23's unsequenced, which clang doesn't know, is ignored",
            .source = "[[unsequenced]] int f(void);\n",
        },
        // One clang applies that the checker doesn't take is refused by name.
        {
            .name = "an attribute clang applies and the checker doesn't take is refused",
            .source = "int f(void) __attribute__((swift_name(\"g()\")));\n",
            .error = CHECK_ATTRIBUTE_ERROR_UNKNOWN,
            .error_word = "swift_name",
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
        // GNU's weak attribute needs external linkage (#147); a typedef's is ignored, and so is one after the entity's
        // definition, as a constructor or a destructor after it is.
        { "weak static function rejected", "static int\n__attribute__((weak)) f(void) { return 1; }\n", true, false },
        { "weak static variable rejected", "static int x\n__attribute__((weak));\n", true, false },
        { "weak block-scope static rejected", "void f(void) { static int x\n__attribute__((weak)); }\n", true, false },
        { "weak automatic variable rejected", "void f(void) { int x\n__attribute__((weak)); }\n", true, false },
        { "weak redeclaration of a static function rejected", "static int f(void);\nint f(void)\n__attribute__((weak));\n", true, false },
        { "weak block-scope extern", "void f(void) { extern int x __attribute__((weak)); }\n", false, false },
        { "weak block-scope function", "void f(void) { int g(void) __attribute__((weak)); }\n", false, false },
        { "weak typedef ignored", "typedef int T __attribute__((weak));\n", false, false },
        { "weak after static function definition ignored", "static int f(void){return 1;}\nstatic int f(void) __attribute__((weak));\n", false, false },
        { "weak after static object definition ignored", "static int x=1;\nextern int x __attribute__((weak));\n", false, false },
        { "weak after tentative static object rejected", "static int x;\nstatic int x __attribute__((weak));\n", true, false },
        { "weak after the definition ignored", "int x = 1;\nint x __attribute__((weak));\n", false, false },
        { "weak declarators after one's definition", "int x = 1;\n__attribute__((weak)) int x, y;\n", false, false },
        { "constructor after the definition ignored", "void f(void) {}\nvoid f(void) __attribute__((constructor));\n", false, false },
        { "constructor priorities", "void __attribute__((constructor(101), destructor(65536))) f(void) {}\n", false, false },
        { "alias function definition rejected", "int g(void) { return 0; }\n__attribute__((alias(\"g\"))) int h(void) { return 1; }\n", true, false },
        { "alias function declaration accepted", "int g(void) { return 0; }\n__attribute__((alias(\"g\"))) int h(void);\n", false, false },
        { "alias followed by a body rejected", "int g(void) { return 0; }\nint h(void) __attribute__((alias(\"g\")));\nint h(void) { return 1; }\n", true, false },
        { "alias followed by a declaration and a body rejected", "int g(void) { return 0; }\nint h(void) __attribute__((alias(\"g\")));\nint h(void);\nint h(void) { return 1; }\n", true, false },
        { "alias followed by a declaration accepted", "int g(void) { return 0; }\nint h(void) __attribute__((alias(\"g\")));\nint h(void);\n", false, false },
        { "repeated alias definition rejected", "int g(void) { return 0; }\nint h(void) __attribute__((alias(\"g\")));\nint h(void) __attribute__((alias(\"g\")));\n", true, false },
        { "body followed by alias rejected", "int g(void) { return 0; }\nint h(void) { return 1; }\nint h(void) __attribute__((alias(\"g\")));\n", true, false },
        { "block alias followed by body rejected", "int g(void) { return 0; }\nvoid f(void) { int h(void) __attribute__((alias(\"g\"))); }\nint h(void) { return 1; }\n", true, false },
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
