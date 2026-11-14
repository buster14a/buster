// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Attributed block declarations with no type in C99 through C17 (#2332): the original parser errors, Sema's declaration diagnostics and
// notes, initializer expressions and later independent errors all survive recovery. Compare complete ordered diagnostic
// lines with the installed Clang, and prove that rejected -c requests preserve an existing output or create none.
// C23 has no implicit-int recovery: its unsupported cases retain the exact pre-change parser diagnostics below.
// GNU89's accepted typed control, like the other standards', is compared with Clang and compiled and run.

// A ready prefix can become a rejected-warning view later (#2748). Its borrowed metadata must be complete; reparsing
// allocates a separate mutable meaning table and leaves the original table and every kept source origin intact.
static void check_test_driver_prefix_meanings(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parse_options = { .scalar = true };
    Arena output = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    const bool ready = output.base != NULL && scratch.base != NULL && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                       pp_run(&pp, "tests/driver/prefix_meanings.c") && parse_initialize(&parser, &parse_options, options.language);
    check_test_expect(test, ready, "prefix reparse fixture initializes");
    if (ready)
    {
        const bool parsed = parse_translation_unit(&parser, &pp);
        CheckPrefix prefix;
        const bool made = !parsed && check_prefix_make(&prefix, &parser, &output, &scratch) == CHECK_PREFIX_READY;
        const bool metadata = made && prefix.parser.meanings.capacity != 0 && prefix.parser.meanings.capacity == parser.meanings.capacity &&
                              prefix.parser.meanings.meanings == parser.meanings.meanings && prefix.parser.meanings.table == parser.meanings.table &&
                              memcmp(prefix.parser.meanings.custom_attributes, parser.meanings.custom_attributes, sizeof(parser.meanings.custom_attributes)) == 0;
        check_test_expect(test, metadata, "a ready prefix borrows the complete original ParseMeanings metadata");
        uint8_t* saved = metadata ? BUSTER_ARENA_PUSH_ARRAY(&output, uint8_t, parser.meanings.capacity) : NULL;
        uint8_t* edits = metadata ? check_prefix_column(&output, prefix.end, sizeof(uint8_t)) : NULL;
        if (saved != NULL && edits != NULL)
        {
            memcpy(saved, parser.meanings.meanings, parser.meanings.capacity);
            CheckDriverRejectedView view = { 0 };
            const bool reparsed = check_driver_rejected_view(&view, &prefix.parser, edits, NULL, &output, &scratch);
            bool own = reparsed && view.parser.meanings.meanings != parser.meanings.meanings &&
                       view.parser.meanings.capacity == parser.meanings.capacity && view.parser.meanings.table == parser.meanings.table &&
                       memcmp(view.parser.meanings.custom_attributes, parser.meanings.custom_attributes, sizeof(parser.meanings.custom_attributes)) == 0 &&
                       memcmp(saved, parser.meanings.meanings, parser.meanings.capacity) == 0 && view.parser.tokens.count == prefix.end;
            for (uint32_t token = 0; own && token < prefix.end; token += 1)
            {
                own = view.to_original[token] == token && view.to_view[token] == token &&
                      view.parser.tokens.sources[token] == prefix.parser.tokens.sources[token];
            }
            check_test_expect(test, own, "a rejected view owns its mutable meanings and preserves the original table and origin maps");
        }
        check_test_expect(test, saved != NULL && edits != NULL, "prefix reparse contract storage initializes");
    }
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&scratch);
    arena_release(&output);
}

// Mismatched closers are not balanced suffixes, and a brace must never be erased as a parenthesis's closer. These
// parser-level checks cover the mixed-brace case whose later Clang diagnostics are outside this bounded repair.
static void check_test_attribute_declaration_boundaries(CheckTest* test)
{
    for (uint32_t c = 14; c <= 16; c += 1)
    {
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        char define[64];
        snprintf(define, sizeof(define), "DBUSTER_CASE=%u", c);
        const char* macros[] = { define };
        const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true, .macros = macros, .macro_count = 1 };
        const bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) && pp_run(&pp, "tests/driver/attribute_declarations.c");
        check_test_expect(test, ready, "mixed-delimiter attributed declaration fixture initializes");
        for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
        {
            const ParseOptions parse_options = { .scalar = scalar != 0 };
            const bool initialized = parse_initialize(&parser, &parse_options, options.language);
            const bool parsed = initialized && parse_translation_unit(&parser, &pp);
            bool refused = false;
            for (uint32_t d = 0; initialized && d < parser.diagnostic_count; d += 1)
            {
                const ParseDiagnostic* diagnostic = parser.diagnostics + d;
                if (diagnostic->message == PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION)
                {
                    refused = !diagnostic->resumed && diagnostic->resume_end == 0;
                }
            }
            check_test_expect(test, initialized && !parsed && refused, "mismatched delimiters and braces never enable attributed suffix recovery");
            parse_release(&parser);
        }
        pp_release(&pp);
        intern_release(&identifiers);
    }
}

