// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-check-debug [--seed N] [--programs N] [--cross-check] [--keep]: the C driver's source rows and DWARF (#395,
// c_check_debug.c).
//
// - Raw rows: check_debug_rows against check_debug_rows_scalar on random functions (their entries' first records
//   ascending, with runs of entries that start no code, and each record's offset after the one before), of every entry
//   count to 130 and larger ones, with each column ending at a PROT_NONE page: the same rows, their offsets ascending
//   and each below the function's length.
// - Kept rows: check_debug_keep against check_debug_keep_scalar on random rows (the function's file, others and none,
//   lines in runs, some past INT32_MAX, the body's start at one of them or none), of every count to 130 and larger,
//   ending at PROT_NONE pages.
// - Lines: fixtures (a header's function, macros, a multi-line call, macros nested past the vector path's walk, an
//   #include and a #line inside a body, a #line before a function, two #lines naming one file around a function's
//   statements, a computed goto through a static table of labels' addresses) and --programs generated programs of macros across
//   lines and #lines, compiled through the pipeline with source rows by each path: every row's file and line by the
//   vector path and the exact path alike, the exact path's as pp_presumed gives them, each function's rows within its
//   lines where nothing moves them, both paths' units and sections the same bytes, and the fixtures' functions with
//   the lines their statements are on, in their files, and none of a macro's definition or a blank line. Each program's
//   functions lowered with the entries' source tokens and without them, by both unit ABI batches: the same typed
//   functions, each entry's token alike by both batches and the function's own, and each call's its callee's.
// - A unit whose selection keeps no function: emitted with source rows into an empty set, a unit of no functions, by
//   both paths alike; with --cross-check, linked with -g against a shared library holding its main (cc), into an
//   executable that runs, whose DWARF llvm-dwarfdump --verify takes.
// - Options that fail before they're parsed (a -l without its name), on a link with -g: the usage message, and status 1.
// - The driver, with --cross-check (gdb and llvm-dwarfdump): the fixture linked with -g by each path into the same
//   bytes, which run; llvm-dwarfdump --verify finds no error; gdb stops at a line's breakpoint on that line, at a
//   function's breakpoint on its first statement, steps over an untaken branch, steps from a computed goto to its
//   target's statement, and gives a backtrace's frames their functions, files and lines, the header's function in the
//   header.
// - Several units, with --cross-check (#779): two units, each with a static function of one name, calling each other's
//   functions, linked with -g by --lanes 1 and 8 and by each path into the same bytes, which run; llvm-dwarfdump
//   --verify finds no error; gdb stops at each unit's functions and lines, in their own files, with backtraces across
//   the units. And a third unit's weak function, which the second's strong one replaces: its DWARF describes its own
//   code, so that its line's breakpoint is elsewhere than the strong one's and never stops the run (while the checker
//   refuses the weak attribute, before #758, the run says so and leaves the third unit out).
// - Objects, with --cross-check (#395's -c -g): the fixture's object by each path into the same bytes, whose five DWARF
//   sections are the -g executable's but at their relocations' fields (zero in the object), and which llvm-dwarfdump
//   --verify finds no error in; linked by cc through GNU ld and through lld, and by link-foreign from GCC's driver's
//   line, into programs that run, whose DWARF llvm-dwarfdump and gdb read as they read the -g executable's.
// - Unwind tables, with --cross-check: c_check_unwind_test.c's.

typedef struct CheckDebugTestTally CheckDebugTestTally;
struct CheckDebugTestTally
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
};

static void check_debug_test_expect(CheckDebugTestTally* tally, bool condition, const char* what, const char* detail)
{
    tally->checks += 1;
    if (!condition)
    {
        tally->failures += 1;
        if (tally->failures <= 24)
        {
            fprintf(stderr, "test-check-debug: FAIL: %s%s%s\n", what, detail != 0 ? ": " : "", detail != 0 ? detail : "");
        }
    }
}

static uint64_t check_debug_test_random(CheckDebugTestTally* tally)
{
    tally->random ^= tally->random << 13;
    tally->random ^= tally->random >> 7;
    tally->random ^= tally->random << 17;
    return tally->random;
}

// `bytes` that end where a PROT_NONE page starts, so that a read or write past them faults.
typedef struct CheckDebugTestGuard CheckDebugTestGuard;
struct CheckDebugTestGuard
{
    uint8_t* mapping;
    uint64_t mapped;
};

static void* check_debug_test_guard(CheckDebugTestGuard* guard, uint64_t bytes)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t pages = (bytes + page - 1) / page * page;
    guard->mapped = pages + page;
    guard->mapping = mmap(0, guard->mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = guard->mapping != MAP_FAILED && mprotect(guard->mapping + pages, page, PROT_NONE) == 0;
    guard->mapping = guard->mapping != MAP_FAILED ? guard->mapping : 0;
    return guarded ? guard->mapping + pages - bytes : 0;
}

static void check_debug_test_unguard(CheckDebugTestGuard* guard)
{
    if (guard->mapping != 0)
    {
        munmap(guard->mapping, guard->mapped);
    }
}

// The raw rows' twins on random functions (see the file's comment).
static void check_debug_test_rows(CheckDebugTestTally* tally)
{
    uint64_t rows_checked = 0;
    for (uint32_t round = 0; round < 131 + 64; round += 1)
    {
        const uint32_t entries = round <= 130 ? round : 131 + (uint32_t)(check_debug_test_random(tally) % 5000);
        // Each entry's records: none for about a third (a leaf fused into its consumer), else 1 to 3; a prologue of 0 to
        // 3 records, and an epilogue of 1 to 2.
        const uint32_t prologue = (uint32_t)(check_debug_test_random(tally) % 4);
        CheckDebugTestGuard guards[3] = { 0 };
        uint32_t* entry_records = check_debug_test_guard(&guards[0], ((uint64_t)entries + 1) * sizeof(uint32_t));
        uint32_t* tokens = check_debug_test_guard(&guards[1], ((uint64_t)entries + 1) * sizeof(uint32_t));
        bool ready = entry_records != 0 && tokens != 0;
        uint32_t record = prologue;
        for (uint32_t e = 0; ready && e < entries; e += 1)
        {
            entry_records[e] = record;
            tokens[e] = (uint32_t)check_debug_test_random(tally);
            const uint32_t choice = (uint32_t)(check_debug_test_random(tally) % 9);
            record += choice < 3 ? 0 : 1 + choice % 3;
        }
        if (ready)
        {
            entry_records[entries] = record;
        }
        const uint32_t records = record + 1 + (uint32_t)(check_debug_test_random(tally) % 2);
        uint32_t* offsets = ready ? check_debug_test_guard(&guards[2], ((uint64_t)records + 1) * sizeof(uint32_t)) : 0;
        ready = ready && offsets != 0;
        uint32_t offset = 0;
        for (uint32_t r = 0; ready && r <= records; r += 1)
        {
            offsets[r] = offset;
            offset += 1 + (uint32_t)(check_debug_test_random(tally) % 15);
        }
        const uint32_t length = ready ? offsets[records] : 0;
        const uint64_t room = (uint64_t)entries + 2 + BUSTER_CHECK_DEBUG_SLACK;
        uint32_t* scalar_offsets = calloc(room, sizeof(uint32_t));
        uint32_t* scalar_tokens = calloc(room, sizeof(uint32_t));
        uint32_t* vector_offsets = calloc(room, sizeof(uint32_t));
        uint32_t* vector_tokens = calloc(room, sizeof(uint32_t));
        ready = ready && scalar_offsets != 0 && scalar_tokens != 0 && vector_offsets != 0 && vector_tokens != 0;
        if (ready)
        {
            const uint32_t declaration = 0x7ffffff0u;
            const uint32_t closing = 0x7ffffff1u;
            const uint32_t scalar = check_debug_rows_scalar(entry_records, tokens, entries, offsets, length, declaration, closing, scalar_offsets, scalar_tokens);
            const uint32_t vector = check_debug_rows(entry_records, tokens, entries, offsets, length, declaration, closing, vector_offsets, vector_tokens);
            bool same = scalar == vector && memcmp(scalar_offsets, vector_offsets, (uint64_t)scalar * sizeof(uint32_t)) == 0 &&
                        memcmp(scalar_tokens, vector_tokens, (uint64_t)scalar * sizeof(uint32_t)) == 0;
            char detail[96];
            snprintf(detail, sizeof(detail), "%u entries, %u rows against %u", entries, vector, scalar);
            check_debug_test_expect(tally, same, "the raw rows' vector path gives the exact path's rows", detail);
            // The exact path against what the rows mean: the prologue's row where its code is, then an entry's where
            // its code starts and the next entry's doesn't, then the epilogue's, ascending, below the length.
            bool meant = scalar <= entries + 2 && (prologue == 0 || (scalar != 0 && scalar_offsets[0] == 0 && scalar_tokens[0] == declaration)) &&
                         scalar != 0 && scalar_tokens[scalar - 1] == closing && scalar_offsets[scalar - 1] == offsets[entry_records[entries]];
            for (uint32_t i = 0; meant && i + 1 < scalar; i += 1)
            {
                meant = scalar_offsets[i] < scalar_offsets[i + 1] && scalar_offsets[i + 1] < length;
            }
            check_debug_test_expect(tally, meant, "the raw rows are the prologue's, the entries' that start code and the epilogue's, ascending", detail);
            rows_checked += scalar;
        }
        free(vector_tokens);
        free(vector_offsets);
        free(scalar_tokens);
        free(scalar_offsets);
        for (uint32_t g = 0; g < 3; g += 1)
        {
            check_debug_test_unguard(&guards[g]);
        }
    }
    printf("  raw rows: %u functions, %llu rows, each path's at PROT_NONE pages\n", 131 + 64, (unsigned long long)rows_checked);
}

