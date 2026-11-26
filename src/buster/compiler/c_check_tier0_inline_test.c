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
// Before the files, the splice's fills and the unit stage's scan for the candidates whose address code takes are checked
// against their definitions on random columns (tier0_inline_test_fills, tier0_inline_test_addressed). Loop-home
// regressions compare the speculative allocation with emitted homes (tier0_inline_homes_test.c, #2841).

#include <stddef.h>

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

// The driver fixture's spliced streams at x86-64-v4, without and with --tier0-inline-shapes. Both splice ping into
// pong and its outside caller. The driver also checks the resulting symbols and runs each build against clang -O0.
// With shapes, sgn's returns splice, while the home check keeps sum_to and widen outlined in scalars (#2841).
#define BUSTER_TIER0_INLINE_TEST_DIGEST UINT64_C(0x7bbcb0712d3152a4)
#define BUSTER_TIER0_INLINE_TEST_SHAPES_DIGEST UINT64_C(0x4a950a7e02bea723)

static void tier0_inline_test_expect(Tier0InlineTestTally* tally, bool condition, const char* file, uint32_t body, const char* message)
{
    tally->checks += 1;
    if (!condition)
    {
        tally->failures += 1;
        fprintf(stderr, "test-tier0-inline: %s: body %u: %s\n", file, body, message);
    }
}

#include "buster/compiler/tier0_inline_homes_test.c"

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
    // The frozen corpus predates asm-result projection rows (#2144). Keep its serialization to the original
    // operation fields, without the new byte or trailing padding. The splice and validation still use complete rows.
    for (uint32_t row = 0; row < a->operation_count; row += 1)
    {
        digest = tier0_inline_test_fold(digest, &a->operations[row], offsetof(Tier0Operation, asm_result));
    }
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
    const bool callees = accessed && count != 0 && check_tier1_inline_table(program, &code.lowered, symbol_count, false, false, &work, NULL, &table);
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

