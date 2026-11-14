// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original issue #1135 fixtures. Exercise the retained policy, not just directive acceptance, through both
// parser schedules and tree-only input. The unsupported surface must continue to fail closed.

#include "c_check_symbol_pragma_test.c"

static void check_test_pragma_extension_history(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const char* source =
        "#pragma STDC FENV_ACCESS ON\ndouble file_on;\n"
        "#pragma clang fp contract(fast)\n#pragma float_control(precise, on)\n"
        "double f(void){\n#pragma STDC FENV_ACCESS OFF\ndouble local_off=0;{\n"
        "#pragma STDC FENV_ACCESS ON\ndouble inner_on=0;}double restored_off=0;return 0;}double restored_on;\n"
        "#pragma STDC FENV_ACCESS OFF\ndouble file_off;\n"
        "#pragma clang fp contract(on)\n#pragma clang fp contract(off)\n"
        "#define CONTROL except\n#define STATE on\n#pragma float_control(push)\n#pragma float_control(CONTROL, STATE)\ndouble exceptions_on;\n"
        "#undef STATE\n#define STATE ignore\ndouble g(void){\n#pragma clang fp exceptions(STATE)\ndouble exceptions_off=0;{\n"
        "#pragma clang fp exceptions(maytrap)\ndouble exceptions_maytrap=0;}double exceptions_restored=0;return 0;}double exceptions_file;\n"
        "#pragma float_control(pop)\ndouble stack_restored;\n"
        "#pragma float_control(precise, off, push)\ndouble relaxed;\n#pragma float_control(precise, on)\n"
        "#pragma clang fp reassociate(on) reciprocal(on) contract(off)\ndouble permissions;\n"
        "#pragma float_control(pop)\ndouble precise_restored;\n"
        "#pragma float_control(except, on, push)\ndouble pushed_strict;\n"
        "#pragma float_control(except, off)\ndouble exceptions_disabled;\n#pragma float_control(pop)\ndouble popped_off;\n"
        "#pragma STDC CX_LIMITED_RANGE ON\n#pragma float_control(push)\n#pragma STDC CX_LIMITED_RANGE OFF\ndouble complex_off;\n"
        "#pragma float_control(pop)\ndouble complex_on;\n#pragma STDC CX_LIMITED_RANGE OFF\ndouble complex_end;\n";
    const uint8_t kinds[] = { CHECK_FP_PRAGMA_FENV_ACCESS, CHECK_FP_PRAGMA_FP_CONTRACT, CHECK_FP_PRAGMA_PRECISE,
                             CHECK_FP_PRAGMA_FENV_ACCESS, CHECK_FP_PRAGMA_FENV_ACCESS, CHECK_FP_PRAGMA_FENV_ACCESS,
                             CHECK_FP_PRAGMA_FP_CONTRACT, CHECK_FP_PRAGMA_FP_CONTRACT, CHECK_FP_PRAGMA_PUSH,
                             CHECK_FP_PRAGMA_EXCEPTIONS, CHECK_FP_PRAGMA_EXCEPTIONS, CHECK_FP_PRAGMA_EXCEPTIONS,
                             CHECK_FP_PRAGMA_POP, CHECK_FP_PRAGMA_PRECISE, CHECK_FP_PRAGMA_PRECISE, CHECK_FP_PRAGMA_OPTIONS,
                             CHECK_FP_PRAGMA_POP, CHECK_FP_PRAGMA_EXCEPTIONS, CHECK_FP_PRAGMA_EXCEPTIONS, CHECK_FP_PRAGMA_POP,
                             CHECK_FP_PRAGMA_CX_LIMITED_RANGE, CHECK_FP_PRAGMA_PUSH, CHECK_FP_PRAGMA_CX_LIMITED_RANGE, CHECK_FP_PRAGMA_POP, CHECK_FP_PRAGMA_CX_LIMITED_RANGE };
    const uint8_t states[] = { 1, 2, 1, 0, 1, 0, 1, 0, 0, 2, 0, 1, 0, 0, 1, 0, 0, 2, 0, 0, 1, 0, 0, 0, 0 };
    const char* names[] = { "file_on", "local_off", "inner_on", "restored_off", "restored_on", "file_off", "exceptions_on", "exceptions_off",
                            "exceptions_maytrap", "exceptions_restored", "exceptions_file", "stack_restored", "relaxed", "permissions", "precise_restored",
                            "pushed_strict", "exceptions_disabled", "popped_off", "complex_off", "complex_on", "complex_end" };
    const uint8_t expected[] = {
        BUSTER_CHECK_FP_CONTRACT_ON | BUSTER_CHECK_FP_ENV_ACCESS,
        BUSTER_CHECK_FP_CONTRACT_ON,
        BUSTER_CHECK_FP_CONTRACT_ON | BUSTER_CHECK_FP_ENV_ACCESS,
        BUSTER_CHECK_FP_CONTRACT_ON,
        BUSTER_CHECK_FP_CONTRACT_ON | BUSTER_CHECK_FP_ENV_ACCESS,
        BUSTER_CHECK_FP_CONTRACT_ON,
        BUSTER_CHECK_FP_ENV_ACCESS,
        0,
        BUSTER_CHECK_FP_ENV_ACCESS,
        0,
        BUSTER_CHECK_FP_ENV_ACCESS,
        0,
        BUSTER_CHECK_FP_CONTRACT_FAST,
        0,
        0,
        BUSTER_CHECK_FP_ENV_ACCESS,
        0,
        0,
        0,
        0,
        0,
    };
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true, .semantic_pragmas = true };
    Arena arena = arena_reserve(UINT64_C(1) << 16);
    Arena scratch = arena_reserve(UINT64_C(1) << 16);
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
    ready = ready && arena.base != 0 && scratch.base != 0 && pp_run_source(&pp, "pragma-extension-history.c", (const uint8_t*)source, strlen(source));
    check_test_expect(test, ready, "FP extension history fixture initializes");
    for (uint32_t schedule = 0; ready && schedule < 4; schedule += 1)
    {
        const ParseOptions parse_options = { .scalar = (schedule & 1) != 0, .tree_only = (schedule & 2) != 0 };
        const bool parsed = parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
        const bool retained = parsed && parser.tokens.pragma_count == BUSTER_ARRAY_LENGTH(kinds);
        check_test_expect(test, retained, "all FP extension/STDC directives are retained on every parser schedule");
        const uint64_t arena_capacity = arena.capacity;
        const uint64_t scratch_capacity = scratch.capacity;
        uint64_t required = 31;
        if (retained)
        {
            arena.length = 0;
            scratch.length = 0;
            memset(arena_push(&arena, arena.capacity, 1), 0xa5, arena.capacity);
            memset(arena_push(&scratch, scratch.capacity, 1), 0x5a, scratch.capacity);
        }
        for (uint32_t pass = 0; retained && pass < 3; pass += 1)
        {
            const uint64_t attempts = pass == 1 ? required - 31 : 1;
            for (uint64_t limit = 0; limit < attempts; limit += 1)
            {
                arena.length = 31;
                arena.capacity = pass == 1 ? 31 + limit : arena_capacity;
                scratch.length = 29;
                scratch.capacity = pass == 2 ? 29 : scratch_capacity;
                uint8_t handled[BUSTER_ARRAY_LENGTH(kinds)] = { 0 };
                CheckInput input = { .graph = { .capacity = parser.tokens.count * 2 } };
                CheckInputReader reader = { .output = &input, .parser = &parser, .pp = &pp, .arena = &arena, .scratch = &scratch };
                const bool valid = check_input_fp_pragmas(&reader, handled);
                if (pass == 0)
                {
                    required = arena.length;
                    const bool published = valid && !input.resource && input.fp_pragma_count == BUSTER_ARRAY_LENGTH(kinds) &&
                                           input.fp_pragmas != 0 && input.fp_environments != 0 && input.cx_limited_ranges != 0 &&
                                           input.graph.fp_environments != 0;
                    check_test_expect(test, published && scratch.length == 29, "FP extensions initialize poisoned policy storage and restore scratch");
                    if (published)
                    {
                        for (uint32_t pragma = 0; pragma < BUSTER_ARRAY_LENGTH(kinds); pragma += 1)
                        {
                            const CheckInputFpPragma* record = input.fp_pragmas + pragma;
                            check_test_expect(test, handled[pragma] == 2 && record->kind == kinds[pragma] && record->environment == states[pragma] &&
                                                   record->valid && record->warnings == 0 && record->pragma == pragma &&
                                                   record->source_begin == parser.tokens.pragma_sources[pragma] &&
                                                   record->source_end > record->source_begin && record->source_end <= pp.output.count &&
                                                   record->token == parser.tokens.pragma_positions[pragma] && record->scope_end <= parser.tokens.count &&
                                                   ((record->scope != BUSTER_CHECK_NONE) == (pragma == 3 || pragma == 4 || pragma == 10 || pragma == 11)),
                                              "FP extension records retain exact state, scope, spelling span and source order");
                        }
                        uint32_t found = 0;
                        for (uint32_t token = 0; token < parser.tokens.count; token += 1)
                        {
                            const uint32_t name = parser.tokens.names[token];
                            for (uint32_t item = 0; name != 0 && item < BUSTER_ARRAY_LENGTH(names); item += 1)
                            {
                                if (intern_length(&identifiers, name) == strlen(names[item]) && memcmp(intern_bytes(&identifiers, name), names[item], strlen(names[item])) == 0)
                                {
                                    found += 1;
                                    check_test_expect(test, input.fp_environments[token] == expected[item] && input.cx_limited_ranges[token] == (item == 19),
                                                      "FP settings restore compound and stack scopes");
                                }
                            }
                        }
                        check_test_expect(test, found == BUSTER_ARRAY_LENGTH(names), "FP extension history visits every policy marker");
                        bool zeroed = true;
                        for (uint32_t node = 0; node < input.graph.capacity; node += 1)
                        {
                            zeroed &= input.graph.fp_environments[node] == 0;
                        }
                        check_test_expect(test, zeroed, "the retained graph policy column starts initialized before node construction");
                    }
                }
                else
                {
                    bool untouched = true;
                    for (uint32_t pragma = 0; pragma < BUSTER_ARRAY_LENGTH(handled); pragma += 1)
                    {
                        untouched &= handled[pragma] == 0;
                    }
                    check_test_expect(test, !valid && input.resource && input.fp_pragma_count == 0 && input.fp_pragmas == 0 &&
                                           input.fp_environments == 0 && input.cx_limited_ranges == 0 && input.graph.fp_environments == 0 &&
                                           untouched && arena.length == 31 && scratch.length == 29,
                                      "FP extension allocation failure publishes neither state nor handled marks");
                }
            }
        }
        arena.capacity = arena_capacity;
        scratch.capacity = scratch_capacity;
        parse_release(&parser);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&scratch);
    arena_release(&arena);
}