// The kept rows' twins on random rows (see the file's comment).
static void check_debug_test_keep(CheckDebugTestTally* tally)
{
    uint64_t kept_total = 0;
    for (uint32_t round = 0; round < 131 + 64; round += 1)
    {
        const uint32_t count = round <= 130 ? round : 131 + (uint32_t)(check_debug_test_random(tally) % 5000);
        CheckDebugTestGuard guards[3] = { 0 };
        uint32_t* offsets = check_debug_test_guard(&guards[0], (uint64_t)count * sizeof(uint32_t));
        uint32_t* keys = check_debug_test_guard(&guards[1], (uint64_t)count * sizeof(uint32_t));
        uint32_t* lines = check_debug_test_guard(&guards[2], (uint64_t)count * sizeof(uint32_t));
        const uint32_t key = 3;
        uint32_t line = 1 + (uint32_t)(check_debug_test_random(tally) % 1000);
        bool ready = offsets != 0 && keys != 0 && lines != 0;
        for (uint32_t i = 0; ready && i < count; i += 1)
        {
            const uint32_t choice = (uint32_t)(check_debug_test_random(tally) % 20);
            offsets[i] = i * 3;
            keys[i] = choice == 0 ? BUSTER_CHECK_DEBUG_NO_FILE : choice == 1 ? key + 1 : key;
            line = choice < 8 ? line : choice < 18 ? line + (uint32_t)(check_debug_test_random(tally) % 5) - 2 : choice == 18 ? 0x80000000u + choice : line + 40;
            lines[i] = line;
            line = line > INT32_MAX ? 7 : line;
        }
        const uint64_t room = (uint64_t)count + BUSTER_CHECK_DEBUG_SLACK;
        uint32_t* scalar_offsets = calloc(room, sizeof(uint32_t));
        uint32_t* scalar_lines = calloc(room, sizeof(uint32_t));
        uint32_t* vector_offsets = calloc(room, sizeof(uint32_t));
        uint32_t* vector_lines = calloc(room, sizeof(uint32_t));
        ready = ready && scalar_offsets != 0 && scalar_lines != 0 && vector_offsets != 0 && vector_lines != 0;
        // The body's start: a row's offset, or none.
        const uint32_t body = count != 0 && check_debug_test_random(tally) % 4 != 0 ? (uint32_t)(check_debug_test_random(tally) % count) * 3 : UINT32_MAX;
        if (ready)
        {
            const uint32_t scalar = check_debug_keep_scalar(offsets, keys, lines, count, key, body, scalar_offsets, scalar_lines);
            const uint32_t vector = check_debug_keep(offsets, keys, lines, count, key, body, vector_offsets, vector_lines);
            const bool same = scalar == vector && memcmp(scalar_offsets, vector_offsets, (uint64_t)scalar * sizeof(uint32_t)) == 0 &&
                              memcmp(scalar_lines, vector_lines, (uint64_t)scalar * sizeof(uint32_t)) == 0;
            char detail[96];
            snprintf(detail, sizeof(detail), "%u rows, %u kept against %u", count, vector, scalar);
            check_debug_test_expect(tally, same, "the kept rows' vector path keeps the exact path's rows", detail);
            // The exact path against what keeping means: rows of the file, each line at most INT32_MAX and unlike the one
            // kept before it, but the body's row.
            bool meant = true;
            for (uint32_t i = 0; meant && i < scalar; i += 1)
            {
                meant = scalar_lines[i] <= INT32_MAX && (i == 0 || scalar_lines[i] != scalar_lines[i - 1] || scalar_offsets[i] == body);
            }
            check_debug_test_expect(tally, meant, "the kept rows' lines change from row to row", detail);
            kept_total += scalar;
        }
        free(vector_lines);
        free(vector_offsets);
        free(scalar_lines);
        free(scalar_offsets);
        for (uint32_t g = 0; g < 3; g += 1)
        {
            check_debug_test_unguard(&guards[g]);
        }
    }
    printf("  kept rows: %u functions' rows, %llu kept, each path's at PROT_NONE pages\n", 131 + 64, (unsigned long long)kept_total);
}

// The fixtures: a header's function, a macro whose definition is lines above its use, a call across two lines, an
// untaken branch, macros nested past the vector path's walk, an #include and a #line inside a body, a #line before a
// function, a computed goto through a static table of its labels' addresses (#431, #426's review), and a naked function,
// whose one row is its asm statement's, as clang's (#147). main returns 0.
typedef struct CheckDebugTestFile CheckDebugTestFile;
struct CheckDebugTestFile
{
    const char* name;
    const char* text;
};

static const CheckDebugTestFile check_debug_test_files[] = {
    {
        "dbg_rows.h",
        "static int header_add(int a, int b)\n"
        "{\n"
        "    return a + b;\n"
        "}\n",
    },
    {
        "dbg_body.inc",
        "    r = r + 1;\n",
    },
    {
        "dbg_rows.c",
        "#include \"dbg_rows.h\"\n"                 // 1
        "#define TWICE(x) ((x) + (x))\n"            // 2
        "\n"                                        // 3
        "static int scale(int value)\n"             // 4
        "{\n"                                       // 5
        "    int doubled = TWICE(value);\n"         // 6
        "    return doubled * 3;\n"                 // 7
        "}\n"                                       // 8
        "\n"                                        // 9
        "#define N0(x) ((x) + 1)\n"                 // 10
        "#define N1(x) N0(x)\n"                     // 11
        "#define N2(x) N1(x)\n"                     // 12
        "#define N3(x) N2(x)\n"                     // 13
        "#define N4(x) N3(x)\n"                     // 14
        "#define N5(x) N4(x)\n"                     // 15
        "#define N6(x) N5(x)\n"                     // 16
        "#define N7(x) N6(x)\n"                     // 17
        "#define N8(x) N7(x)\n"                     // 18
        "#define N9(x) N8(x)\n"                     // 19
        "static int nested(int v)\n"                // 20
        "{\n"                                       // 21
        "    return N9(v) - 10;\n"                  // 22
        "}\n"                                       // 23
        "\n"                                        // 24
        "static int included(int v)\n"              // 25
        "{\n"                                       // 26
        "    int r = v;\n"                          // 27
        "#include \"dbg_body.inc\"\n"               // 28
        "    return r - 1;\n"                       // 29
        "}\n"                                       // 30
        "\n"                                        // 31
        "static int moved(int v)\n"                 // 32
        "{\n"                                       // 33
        "    int r = v * 2;\n"                      // 34
        "#line 500\n"                               // 35
        "    return r / 2;\n"                       // 500
        "}\n"                                       // 501
        "int dispatch(int v);\n"                    // 502, defined in dbg_goto.c below
        "#line 39\n"                                // 503, then the file's own numbers again
        "int main(int argc, char** argv)\n"         // 39
        "{\n"                                       // 40
        "    (void)argv;\n"                         // 41
        "    int total = scale(argc);\n"            // 42
        "    total += header_add(total,\n"          // 43
        "                        2);\n"             // 44
        "    if (total > 100)\n"                    // 45
        "    {\n"                                   // 46
        "        total = 0;\n"                      // 47
        "    }\n"                                   // 48
        "    total = total + nested(3) - 4;\n"      // 49
        "    total = total + included(5) - 5;\n"    // 50
        "    total = total + moved(6) - 6;\n"       // 51
        "    return total - 6 + dispatch(2);\n"     // 52
        "}\n"                                       // 53
        "#line 1 \"dbg_renamed.c\"\n"
        "int renamed(int v)\n"                      // dbg_renamed.c:1
        "{\n"                                       // 2
        "    return v + 1;\n"                       // 3
        "}\n"                                       // 4
        "#line 10 \"dbg_same.c\"\n"
        "int same_named(int v)\n"                   // dbg_same.c:10
        "{\n"                                       // 11
        "    int x = v * 3;\n"                      // 12
        "#line 100 \"dbg_same.c\"\n"
        "    return x + 1;\n"                       // 100: a second #line of the same name, the same file
        "}\n"                                       // 101
        "#line 1 \"dbg_goto.c\"\n"
        "int dispatch(int v)\n"                                     // dbg_goto.c:1
        "{\n"                                                       // 2
        "    static void* const table[] = { &&low, &&high };\n"    // 3
        "    void* target = table[v > 1];\n"                        // 4
        "    goto *target;\n"                                       // 5
        "low:\n"                                                    // 6
        "    v = -v;\n"                                             // 7
        "high:\n"                                                   // 8
        "    return v;\n"                                           // 9
        "}\n"                                                       // 10
        "#line 1 \"dbg_naked.c\"\n"
        "__attribute__((naked)) int naked_seven(void)\n"            // dbg_naked.c:1
        "{\n"                                                       // 2
        "    __asm__(\n"                                            // 3
        "        \"movl $7, %eax\\n\"\n"                             // 4
        "        \"ret\\n\");\n"                                     // 5
        "}\n",                                                      // 6
    },
};

// What a fixture's function's rows must be: its file's name, its declaration's line, the lines it must have a row on
// (its statements', and its closing brace's, its epilogue; a function with a prologue has a row on its declaration's
// line too, which one without one doesn't need) and those it mustn't.
typedef struct CheckDebugTestExpected CheckDebugTestExpected;
struct CheckDebugTestExpected
{
    const char* function;
    const char* file;
    uint32_t declaration;
    uint32_t required[12];
    uint32_t forbidden[8];
};

static const CheckDebugTestExpected check_debug_test_expected[] = {
    { "header_add", "dbg_rows.h", 1, { 3, 4 }, { 2 } },
    { "scale", "dbg_rows.c", 4, { 6, 7, 8 }, { 2, 3, 5 } },
    { "nested", "dbg_rows.c", 20, { 22, 23 }, { 10, 11, 18, 19, 21 } },
    { "included", "dbg_rows.c", 25, { 27, 29, 30 }, { 1, 28 } },
    { "moved", "dbg_rows.c", 32, { 34, 500, 501 }, { 35, 36 } },
    { "main", "dbg_rows.c", 39, { 42, 43, 45, 47, 49, 50, 51, 52, 53 }, { 40, 46 } },
    { "renamed", "dbg_renamed.c", 1, { 3, 4 }, { 2 } },
    { "same_named", "dbg_same.c", 10, { 12, 100, 101 }, { 11 } },
    { "dispatch", "dbg_goto.c", 1, { 4, 5, 7, 9, 10 }, { 2, 3, 6, 8 } },
    { "naked_seven", "dbg_naked.c", 1, { 3 }, { 2, 4, 5, 6 } },
};

