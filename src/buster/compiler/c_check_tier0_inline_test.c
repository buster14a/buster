// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier0-inline (#1398): Tier 0's splice (tier0_inline.c) and its use in the emission (c_check_tier0_inline.c) on
// real C, the vector path against its scalar twin. Include after tier0_source_benchmark.c and c_checker_driver_test.c.
//
//   build/compiler test-tier0-inline [preprocessor options] [file...]
//
// Each file is prepared as -c prepares it (check_source_prepare), its reachable definitions lowered by check_program_emit,
// and the unit's inline callees' table built as the emission builds it (check_tier1_inline_table). Then every body goes
// through check_tier0_inline_body twice, by the vector path and by the scalar twin, each with its own lane cache: both
// must splice the same bodies, and every column of each G must be the same bytes, its starts too; tier0_validate accepts
// each (the splice's own check) and Tier 0 compiles it. Without a file, it takes the driver fixture
// (check_test_tier0_inline_source) at x86-64-v4. The driver fixture (test-checker-driver) runs the programs.

typedef struct Tier0InlineTestTally Tier0InlineTestTally;
struct Tier0InlineTestTally
{
    uint64_t checks;
    uint64_t failures;
    uint64_t bodies;
    uint64_t spliced;
    uint64_t sites;
    uint64_t entries_before;
    uint64_t entries_after;
};

static void tier0_inline_test_expect(Tier0InlineTestTally* tally, bool condition, const char* file, uint32_t body, const char* message)
{
    tally->checks += 1;
    if (!condition)
    {
        tally->failures += 1;
        fprintf(stderr, "test-tier0-inline: %s: body %u: %s\n", file, body, message);
    }
}

// Whether `a` and `b` are the same bytes for `count` elements of `size`, either both absent.
static bool tier0_inline_test_same_column(const void* a, const void* b, uint64_t count, uint64_t size)
{
    return (a == 0 && b == 0) || (a != 0 && b != 0 && memcmp(a, b, count * size) == 0) || count == 0;
}

// Whether two spliced streams are the same in every column the splice writes.
static bool tier0_inline_test_same(const Tier0Function* a, const Tier0Function* b)
{
    bool same = a->count == b->count && a->local_count == b->local_count && a->call_count == b->call_count && a->argument_count == b->argument_count &&
                a->operation_count == b->operation_count && a->x87_constant_count == b->x87_constant_count &&
                a->vector_constant_count == b->vector_constant_count && a->vectors == b->vectors && a->masks == b->masks && a->x87 == b->x87 &&
                a->int128 == b->int128 && a->vector_bytes == b->vector_bytes && a->label_count == b->label_count && a->table_count == b->table_count;
    same = same && tier0_inline_test_same_column(a->kinds, b->kinds, a->count, 1) && tier0_inline_test_same_column(a->classes, b->classes, a->count, 1) &&
           tier0_inline_test_same_column(a->values, b->values, a->count, sizeof(uint64_t));
    same = same && tier0_inline_test_same_column(a->local_classes, b->local_classes, a->local_count, 1) &&
           tier0_inline_test_same_column(a->local_flags, b->local_flags, a->local_count, 1) &&
           tier0_inline_test_same_column(a->local_sizes, b->local_sizes, a->local_count, sizeof(uint32_t)) &&
           tier0_inline_test_same_column(a->local_alignments, b->local_alignments, a->local_count, 1);
    same = same && tier0_inline_test_same_column(a->call_targets, b->call_targets, a->call_count, sizeof(SymbolId)) &&
           tier0_inline_test_same_column(a->call_first, b->call_first, a->call_count, sizeof(uint32_t)) &&
           tier0_inline_test_same_column(a->call_argument_counts, b->call_argument_counts, a->call_count, sizeof(uint32_t)) &&
           tier0_inline_test_same_column(a->call_variadic, b->call_variadic, a->call_count, 1) &&
           tier0_inline_test_same_column(a->calls, b->calls, a->call_count, sizeof(AbiCall)) &&
           tier0_inline_test_same_column(a->call_result_objects, b->call_result_objects, a->call_count, sizeof(uint32_t)) &&
           tier0_inline_test_same_column(a->call_tails, b->call_tails, a->call_count, 1);
    same = same && tier0_inline_test_same_column(a->argument_locations, b->argument_locations, a->argument_count, sizeof(AbiLocation)) &&
           tier0_inline_test_same_column(a->argument_classes, b->argument_classes, a->argument_count, 1) &&
           tier0_inline_test_same_column(a->argument_sizes, b->argument_sizes, a->argument_count, sizeof(uint32_t));
    same = same && tier0_inline_test_same_column(a->operations, b->operations, a->operation_count, sizeof(Tier0Operation)) &&
           tier0_inline_test_same_column(a->x87_constants, b->x87_constants, 2 * (uint64_t)a->x87_constant_count, sizeof(uint64_t)) &&
           tier0_inline_test_same_column(a->x87_symbols, b->x87_symbols, a->x87_constant_count, sizeof(SymbolId)) &&
           tier0_inline_test_same_column(a->vector_constants, b->vector_constants, 8 * (uint64_t)a->vector_constant_count, sizeof(uint64_t)) &&
           tier0_inline_test_same_column(a->vector_constant_bytes, b->vector_constant_bytes, a->vector_constant_count, 1) &&
           tier0_inline_test_same_column(a->label_entries, b->label_entries, a->label_count, sizeof(uint32_t));
    return same;
}