static void check_test_ms_pragma_history(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    static const char source[] =
        "int ordinary;\n#pragma ms_struct on\nint on_first;\n"
        "void f(void){int before;\n#pragma ms_struct off\nint off_inside;\n#pragma ms_struct on\nint on_inside;}\nint leaked;\n"
        "#pragma ms_struct on extra\nint unchanged;\n#pragma ms_struct nonsense\nint unchanged_bad;\n"
        "#pragma ms_struct off\nint off_file;\n#pragma ms_struct on\n#pragma ms_struct reset\nint reset_file;\n"
        "#define MODE on\n#pragma ms_struct MODE\nint macro_file;\n";
    static const uint8_t states[] = { 1, 0, 1, 1, 1, 0, 1, 0, 1 };
    static const uint8_t warnings[] = { 0, 0, 0, 2, 1, 0, 0, 0, 0 };
    static const char* const names[] = { "ordinary", "on_first", "before", "off_inside", "on_inside", "leaked", "unchanged", "unchanged_bad",
                                         "off_file", "reset_file", "macro_file" };
    static const uint8_t expected[] = { 0, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1 };
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true, .semantic_pragmas = true };
    Arena arena = arena_reserve(UINT64_C(1) << 16);
    Arena scratch = arena_reserve(UINT64_C(1) << 16);
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
    ready = ready && arena.base != NULL && scratch.base != NULL && pp_run_source(&pp, "ms-pragma-history.c", (const uint8_t*)source, sizeof(source) - 1);
    check_test_expect(test, ready, "Microsoft layout source-state fixture initializes");
    for (uint32_t schedule = 0; ready && schedule < 4; schedule += 1)
    {
        const ParseOptions parse_options = { .scalar = (schedule & 1) != 0, .tree_only = (schedule & 2) != 0 };
        const bool parsed = parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
        const bool retained = parsed && parser.tokens.pragma_count == BUSTER_ARRAY_LENGTH(states);
        check_test_expect(test, retained, "Microsoft layout directives survive every parser schedule");
        const uint64_t arena_capacity = arena.capacity;
        const uint64_t scratch_capacity = scratch.capacity;
        for (uint32_t pass = 0; retained && pass < 3; pass += 1)
        {
            arena.length = 31;
            scratch.length = 29;
            arena.capacity = pass == 1 ? arena.length : arena_capacity;
            scratch.capacity = pass == 2 ? scratch.length : scratch_capacity;
            uint8_t handled[BUSTER_ARRAY_LENGTH(states)] = { 0 };
            CheckInput input = { .token_count = parser.tokens.count };
            CheckInputReader reader = { .output = &input, .parser = &parser, .pp = &pp, .arena = &arena, .scratch = &scratch };
            const bool valid = check_input_ms_pragmas(&reader, handled, true);
            if (pass == 0)
            {
                const bool published = valid && !input.resource && input.ms_pragmas != NULL && input.ms_struct != NULL &&
                                       input.ms_pragmas->count == BUSTER_ARRAY_LENGTH(states);
                check_test_expect(test, published && scratch.length == 29, "Microsoft layout history publishes with scratch restored");
                for (uint32_t i = 0; published && i < BUSTER_ARRAY_LENGTH(states); i += 1)
                {
                    const CheckInputMsPragma* record = input.ms_pragmas->records + i;
                    check_test_expect(test, handled[i] == 1 && record->supported && !record->error && record->pragma == i && record->after == states[i] &&
                                           record->warning == warnings[i] && record->token == parser.tokens.pragma_positions[i] &&
                                           record->source == parser.tokens.pragma_sources[i],
                                      "Microsoft layout keeps exact source order and leaves state unchanged after malformed directives");
                }
                uint32_t found = 0;
                for (uint32_t token = 0; published && token < parser.tokens.count; token += 1)
                {
                    const uint32_t name = parser.tokens.names[token];
                    for (uint32_t i = 0; name != 0 && i < BUSTER_ARRAY_LENGTH(names); i += 1)
                    {
                        if (intern_length(&identifiers, name) == strlen(names[i]) && memcmp(intern_bytes(&identifiers, name), names[i], strlen(names[i])) == 0)
                        {
                            found += 1;
                            check_test_expect(test, input.ms_struct[token] == expected[i], "Microsoft layout state persists across function boundaries and expands pragma operands");
                        }
                    }
                }
                check_test_expect(test, published && found == BUSTER_ARRAY_LENGTH(names), "Microsoft layout source-state fixture visits every marker");
                const bool warned = published && check_input_ms_warnings(&reader);
                check_test_expect(test, warned && input.pragma_warnings.count == 2 &&
                                       input.pragma_warnings.events[0].flags == BUSTER_CHECK_WARNING_MS_PRAGMA &&
                                       input.pragma_warnings.events[1].flags == BUSTER_CHECK_WARNING_MS_PRAGMA,
                                  "malformed Microsoft pragmas retain distinct lexical warnings");
            }
            else
            {
                bool untouched = true;
                for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(handled); i += 1)
                {
                    untouched &= handled[i] == 0;
                }
                check_test_expect(test, !valid && input.resource && input.ms_pragmas == NULL && input.ms_struct == NULL && untouched &&
                                       arena.length == 31 && scratch.length == 29,
                                  "Microsoft layout allocation failure publishes no partial state or handler marks");
            }
        }
        arena.capacity = arena_capacity;
        scratch.capacity = scratch_capacity;
        parse_release(&parser);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&scratch);
    arena_release(&arena);
}