// One compilation through the pipeline, with source rows (the driver's steps to check_program_emit): everything the
// program points at lives here, for another emission after.
typedef struct CheckDebugTestUnit CheckDebugTestUnit;
struct CheckDebugTestUnit
{
    CheckInput input;
    CheckStream stream;
    CheckInitializers initializers;
    CheckAsms asms;
    CheckDataPrepared data;
    CheckProgram program;
    CheckProgramSelection selected;
    CheckProgramCode code;
    CheckProgramRows rows;
    Arena arena;
    Arena output;
    Arena bytes;
    Arena scratch;
    Arena type_arena;
    Arena index_arena;
    TypeTable types;
    bool ready;
};

static void check_debug_test_close(CheckDebugTestUnit* unit)
{
    arena_release(&unit->index_arena);
    arena_release(&unit->type_arena);
    arena_release(&unit->scratch);
    arena_release(&unit->bytes);
    arena_release(&unit->output);
    arena_release(&unit->arena);
}

// Compiles `path` (with -I `directory`) into `unit` by the scalar path or the vector one, with `pp` and `parser`, which
// the caller keeps and releases.
static void check_debug_test_compile(CheckDebugTestUnit* unit, Preprocessor* pp, Parser* parser, InternTable* identifiers, const char* path, const char* directory, bool scalar)
{
    // Released before, or never made: what the caller releases after is what this call made.
    *pp = (Preprocessor){ 0 };
    *parser = (Parser){ 0 };
    *identifiers = (InternTable){ 0 };
    *unit = (CheckDebugTestUnit){
        .arena = arena_reserve(UINT64_C(1) << 32),
        .output = arena_reserve(UINT64_C(1) << 32),
        .bytes = arena_reserve(UINT64_C(1) << 30),
        .scratch = arena_reserve(UINT64_C(1) << 32),
    };
    char include[PATH_MAX + 2];
    snprintf(include, sizeof(include), "-I%s", directory);
    char* arguments[] = { include, (char*)"-march=x86-64", (char*)path };
    PPOptions options;
    const char* input = 0;
    const char* output = 0;
    bool markers = false;
    bool ready = unit->arena.base != 0 && unit->output.base != 0 && unit->bytes.base != 0 && unit->scratch.base != 0 &&
                 pp_parse_options(BUSTER_ARRAY_LENGTH(arguments), arguments, &options, &unit->arena, &input, &output, &markers);
    options.semantic_pragmas = true;
    options.quiet = true;
    const ParseOptions parse_options = { .scalar = scalar };
    ready = ready && intern_initialize(identifiers) && pp_initialize(pp, &options, identifiers) && parse_initialize(parser, &parse_options, options.language) &&
            pp_run(pp, input) && pp->errors == 0 && parse_translation_unit(parser, pp) && parser->errors == 0;
    const uint32_t tokens = ready ? parser->tokens.count : 0;
    const TypeCapacity capacity = {
        .types = tokens * 4 + 64,
        .parameters = tokens,
        .tags = tokens,
        .members = tokens,
    };
    if (ready)
    {
        unit->type_arena = arena_reserve(type_capacity_bytes(capacity));
        unit->index_arena = arena_reserve(type_index_capacity_bytes(capacity));
        ready = type_initialize(&unit->types, &unit->type_arena, &unit->index_arena, capacity);
    }
    const CheckOptions check_options = { .scalar = scalar };
    ready = ready && check_input(&unit->input, parser, pp, &unit->types, &unit->arena, &unit->scratch) &&
            check_graph_run(&unit->types, &unit->input.graph, &unit->output, &unit->scratch, &check_options, &unit->stream) && unit->stream.report_count == 0 &&
            check_asms_run(&unit->asms, &unit->input, &unit->stream, &unit->types, &unit->output, &unit->scratch) &&
            check_initializers_run(&unit->initializers, &unit->input, &unit->stream, parser, &unit->types, &unit->output, &unit->scratch) &&
            check_data_prepare(&unit->data, &unit->input, &unit->stream, &unit->initializers, &unit->types, scalar, &unit->output, &unit->scratch);
    const CheckProgramOptions program_options = {
        .initializers = &unit->initializers,
        .asms = &unit->asms,
        .identifiers = identifiers,
        .initializer_references = unit->data.references,
        .initializer_reference_count = unit->data.reference_count,
        .scalar = scalar,
    };
    ready = ready && check_program(&unit->program, parser, &unit->input, &unit->stream, &unit->types, program_options, &unit->output, &unit->scratch) &&
            check_program_select(&unit->selected, &unit->program, &unit->data, scalar, &unit->output, &unit->scratch) &&
            check_program_emit(&unit->code, &unit->selected.program, parser, identifiers, unit->selected.data.capacity, scalar, &unit->output, &unit->bytes,
                               &unit->scratch, NULL, &unit->rows, false, true);
    unit->ready = ready;
}

// The lowering with the entries' source tokens (check_program_lower_sources) against the one without them: each of the
// compiled program's functions lowered with them and without them, by both unit ABI batches, the same typed function,
// each entry's token alike by both batches and one of the function's own, from its declaration to its closing brace,
// and each call's its callee's.
static void check_debug_test_lowering(CheckDebugTestTally* tally, CheckDebugTestUnit* unit, const Parser* parser, const char* what)
{
    const CheckProgram* program = &unit->selected.program;
    const uint64_t mark = unit->output.length;
    CheckProgramLowered plain = { 0 };
    CheckProgramLowered sourced[2] = { 0 };
    const bool lowered = check_program_lower(&plain, program, true, &unit->output, &unit->scratch) &&
                         check_program_lower_sources(sourced, program, true, &unit->output, &unit->scratch) &&
                         check_program_lower_sources(sourced + 1, program, false, &unit->output, &unit->scratch);
    check_debug_test_expect(tally, lowered, "lower with the entries' source tokens as without them", what);
    for (uint32_t b = 0; lowered && b < program->body_count; b += 1)
    {
        const uint32_t count = sourced[0].functions[b].count;
        const uint32_t closing = parser->tree.parents[program->bodies[b].opening];
        bool own = check_test_program_function(plain.functions + b, sourced[0].functions + b) && count == sourced[1].functions[b].count &&
                   memcmp(sourced[0].entry_tokens[b], sourced[1].entry_tokens[b], (uint64_t)count * sizeof(uint32_t)) == 0;
        for (uint32_t e = 0; own && e < count; e += 1)
        {
            own = sourced[0].entry_tokens[b][e] >= program->bodies[b].declaration && sourced[0].entry_tokens[b][e] <= closing;
        }
        check_debug_test_expect(tally, own, "each entry's source token is the function's own, alike by both batches, which change nothing else", what);
        // A call's entry takes its callee's token, the name before its arguments' parenthesis (check_lower_call), not the
        // last argument's, whose lowering runs before the call's task (CheckLowerWriter.task_sources).
        bool calls = own;
        for (uint32_t e = 0; calls && e < count; e += 1)
        {
            const uint32_t token = sourced[0].entry_tokens[b][e];
            calls = sourced[0].functions[b].kinds[e] != BUSTER_TYPED_CALL || (token + 1 < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER &&
                                                                               parser->tokens.kinds[token + 1] == PARSE_KIND_L_PAREN);
        }
        check_debug_test_expect(tally, calls, "each call's entry takes its callee's token", what);
    }
    arena_pop_to(&unit->output, mark);
}

// Each row's file and line by both paths, and the exact path's against pp_presumed; each function's rows within its
// declaration's and closing brace's lines when its span holds (nothing between them moves a line). Returns the rows.
static uint64_t check_debug_test_lines(CheckDebugTestTally* tally, CheckDebugTestUnit* unit, Preprocessor* pp, const Parser* parser, const char* what)
{
    const CheckProgram* program = &unit->selected.program;
    const CheckProgramRows* rows = &unit->rows;
    uint64_t checked = 0;
    uint32_t* keys[2] = { calloc((uint64_t)rows->count + BUSTER_CHECK_DEBUG_SLACK, sizeof(uint32_t)), calloc((uint64_t)rows->count + BUSTER_CHECK_DEBUG_SLACK, sizeof(uint32_t)) };
    uint32_t* lines[2] = { calloc((uint64_t)rows->count + BUSTER_CHECK_DEBUG_SLACK, sizeof(uint32_t)), calloc((uint64_t)rows->count + BUSTER_CHECK_DEBUG_SLACK, sizeof(uint32_t)) };
    CheckDebugSource source;
    Arena* arena = &unit->scratch;
    const uint64_t mark = arena->length;
    const bool ready = keys[0] != 0 && keys[1] != 0 && lines[0] != 0 && lines[1] != 0 && check_debug_source(&source, pp, parser, arena);
    check_debug_test_expect(tally, ready, "room for the rows' lines", what);
    for (uint32_t b = 0; ready && b < program->body_count; b += 1)
    {
        const CheckProgramBody* body = &program->bodies[b];
        const uint32_t first = rows->first[b];
        const uint32_t count = rows->counts[b];
        const uint32_t closing = parser->tree.parents[body->opening];
        for (uint32_t path = 0; path < 2; path += 1)
        {
            check_debug_lines(&source, body->declaration, closing, rows->tokens + first, count, keys[path] + first, lines[path] + first, path == 0);
        }
        const bool same = memcmp(keys[0] + first, keys[1] + first, (uint64_t)count * sizeof(uint32_t)) == 0 &&
                          memcmp(lines[0] + first, lines[1] + first, (uint64_t)count * sizeof(uint32_t)) == 0;
        char detail[160];
        snprintf(detail, sizeof(detail), "%s, function %u (%u rows)", what, b, count);
        check_debug_test_expect(tally, same, "the rows' lines by the vector path are the exact path's", detail);
        // The exact path against the diagnostics' places.
        bool presumed = true;
        for (uint32_t r = first; presumed && r < first + count; r += 1)
        {
            const uint32_t output = parser->tokens.sources[rows->tokens[r]];
            const uint32_t location = check_lazy_expansion_location(pp, output, pp_history_at(pp, output));
            const char* name = 0;
            uint32_t length = 0;
            const uint32_t line = location != 0 ? pp_presumed(pp, location, &name, &length) : 0;
            const String8 file = keys[0][r] != BUSTER_CHECK_DEBUG_NO_FILE ? check_debug_file_name(pp, keys[0][r]) : (String8){ 0 };
            presumed = keys[0][r] == BUSTER_CHECK_DEBUG_NO_FILE || (line == lines[0][r] && file.length == length && memcmp(file.pointer, name, length) == 0);
        }
        check_debug_test_expect(tally, presumed, "each row's file and line are where the diagnostics place its token", detail);
        CheckDebugSpan span;
        if (check_debug_span(&source, body->declaration, closing, &span))
        {
            // Rows from the span's tokens keep within its lines (a row from a file the body includes has another key).
            bool within = true;
            for (uint32_t r = first; within && r < first + count; r += 1)
            {
                within = keys[0][r] != span.key || (lines[0][r] >= span.first_line + span.delta && lines[0][r] <= span.end_line + span.delta);
            }
            check_debug_test_expect(tally, within, "the rows in the function's file are within its lines", detail);
        }
        checked += count;
    }
    for (uint32_t path = 0; path < 2; path += 1)
    {
        free(keys[path]);
        free(lines[path]);
    }
    arena_pop_to(arena, mark);
    return checked;
}

