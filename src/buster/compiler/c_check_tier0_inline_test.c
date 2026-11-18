// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier0-inline (#1398): Tier 0's splice (tier0_inline.c) and its use in the emission (c_check_tier0_inline.c) on
// real C. Include after tier0_source_benchmark.c and c_checker_driver_test.c.
//
//   build/compiler test-tier0-inline [--tier0-inline-shapes] [preprocessor options] [file...]
//
// Each file is prepared as -c prepares it (check_source_prepare), its reachable definitions lowered by check_program_emit,
// and the unit's inline callees' table built as the emission builds it (check_tier1_inline_table). Then every body goes
// through check_tier0_inline_body with the lane's cache: each G's starts must increase to its end, tier0_validate must
// accept it (the splice's own check) and Tier 0 compile it. Without a file, it takes the driver fixture
// (check_test_tier0_inline_source) at x86-64-v4, whose spliced streams, every column and the starts, must fold to the
// frozen digest of their run (with and without the shapes). --tier0-inline-shapes splices callees with several returns or
// a result in memory too, as -c's option does (#1398 step 5). The driver fixture (test-checker-driver) runs the programs.

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
    // The spliced streams' columns and starts, folded in body order (tier0_inline_test_fold_function).
    uint64_t digest;
};

// The driver fixture's spliced streams' digests at x86-64-v4, without and with --tier0-inline-shapes. Frozen on the tree
// where the splice's vector path and its scalar twin still gave the same bytes in every column (#2924); a change to the
// splice, to the lowering that feeds it or to the fixture moves them, and a run reports its own.
#define BUSTER_TIER0_INLINE_TEST_DIGEST UINT64_C(0xac830c6310ee733c)
#define BUSTER_TIER0_INLINE_TEST_SHAPES_DIGEST UINT64_C(0x8c6c31f087ae1e75)

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
static uint64_t tier0_inline_test_fold(uint64_t digest, const void* bytes, uint64_t size)
{
    const uint8_t* data = bytes;
    for (uint64_t i = 0; data != 0 && i < size; i += 1)
    {
        digest = (digest ^ data[i]) * UINT64_C(0x100000001b3);
    }
    return digest;
}

// A spliced stream folded into `digest`: its counts and flags, then every column the splice writes.
static uint64_t tier0_inline_test_fold_function(uint64_t digest, const Tier0Function* a)
{
    const uint32_t counts[] = {
        a->count,
        a->local_count,
        a->call_count,
        a->argument_count,
        a->operation_count,
        a->x87_constant_count,
        a->vector_constant_count,
        a->vectors,
        a->masks,
        a->x87,
        a->int128,
        a->vector_bytes,
        a->label_count,
        a->table_count,
    };
    digest = tier0_inline_test_fold(digest, counts, sizeof(counts));
    digest = tier0_inline_test_fold(digest, a->kinds, a->count);
    digest = tier0_inline_test_fold(digest, a->classes, a->count);
    digest = tier0_inline_test_fold(digest, a->values, (uint64_t)a->count * sizeof(uint64_t));
    digest = tier0_inline_test_fold(digest, a->local_classes, a->local_count);
    digest = tier0_inline_test_fold(digest, a->local_flags, a->local_count);
    digest = tier0_inline_test_fold(digest, a->local_sizes, (uint64_t)a->local_count * sizeof(uint32_t));
    digest = tier0_inline_test_fold(digest, a->local_alignments, a->local_count);
    digest = tier0_inline_test_fold(digest, a->call_targets, (uint64_t)a->call_count * sizeof(SymbolId));
    digest = tier0_inline_test_fold(digest, a->call_first, (uint64_t)a->call_count * sizeof(uint32_t));
    digest = tier0_inline_test_fold(digest, a->call_argument_counts, (uint64_t)a->call_count * sizeof(uint32_t));
    digest = tier0_inline_test_fold(digest, a->call_variadic, a->call_count);
    digest = tier0_inline_test_fold(digest, a->calls, (uint64_t)a->call_count * sizeof(AbiCall));
    digest = tier0_inline_test_fold(digest, a->call_result_objects, (uint64_t)a->call_count * sizeof(uint32_t));
    digest = tier0_inline_test_fold(digest, a->call_tails, a->call_count);
    digest = tier0_inline_test_fold(digest, a->argument_locations, (uint64_t)a->argument_count * sizeof(AbiLocation));
    digest = tier0_inline_test_fold(digest, a->argument_classes, a->argument_count);
    digest = tier0_inline_test_fold(digest, a->argument_sizes, (uint64_t)a->argument_count * sizeof(uint32_t));
    digest = tier0_inline_test_fold(digest, a->operations, (uint64_t)a->operation_count * sizeof(Tier0Operation));
    digest = tier0_inline_test_fold(digest, a->x87_constants, 2 * (uint64_t)a->x87_constant_count * sizeof(uint64_t));
    digest = tier0_inline_test_fold(digest, a->vector_constants, 8 * (uint64_t)a->vector_constant_count * sizeof(uint64_t));
    digest = tier0_inline_test_fold(digest, a->vector_constant_bytes, a->vector_constant_count);
    digest = tier0_inline_test_fold(digest, a->label_entries, (uint64_t)a->label_count * sizeof(uint32_t));
    return digest;
}

