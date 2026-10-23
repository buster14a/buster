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
    Arena storage = arena_reserve(UINT64_C(1) << 16);
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
                    check_test_expect(test, check_input_attribute_argument(&parser, attribute, argument, &first, &end) && first < end,
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

static void check_test_attributes(CheckTest* test, const char* clang)
{
    check_test_attribute_storage(test);
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

static void check_test_attribute_sources(CheckTest* test, const char* clang)
{
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