// Whether two units' DWARF is the same: their rows, functions and sections.
static bool check_debug_test_same(const CheckDebug* a, const CheckDebug* b)
{
    bool same = a->unit.function_count == b->unit.function_count && a->unit.row_count == b->unit.row_count && a->unit.file_count == b->unit.file_count &&
                memcmp(a->unit.offsets, b->unit.offsets, (uint64_t)a->unit.row_count * sizeof(uint32_t)) == 0 &&
                memcmp(a->unit.lines, b->unit.lines, (uint64_t)a->unit.row_count * sizeof(uint32_t)) == 0;
    for (uint32_t section = 0; same && section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        same = a->output.sections[section].size == b->output.sections[section].size &&
               memcmp(a->output.sections[section].bytes, b->output.sections[section].bytes, a->output.sections[section].size) == 0;
    }
    return same;
}

// The fixtures' functions' rows against check_debug_test_expected.
static void check_debug_test_expectations(CheckDebugTestTally* tally, const CheckDebug* debug)
{
    for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(check_debug_test_expected); e += 1)
    {
        const CheckDebugTestExpected* expected = &check_debug_test_expected[e];
        const DebugFunction* function = 0;
        for (uint32_t f = 0; f < debug->unit.function_count; f += 1)
        {
            const DebugFunction* candidate = &debug->unit.functions[f];
            function = candidate->name.length == strlen(expected->function) && memcmp(candidate->name.pointer, expected->function, candidate->name.length) == 0 ? candidate : function;
        }
        char lines[512] = { 0 };
        uint64_t used = 0;
        for (uint32_t r = 0; function != 0 && r < function->row_count && used < sizeof(lines); r += 1)
        {
            used += (uint64_t)snprintf(lines + used, sizeof(lines) - used, " %u", debug->unit.lines[function->first_row + r]);
        }
        char detail[640];
        snprintf(detail, sizeof(detail), "%s: rows on lines%s", expected->function, lines);
        const String8 file = function != 0 && function->file < debug->unit.file_count ? debug->unit.files[function->file] : (String8){ 0 };
        const uint64_t name = strlen(expected->file);
        const bool filed = file.length >= name && memcmp(file.pointer + file.length - name, expected->file, name) == 0;
        check_debug_test_expect(tally, function != 0 && filed && function->line == expected->declaration, "a fixture's function in its file, at its declaration's line", detail);
        bool required = function != 0;
        for (uint32_t i = 0; required && i < BUSTER_ARRAY_LENGTH(expected->required) && expected->required[i] != 0; i += 1)
        {
            bool found = false;
            for (uint32_t r = 0; r < function->row_count; r += 1)
            {
                found |= debug->unit.lines[function->first_row + r] == expected->required[i];
            }
            required = found;
        }
        check_debug_test_expect(tally, required, "a fixture's function has a row on each of its statements' lines", detail);
        bool forbidden = function != 0;
        for (uint32_t i = 0; forbidden && i < BUSTER_ARRAY_LENGTH(expected->forbidden) && expected->forbidden[i] != 0; i += 1)
        {
            for (uint32_t r = 0; r < function->row_count; r += 1)
            {
                forbidden &= debug->unit.lines[function->first_row + r] != expected->forbidden[i];
            }
        }
        check_debug_test_expect(tally, forbidden, "a fixture's function has no row on a macro's definition, a brace that starts nothing or a blank line", detail);
    }
}

// Appends to a generated program's text while it fits; past `capacity`, *used only grows.
static __attribute__((format(printf, 4, 5))) void check_debug_test_append(char* text, uint64_t capacity, uint64_t* used, const char* format, ...)
{
    if (*used < capacity)
    {
        va_list arguments;
        va_start(arguments, format);
        const int written = vsnprintf(text + *used, capacity - *used, format, arguments);
        va_end(arguments);
        *used += written > 0 ? (uint64_t)written : 0;
    }
}

// A generated program of `functions` functions (2 to 7 for 0) whose statements use macros, some across lines, with
// #lines between functions and inside bodies (`directives`, or none), and main calling them. Returns its length, or
// `capacity` when it doesn't fit.
static uint64_t check_debug_test_generate(CheckDebugTestTally* tally, char* text, uint64_t capacity, uint32_t functions, bool directives)
{
    static const char header[] =
        "#define G_ADD(a, b) ((a) + (b))\n"
        "#define G_MUL(a, b) ((a) * (b))\n"
        "#define G_ID(x) (x)\n"
        "#define G_NEST(x) G_ID(G_ADD(x, G_K))\n"
        "#define G_K 7\n"
        "#define G_STEP(r, b) r = G_ADD(r, G_MUL(b, 3)) - G_K\n";
    static const char* const statements[] = {
        "    r = G_ADD(r, b);\n",
        "    r = G_ADD(r,\n              b);\n",
        "    r = G_NEST(r) - G_K;\n",
        "    if (r > G_K)\n    {\n        r = G_MUL(r, 2) - b;\n    }\n",
        "    G_STEP(r, b);\n",
        "    r = r\n        + G_ID(b)\n        - 1;\n",
        "    for (int i = 0; i < 3; i += 1)\n    {\n        r = G_ADD(r, i);\n    }\n",
        "    r = r ^ G_K;\n",
    };
    uint64_t used = 0;
    check_debug_test_append(text, capacity, &used, "%s", header);
    functions = functions != 0 ? functions : 2 + (uint32_t)(check_debug_test_random(tally) % 6);
    for (uint32_t f = 0; f < functions; f += 1)
    {
        const uint32_t choice = directives ? (uint32_t)(check_debug_test_random(tally) % 8) : 2;
        if (choice == 0)
        {
            check_debug_test_append(text, capacity, &used, "#line %u \"generated_%u.c\"\n", 100 + f * 50, f);
        }
        else if (choice == 1)
        {
            check_debug_test_append(text, capacity, &used, "#line %u\n", 300 + f * 70);
        }
        check_debug_test_append(text, capacity, &used, "static int g%u(int a, int b)\n{\n    int r = a;\n", f);
        const uint32_t count = 1 + (uint32_t)(check_debug_test_random(tally) % 8);
        for (uint32_t s = 0; s < count; s += 1)
        {
            if (directives && check_debug_test_random(tally) % 16 == 0)
            {
                check_debug_test_append(text, capacity, &used, "#line %u\n", 1000 + f * 100 + s);
            }
            check_debug_test_append(text, capacity, &used, "%s", statements[check_debug_test_random(tally) % BUSTER_ARRAY_LENGTH(statements)]);
        }
        check_debug_test_append(text, capacity, &used, "    return r;\n}\n");
    }
    check_debug_test_append(text, capacity, &used, "int main(void)\n{\n    int total = 0;\n");
    for (uint32_t f = 0; f < functions; f += 1)
    {
        check_debug_test_append(text, capacity, &used, "    total += g%u(total, %u);\n", f, f + 1);
    }
    check_debug_test_append(text, capacity, &used, "    return total & 1;\n}\n");
    return used < capacity ? used : capacity;
}