// Frozen C23 parser diagnostics from c471f32f0. These are an unchanged-behavior control, not Clang parity: C23's
// invalid-declaration recovery differs from pre-C23 implicit int and is outside this fix.
static bool check_test_attribute_c23_baseline(uint32_t c, const char* source, char* text, uint32_t capacity)
{
    static const uint16_t places[][4] = {
        { 10, 30, 32, 0 }, { 17, 30, 0, 0 }, { 24, 30, 0, 0 }, { 32, 31, 34, 0 },
        { 39, 30, 0, 0 }, { 47, 36, 0, 0 }, { 54, 38, 0, 0 }, { 61, 30, 0, 62 },
        { 69, 30, 0, 0 }, { 76, 30, 32, 0 }, { 83, 30, 32, 0 }, { 88, 30, 32, 0 },
        { 93, 30, 32, 0 }, { 0, 0, 0, 0 }, { 107, 30, 36, 0 }, { 112, 30, 36, 0 },
    };
    const bool bounded = c < BUSTER_ARRAY_LENGTH(places) && places[c][0] != 0;
    int length = bounded ? snprintf(text, capacity, "%s:%u:%u: error: a type specifier is required for all declarations\n", source, places[c][0], places[c][1]) : -1;
    bool valid = length > 0 && (uint32_t)length < capacity;
    if (valid && places[c][2] != 0)
    {
        const int next = snprintf(text + length, capacity - (uint32_t)length, "%s:%u:%u: error: expected ';' at end of declaration\n", source, places[c][0], places[c][2]);
        valid = next > 0 && (uint32_t)next < capacity - (uint32_t)length;
    }
    if (valid && places[c][3] != 0)
    {
        const int next = snprintf(text + length, capacity - (uint32_t)length, "%s:%u:%u: error: a type specifier is required for all declarations\n", source, places[c][3], places[c][1]);
        valid = next > 0 && (uint32_t)next < capacity - (uint32_t)length;
    }
    return valid;
}

static bool check_test_attribute_declaration_output(const char* path, const char* sentinel, bool accepted)
{
    const int descriptor = open(path, O_RDONLY);
    bool valid = descriptor >= 0;
    if (valid && accepted)
    {
        unsigned char magic[4] = { 0 };
        valid = read(descriptor, magic, sizeof(magic)) == sizeof(magic) && memcmp(magic, "\177ELF", sizeof(magic)) == 0;
    }
    else if (valid)
    {
        char* contents = NULL;
        valid = check_test_driver_read_file(descriptor, &contents) && contents != NULL && strcmp(contents, sentinel) == 0;
        free(contents);
    }
    if (descriptor >= 0)
    {
        close(descriptor);
    }
    return valid;
}