static uint64_t tier0_inline_test_random(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

// The splice's fills against their definitions (#2924), on random columns, every lane of their 64-entry steps and of
// their tails: tier0_inline_segment copies the caller's entries [from, to), a branch's target (tier0_inline_branch)
// remapped through the starts where it is within the caller; tier0_inline_body copies a callee's entries past the site's
// skipped ones, a RETURN made a JUMP (VOID) or a JUMP_VALUE to the body's end, each value rebased from the site's bases
// (tier0_inline_bases) by its meaning (tier0_inline_meaning, a CONST's by its class), a target past the body clamped to
// it. Neither writes outside the entries it returns.
static void tier0_inline_test_fills(Tier0InlineTestTally* tally)
{
    enum
    {
        CAPACITY = 200,
        SLACK = 64,
    };
    uint64_t random = UINT64_C(0x9e3779b97f4a7c15);
    uint8_t kinds[CAPACITY];
    uint8_t classes[CAPACITY];
    uint64_t values[CAPACITY];
    uint32_t starts[CAPACITY + 1];
    uint8_t g_kinds[CAPACITY + 2 * SLACK];
    uint8_t g_classes[CAPACITY + 2 * SLACK];
    uint64_t g_values[CAPACITY + 2 * SLACK];
    bool segments = true;
    bool bodies = true;
    uint64_t remapped = 0;
    uint64_t clamped = 0;
    uint64_t rewritten = 0;
    uint64_t constants[2] = { 0, 0 };
    for (uint32_t round = 0; round < 400; round += 1)
    {
        const uint32_t count = 1 + (uint32_t)(tier0_inline_test_random(&random) % (CAPACITY - 10));
        for (uint32_t e = 0; e < count; e += 1)
        {
            kinds[e] = (uint8_t)(tier0_inline_test_random(&random) % BUSTER_TYPED_KIND_LIMIT);
            classes[e] = (uint8_t)(tier0_inline_test_random(&random) % BUSTER_TYPED_CLASS_LIMIT);
            const uint64_t pick = tier0_inline_test_random(&random);
            values[e] = pick % 4 == 0 ? pick >> 20 : (pick >> 2) % ((uint64_t)count + 3);
        }
        for (uint32_t e = 0; e <= count; e += 1)
        {
            starts[e] = (uint32_t)(tier0_inline_test_random(&random) % 100000);
        }
        const Tier0Function function = { .kinds = kinds, .classes = classes, .values = values, .count = count };
        Tier0InlineColumns g = { .kinds = g_kinds, .classes = g_classes, .values = g_values };
        const uint32_t at = SLACK / 2 + (uint32_t)(tier0_inline_test_random(&random) % (SLACK / 2));
        // The caller's segment.
        const uint32_t from = (uint32_t)(tier0_inline_test_random(&random) % count);
        const uint32_t to = from + (uint32_t)(tier0_inline_test_random(&random) % (count - from + 1));
        memset(g_kinds, 0xa5, sizeof(g_kinds));
        memset(g_classes, 0xa5, sizeof(g_classes));
        memset(g_values, 0xa5, sizeof(g_values));
        Tier0InlinePlan plan = { .starts = starts };
        const uint32_t segment_end = tier0_inline_segment(&function, &plan, from, to, &g, at);
        bool same = segment_end == at + (to - from);
        for (uint32_t e = from; same && e < to; e += 1)
        {
            const bool branch = tier0_inline_branch(kinds[e]) && values[e] <= count;
            remapped += branch ? 1u : 0u;
            same = g_kinds[at + e - from] == kinds[e] && g_classes[at + e - from] == classes[e] && g_values[at + e - from] == (branch ? starts[values[e]] : values[e]);
        }
        for (uint32_t w = 0; same && w < CAPACITY + 2 * SLACK; w += 1)
        {
            same = (w >= at && w < segment_end) || (g_kinds[w] == 0xa5 && g_classes[w] == 0xa5 && g_values[w] == UINT64_C(0xa5a5a5a5a5a5a5a5));
        }
        segments = segments && same;
        // The callee's body at a site, forwarded (its first parameters' entries left out) one round in four.
        Tier0InlineCallee distinct = {
            .body = count,
            .operation_base = (uint32_t)(tier0_inline_test_random(&random) % 5000),
            .x87_base = (uint32_t)(tier0_inline_test_random(&random) % 5000),
            .vector_base = (uint32_t)(tier0_inline_test_random(&random) % 5000),
        };
        const bool forward = tier0_inline_test_random(&random) % 4 == 0;
        const Tier0InlineSite site = {
            .local_base = (uint32_t)(tier0_inline_test_random(&random) % 5000),
            .row_base = (uint32_t)(tier0_inline_test_random(&random) % 5000),
            .body_base = 1000 + (uint32_t)(tier0_inline_test_random(&random) % 5000),
            .forward = forward,
            .parameters = forward ? (uint32_t)(tier0_inline_test_random(&random) % count) : 0,
        };
        plan.distinct = &distinct;
        uint64_t bases[BUSTER_TIER0_INLINE_MEANINGS];
        tier0_inline_bases(&plan, &site, bases);
        const uint32_t skipped = tier0_inline_skipped(&site);
        memset(g_kinds, 0xa5, sizeof(g_kinds));
        memset(g_classes, 0xa5, sizeof(g_classes));
        memset(g_values, 0xa5, sizeof(g_values));
        const uint32_t body_end = tier0_inline_body(&function, &plan, &site, &g, at);
        same = body_end == at + (count - skipped);
        for (uint32_t e = skipped; same && e < count; e += 1)
        {
            const bool returns = kinds[e] == BUSTER_TYPED_RETURN;
            const uint32_t kind = !returns ? kinds[e] : classes[e] == BUSTER_TYPED_CLASS_VOID ? BUSTER_TYPED_JUMP : BUSTER_TYPED_JUMP_VALUE;
            const uint32_t meaning = tier0_inline_meaning(kind, classes[e]);
            const uint64_t target = returns ? count : values[e];
            const bool clamp = meaning == BUSTER_TIER0_INLINE_ENTRY && target > count;
            rewritten += returns ? 1u : 0u;
            clamped += clamp ? 1u : 0u;
            constants[0] += kind == BUSTER_TYPED_CONST && meaning == BUSTER_TIER0_INLINE_X87 ? 1u : 0u;
            constants[1] += kind == BUSTER_TYPED_CONST && meaning == BUSTER_TIER0_INLINE_VECTOR ? 1u : 0u;
            const uint64_t wanted = (clamp ? count : target) + bases[meaning];
            same = g_kinds[at + e - skipped] == kind && g_classes[at + e - skipped] == classes[e] && g_values[at + e - skipped] == wanted;
        }
        for (uint32_t w = 0; same && w < CAPACITY + 2 * SLACK; w += 1)
        {
            same = (w >= at && w < body_end) || (g_kinds[w] == 0xa5 && g_classes[w] == 0xa5 && g_values[w] == UINT64_C(0xa5a5a5a5a5a5a5a5));
        }
        bodies = bodies && same;
    }
    tier0_inline_test_expect(tally, segments, "fills", 0, "the caller's segments copy every lane, a branch's target remapped through the starts");
    tier0_inline_test_expect(tally, bodies, "fills", 1, "the callee's bodies copy every lane, a RETURN made a jump to the end, each value rebased by its meaning and a target past the body clamped");
    tier0_inline_test_expect(tally, remapped != 0 && clamped != 0 && rewritten != 0 && constants[0] != 0 && constants[1] != 0, "fills", 2,
                             "the random columns reach remapped targets, clamped targets, rewritten RETURNs and both constants' meanings");
    printf("test-tier0-inline: fills: %" PRIu64 " targets remapped, %" PRIu64 " clamped, %" PRIu64 " RETURNs rewritten, %" PRIu64 " x87 and %" PRIu64 " vector constants rebased\n",
           remapped, clamped, rewritten, constants[0], constants[1]);
}

// The unit stage's scan for the candidates whose address code takes (check_tier1_inline_addressed_entries, #2541 step 2)
// against its definition (#2924), at every count to 200: each entry of kind BUSTER_TYPED_GLOBAL_ADDRESS marks the candidate
// its value names, or the byte past the bodies where it names none (0, a symbol past the table's, or one that is no
// candidate). Each count runs its random kinds, then its last entry alone an address, then each full batch's last lane
// alone one; the scan writes no byte but its marks.
static void tier0_inline_test_addressed(Tier0InlineTestTally* tally)
{
    enum
    {
        CAPACITY = 200,
        SYMBOLS = 40,
        BODIES = 24,
        GUARD = 16,
    };
    uint64_t random = UINT64_C(0x2545f4914f6cdd1d);
    uint8_t kinds[CAPACITY];
    uint64_t values[CAPACITY];
    uint32_t candidates[SYMBOLS];
    uint8_t addressed[BODIES + 1 + GUARD];
    uint8_t expected[BODIES + 1 + GUARD];
    bool same = true;
    uint64_t marks = 0;
    uint64_t others = 0;
    uint64_t lasts = 0;
    for (uint32_t count = 0; count <= CAPACITY; count += 1)
    {
        for (uint32_t shape = 0; shape < 2 + count / 64; shape += 1)
        {
            for (uint32_t s = 0; s < SYMBOLS; s += 1)
            {
                const uint64_t pick = tier0_inline_test_random(&random);
                candidates[s] = pick % 4 == 0 ? 0 : 1 + (uint32_t)((pick >> 2) % BODIES);
            }
            for (uint32_t e = 0; e < count; e += 1)
            {
                const uint64_t pick = tier0_inline_test_random(&random);
                // Shape 0, random kinds; shape 1, the last entry alone an address; shape 2 + k, batch k's last lane alone.
                const bool address = shape == 0 ? pick % 8 == 0 : shape == 1 ? e + 1 == count : e == (shape - 2) * 64 + 63;
                const uint8_t other = (uint8_t)((pick >> 3) % BUSTER_TYPED_KIND_LIMIT);
                kinds[e] = address ? BUSTER_TYPED_GLOBAL_ADDRESS : other == BUSTER_TYPED_GLOBAL_ADDRESS ? (uint8_t)((other + 1) % BUSTER_TYPED_KIND_LIMIT) : other;
                // Mostly a symbol of the table (0 among them), sometimes one past it, sometimes any 64-bit value.
                values[e] = pick % 16 == 0 ? pick : (pick >> 8) % (SYMBOLS + 4);
            }
            memset(addressed, 0, BODIES + 1);
            memset(addressed + BODIES + 1, 0xa5, GUARD);
            memcpy(expected, addressed, sizeof(expected));
            for (uint32_t e = 0; e < count; e += 1)
            {
                if (kinds[e] == BUSTER_TYPED_GLOBAL_ADDRESS)
                {
                    const uint32_t candidate = values[e] != 0 && values[e] < SYMBOLS ? candidates[values[e]] : 0;
                    expected[candidate != 0 ? candidate - 1 : BODIES] = 1;
                    marks += candidate != 0 ? 1u : 0u;
                    others += candidate == 0 ? 1u : 0u;
                    lasts += shape != 0 ? 1u : 0u;
                }
            }
            check_tier1_inline_addressed_entries(kinds, values, count, candidates, SYMBOLS, BODIES, addressed);
            same = same && memcmp(addressed, expected, sizeof(expected)) == 0;
        }
    }
    tier0_inline_test_expect(tally, same, "addressed", 0, "the scan marks each address's candidate, or the byte past the bodies, at every count and lane, and writes nothing else");
    tier0_inline_test_expect(tally, marks != 0 && others != 0 && lasts != 0, "addressed", 1, "the random columns reach candidates, other symbols and the lone last entries");
    printf("test-tier0-inline: addressed: %" PRIu64 " candidates marked, %" PRIu64 " other symbols, %" PRIu64 " lone last entries\n", marks, others, lasts);
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
    if (valid)
    {
        tier0_inline_test_fills(&tally);
        tier0_inline_test_addressed(&tally);
        tier0_inline_test_homes(&tally);
    }
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