// The lines on the fixtures and on generated programs (see the file's comment).
static void check_debug_test_sources(CheckDebugTestTally* tally, const char* directory, uint32_t programs)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    uint64_t rows = 0;
    bool written = true;
    for (uint32_t f = 0; written && f < BUSTER_ARRAY_LENGTH(check_debug_test_files); f += 1)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", directory, check_debug_test_files[f].name);
        written = foreign_test_write(path, check_debug_test_files[f].text, strlen(check_debug_test_files[f].text));
    }
    check_debug_test_expect(tally, written, "write the fixtures", directory);
    char* generated = malloc(1u << 16);
    for (uint32_t program = 0; written && generated != 0 && program <= programs; program += 1)
    {
        char path[PATH_MAX];
        if (program == 0)
        {
            snprintf(path, sizeof(path), "%s/dbg_rows.c", directory);
        }
        else
        {
            snprintf(path, sizeof(path), "%s/generated%u.c", directory, program);
            const uint64_t length = check_debug_test_generate(tally, generated, 1u << 16, 0, true);
            written = length < (1u << 16) && foreign_test_write(path, generated, length);
        }
        CheckDebug debugs[2] = { 0 };
        uint64_t sizes[2] = { 0 };
        for (uint32_t path_kind = 0; written && path_kind < 2; path_kind += 1)
        {
            CheckDebugTestUnit unit;
            check_debug_test_compile(&unit, &pp, &parser, &identifiers, path, directory, path_kind == 0);
            check_debug_test_expect(tally, unit.ready, "compile through the pipeline with source rows", path);
            if (unit.ready)
            {
                check_debug_test_lowering(tally, &unit, &parser, path);
                rows += check_debug_test_lines(tally, &unit, &pp, &parser, path);
                CheckDebug built;
                const bool made = check_debug_build(&built, &unit.selected.program, &unit.rows, &parser, &pp, path_kind == 0, &unit.output, &unit.scratch);
                check_debug_test_expect(tally, made, "make the debug.c unit", path);
                if (made)
                {
                    // Copied out of the unit's arenas, which close with it.
                    debugs[path_kind] = built;
                    uint32_t* offsets = malloc(((uint64_t)built.unit.row_count + 1) * sizeof(uint32_t));
                    uint32_t* lines = malloc(((uint64_t)built.unit.row_count + 1) * sizeof(uint32_t));
                    DebugFunction* functions = malloc(((uint64_t)built.unit.function_count + 1) * sizeof(DebugFunction));
                    String8* files = malloc(((uint64_t)built.unit.file_count + 1) * sizeof(String8));
                    if (offsets != 0 && lines != 0 && functions != 0 && files != 0)
                    {
                        memcpy(offsets, built.unit.offsets, (uint64_t)built.unit.row_count * sizeof(uint32_t));
                        memcpy(lines, built.unit.lines, (uint64_t)built.unit.row_count * sizeof(uint32_t));
                        memcpy(functions, built.unit.functions, (uint64_t)built.unit.function_count * sizeof(DebugFunction));
                        for (uint32_t i = 0; i < built.unit.file_count; i += 1)
                        {
                            files[i] = (String8){ .pointer = strndup(built.unit.files[i].pointer, built.unit.files[i].length), .length = built.unit.files[i].length };
                        }
                        for (uint32_t i = 0; i < built.unit.function_count; i += 1)
                        {
                            functions[i].name = (String8){ .pointer = strndup(functions[i].name.pointer, functions[i].name.length), .length = functions[i].name.length };
                        }
                    }
                    debugs[path_kind].unit.offsets = offsets;
                    debugs[path_kind].unit.lines = lines;
                    debugs[path_kind].unit.functions = functions;
                    debugs[path_kind].unit.files = files;
                    for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
                    {
                        uint8_t* copy = malloc(built.output.sections[section].size + 1);
                        if (copy != 0)
                        {
                            memcpy(copy, built.output.sections[section].bytes, built.output.sections[section].size);
                        }
                        debugs[path_kind].output.sections[section].bytes = copy;
                    }
                    sizes[path_kind] = built.unit.row_count;
                }
            }
            check_debug_test_close(&unit);
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
        }
        if (written && debugs[0].unit.offsets != 0 && debugs[1].unit.offsets != 0)
        {
            check_debug_test_expect(tally, check_debug_test_same(&debugs[0], &debugs[1]), "both paths' units and sections are the same bytes", path);
            if (program == 0)
            {
                check_debug_test_expectations(tally, &debugs[1]);
            }
        }
        for (uint32_t path_kind = 0; path_kind < 2; path_kind += 1)
        {
            for (uint32_t i = 0; debugs[path_kind].unit.functions != 0 && i < debugs[path_kind].unit.function_count; i += 1)
            {
                free(debugs[path_kind].unit.functions[i].name.pointer);
            }
            for (uint32_t i = 0; debugs[path_kind].unit.files != 0 && i < debugs[path_kind].unit.file_count; i += 1)
            {
                free(debugs[path_kind].unit.files[i].pointer);
            }
            free((void*)debugs[path_kind].unit.offsets);
            free((void*)debugs[path_kind].unit.lines);
            free((void*)debugs[path_kind].unit.functions);
            free((void*)debugs[path_kind].unit.files);
            for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
            {
                free(debugs[path_kind].output.sections[section].bytes);
            }
        }
        (void)sizes;
    }
    free(generated);
    printf("  lines: the fixture and %u generated programs, %llu rows by each path\n", programs, (unsigned long long)rows);
}

// A unit whose selection keeps no function (#426's review): data and an unused static function, emitted with source
// rows by each path into an empty set of rows, which make a debug.c unit of no functions, the same bytes by both.
static void check_debug_test_empty(CheckDebugTestTally* tally, const char* directory)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/dbg_nofunctions.c", directory);
    static const char text[] = "int dbg_nofunctions_value = 7;\n\nstatic int dbg_nofunctions_unused(void)\n{\n    return dbg_nofunctions_value;\n}\n";
    const bool written = foreign_test_write(path, text, sizeof(text) - 1);
    uint8_t* sections[2][BUSTER_DEBUG_SECTIONS] = { 0 };
    uint64_t sizes[2][BUSTER_DEBUG_SECTIONS] = { 0 };
    bool built[2] = { false, false };
    for (uint32_t side = 0; written && side < 2; side += 1)
    {
        CheckDebugTestUnit unit;
        check_debug_test_compile(&unit, &pp, &parser, &identifiers, path, directory, side == 0);
        check_debug_test_expect(tally, unit.ready && unit.selected.program.body_count == 0 && unit.rows.count == 0,
                                "emit a unit whose selection keeps no function with source rows, none of them", path);
        CheckDebug debug;
        built[side] = unit.ready && check_debug_build(&debug, &unit.selected.program, &unit.rows, &parser, &pp, side == 0, &unit.output, &unit.scratch);
        check_debug_test_expect(tally, built[side] && debug.unit.function_count == 0 && debug.unit.row_count == 0, "make its debug.c unit, of no functions", path);
        for (uint32_t section = 0; built[side] && section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            sizes[side][section] = debug.output.sections[section].size;
            sections[side][section] = malloc(sizes[side][section] + 1);
            if (sections[side][section] != 0)
            {
                memcpy(sections[side][section], debug.output.sections[section].bytes, sizes[side][section]);
            }
        }
        check_debug_test_close(&unit);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
    bool same = built[0] && built[1];
    for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        same = same && sections[0][section] != 0 && sections[1][section] != 0 && sizes[0][section] == sizes[1][section] &&
               memcmp(sections[0][section], sections[1][section], sizes[0][section]) == 0;
        free(sections[0][section]);
        free(sections[1][section]);
    }
    check_debug_test_expect(tally, same, "the empty unit's sections are the same bytes by both paths", path);
}

// The driver's link with -g of a unit whose selection keeps no function, its main a shared library's (built with cc):
// by each path into the same bytes, which run, with DWARF llvm-dwarfdump --verify finds no error in.
static void check_debug_test_library_main(CheckDebugTestTally* tally, const char* directory, Arena* arena)
{
    char library_source[PATH_MAX];
    char library[PATH_MAX];
    char library_errors[PATH_MAX + 8];
    char source[PATH_MAX];
    char search[PATH_MAX + 2];
    char environment[PATH_MAX + 32];
    snprintf(library_source, sizeof(library_source), "%s/dbg_libmain.c", directory);
    snprintf(library, sizeof(library), "%s/libdbgmain.so", directory);
    snprintf(library_errors, sizeof(library_errors), "%s.err", library);
    snprintf(source, sizeof(source), "%s/dbg_nofunctions.c", directory);
    snprintf(search, sizeof(search), "-L%s", directory);
    snprintf(environment, sizeof(environment), "LD_LIBRARY_PATH=%s", directory);
    static const char text[] = "int main(void)\n{\n    return 0;\n}\n";
    char* compile[] = { (char*)"cc", (char*)"-shared", (char*)"-fPIC", (char*)"-o", library, library_source, 0 };
    const bool made = foreign_test_write(library_source, text, sizeof(text) - 1) && object_test_run(compile, "/dev/null", library_errors) == 0;
    check_debug_test_expect(tally, made, "cc builds a shared library whose main the unit without functions takes", library_errors);
    String8 files[2] = { 0 };
    char outputs[2][PATH_MAX];
    for (uint32_t path = 0; made && path < 2; path += 1)
    {
        char errors[PATH_MAX + 8];
        snprintf(outputs[path], sizeof(outputs[path]), "%s/dbg_nofunctions%s", directory, path == 0 ? "" : ".scalar");
        snprintf(errors, sizeof(errors), "%s.err", outputs[path]);
        char* arguments[] = { (char*)"/proc/self/exe", (char*)"-g", (char*)"-o", outputs[path], source, search, (char*)"-ldbgmain", path == 1 ? (char*)"--scalar" : 0, 0 };
        const bool linked = object_test_run(arguments, "/dev/null", errors) == 0;
        check_debug_test_expect(tally, linked, "the driver links a unit without functions with -g, its main a library's", errors);
        files[path] = linked ? foreign_test_read(arena, outputs[path]) : (String8){ 0 };
        char* run[] = { (char*)"env", environment, outputs[path], 0 };
        check_debug_test_expect(tally, linked && object_test_run(run, "/dev/null", "/dev/null") == 0, "it runs and returns 0", outputs[path]);
    }
    if (made)
    {
        check_debug_test_expect(tally, files[0].pointer != 0 && files[1].pointer != 0 && files[0].length == files[1].length &&
                                           memcmp(files[0].pointer, files[1].pointer, files[0].length) == 0,
                                "the vector path's executable is --scalar's, byte for byte", outputs[0]);
        char report[PATH_MAX + 16];
        snprintf(report, sizeof(report), "%s.verify", outputs[0]);
        char* verify[] = { (char*)"llvm-dwarfdump", (char*)"--verify", outputs[0], 0 };
        object_test_run(verify, report, "/dev/null");
        const String8 verified = foreign_test_read(arena, report);
        check_debug_test_expect(tally, verified.pointer != 0 && strstr(verified.pointer, "No errors.") != 0, "llvm-dwarfdump --verify finds no error in its unit of no functions", report);
    }
}