static void check_test_driver_attribute_declarations(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_prefix_meanings(test);
    check_test_attribute_declaration_boundaries(test);
    const char* source = "tests/driver/attribute_declarations.c";
    const bool available = access(source, R_OK) == 0;
    check_test_expect(test, available, "attributed declaration fixture is available (run from the repository root)");
    static const char* const standards[] = { "-std=gnu99", "-std=gnu17", "-std=gnu23", "-std=gnu89" };
    static const char* const options[] = { NULL, "-Wall", "-w", "-Werror", "-Werror=ignored-attributes", "-Wno-ignored-attributes", "-ferror-limit=2" };
    static const char* const lanes[] = { NULL, NULL, NULL, NULL, "1", "2", "3" };
    static const char sentinel[] = "preserve the existing declaration-recovery output\n";
    enum { CASES = 16, CONTROL = 13, LINES = 12288 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    check_test_expect(test, expected_lines != NULL && actual_lines != NULL, "attributed declaration diagnostic buffers initialize");
    char object[PATH_MAX];
    char binary[PATH_MAX];
    snprintf(object, sizeof(object), "%s/attribute-declaration.o", directory);
    snprintf(binary, sizeof(binary), "%s/attribute-declaration", directory);
    for (uint32_t c = 0; available && expected_lines != NULL && actual_lines != NULL && c < CASES; c += 1)
    {
        char define[64];
        snprintf(define, sizeof(define), "-DBUSTER_CASE=%u", c);
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(standards); s += 1)
        {
            if (s == 3 && c != CONTROL)
            {
                continue;
            }
            for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(options); o += 1)
            {
                char* arguments[16] = { (char*)clang, (char*)standards[s], "-fsyntax-only", "-fno-caret-diagnostics", "-ferror-limit=0", (char*)source, define };
                uint32_t count = 7;
                if (options[o] != NULL)
                {
                    arguments[count++] = (char*)options[o];
                }
                arguments[count] = NULL;
                char* expected = NULL;
                const int oracle = check_test_driver_spawn(arguments, &expected);
                bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
                check_test_expect(test, lines && (oracle == 0) == (c == CONTROL), "attributed declaration oracle status and diagnostic lines");
                const bool baseline = s == 2 && c != CONTROL;
                if (baseline)
                {
                    lines = check_test_attribute_c23_baseline(c, source, expected_lines, LINES);
                }
                for (uint32_t mode = 0; lines && mode < BUSTER_ARRAY_LENGTH(lanes); mode += 1)
                {
                    arguments[0] = (char*)executable;
                    arguments[2] = mode >= 2 ? "-c" : "-fsyntax-only";
                    uint32_t at = count;
                    if (mode == 1 || mode == 3)
                    {
                        arguments[at++] = "--scalar";
                    }
                    if (lanes[mode] != NULL)
                    {
                        arguments[at++] = "--deferred-bodies";
                        arguments[at++] = "--lanes";
                        arguments[at++] = (char*)lanes[mode];
                    }
                    bool output = true;
                    if (mode >= 2)
                    {
                        arguments[at++] = "-o";
                        arguments[at++] = object;
                        output = parse_test_write(object, sentinel, sizeof(sentinel) - 1);
                    }
                    arguments[at] = NULL;
                    char* actual = NULL;
                    const int status = check_test_driver_spawn(arguments, &actual);
                    const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
                    output = output && (mode < 2 || check_test_attribute_declaration_output(object, sentinel, c == CONTROL));
                    char description[8192];
                    snprintf(description, sizeof(description), "attributed declaration %u, %s %s, mode %u: status %d (clang %d), output %u, lines\n%s(%s\n%s)",
                             c, standards[s], options[o] != NULL ? options[o] : "", mode, status, oracle, output, actual_lines,
                             baseline ? "unchanged C23 baseline" : "clang", expected_lines);
                    check_test_expect(test, same && status == oracle && output, description);
                    free(actual);
                    if (c == CONTROL && o == 0 && mode >= 2 && status == 0 && output)
                    {
                        char* linked = NULL;
                        char* link[] = { (char*)clang, object, "-o", binary, NULL };
                        const int link_status = check_test_driver_spawn(link, &linked);
                        free(linked);
                        char* ran = NULL;
                        char* run[] = { binary, NULL };
                        const int run_status = link_status == 0 ? check_test_driver_spawn(run, &ran) : -1;
                        free(ran);
                        check_test_expect(test, link_status == 0 && run_status == 0, "attributed declaration control links and preserves grouped/pointer values");
                    }
                }
                free(expected);
            }
        }
    }
    // A rejected request also leaves a previously absent path absent, across both readers and all deferred lane counts.
    for (uint32_t mode = 2; available && mode < BUSTER_ARRAY_LENGTH(lanes); mode += 1)
    {
        unlink(object);
        char* arguments[14] = { (char*)executable, "-std=gnu17", "-c", (char*)source, "-DBUSTER_CASE=0", "-o", object };
        uint32_t at = 7;
        if (mode == 3)
        {
            arguments[at++] = "--scalar";
        }
        if (lanes[mode] != NULL)
        {
            arguments[at++] = "--deferred-bodies";
            arguments[at++] = "--lanes";
            arguments[at++] = (char*)lanes[mode];
        }
        arguments[at] = NULL;
        char* actual = NULL;
        const int status = check_test_driver_spawn(arguments, &actual);
        free(actual);
        check_test_expect(test, status == 1 && access(object, F_OK) != 0, "an attributed declaration parse failure creates no output");
    }
    free(actual_lines);
    free(expected_lines);
    unlink(binary);
    unlink(object);
}