typedef struct CheckPragmaExtensionFixture CheckPragmaExtensionFixture;
struct CheckPragmaExtensionFixture
{
    const char* source;
    const char* loop_text;
    const char* first;
    const char* second;
    CheckInputMessage message;
    uint32_t records;
    uint32_t ms_records;
    uint8_t fp_error;
    bool accepted;
};

static void check_test_pragma_extension_sources(CheckTest* test)
{
    static const CheckPragmaExtensionFixture fixtures[] =
    {
        { .source = "#pragma weak value\nint value=7;\n", .accepted = true },
        { .source = "int value;\n#pragma weak value\n", .accepted = true },
        { .source = "int value=7;\n#pragma weak value\n", .accepted = true },
        { .source = "#pragma weak missing\n", .accepted = true },
        { .source = "#pragma weak value\nstatic int value=7;\n", .accepted = true },
        { .source = "#pragma weak value\ntypedef int value;\n", .accepted = true },
        { .source = "int f(void){\n#pragma weak value\nreturn 0;}\nint value=7;\n", .accepted = true },
        { .source = "extern int value; int f(void){return value;}\n#pragma weak value\nint value=7;\n", .accepted = true },
        { .source = "#pragma redefine_extname value renamed\nint value=7;\n", .accepted = true },
        { .source = "extern int value; int size(void){return sizeof(value);}\n#pragma redefine_extname value renamed\nint value=7;\n", .accepted = true },
        { .source = "int value;\n#pragma redefine_extname value renamed\n", .accepted = true },
        { .source = "int value=7;\n#pragma redefine_extname value renamed\n", .accepted = true },
        { .source = "#pragma redefine_extname value renamed\nstatic int value=7;\n", .accepted = true },
        { .source = "#pragma weak\n#pragma weak alias=\n#pragma redefine_extname value\nint value;\n", .accepted = true },
        { .source = "#pragma weak alias=value\nint value=7;\n", .accepted = true },
        { .source = "#pragma weak alias=value\nextern int value;\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "extern int value; int f(void){return value;}\n#pragma redefine_extname value renamed\nint value=7;\n", .accepted = true },
        { .source = "#define SOURCE value\n#pragma weak SOURCE\nint value=7;\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "#pragma ms_struct on\nint value;\n", .ms_records = 1, .accepted = true },
        { .source = "#pragma ms_struct on\nstruct S{char head;unsigned a:3,b:5;char tail;};\n", .ms_records = 1, .accepted = true },
        { .source = "#pragma ms_struct on\nstruct S;\n#pragma ms_struct off\nstruct S{char head;unsigned a:3,b:5;char tail;};\n", .ms_records = 2, .accepted = true },
        { .source = "struct S;\n#pragma ms_struct on\nstruct S{char head;unsigned a:3,b:5;char tail;};\n", .ms_records = 1, .accepted = true },
        { .source = "struct __attribute__((ms_struct)) S;\nstruct S{char head;unsigned a:3,b:5;char tail;};\n", .accepted = true },
        { .source = "struct __attribute__((ms_struct,ms_struct,ms_struct)) S{char head;unsigned a:3,b:5;char tail;};\n", .accepted = true },
        { .source = "struct __attribute__((ms_struct)) S;\nstruct __attribute__((gcc_struct)) S{char head;unsigned a:3,b:5;char tail;};\n", .accepted = true },
        { .source = "struct __attribute__((gcc_struct)) S;\nstruct __attribute__((ms_struct)) S{char head;unsigned a:3,b:5;char tail;};\n", .accepted = true },
        { .source = "#pragma ms_struct on\nstruct __attribute__((gcc_struct)) S{char head;unsigned a:3,b:5;char tail;};\n", .ms_records = 1, .accepted = true },
        { .source = "void f(void){int value=0;\n#pragma ms_struct on\nvalue++;}\nstruct S{char head;unsigned a:3,b:5;char tail;};\n", .ms_records = 1, .accepted = true },
        { .source = "#pragma ms_struct on\n#pragma ms_struct reset\nstruct S{char head;unsigned a:3,b:5;char tail;};\n", .ms_records = 2, .accepted = true },
        { .source = "#pragma ms_struct\n#pragma ms_struct on extra\n#pragma ms_struct nonsense\nint value;\n", .ms_records = 3, .accepted = true },
        { .source = "struct __attribute__((ms_struct,gcc_struct)) S{unsigned a:3;};\n", .message = CHECK_INPUT_ATTRIBUTE_EXCLUSION },
        { .source = "struct __attribute__((gcc_struct,ms_struct)) S{unsigned a:3;};\n", .message = CHECK_INPUT_ATTRIBUTE_EXCLUSION },
        { .source = "struct S{\n#pragma ms_struct on\nunsigned a:3;};\n", .message = CHECK_INPUT_DECLARATION },
        { .source = "#pragma ms_struct on\nstruct S{\n#pragma ms_struct off\nunsigned a:3;};\n", .message = CHECK_INPUT_DECLARATION },
        { .source = "#pragma ms_struct on\nstruct S{unsigned _BitInt(129) a:32;};\n", .message = CHECK_INPUT_MS_STRUCT_ALIGNMENT },
        { .source = "#define STATE on\n#pragma ms_struct STATE\nstruct S{unsigned a:3;};\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "#pragma fenv_access(on)\ndouble f(double x){return x/3.0;}\n", .accepted = true },
        { .source = "#pragma fenv_access(off)\nint x;\n", .accepted = true },
        { .source = "#pragma clang fp contract(on)\n#pragma clang fp contract(off)\n#pragma clang fp contract(fast)\nint x;\n", .records = 3, .accepted = true },
        { .source = "#pragma float_control(precise, on)\nint x;\n", .records = 1, .accepted = true },
        { .source = "int f(void){\n#pragma fenv_access(on)\n#pragma clang fp contract(off)\n#pragma float_control(precise, on)\nreturn 0;}\n", .records = 2, .accepted = true },
        { .source = "#define ACCESS _Pragma(\"fenv_access(on)\")\nACCESS\ndouble f(double x){return x/3.0;}\n", .accepted = true },
        { .source = "#define CONTRACT _Pragma(\"clang fp contract(fast)\")\nCONTRACT\nint x;\n", .records = 1, .accepted = true },
        { .source = "#define PRECISE _Pragma(\"float_control(precise, on)\")\nPRECISE\nint x;\n", .records = 1, .accepted = true },
        { .source = "#pragma STDC FENV_ACCESS ON\n#pragma fenv_access(off)\nint x;\n", .records = 1, .accepted = true },
        { .source = "#define fenv_access unused\n#pragma fenv_access(on)\nint x;\n", .accepted = true },
        { .source = "#define fp unused\n#pragma clang fp contract(on)\nint x;\n", .records = 1, .accepted = true },
        { .source = "#define STATE on\n#pragma fenv_access(STATE)\nint x;\n", .accepted = true },
        { .source = "#define on off\n#pragma fenv_access(on)\nint x;\n", .accepted = true },
        { .source = "#define off on\n#pragma fenv_access(off)\nint x;\n", .accepted = true },
        { .source = "#define contract reassociate\n#pragma clang fp contract(off)\nint x;\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "#define fast off\n#pragma clang fp contract(fast)\nint x;\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "#define precise except\n#pragma float_control(precise, on)\nint x;\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "#define on off\n#pragma float_control(precise, on)\nint x;\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "#define on off\n#undef on\n#pragma fenv_access(on)\nint x;\n", .accepted = true },
        { .source = "#pragma fenv_access(ON)\nint x;\n", .accepted = true },
        { .source = "#pragma fenv_access(default)\nint x;\n", .accepted = true },
        { .source = "#pragma fenv_access(on\nint x;\n", .accepted = true },
        { .source = "#pragma fenv_access(on) trailing\nint x;\n", .accepted = true },
        { .source = "#pragma fenv_access(\n#pragma fenv_access(on)\nint x;\n", .accepted = true },
        { .source = "#pragma clang fp contract(unknown)\nint x;\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "#pragma clang fp contract(off\nint x;\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "#pragma clang fp contract(off) reassociate(on)\nint x;\n", .records = 1, .accepted = true },
        { .source = "#pragma clang fp reassociate(on)\nint x;\n", .records = 1, .accepted = true },
        { .source = "#pragma clang fp reciprocal(on) exceptions(strict) contract(fast)\nint x;\n", .records = 1, .accepted = true },
        { .source = "#pragma clang fp exceptions(maytrap)\n#pragma clang fp exceptions(ignore)\nint x;\n", .records = 2, .accepted = true },
        { .source = "#pragma float_control(precise, off)\nint x;\n", .records = 1, .accepted = true },
        { .source = "#pragma float_control(except, on)\nint x;\n", .records = 1, .accepted = true },
        { .source = "#pragma float_control(push)\nint x;\n", .records = 1, .accepted = true },
        { .source = "#pragma float_control(pop)\nint x;\n", .records = 1, .accepted = true },
        { .source = "#pragma float_control(precise, on, push)\nint x;\n", .records = 1, .accepted = true },
        { .source = "#pragma float_control(precise)\n#pragma float_control(except, push)\n#pragma float_control(pop)\nint x;\n", .records = 3, .accepted = true },
        { .source = "#pragma float_control(precise, off)\n#pragma float_control(except, on)\nint x;\n", .message = CHECK_INPUT_DECLARATION },
        { .source = "#pragma float_control(precise, off)\n#pragma STDC FENV_ACCESS ON\nint x;\n", .message = CHECK_INPUT_DECLARATION },
        { .source = "#pragma float_control(except, on)\n#pragma float_control(precise, off)\nint x;\n", .message = CHECK_INPUT_DECLARATION },
        { .source = "#pragma STDC FENV_ACCESS ON\n#pragma float_control(precise, off)\nint x;\n", .message = CHECK_INPUT_DECLARATION },
        { .source = "#pragma clang fp reassociate(on)\n#pragma float_control(except, on)\nint x;\n", .message = CHECK_INPUT_DECLARATION },
        { .source = "#pragma clang fp reciprocal(on)\n#pragma STDC FENV_ACCESS ON\nint x;\n", .message = CHECK_INPUT_DECLARATION },
        { .source = "int f(void){\n#pragma float_control(push)\nreturn 0;}\n", .message = CHECK_INPUT_DECLARATION },
        { .source = "#pragma clang fp eval_method(double)\nint x;\n", .records = 1, .accepted = true },
        // The semantic reader receives resolved options directly; the production driver retains its separate
        // refusal for these global code-generation modes. Pinned Clang probes establish each effective conflict.
        { .source = "#pragma clang fp eval_method(double)\nint x;\n", .first = "-ffast-math", .records = 1,
          .fp_error = CHECK_FP_ERROR_EVAL_OPTIONS, .message = CHECK_INPUT_DECLARATION },
        { .source = "#pragma clang fp eval_method(source)\nint x;\n", .first = "-freciprocal-math", .records = 1,
          .fp_error = CHECK_FP_ERROR_EVAL_RECIPROCAL_OPTION, .message = CHECK_INPUT_DECLARATION },
        { .source = "#pragma clang fp eval_method(double)\nint x;\n", .first = "-fassociative-math", .records = 1, .accepted = true },
        { .source = "#pragma clang fp eval_method(double)\nint x;\n", .first = "-fassociative-math", .second = "-fno-signed-zeros", .records = 1,
          .fp_error = CHECK_FP_ERROR_EVAL_REASSOCIATE_OPTION, .message = CHECK_INPUT_DECLARATION },
        { .source = "#pragma clang fp eval_method(double)\nint x;\n", .first = "-ffast-math", .second = "-fno-fast-math", .records = 1, .accepted = true },
        { .source = "#pragma clang fp eval_method(source)\n#pragma clang fp eval_method(extended)\nint x;\n", .records = 2, .accepted = true },
        { .source = "#pragma clang fp eval_method(double) reassociate(on)\nint x;\n", .records = 1, .accepted = true },
        { .source = "#pragma clang fp reciprocal(on) eval_method(extended)\nint x;\n", .records = 1, .accepted = true },
        { .source = "#pragma clang fp eval_method(double)\n#pragma clang fp reassociate(on)\nint x;\n", .message = CHECK_INPUT_DECLARATION },
        { .source = "#pragma clang fp eval_method(source)\n#pragma clang fp reciprocal(on)\nint x;\n", .message = CHECK_INPUT_DECLARATION },
        { .source = "#pragma float_control(push)\n#pragma clang fp eval_method(double)\n#pragma float_control(pop)\n#pragma clang fp reciprocal(on)\nint x;\n",
          .message = CHECK_INPUT_DECLARATION },
        { .source = "int f(void){\n#pragma clang fp eval_method(double)\nreturn 0;}\n#pragma clang fp reciprocal(on)\nint x;\n", .records = 2, .accepted = true },
        { .source = "#pragma clang fp eval_method(unknown)\nint x;\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "#pragma clang fp reciprocal(on) contract(unknown)\nint x;\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "#define strict ignore\n#pragma clang fp exceptions(strict)\nint x;\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "#pragma float_control(precise on)\nint x;\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "#pragma float_control(precise, on) trailing\nint x;\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "int f(void){int x=0;\n#pragma fenv_access(on)\nreturn x;}\n", .accepted = true },
        { .source = "int f(void){int x=0;\n#pragma clang fp contract(off)\nreturn x;}\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "int f(void){int x=0;\n#pragma float_control(precise, on)\nreturn x;}\n", .message = CHECK_INPUT_PRAGMA_SEMANTICS },
        { .source = "struct S{\n#pragma fenv_access(on)\nint x;};\n", .accepted = true },
        { .source = "int x=\n#pragma fenv_access(on)\n1;\n", .accepted = true },
        { .source = "#pragma GCC unroll 4\nint x;\n", .message = CHECK_INPUT_LOOP_HINT, .loop_text = "invalid '#pragma GCC unroll' directive" },
        { .source = "#pragma GCC unroll 4\nint f(void){return 0;}\n", .message = CHECK_INPUT_LOOP_HINT, .loop_text = "invalid '#pragma GCC unroll' directive" },
        { .source = "int x;\n#pragma GCC unroll 4\n", .message = CHECK_INPUT_LOOP_HINT, .loop_text = "invalid '#pragma GCC unroll' directive" },
        { .source = "#pragma GCC unroll 4\n#pragma clang loop unroll_count(4)\nint x;\n", .message = CHECK_INPUT_LOOP_HINT, .loop_text = "invalid '#pragma GCC unroll' directive" },
        { .source = "#pragma clang loop unroll_count(4)\nint x;\n", .message = CHECK_INPUT_LOOP_HINT, .loop_text = "invalid '#pragma clang loop' directive" },
        { .source = "#pragma unroll 4\nint x;\n", .message = CHECK_INPUT_LOOP_HINT, .loop_text = "invalid '#pragma unroll' directive" },
        { .source = "#pragma nounroll\nint x;\n", .message = CHECK_INPUT_LOOP_HINT, .loop_text = "invalid '#pragma nounroll' directive" },
        { .source = "#pragma unroll_and_jam 4\nint x;\n", .message = CHECK_INPUT_LOOP_HINT, .loop_text = "invalid '#pragma unroll_and_jam' directive" },
        { .source = "#pragma nounroll_and_jam\nint x;\n", .message = CHECK_INPUT_LOOP_HINT, .loop_text = "invalid '#pragma nounroll_and_jam' directive" },
        { .source = "int f(void){\n#pragma GCC unroll 4\nreturn 0;}\n", .message = CHECK_INPUT_LOOP_PLACEMENT,
          .loop_text = "expected a for, while, or do-while loop to follow '#pragma unroll'" },
        { .source = "int f(int n){\n#pragma GCC unroll 4\nfor(int i=0;i<n;i++){}return n;}\n", .accepted = true },
        { .source = "int f(int n){\n#pragma GCC unroll 0\nfor(int i=0;i<n;i++){}return n;}\n", .accepted = true },
        { .source = "int f(int n){\n#pragma GCC unroll 1\nfor(int i=0;i<n;i++){}return n;}\n", .accepted = true },
    };
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(fixtures); fixture += 1)
    {
        const CheckPragmaExtensionFixture* expected = fixtures + fixture;
        uint8_t floats[3] = { 0 };
        PPOptions configured = options;
        configured.predefines.floats = floats;
        configured.predefines.float_capacity = BUSTER_ARRAY_LENGTH(floats);
        configured.semantic_pragmas = expected->first != NULL;
        bool ready = expected->first == NULL || predefines_option(&configured.predefines, expected->first, NULL) == PREDEFINES_PARSE_TAKEN;
        ready = ready && (expected->second == NULL || predefines_option(&configured.predefines, expected->second, NULL) == PREDEFINES_PARSE_TAKEN);
        ready = ready && intern_initialize(&identifiers) && pp_initialize(&pp, &configured, &identifiers);
        ready = ready && pp_run_source(&pp, "pragma-extensions.c", (const uint8_t*)expected->source, strlen(expected->source));
        check_test_expect(test, ready, "pragma extension source fixture preprocesses");
        for (uint32_t schedule = 0; ready && schedule < 4; schedule += 1)
        {
            CheckTestTypes types = { 0 };
            Arena arena = arena_reserve(UINT64_C(1) << 24);
            Arena scratch = arena_reserve(UINT64_C(1) << 24);
            const ParseOptions parse_options = { .scalar = (schedule & 1) != 0, .tree_only = (schedule & 2) != 0 };
            const bool parsed = check_test_types_open(&types) && arena.base != 0 && scratch.base != 0 &&
                                parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
            CheckInput input = { 0 };
            const bool adapted = parsed && check_input(&input, &parser, &pp, &types.table, &arena, &scratch);
            const bool accepted = parsed && !input.resource && adapted && input.report_count == 0;
            const bool rejected = parsed && !input.resource && input.report_count != 0 && input.reports[0].message == expected->message;
            const uint32_t ms_records = input.ms_pragmas != NULL ? input.ms_pragmas->count : 0;
            check_test_expect(test, expected->accepted ? accepted && input.fp_pragma_count == expected->records && ms_records == expected->ms_records : rejected, expected->source);
            if (expected->first != NULL)
            {
                check_test_expect(test, input.fp_pragma_count == 1 && input.fp_pragmas[0].error == expected->fp_error &&
                                       (expected->fp_error == 0 || input.fp_pragmas[0].error_source == parser.tokens.pragma_sources[0] + 3),
                                  "effective global floating permissions retain the exact evaluation conflict and source");
            }
            if (expected->loop_text != NULL)
            {
                check_test_expect(test, rejected && input.loop_diagnostic != NULL && input.loop_diagnostic->pragma == 1 &&
                                       input.loop_diagnostic->message == expected->message && strcmp(input.loop_diagnostic->text, expected->loop_text) == 0,
                                  "loop placement diagnostics preserve the original pragma family and earliest source order");
                if (expected->message == CHECK_INPUT_LOOP_HINT)
                {
                    check_test_expect(test, input.loop_diagnostic != NULL &&
                                                 input.loop_diagnostic->source ==
                                                     parser.tokens.pragma_sources[input.loop_diagnostic->pragma - 1] + 1,
                                      "file-scope loop diagnostics point at the directive");
                }
            }
            parse_release(&parser);
            check_test_types_close(&types);
            arena_release(&scratch);
            arena_release(&arena);
        }
        pp_release(&pp);
        intern_release(&identifiers);
    }
}

static void check_test_pragma_extensions(CheckTest* test)
{
    check_test_pragma_extension_history(test);
    check_test_ms_pragma_history(test);
    check_test_symbol_pragmas(test);
    check_test_pragma_extension_sources(test);
}