// gdb's transcript of `commands` on `program`, into `report`.
static String8 check_debug_test_gdb(const char* program, const char* report, const char* const* commands, uint32_t count, Arena* arena)
{
    char* arguments[40] = { (char*)"gdb", (char*)"-nx", (char*)"-batch", (char*)"-ex", (char*)"set debuginfod enabled off" };
    uint32_t used = 5;
    for (uint32_t i = 0; i < count && used + 3 < BUSTER_ARRAY_LENGTH(arguments); i += 1)
    {
        arguments[used] = (char*)"-ex";
        arguments[used + 1] = (char*)commands[i];
        used += 2;
    }
    arguments[used] = (char*)program;
    arguments[used + 1] = 0;
    object_test_run(arguments, report, "/dev/null");
    return foreign_test_read(arena, report);
}

// What llvm-dwarfdump and gdb read of an executable of the fixture's, `program` (see the file's comment): the driver's
// with -g, or a link of its object with -c -g.
static void check_debug_test_read(CheckDebugTestTally* tally, const char* program, Arena* arena)
{
    char report[PATH_MAX + 16];
    snprintf(report, sizeof(report), "%s.verify", program);
    char* verify[] = { (char*)"llvm-dwarfdump", (char*)"--verify", (char*)program, 0 };
    object_test_run(verify, report, "/dev/null");
    const String8 verified = foreign_test_read(arena, report);
    check_debug_test_expect(tally, verified.pointer != 0 && strstr(verified.pointer, "No errors.") != 0, "llvm-dwarfdump --verify finds no error", report);
    // A line's breakpoint, a step over the untaken branch, and a function's breakpoint with its backtrace.
    snprintf(report, sizeof(report), "%s.gdb-lines", program);
    static const char* const lines[] = { "break dbg_rows.c:45", "run", "next", "info line *$pc" };
    const String8 stepped = check_debug_test_gdb(program, report, lines, BUSTER_ARRAY_LENGTH(lines), arena);
    const char* stop = stepped.pointer != 0 ? strstr(stepped.pointer, "Breakpoint 1, main") : 0;
    check_debug_test_expect(tally, stop != 0 && strstr(stop, "dbg_rows.c:45") != 0 && strstr(stop, "\n49\t") != 0,
                            "gdb stops at a line's breakpoint on its line and steps over the untaken branch to the next statement", report);
    snprintf(report, sizeof(report), "%s.gdb-functions", program);
    static const char* const functions[] = { "break scale", "break header_add", "run", "bt", "continue", "bt" };
    const String8 traced = check_debug_test_gdb(program, report, functions, BUSTER_ARRAY_LENGTH(functions), arena);
    const char* scale = traced.pointer != 0 ? strstr(traced.pointer, "Breakpoint 1, scale") : 0;
    const char* header = scale != 0 ? strstr(scale, "Breakpoint 2, header_add") : 0;
    check_debug_test_expect(tally, scale != 0 && header != 0 && strstr(scale, "dbg_rows.c:6") != 0 && strstr(scale, "main () at") != 0 && strstr(scale, "dbg_rows.c:42") != 0,
                            "gdb stops at a function's breakpoint on its first statement, and its backtrace gives the caller's line", report);
    check_debug_test_expect(tally, header != 0 && strstr(header, "dbg_rows.h:3") != 0 && strstr(header, "dbg_rows.c:43") != 0,
                            "the header's function stops in the header, called from its callee's line (the call's first)", report);
    // The computed goto: stopped at it, called from main's last line, then a step to the label's statement it goes to.
    snprintf(report, sizeof(report), "%s.gdb-goto", program);
    static const char* const jumps[] = { "break dbg_goto.c:5", "run", "bt", "next", "info line *$pc" };
    const String8 jumped = check_debug_test_gdb(program, report, jumps, BUSTER_ARRAY_LENGTH(jumps), arena);
    const char* dispatch = jumped.pointer != 0 ? strstr(jumped.pointer, "Breakpoint 1, dispatch") : 0;
    // gdb names the file with the compilation's directory before it, the #line's name being relative.
    const char* target = dispatch != 0 ? strstr(dispatch, "\nLine 9 of \"") : 0;
    check_debug_test_expect(tally, dispatch != 0 && strstr(dispatch, "dbg_goto.c:5") != 0 && strstr(dispatch, "dbg_rows.c:52") != 0 && target != 0 &&
                                       strstr(target, "/dbg_goto.c\" starts at address") != 0,
                            "gdb stops at a computed goto and steps to the statement of the label it goes to", report);
}

// The driver with -g, by each path, and what llvm-dwarfdump and gdb read of its executable (see the file's comment).
static void check_debug_test_driver(CheckDebugTestTally* tally, const char* directory, Arena* arena)
{
    char source[PATH_MAX];
    char include[PATH_MAX + 2];
    char outputs[2][PATH_MAX];
    snprintf(source, sizeof(source), "%s/dbg_rows.c", directory);
    snprintf(include, sizeof(include), "-I%s", directory);
    String8 files[2] = { 0 };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        char errors[PATH_MAX + 8];
        snprintf(outputs[path], sizeof(outputs[path]), "%s/dbg_rows%s", directory, path == 0 ? "" : ".scalar");
        snprintf(errors, sizeof(errors), "%s.err", outputs[path]);
        char* arguments[] = { (char*)"/proc/self/exe", (char*)"-g", include, (char*)"-o", outputs[path], source, path == 1 ? (char*)"--scalar" : 0, 0 };
        const bool linked = object_test_run(arguments, "/dev/null", errors) == 0;
        check_debug_test_expect(tally, linked, "the driver links the fixture with -g", errors);
        files[path] = linked ? foreign_test_read(arena, outputs[path]) : (String8){ 0 };
        char* run[] = { outputs[path], 0 };
        check_debug_test_expect(tally, linked && object_test_run(run, "/dev/null", "/dev/null") == 0, "the fixture runs and returns 0", outputs[path]);
    }
    check_debug_test_expect(tally, files[0].pointer != 0 && files[1].pointer != 0 && files[0].length == files[1].length && memcmp(files[0].pointer, files[1].pointer, files[0].length) == 0,
                            "the vector path's executable is --scalar's, byte for byte", outputs[0]);
    check_debug_test_read(tally, outputs[0], arena);
}

// Whether `needle` is in the text from `start` on, before `end` (or anywhere after `start`, for a null `end`).
static bool check_debug_test_within(const char* start, const char* end, const char* needle)
{
    const char* found = start != 0 ? strstr(start, needle) : 0;
    return found != 0 && (end == 0 || found < end);
}

// The address gdb gave breakpoint `number` in `transcript` ("Breakpoint N at 0x..."), or 0.
static uint64_t check_debug_test_breakpoint(const char* transcript, uint32_t number)
{
    char prefix[48];
    snprintf(prefix, sizeof(prefix), "Breakpoint %u at 0x", number);
    const char* found = transcript != 0 ? strstr(transcript, prefix) : 0;
    return found != 0 ? strtoull(found + strlen(prefix), 0, 16) : 0;
}

