// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original runtime malformed-call fixtures (#2406): complete diagnostics and status against Clang, including callee
// lookup, availability, unread arguments, surviving operands and later statements. Rejected output stays unchanged.

// One mixed source crosses resumed, prefix, repaired and rejected views. Its parallel source/use columns must stay
// paired, including repeated macro expansions and the calls outside a prefix. Other runtime sources guard the scope.
static void check_test_malformed_call_views(CheckTest* test)
{
    static const uint32_t cases[] =
    {
        14,
        15,
        16,
        17,
        18,
        19,
        28,
        29,
        30,
        31,
        35,
        36,
        37,
        40,
        41,
        42,
        43,
        44,
        45,
        46,
        49,
        51,
        52,
        53,
        54,
        55,
        56,
        57,
        58,
        59,
        60,
        61,
        62,
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(cases); fixture += 1)
    {
        const uint32_t c = cases[fixture];
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        char define[64];
        snprintf(define, sizeof(define), "DBUSTER_CASE=%u", c);
        const char* macros[] = { define };
        const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true, .macros = macros, .macro_count = 1 };
        Arena output = arena_reserve(UINT64_C(1) << 22);
        Arena scratch = arena_reserve(UINT64_C(1) << 22);
        const bool ready = output.base != NULL && scratch.base != NULL && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                           pp_run(&pp, "tests/checker/parse-recovery/malformed-calls.c");
        check_test_expect(test, ready, "malformed calls: view and boundary fixture initializes");
        for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
        {
            const ParseOptions parse_options = { .scalar = scalar != 0 };
            const bool initialized = parse_initialize(&parser, &parse_options, options.language);
            const bool parsed = initialized && parse_translation_unit(&parser, &pp);
            CheckDriverTypoEdits edits = { .table = &identifiers, .scratch = &scratch };
            const CheckPrefixTails tails = { .end = check_driver_typo_tail, .context = &edits };
            CheckResumed resumed = { 0 };
            const CheckPrefixStatus status = initialized ? check_prefix_resume(&resumed, &parser, NULL, 0, &tails, &output, &scratch) : CHECK_PREFIX_INVALID;
            if (c == 14)
            {
                static const uint8_t values[] = { 0, 1, 0, 1, 1, 0 };
                const Parser* view = &resumed.parser;
                bool metadata = !parsed && status == CHECK_PREFIX_READY && view->invalid_call_count == BUSTER_ARRAY_LENGTH(values) &&
                                view->invalid_call_sources != NULL && view->invalid_call_values != NULL &&
                                memcmp(view->invalid_call_values, values, sizeof(values)) == 0;
                for (uint32_t i = 1; metadata && i < view->invalid_call_count; i += 1)
                {
                    metadata = view->invalid_call_sources[i - 1] < view->invalid_call_sources[i];
                }
                check_test_expect(test, metadata, "malformed calls: mixed macro sources retain ordered, paired classification/value flags through resume rounds");
                CheckPrefix prefix;
                const bool prefixed = metadata && check_prefix_make(&prefix, view, &output, &scratch) == CHECK_PREFIX_READY;
                check_test_expect(test, prefixed && prefix.parser.invalid_call_sources == view->invalid_call_sources &&
                                        prefix.parser.invalid_call_values == view->invalid_call_values && prefix.parser.invalid_call_count == view->invalid_call_count,
                                  "malformed calls: prefix borrows both complete invalid-call columns");
                CheckRepair repair;
                const bool repaired = metadata && check_repair_make(&repair, view, &pp, &output, &scratch) == CHECK_REPAIR_READY;
                check_test_expect(test, repaired && repair.parser.invalid_call_sources == view->invalid_call_sources &&
                                        repair.parser.invalid_call_values == view->invalid_call_values && repair.parser.invalid_call_count == view->invalid_call_count,
                                  "malformed calls: repaired view borrows both invalid-call columns");
                uint8_t* kept = prefixed ? check_prefix_column(&output, prefix.end, sizeof(uint8_t)) : NULL;
                CheckDriverRejectedView rejected = { 0 };
                const bool rechecked = kept != NULL && check_driver_rejected_view(&rejected, &prefix.parser, kept, NULL, &output, &scratch);
                check_test_expect(test, rechecked && rejected.parser.invalid_call_sources == view->invalid_call_sources &&
                                        rejected.parser.invalid_call_values == view->invalid_call_values && rejected.parser.invalid_call_count == view->invalid_call_count,
                                  "malformed calls: rejected view borrows both invalid-call columns");
            }
            else if ((c >= 31 && c <= 40) || c == 15 || c == 17 || c == 18 || c == 44 || c == 45 || c == 49 || c == 51 || c == 52 ||
                     (c >= 53 && c <= 59))
            {
                const Parser* view = &resumed.parser;
                const uint32_t calls = c == 36 || c == 37 ? 2 : 1;
                bool metadata = !parsed && status == CHECK_PREFIX_READY && view->invalid_call_count == calls &&
                                view->invalid_call_sources != NULL && view->invalid_call_values != NULL;
                for (uint32_t i = 0; metadata && i < calls; i += 1)
                {
                    metadata = view->invalid_call_values[i] == (c == 55 ? 0 : 1) &&
                               (i == 0 || view->invalid_call_sources[i - 1] < view->invalid_call_sources[i]);
                }
                check_test_expect(test, metadata, "malformed calls: earlier arguments, catalog calls and repeated/nested repairs keep unique ordered callee sources and value flags");
                if (c == 15 || c == 45 || c == 49 || c == 51 || c == 52)
                {
                    uint32_t original_errors = 0;
                    for (uint32_t d = 0; metadata && d < resumed.diagnostic_count; d += 1)
                    {
                        const ParseDiagnostic* diagnostic = resumed.diagnostics + d;
                        original_errors += diagnostic->resume_call_semi && diagnostic->call_statement &&
                                           diagnostic->message == PARSE_MESSAGE_EXPECTED_EXPRESSION && diagnostic->token < parser.tokens.count &&
                                           parser.tokens.kinds[diagnostic->token] == PARSE_KIND_TYPE;
                    }
                    bool closer = false;
                    for (uint32_t t = 0; metadata && t + 1 < view->tokens.count; t += 1)
                    {
                        closer |= view->tokens.kinds[t] == PARSE_KIND_R_PAREN && view->tokens.kinds[t + 1] == PARSE_KIND_SEMI &&
                                  view->tokens.sources[t] == view->tokens.sources[t + 1];
                    }
                    check_test_expect(test, metadata && original_errors == 1 && closer,
                                      "malformed calls: early semicolon keeps one original argument error and the synthetic closer's source");
                }
            }
            else
            {
                check_test_expect(test, !parsed && status != CHECK_PREFIX_RESOURCE && status != CHECK_PREFIX_INVALID && resumed.parser.invalid_call_count == 0,
                                  "malformed calls: nested semicolon calls, mismatched delimiters, members and empty arguments keep existing recovery");
            }
            parse_release(&parser);
            arena_pop_to(&scratch, 0);
            arena_pop_to(&output, 0);
        }
        pp_release(&pp);
        intern_release(&identifiers);
        arena_release(&scratch);
        arena_release(&output);
    }
}