// One file's bodies by both paths (the file's comment): false where it couldn't be prepared or emitted.
static bool tier0_inline_test_file(Tier0InlineTestTally* tally, const char* file, char** options, uint32_t option_count)
{
    CheckSource prepared;
    const bool chosen = check_source_prepare(&prepared, file, options, option_count, (CheckSourceRequest){ .select = true, .emits = true });
    const CheckProgramSelection* selected = &prepared.selected;
    const CheckProgram* program = &selected->program;
    Arena lowered = arena_reserve(UINT64_C(1) << 34);
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    Arena work = arena_reserve(UINT64_C(1) << 34);
    CheckProgramCode code = { .status = CHECK_PROGRAM_RESOURCE, .token = BUSTER_CHECK_NONE };
    const bool emitted = chosen && lowered.base != 0 && bytes.base != 0 && work.base != 0 &&
                         check_program_emit(&code, program, prepared.parser, prepared.pp->table, selected->data.capacity, false, &lowered, &bytes,
                                            &prepared.path.scratch, NULL, NULL, false, false);
    uint8_t* defined = 0;
    const bool accessed = emitted && check_program_access(&code, program, &lowered, &prepared.path.scratch, &defined);
    const uint32_t count = program->body_count;
    const uint32_t symbol_count = accessed && count != 0 ? code.lowered.functions[0].symbol_count : 0;
    CheckTier1Inline table = { 0 };
    const bool callees = accessed && count != 0 && check_tier1_inline_table(program, &code.lowered, symbol_count, false, &work, &table);
    if (callees)
    {
        memset(table.ready, 1, count);
    }
    CheckTier0InlineLane caches[2] = { 0 };
    uint64_t sites = 0;
    for (uint32_t b = 0; callees && b < count; b += 1)
    {
        Tier0Inlined inlined[2];
        bool spliced[2];
        for (uint32_t path = 0; path < 2; path += 1)
        {
            CheckEmitRun run = { .program = program, .lowered = &code.lowered, .scalar = path != 0, .tier0_inline = &table, .inline_table = &table };
            CheckEmitLane lane = { .tier0_inline = &caches[path] };
            spliced[path] = check_tier0_inline_body(&run, &lane, b, &work, &inlined[path]);
        }
        const Tier0Function* function = &code.lowered.functions[b];
        tally->bodies += 1;
        tier0_inline_test_expect(tally, spliced[0] == spliced[1], file, b, "the two paths splice the same bodies");
        if (spliced[0] && spliced[1])
        {
            tally->spliced += 1;
            sites += inlined[0].sites;
            tally->entries_before += function->count;
            tally->entries_after += inlined[0].function.count;
            tier0_inline_test_expect(tally, inlined[0].sites == inlined[1].sites && tier0_inline_test_same(&inlined[0].function, &inlined[1].function), file, b,
                                     "the two paths' streams are the same bytes");
            tier0_inline_test_expect(tally, memcmp(inlined[0].starts, inlined[1].starts, ((uint64_t)function->count + 1) * sizeof(uint32_t)) == 0, file, b,
                                     "the two paths' starts are the same");
            bool increasing = inlined[0].starts[function->count] == inlined[0].function.count;
            for (uint32_t e = 0; increasing && e < function->count; e += 1)
            {
                increasing = inlined[0].starts[e] <= inlined[0].starts[e + 1];
            }
            tier0_inline_test_expect(tally, increasing, file, b, "the starts increase to G's end");
            tier0_inline_test_expect(tally, tier0_validate(&inlined[0].function).error == TIER0_OK, file, b, "tier0_validate accepts the spliced stream");
            const uint64_t mark = work.length;
            const Tier0Code compiled = tier0_compile(&inlined[0].function, &work);
            const Tier0Code twin = tier0_compile_scalar(&inlined[1].function, &work);
            tier0_inline_test_expect(tally, compiled.error == TIER0_OK && twin.error == TIER0_OK && compiled.count == twin.count, file, b,
                                     "Tier 0 compiles the spliced stream by both paths");
            arena_pop_to(&work, mark);
        }
    }
    tally->sites += sites;
    const bool valid = emitted && accessed;
    printf("test-tier0-inline: file=%s bodies=%u callees=%s sites=%" PRIu64 "\n", file, count, callees ? "yes" : "no", sites);
    if (!valid)
    {
        fprintf(stderr, "test-tier0-inline: %s: couldn't be prepared and lowered (%s)\n", file, code.reason != NULL ? code.reason : "a refusal or a resource");
    }
    for (uint32_t path = 0; path < 2; path += 1)
    {
        arena_release(&caches[path].cache);
    }
    arena_release(&work);
    arena_release(&bytes);
    arena_release(&lowered);
    check_source_release(&prepared);
    return valid;
}

