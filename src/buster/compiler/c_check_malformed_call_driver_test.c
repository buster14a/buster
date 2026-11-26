// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original runtime malformed-call fixtures (#2406): complete diagnostics and status against Clang, including callee
// lookup, availability, unread arguments, surviving operands and later statements. Rejected output stays unchanged.

// One mixed source crosses resumed, prefix, repaired and rejected views. Its parallel source/use columns must stay
// paired, including repeated macro expansions and the calls outside a prefix. Other runtime sources guard the scope.
static void check_test_malformed_call_views(CheckTest* test)
{
    char source[PATH_MAX];
    const bool named = check_test_fixture_path(source, sizeof(source), "checker/parse-recovery/malformed-calls/malformed-calls.c");
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
        const bool ready = named && output.base != NULL && scratch.base != NULL && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                           pp_run(&pp, source);
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
                                  parse_source_index(&view->tokens, t) == parse_source_index(&view->tokens, t + 1);
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

typedef struct CheckMalformedCallCase CheckMalformedCallCase;
struct CheckMalformedCallCase
{
    const char* source;
    uint32_t id;
    uint32_t later_errors;
    uint32_t accepted;
    uint32_t languages;
};

// This suite keeps its independent later-error counts and language masks as data too. Validate every row/file before
// sharding, so a malformed row or an unregistered source cannot disappear in a part that would not execute it.
static bool check_test_malformed_calls_load(CheckTestFixtures* files, CheckMalformedCallCase** cases)
{
    *files = (CheckTestFixtures){ .storage = arena_reserve(UINT64_C(1) << 25) };
    *cases = arena_push(&files->storage, sizeof(**cases) * CHECK_TEST_FIXTURE_CASES, 8);
    files->files = arena_push(&files->storage, sizeof(*files->files) * CHECK_TEST_FIXTURE_CASES, 8);
    char folder[PATH_MAX];
    char path[PATH_MAX + 132];
    const bool named = check_test_fixture_path(folder, sizeof(folder), "checker/parse-recovery/malformed-calls") &&
                       test_fixture_path(path, sizeof(path), folder, "catalog.tsv");
    uint64_t length = 0;
    char* catalog = named ? check_test_fixture_read(files, path, &length) : NULL;
    bool valid = *cases != NULL && files->files != NULL && catalog != NULL && length != 0 && catalog[length - 1] == '\n';
    const char* header = valid ? strsep(&catalog, "\n") : "";
    valid = valid && strcmp(header, "buster-fixtures v1 malformed-calls") == 0;
    uint32_t line = 1;
    while (valid && *catalog != 0)
    {
        char* row = strsep(&catalog, "\n");
        line += 1;
        if (row[0] == '#')
        {
            continue;
        }
        char* fields[5] = { 0 };
        valid = check_test_fixture_columns(row, fields, BUSTER_ARRAY_LENGTH(fields)) && files->count < CHECK_TEST_FIXTURE_CASES;
        CheckMalformedCallCase* fixture = valid ? *cases + files->count : NULL;
        if (valid)
        {
            *fixture = (CheckMalformedCallCase){ 0 };
            const size_t size = strlen(fields[1]);
            valid = check_test_fixture_u32(fields[0], &fixture->id) && check_test_fixture_word(fields[1], 128) &&
                    size > 2 && strcmp(fields[1] + size - 2, ".c") == 0 &&
                    check_test_fixture_u32(fields[2], &fixture->later_errors) &&
                    check_test_fixture_u32(fields[3], &fixture->accepted) && fixture->accepted <= 1 &&
                    check_test_fixture_u32(fields[4], &fixture->languages) && fixture->languages > 0 && fixture->languages < 8;
            for (uint32_t i = 0; valid && i < files->count; i += 1)
            {
                valid = fixture->id != (*cases)[i].id;
                if (strcmp(fields[1], files->files[i]) == 0)
                {
                    fixture->source = (*cases)[i].source;
                }
            }
            if (valid && fixture->source == NULL)
            {
                valid = test_fixture_path(path, sizeof(path), folder, fields[1]);
                uint64_t source_length = 0;
                valid = valid && check_test_fixture_read_bytes(files, path, true, &source_length) != NULL;
                const size_t path_length = strlen(path) + 1;
                char* stored = valid ? arena_push(&files->storage, path_length, 1) : NULL;
                valid = stored != NULL;
                if (valid)
                {
                    memcpy(stored, path, path_length);
                    fixture->source = stored;
                }
            }
            if (valid)
            {
                files->files[files->count++] = fields[1];
            }
        }
    }
    valid = valid && files->count != 0 && check_test_fixture_orphans(files, folder);
    if (!valid)
    {
        fprintf(stderr, "malformed-calls/catalog.tsv line %u: %s\n", line, files->problem[0] ? files->problem : "invalid or duplicate case metadata");
    }
    return valid;
}

// A long sweep (32 s on a 4-CPU Xeon VM, #3399): the views' check and each case are units of test-checker-driver's deal
// (check_test_driver_takes).
static void check_test_driver_malformed_calls(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    if (check_test_driver_takes())
    {
        check_test_malformed_call_views(test);
    }
    CheckTestFixtures files;
    CheckMalformedCallCase* cases = NULL;
    const bool available = check_test_malformed_calls_load(&files, &cases);
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
    const bool valid = available && prepared;
    for (uint32_t fixture = 0; valid && fixture < files.count; fixture += 1)
    {
        const uint32_t id = cases[fixture].id;
        const uint32_t later_errors = cases[fixture].later_errors;
        const uint32_t accepted = cases[fixture].accepted;
        const uint32_t languages = cases[fixture].languages;
        const char* source = cases[fixture].source;
        char define[64];
        snprintf(define, sizeof(define), "-DBUSTER_CASE=%u", id);
        if (!check_test_driver_takes())
        {
            continue;
        }
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
    unlink(object);
    unlink(saved);
    free(expected_lines);
    free(actual_lines);
    arena_release(&files.storage);
}