// The driver with -g on several units (#779; see the file's comment).
static void check_debug_test_units(CheckDebugTestTally* tally, const char* directory, Arena* arena)
{
    static const char main_text[] = "#include <stdio.h>\n"                                // 1
                                    "\n"                                                  // 2
                                    "int units_scale(int value);\n"                       // 3
                                    "int units_pick(void);\n"                             // 4
                                    "\n"                                                  // 5
                                    "static int units_helper(int value)\n"                // 6
                                    "{\n"                                                 // 7
                                    "    return value + 1;\n"                             // 8
                                    "}\n"                                                 // 9
                                    "\n"                                                  // 10
                                    "int main(void)\n"                                    // 11
                                    "{\n"                                                 // 12
                                    "    int total = units_scale(20);\n"                  // 13
                                    "    total = units_helper(total);\n"                  // 14
                                    "    printf(\"%d %d\\n\", total, units_pick());\n"    // 15
                                    "    return total == 41 && units_pick() == 7 ? 0 : 1;\n" // 16
                                    "}\n";                                                // 17
    static const char scale_text[] = "static int units_helper(int value)\n"               // 1
                                     "{\n"                                                // 2
                                     "    return value * 3;\n"                            // 3
                                     "}\n"                                                // 4
                                     "\n"                                                 // 5
                                     "int units_scale(int value)\n"                       // 6
                                     "{\n"                                                // 7
                                     "    int doubled = value * 2;\n"                     // 8
                                     "    return units_helper(doubled) / 3;\n"            // 9
                                     "}\n"                                                // 10
                                     "\n"                                                 // 11
                                     "int units_pick(void)\n"                             // 12
                                     "{\n"                                                // 13
                                     "    return 7;\n"                                    // 14
                                     "}\n";                                               // 15
    static const char weak_text[] = "__attribute__((weak)) int units_pick(void)\n"        // 1
                                    "{\n"                                                 // 2
                                    "    return 9;\n"                                     // 3
                                    "}\n"                                                 // 4
                                    "\n"                                                  // 5
                                    "int units_weak_caller(void)\n"                       // 6
                                    "{\n"                                                 // 7
                                    "    return units_pick() + 1;\n"                      // 8
                                    "}\n";                                                // 9
    char sources[3][PATH_MAX];
    static const char* const names[] = { "dbg_units_main.c", "dbg_units_scale.c", "dbg_units_weak.c" };
    const char* const texts[] = { main_text, scale_text, weak_text };
    const uint64_t lengths[] = { sizeof(main_text) - 1, sizeof(scale_text) - 1, sizeof(weak_text) - 1 };
    bool written = true;
    for (uint32_t s = 0; s < 3; s += 1)
    {
        snprintf(sources[s], sizeof(sources[s]), "%s/%s", directory, names[s]);
        written = written && foreign_test_write(sources[s], texts[s], lengths[s]);
    }
    check_debug_test_expect(tally, written, "write the units' fixtures", directory);
    // The two units by --lanes 1 and 8 and by each path.
    static const char* const variants[][3] = { { "--lanes", "1", 0 }, { "--lanes", "8", 0 }, { "--scalar", "--lanes", "8" } };
    char outputs[3][PATH_MAX];
    String8 files[3] = { 0 };
    for (uint32_t v = 0; written && v < BUSTER_ARRAY_LENGTH(variants); v += 1)
    {
        char errors[PATH_MAX + 8];
        snprintf(outputs[v], sizeof(outputs[v]), "%s/dbg_units_%u", directory, v);
        snprintf(errors, sizeof(errors), "%s.err", outputs[v]);
        char* arguments[12] = { (char*)"/proc/self/exe", (char*)"-g" };
        uint32_t used = 2;
        for (uint32_t o = 0; o < 3 && variants[v][o] != 0; o += 1)
        {
            arguments[used++] = (char*)variants[v][o];
        }
        arguments[used++] = (char*)"-o";
        arguments[used++] = outputs[v];
        arguments[used++] = sources[0];
        arguments[used++] = sources[1];
        arguments[used] = 0;
        const bool linked = object_test_run(arguments, "/dev/null", errors) == 0;
        check_debug_test_expect(tally, linked, "the driver links two units with -g", errors);
        files[v] = linked ? foreign_test_read(arena, outputs[v]) : (String8){ 0 };
        char* run[] = { outputs[v], 0 };
        check_debug_test_expect(tally, linked && object_test_run(run, "/dev/null", "/dev/null") == 0, "the units' program runs and returns 0", outputs[v]);
        check_debug_test_expect(tally, v == 0 || (files[v].pointer != 0 && files[0].pointer != 0 && files[v].length == files[0].length &&
                                                  memcmp(files[v].pointer, files[0].pointer, files[0].length) == 0),
                                "the units' executable with -g is the same bytes by --lanes 1 and 8 and by each path", outputs[v]);
    }
    if (files[0].pointer != 0)
    {
        char report[PATH_MAX + 16];
        snprintf(report, sizeof(report), "%s.verify", outputs[0]);
        char* verify[] = { (char*)"llvm-dwarfdump", (char*)"--verify", outputs[0], 0 };
        object_test_run(verify, report, "/dev/null");
        const String8 verified = foreign_test_read(arena, report);
        check_debug_test_expect(tally, verified.pointer != 0 && strstr(verified.pointer, "No errors.") != 0, "llvm-dwarfdump --verify finds no error in the units' DWARF",
                                report);
        // Each unit's function, its static function of the name the other's has, and their callers' lines.
        snprintf(report, sizeof(report), "%s.gdb-units", outputs[0]);
        static const char* const stops[] = { "break units_scale", "break dbg_units_scale.c:3", "break dbg_units_main.c:8", "run", "bt", "continue", "bt", "continue", "bt" };
        const String8 traced = check_debug_test_gdb(outputs[0], report, stops, BUSTER_ARRAY_LENGTH(stops), arena);
        const char* first = traced.pointer != 0 ? strstr(traced.pointer, "Breakpoint 1, units_scale") : 0;
        const char* second = first != 0 ? strstr(first, "Breakpoint 2, units_helper") : 0;
        const char* third = second != 0 ? strstr(second, "Breakpoint 3, units_helper") : 0;
        check_debug_test_expect(tally, check_debug_test_within(first, second, "dbg_units_scale.c:8") && check_debug_test_within(first, second, "main () at") &&
                                           check_debug_test_within(first, second, "dbg_units_main.c:13"),
                                "gdb stops at the second unit's function on its first statement, called from the first unit's line", report);
        check_debug_test_expect(tally, check_debug_test_within(second, third, "dbg_units_scale.c:3") && check_debug_test_within(second, third, "dbg_units_scale.c:9") &&
                                           check_debug_test_within(second, third, "dbg_units_main.c:13"),
                                "the second unit's static function stops in its own file, called from its unit's caller", report);
        check_debug_test_expect(tally, check_debug_test_within(third, 0, "dbg_units_main.c:8") && check_debug_test_within(third, 0, "dbg_units_main.c:14"),
                                "the first unit's static function of the same name stops in its own file", report);
    }
    // The weak function the second unit's strong one replaces, in a third unit.
    char weak_output[PATH_MAX];
    char weak_errors[PATH_MAX + 8];
    snprintf(weak_output, sizeof(weak_output), "%s/dbg_units_weak", directory);
    snprintf(weak_errors, sizeof(weak_errors), "%s.err", weak_output);
    char* weak_arguments[] = { (char*)"/proc/self/exe", (char*)"-g", (char*)"-o", weak_output, sources[0], sources[1], sources[2], 0 };
    const bool weak_linked = written && object_test_run(weak_arguments, "/dev/null", weak_errors) == 0;
    const String8 weak_message = written && !weak_linked ? foreign_test_read(arena, weak_errors) : (String8){ 0 };
    const bool refused = weak_message.pointer != 0 && strstr(weak_message.pointer, "unsupported: this attribute (alias, ifunc, constructor, destructor, weak") != 0;
    if (refused)
    {
        printf("  units: the checker refuses the weak attribute (#758), so the replaced weak function's DWARF isn't checked\n");
    }
    else if (written)
    {
        check_debug_test_expect(tally, weak_linked, "the driver links the units with -g, the third's weak function replaced", weak_errors);
        char* run[] = { weak_output, 0 };
        check_debug_test_expect(tally, weak_linked && object_test_run(run, "/dev/null", "/dev/null") == 0, "the program with the replaced weak function runs and returns 0",
                                weak_output);
        char report[PATH_MAX + 16];
        snprintf(report, sizeof(report), "%s.verify", weak_output);
        char* verify[] = { (char*)"llvm-dwarfdump", (char*)"--verify", weak_output, 0 };
        object_test_run(verify, report, "/dev/null");
        const String8 verified = weak_linked ? foreign_test_read(arena, report) : (String8){ 0 };
        check_debug_test_expect(tally, verified.pointer != 0 && strstr(verified.pointer, "No errors.") != 0, "llvm-dwarfdump --verify finds no error with the replaced function",
                                report);
        snprintf(report, sizeof(report), "%s.gdb-weak", weak_output);
        static const char* const stops[] = { "break dbg_units_weak.c:3", "break dbg_units_scale.c:14", "run" };
        const String8 traced = weak_linked ? check_debug_test_gdb(weak_output, report, stops, BUSTER_ARRAY_LENGTH(stops), arena) : (String8){ 0 };
        const uint64_t replaced = check_debug_test_breakpoint(traced.pointer, 1);
        const uint64_t strong = check_debug_test_breakpoint(traced.pointer, 2);
        check_debug_test_expect(tally, replaced != 0 && strong != 0 && replaced != strong && traced.pointer != 0 &&
                                           strstr(traced.pointer, "Breakpoint 2, units_pick") != 0 && strstr(traced.pointer, "Breakpoint 1, ") == 0,
                                "the replaced weak function's line is at its own code, which never runs, and the strong one's stops the run", report);
    }
}

// A relocatable object's section named `name`, and with `relocations` its .rela section; or zeros.
static Elf64_Shdr check_debug_test_section(String8 file, const char* name, bool relocations)
{
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file.pointer;
    Elf64_Shdr result = { 0 };
    const bool valid = file.length >= sizeof(Elf64_Ehdr) && header->e_type == ET_REL && header->e_shoff <= file.length &&
                       header->e_shnum <= (file.length - header->e_shoff) / sizeof(Elf64_Shdr) && header->e_shstrndx < header->e_shnum;
    const Elf64_Shdr* sections = valid ? (const Elf64_Shdr*)(file.pointer + header->e_shoff) : 0;
    const Elf64_Shdr* names = valid ? &sections[header->e_shstrndx] : 0;
    for (uint32_t i = 1; valid && names->sh_offset < file.length && i < header->e_shnum; i += 1)
    {
        const Elf64_Shdr* section = &sections[i];
        const uint32_t named = relocations ? (section->sh_type == SHT_RELA && section->sh_info < header->e_shnum ? sections[section->sh_info].sh_name : UINT32_MAX)
                                           : (section->sh_type != SHT_RELA ? section->sh_name : UINT32_MAX);
        const char* text = named < names->sh_size && names->sh_offset + names->sh_size <= file.length ? file.pointer + names->sh_offset + named : 0;
        const bool inside = section->sh_type == SHT_NOBITS || (section->sh_offset <= file.length && section->sh_size <= file.length - section->sh_offset);
        result = text != 0 && inside && memchr(text, 0, names->sh_size - named) != 0 && strcmp(text, name) == 0 ? *section : result;
    }
    return result;
}

// Whether the object's five DWARF sections are the executable's but at their relocations' fields, which hold zero in
// the object: -c -g's unit is -g's.
static bool check_debug_test_same_unit(String8 object, String8 executable)
{
    static const char* const names[] = { ".debug_info", ".debug_abbrev", ".debug_line", ".debug_addr", ".debug_rnglists" };
    const ExecutableTestImage image = executable_test_image_of(executable, ET_DYN);
    bool result = image.valid;
    for (uint32_t s = 0; result && s < BUSTER_ARRAY_LENGTH(names); s += 1)
    {
        const Elf64_Shdr ours = check_debug_test_section(object, names[s], false);
        const Elf64_Shdr relocations = check_debug_test_section(object, names[s], true);
        const Elf64_Shdr* theirs = executable_test_section(&image, names[s]);
        result = ours.sh_size != 0 && theirs != 0 && theirs->sh_size == ours.sh_size && theirs->sh_offset <= executable.length &&
                 theirs->sh_size <= executable.length - theirs->sh_offset;
        uint8_t* relocated = result ? calloc(ours.sh_size, 1) : 0;
        result = result && relocated != 0;
        for (uint64_t r = 0; result && r < relocations.sh_size / sizeof(Elf64_Rela); r += 1)
        {
            Elf64_Rela relocation;
            memcpy(&relocation, object.pointer + relocations.sh_offset + r * sizeof(relocation), sizeof(relocation));
            const uint32_t type = (uint32_t)ELF64_R_TYPE(relocation.r_info);
            const uint64_t width = type == R_X86_64_64 ? 8 : type == R_X86_64_32 ? 4 : 0;
            result = width != 0 && relocation.r_offset <= ours.sh_size - width;
            if (result)
            {
                memset(relocated + relocation.r_offset, 1, width);
            }
        }
        for (uint64_t at = 0; result && at < ours.sh_size; at += 1)
        {
            const uint8_t byte = (uint8_t)object.pointer[ours.sh_offset + at];
            result = relocated[at] != 0 ? byte == 0 : byte == (uint8_t)executable.pointer[theirs->sh_offset + at];
        }
        free(relocated);
    }
    return result;
}