static void check_test_driver_malformed_calls(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_malformed_call_views(test);
    CheckTestFixtures files = { .storage = arena_reserve(CHECK_TEST_FIXTURE_BYTES + 4096) };
    uint64_t length = 0;
    char* catalog = files.storage.base != NULL ? check_test_fixture_read(&files, "tests/checker/parse-recovery/malformed-calls.cases", &length) : NULL;
    const char* source = "tests/checker/parse-recovery/malformed-calls.c";
    const bool available = catalog != NULL && access(source, R_OK) == 0;
    check_test_expect(test, available, "malformed calls: runtime source and cases available");
    static const char* const standards[] = { "-std=gnu89", "-std=gnu17", "-std=gnu23" };
    static const char* const policies[] = { "-Wall", "-Werror", "-w", "-Wno-implicit-function-declaration", "-fno-builtin", "-ferror-limit=2" };
    static const char sentinel[] = "preserved malformed-call output\n";
    enum { LINES = 32768 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char object[PATH_MAX];
    char saved[PATH_MAX];
    snprintf(object, sizeof(object), "%s/malformed-call.o", directory);
    snprintf(saved, sizeof(saved), "%s/malformed-call.saved", directory);
    const bool prepared = parse_test_write(saved, sentinel, sizeof(sentinel) - 1) && expected_lines != NULL && actual_lines != NULL;
    check_test_expect(test, prepared, "malformed calls: output and diagnostic buffers available");
    char* cursor = NULL;
    char* line = available ? strtok_r(catalog, "\n", &cursor) : NULL;
    uint32_t fixtures = 0;
    bool valid = available && prepared;
    for (; valid && line != NULL; line = strtok_r(NULL, "\n", &cursor))
    {
        if (line[0] == '#')
        {
            continue;
        }
        uint32_t id = 0;
        uint32_t later_errors = 0;
        uint32_t accepted = 0;
        uint32_t languages = 0;
        char extra;
        valid = sscanf(line, "%u %u %u %u %c", &id, &later_errors, &accepted, &languages, &extra) == 4 && accepted <= 1 && languages > 0 && languages < 8;
        check_test_expect(test, valid, "malformed calls: runtime case metadata is valid");
        char define[64];
        snprintf(define, sizeof(define), "-DBUSTER_CASE=%u", id);
        fixtures += valid;
        for (uint32_t s = 0; valid && s < BUSTER_ARRAY_LENGTH(standards); s += 1)
        {
            if ((languages & (1u << s)) == 0)
            {
                continue;
            }
            for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(policies); p += 1)
            {
                char* reference[] = { (char*)clang, (char*)standards[s], "-fsyntax-only", "-fno-caret-diagnostics", (char*)policies[p],
                                      define, (char*)source, NULL };
                char* expected = NULL;
                const int oracle = check_test_driver_spawn(reference, &expected);
                const bool read = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
                uint32_t later = 0;
                const char* found = read ? expected_lines : "";
                while ((found = strstr(found, "undeclared identifier 'after_")) != NULL)
                {
                    later += 1;
                    found += 1;
                }
                check_test_expect(test, read && oracle == (accepted ? 0 : 1) &&
                                        (p + 1 == BUSTER_ARRAY_LENGTH(policies) || later == later_errors),
                                  "malformed calls: clang status and independent later-error count");
                for (uint32_t mode = 0; read && mode < 6; mode += 1)
                {
                    const bool compile = mode >= 2;
                    char* arguments[20] = { (char*)executable, (char*)standards[s], compile ? "-c" : "-fsyntax-only",
                                            "-fno-caret-diagnostics", (char*)policies[p], define, (char*)source };
                    uint32_t count = 7;
                    if ((mode & 1) != 0)
                    {
                        arguments[count++] = "--scalar";
                    }
                    bool output_ready = true;
                    if (compile)
                    {
                        arguments[count++] = "-o";
                        arguments[count++] = object;
                        arguments[count++] = "--lanes";
                        arguments[count++] = mode >= 4 ? "3" : "1";
                        if (mode >= 4)
                        {
                            arguments[count++] = "--deferred-bodies";
                        }
                        unlink(object);
                        output_ready = parse_test_write(object, sentinel, sizeof(sentinel) - 1);
                    }
                    arguments[count] = NULL;
                    char* actual = NULL;
                    const int status = output_ready ? check_test_driver_spawn(arguments, &actual) : -1;
                    actual_lines[0] = 0;
                    const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(actual_lines, expected_lines) == 0;
                    bool output = !compile || check_test_driver_same_files(saved, object);
                    if (compile && accepted)
                    {
                        uint64_t bytes_count = 0;
                        uint8_t* bytes = check_test_driver_read_aligned(object, &bytes_count);
                        const Elf64_Ehdr* header = (const Elf64_Ehdr*)bytes;
                        output = bytes != NULL && bytes_count >= sizeof(*header) && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 &&
                                 header->e_ident[EI_CLASS] == ELFCLASS64 && header->e_type == ET_REL && header->e_machine == EM_X86_64;
                        free(bytes);
                    }
                    char description[8192];
                    snprintf(description, sizeof(description), "malformed calls %u, %s %s, mode %u: status %d (clang %d), lines\n%s(clang\n%s)",
                             id, standards[s], policies[p], mode, status, oracle, actual_lines, expected_lines);
                    check_test_expect(test, same && status == oracle && output, description);
                    free(actual);
                }
                free(expected);
            }
        }
    }
    check_test_expect(test, fixtures != 0, "malformed calls: runtime case list is nonempty");
    unlink(object);
    unlink(saved);
    free(expected_lines);
    free(actual_lines);
    arena_release(&files.storage);
}