// One file's bodies (the file's comment): false where it couldn't be prepared or emitted.
static bool tier0_inline_test_file(Tier0InlineTestTally* tally, const char* file, char** options, uint32_t option_count, bool shapes)
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
    const bool callees = accessed && count != 0 && check_tier1_inline_table(program, &code.lowered, symbol_count, false, false, &work, &table);
    if (callees)
    {
        memset(table.ready, 1, count);
    }
    CheckTier0InlineLane cache = { 0 };
    uint64_t sites = 0;
    for (uint32_t b = 0; callees && b < count; b += 1)
    {
        Tier0Inlined inlined;
        CheckEmitRun run = {
            .program = program,
            .lowered = &code.lowered,
            .tier0_inline = &table,
            .inline_table = &table,
            .tier0_inline_shapes = shapes,
        };
        CheckEmitLane lane = { .tier0_inline = &cache };
        const bool spliced = check_tier0_inline_body(&run, &lane, b, &work, &inlined);
        const Tier0Function* function = &code.lowered.functions[b];
        tally->bodies += 1;
        if (spliced)
        {
            tally->spliced += 1;
            sites += inlined.sites;
            tally->entries_before += function->count;
            tally->entries_after += inlined.function.count;
            const uint64_t body[2] = { b, inlined.sites };
            tally->digest = tier0_inline_test_fold(tally->digest, body, sizeof(body));
            tally->digest = tier0_inline_test_fold(tally->digest, inlined.starts, ((uint64_t)function->count + 1) * sizeof(uint32_t));
            tally->digest = tier0_inline_test_fold_function(tally->digest, &inlined.function);
            bool increasing = inlined.starts[function->count] == inlined.function.count;
            for (uint32_t e = 0; increasing && e < function->count; e += 1)
            {
                increasing = inlined.starts[e] <= inlined.starts[e + 1];
            }
            tier0_inline_test_expect(tally, increasing, file, b, "the starts increase to G's end");
            tier0_inline_test_expect(tally, tier0_validate(&inlined.function).error == TIER0_OK, file, b, "tier0_validate accepts the spliced stream");
            const uint64_t mark = work.length;
            const Tier0Code compiled = tier0_compile(&inlined.function, &work);
            tier0_inline_test_expect(tally, compiled.error == TIER0_OK, file, b, "Tier 0 compiles the spliced stream");
            arena_pop_to(&work, mark);
        }
    }
    tally->sites += sites;
    const bool valid = emitted && accessed;
    printf("test-tier0-inline: file=%s bodies=%u callees=%s shapes=%s sites=%" PRIu64 "\n", file, count, callees ? "yes" : "no", shapes ? "yes" : "no", sites);
    if (!valid)
    {
        fprintf(stderr, "test-tier0-inline: %s: couldn't be prepared and lowered (%s)\n", file, code.reason != NULL ? code.reason : "a refusal or a resource");
    }
    arena_release(&cache.cache);
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
    bool shapes = false;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (arguments[i][0] != '-')
        {
            files[file_count++] = arguments[i];
        }
        else if (strcmp(arguments[i], "--tier0-inline-shapes") == 0)
        {
            shapes = true;
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
    Tier0InlineTestTally tally = { .digest = UINT64_C(0xcbf29ce484222325) };
    for (uint32_t f = 0; valid && f < file_count; f += 1)
    {
        valid = tier0_inline_test_file(&tally, files[f], options, option_count, shapes);
    }
    if (own)
    {
        unlink(fixture);
        // The fixture's calls of always_inline functions are inlined, into the streams of the frozen digest.
        tally.checks += 1;
        tally.failures += valid && tally.sites >= 20 ? 0 : 1;
        const uint64_t frozen = shapes ? BUSTER_TIER0_INLINE_TEST_SHAPES_DIGEST : BUSTER_TIER0_INLINE_TEST_DIGEST;
        tally.checks += 1;
        tally.failures += valid && tally.digest == frozen ? 0 : 1;
        if (tally.digest != frozen)
        {
            fprintf(stderr, "test-tier0-inline: the fixture's spliced streams fold to %016" PRIx64 ", not the frozen %016" PRIx64 "\n", tally.digest, frozen);
        }
    }
    printf("test-tier0-inline: %" PRIu64 " bodies, %" PRIu64 " spliced, %" PRIu64 " calls inlined, entries %" PRIu64 " -> %" PRIu64 "; %" PRIu64 " checks, %" PRIu64
           " failures; digest %016" PRIx64 "\n",
           tally.bodies, tally.spliced, tally.sites, tally.entries_before, tally.entries_after, tally.checks, tally.failures, tally.digest);
    if (!valid)
    {
        fprintf(stderr, "usage: compiler test-tier0-inline [--tier0-inline-shapes] [preprocessor options] [file...]\n");
    }
    free(files);
    free(options);
    return valid && tally.failures == 0 ? 0 : 1;
}