// link-foreign (this executable, as a process of its own) on GCC's driver's line for a PIE of `object`, into `program`:
// whether it linked, its errors in `errors`.
static bool check_debug_test_link_foreign(const char* object, const char* program, const char* errors, Arena* arena)
{
    ForeignTestSystem system;
    foreign_test_system(&system);
    ForeignTestLine* line = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestLine, 1);
    char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, BUSTER_FOREIGN_TEST_MAX_ARGUMENTS + 8);
    bool linked = false;
    if (system.found && line != 0 && arguments != 0)
    {
        *line = (ForeignTestLine){ 0 };
        foreign_test_pie_begin(line, &system);
        foreign_test_add(line, (char*)object, true, true);
        foreign_test_pie_end(line, &system);
        static const char* const head[] = { "/proc/self/exe", "link-foreign", "-pie", "--dynamic-linker", "/lib64/ld-linux-x86-64.so.2", "-o" };
        uint32_t count = 0;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(head); i += 1)
        {
            arguments[count] = (char*)head[i];
            count += 1;
        }
        arguments[count] = (char*)program;
        count += 1;
        for (uint32_t i = 0; i < line->our_count; i += 1)
        {
            arguments[count] = line->ours[i];
            count += 1;
        }
        arguments[count] = 0;
        linked = object_test_run(arguments, "/dev/null", errors) == 0;
    }
    return linked;
}

// The driver with -c -g (#395), by each path, and what llvm-dwarfdump and gdb read of its object linked by cc, through
// GNU ld and lld, and by link-foreign, from GCC's driver's line: the unit -g's executable has (check_debug_test_driver
// made it, dbg_rows), relocated, in programs that run and read alike.
static void check_debug_test_objects(CheckDebugTestTally* tally, const char* directory, Arena* arena)
{
    char source[PATH_MAX];
    char include[PATH_MAX + 2];
    char objects[2][PATH_MAX];
    char executable[PATH_MAX];
    snprintf(source, sizeof(source), "%s/dbg_rows.c", directory);
    snprintf(include, sizeof(include), "-I%s", directory);
    snprintf(executable, sizeof(executable), "%s/dbg_rows", directory);
    String8 files[2] = { 0 };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        char errors[PATH_MAX + 8];
        snprintf(objects[path], sizeof(objects[path]), "%s/dbg_rows%s.o", directory, path == 0 ? "" : ".scalar");
        snprintf(errors, sizeof(errors), "%s.err", objects[path]);
        char* arguments[] = { (char*)"/proc/self/exe", (char*)"-c", (char*)"-g", include, (char*)"-o", objects[path], source, path == 1 ? (char*)"--scalar" : 0, 0 };
        const bool written = object_test_run(arguments, "/dev/null", errors) == 0;
        check_debug_test_expect(tally, written, "the driver writes the fixture's object with -c -g", errors);
        files[path] = written ? foreign_test_read(arena, objects[path]) : (String8){ 0 };
    }
    check_debug_test_expect(tally, files[0].pointer != 0 && files[1].pointer != 0 && files[0].length == files[1].length && memcmp(files[0].pointer, files[1].pointer, files[0].length) == 0,
                            "the vector path's object is --scalar's, byte for byte", objects[0]);
    const String8 linked_g = foreign_test_read(arena, executable);
    check_debug_test_expect(tally, files[0].pointer != 0 && linked_g.pointer != 0 && check_debug_test_same_unit(files[0], linked_g),
                            "the object's DWARF is -g's executable's, but for its relocations' fields", objects[0]);
    char report[PATH_MAX + 16];
    snprintf(report, sizeof(report), "%s.verify", objects[0]);
    char* verify[] = { (char*)"llvm-dwarfdump", (char*)"--verify", objects[0], 0 };
    object_test_run(verify, report, "/dev/null");
    const String8 verified = foreign_test_read(arena, report);
    check_debug_test_expect(tally, verified.pointer != 0 && strstr(verified.pointer, "No errors.") != 0, "llvm-dwarfdump --verify finds no error in the object", report);
    // cc through GNU ld and lld, then link-foreign.
    static const char* const linkers[] = { "-fuse-ld=bfd", "-fuse-ld=lld" };
    for (uint32_t l = 0; files[0].pointer != 0 && l < BUSTER_ARRAY_LENGTH(linkers) + 1; l += 1)
    {
        char program[PATH_MAX + 16];
        char errors[PATH_MAX + 24];
        snprintf(program, sizeof(program), "%s/dbg_rows.%s", directory, l == 0 ? "bfd" : l == 1 ? "lld" : "foreign");
        snprintf(errors, sizeof(errors), "%s.err", program);
        bool linked = false;
        if (l < BUSTER_ARRAY_LENGTH(linkers))
        {
            char* arguments[] = { (char*)"cc", (char*)linkers[l], (char*)"-o", program, objects[0], 0 };
            linked = object_test_run(arguments, "/dev/null", errors) == 0;
        }
        else
        {
            linked = check_debug_test_link_foreign(objects[0], program, errors, arena);
        }
        check_debug_test_expect(tally, linked, l == 0 ? "cc links the object through GNU ld" : l == 1 ? "cc links the object through lld" : "link-foreign links the object", errors);
        char* run[] = { program, 0 };
        check_debug_test_expect(tally, linked && object_test_run(run, "/dev/null", "/dev/null") == 0, "the linked fixture runs and returns 0", program);
        if (linked)
        {
            check_debug_test_read(tally, program, arena);
        }
    }
}

// The driver (/proc/self/exe) on a link with -g whose options fail before pp_parse_options runs, a -l without its name
// (#426's review): it must fail with the usage message, having read no option it didn't parse.
static void check_debug_test_unparsed(CheckDebugTestTally* tally, const char* directory, Arena* arena)
{
    char source[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(source, sizeof(source), "%s/dbg_unparsed.c", directory);
    snprintf(output, sizeof(output), "%s/dbg_unparsed", directory);
    snprintf(errors, sizeof(errors), "%s/dbg_unparsed.err", directory);
    static const char text[] = "int main(void)\n{\n    return 0;\n}\n";
    char* arguments[] = { (char*)"/proc/self/exe", (char*)"-g", (char*)"-o", output, source, (char*)"-l", 0 };
    const bool written = foreign_test_write(source, text, sizeof(text) - 1);
    const int status = written ? object_test_run(arguments, "/dev/null", errors) : -1;
    const String8 message = written ? foreign_test_read(arena, errors) : (String8){ 0 };
    check_debug_test_expect(tally, status == 1 && message.pointer != 0 && strstr(message.pointer, "usage: compiler") != 0,
                            "the driver refuses a link with -g whose options fail before they're parsed, with the usage message", errors);
}

// The unwind tables' checks (c_check_unwind_test.c, after this file).
static void check_unwind_test_driver(CheckDebugTestTally* tally, const char* directory, Arena* arena);

static int run_check_debug_tests(int argc, char** argv)
{
    CheckDebugTestTally tally = { .random = 0x9e3779b97f4a7c15ull };
    bool cross_check = false;
    bool keep = false;
    uint32_t programs = 24;
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            tally.random ^= strtoull(argv[++i], 0, 0) * 0x2545f4914f6cdd1dull;
            tally.random |= 1;
        }
        else if (strcmp(argv[i], "--programs") == 0 && i + 1 < argc)
        {
            programs = (uint32_t)strtoul(argv[++i], 0, 0);
        }
        else if (strcmp(argv[i], "--cross-check") == 0)
        {
            cross_check = true;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            options = false;
        }
    }
    check_debug_test_expect(&tally, options, "options: [--seed N] [--programs N] [--cross-check] [--keep]", 0);
    Arena arena = arena_reserve(UINT64_C(1) << 32);
    char directory[PATH_MAX];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-check-debug-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    const bool made = options && arena.base != 0 && mkdtemp(directory) != 0;
    check_debug_test_expect(&tally, made, "make the test's directory", directory);
    if (made)
    {
        const bool initialized = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize();
        check_debug_test_expect(&tally, initialized, "initialize the backend", 0);
        check_debug_test_rows(&tally);
        check_debug_test_keep(&tally);
        check_debug_test_sources(&tally, directory, programs);
        check_debug_test_empty(&tally, directory);
        check_debug_test_unparsed(&tally, directory, &arena);
        if (cross_check)
        {
            check_debug_test_driver(&tally, directory, &arena);
            check_debug_test_units(&tally, directory, &arena);
            check_debug_test_objects(&tally, directory, &arena);
            check_debug_test_library_main(&tally, directory, &arena);
            check_unwind_test_driver(&tally, directory, &arena);
        }
        if (!keep && tally.failures == 0)
        {
            char* remove[] = { (char*)"rm", (char*)"-rf", directory, 0 };
            object_test_run(remove, "/dev/null", "/dev/null");
        }
        else
        {
            printf("  kept %s\n", directory);
        }
    }
    arena_release(&arena);
    printf("check debug: %llu checks, %llu failures\n", (unsigned long long)tally.checks, (unsigned long long)tally.failures);
    return tally.failures == 0 ? 0 : 1;
}