static int run_tier0_inline_tests(int count, char** arguments)
{
    char** options = calloc((size_t)count + 2, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (arguments[i][0] != '-')
        {
            files[file_count++] = arguments[i];
        }
        else
        {
            options[option_count++] = arguments[i];
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count++] = valid ? arguments[++i] : 0;
            }
        }
    }
    // Without a file, the driver fixture at x86-64-v4.
    char fixture[] = "/tmp/buster-tier0-inline-XXXXXX.c";
    const bool own = valid && file_count == 0;
    if (own)
    {
        const int descriptor = mkstemps(fixture, 2);
        valid = descriptor >= 0;
        if (valid)
        {
            close(descriptor);
            valid = parse_test_write(fixture, check_test_tier0_inline_source, strlen(check_test_tier0_inline_source));
        }
        files[file_count++] = fixture;
        options[option_count++] = "-march=x86-64-v4";
    }
    Tier0InlineTestTally tally = { 0 };
    for (uint32_t f = 0; valid && f < file_count; f += 1)
    {
        valid = tier0_inline_test_file(&tally, files[f], options, option_count);
    }
    if (own)
    {
        unlink(fixture);
        // The fixture's calls of always_inline functions are inlined.
        tally.checks += 1;
        tally.failures += valid && tally.sites >= 20 ? 0 : 1;
    }
    printf("test-tier0-inline: %" PRIu64 " bodies, %" PRIu64 " spliced, %" PRIu64 " calls inlined, entries %" PRIu64 " -> %" PRIu64 "; %" PRIu64 " checks, %" PRIu64
           " failures\n",
           tally.bodies, tally.spliced, tally.sites, tally.entries_before, tally.entries_after, tally.checks, tally.failures);
    if (!valid)
    {
        fprintf(stderr, "usage: compiler test-tier0-inline [preprocessor options] [file...]\n");
    }
    free(files);
    free(options);
    return valid && tally.failures == 0 ? 0 : 1;
}
