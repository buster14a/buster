// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #106's identity test, for test-fragments: a unit the checker reads with the fragment readers (c_check_fragments.c)
// must give the postorder reader's result, column for column. Each unit is checked as test-checker-corpus checks it
// (check_corpus_check_policy: the scalar parser and schedule, check_input, check_graph_run, the asm operands, the
// initializers and the program), once as the reference, then once per fragment mode: variants B (flat prefix) and C
// (direct windows), each with the interleave's vector path and its scalar twin, at each operator payload (16, 32 and
// 48 bytes). check_corpus_equal compares each with the reference: the parser's tree and diagnostics, every column of
// the input and the graph, the typed stream, the initializers and the program. The reader also checks each operator's
// operands against its children's in the tree as it takes them (CFragmentRequest.verify), and must read every published
// expression of a unit without reports. The vector path (D) is compared with the reference too. Included by main.c in
// the fragments' unit alone, after c_check_corpus.c.

#ifndef BUSTER_C_CHECK_FRAGMENTS_TEST_INCLUDED
#define BUSTER_C_CHECK_FRAGMENTS_TEST_INCLUDED

typedef struct CheckFragmentsTestMode CheckFragmentsTestMode;
struct CheckFragmentsTestMode
{
    const char* name;
    uint32_t payload;
    bool flat;
    bool vector;
};

static const CheckFragmentsTestMode check_fragments_test_modes[] =
{
    { "B/16/vector", 16, true, true },
    { "B/16/scalar", 16, true, false },
    { "C/16/vector", 16, false, true },
    { "C/16/scalar", 16, false, false },
    { "B/32/vector", 32, true, true },
    { "B/32/scalar", 32, true, false },
    { "C/32/vector", 32, false, true },
    { "C/32/scalar", 32, false, false },
    { "B/48/vector", 48, true, true },
    { "B/48/scalar", 48, true, false },
    { "C/48/vector", 48, false, true },
    { "C/48/scalar", 48, false, false },
};

// What the identity test compared: the units, those the checker accepted, the fragment readings compared with their
// unit's reference and their totals, and the failures.
typedef struct CheckFragmentsTestTotals CheckFragmentsTestTotals;
struct CheckFragmentsTestTotals
{
    uint64_t units;
    uint64_t accepted;
    uint64_t readings;
    uint64_t expressions;
    uint64_t operators;
    uint64_t raw_operators;
    uint64_t leaves;
    uint64_t events;
    uint64_t flat_positions;
    uint32_t cursor_peak;
    uint32_t frame_peak;
    uint64_t failures;
};

// The fragment readers on one preprocessed unit, `name` for the messages: the reference, each mode's reading and the
// vector path, each compared with the reference. Returns the failures.
static uint32_t check_fragments_test_unit(Preprocessor* pp, const char* name, CheckFragmentsTestTotals* totals)
{
    static Parser parsers[2];
    uint32_t failures = 0;
    CheckCorpusPath reference = { 0 };
    const bool accepted = check_corpus_check_policy(&reference, &parsers[0], pp, true, false);
    totals->units += 1;
    totals->accepted += accepted;
    for (uint32_t m = 0; reference.parsed && m < BUSTER_ARRAY_LENGTH(check_fragments_test_modes); m += 1)
    {
        const CheckFragmentsTestMode* mode = &check_fragments_test_modes[m];
        CheckCorpusPath path = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 36);
        Arena temporary = arena_reserve(UINT64_C(1) << 36);
        CFragmentRequest request =
        {
            .arena = &arena,
            .temporary = &temporary,
            .payload = mode->payload,
            .flat = mode->flat,
            .vector = mode->vector,
            .verify = true,
        };
        bool valid = arena.base != 0 && temporary.base != 0;
        if (valid)
        {
            c_fragment_request = &request;
            check_corpus_check_policy(&path, &parsers[1], pp, true, false);
            c_fragment_request = 0;
            const CheckFragments* consumer = request.consumer;
            // A unit whose expressions were never read (the checker stopped before them) has no consumer to look at.
            valid = request.filled && request.sink.store.valid && !request.sink.syntax_error && request.sink.unsupported == 0 &&
                    (consumer == 0 ? !path.adapted && !path.input.diagnostic_graph : consumer->valid);
            if (!valid)
            {
                fprintf(stderr, "test-fragments: %s: the %s reader failed: filled=%u store=%u syntax_error=%u unsupported=%u (first %u) consumer=%u failed at token %u\n", name,
                        mode->name, request.filled, request.sink.store.valid, request.sink.syntax_error, request.sink.unsupported, request.sink.first_unsupported,
                        consumer != 0 && consumer->valid, consumer != 0 ? consumer->failed : BUSTER_CHECK_NONE);
            }
            const bool equal = check_corpus_equal(&reference, &path, &parsers[0], &parsers[1]);
            if (!equal)
            {
                fprintf(stderr, "test-fragments: %s: the %s reader's checker result differs from the postorder reader's\n", name, mode->name);
            }
            valid &= equal;
            if (valid && consumer != 0)
            {
                const CheckFragmentStatistics* statistics = &consumer->statistics;
                totals->readings += 1;
                totals->expressions += statistics->expressions;
                totals->operators += statistics->operators;
                totals->raw_operators += statistics->raw_operators;
                totals->leaves += statistics->leaves;
                totals->events += statistics->events;
                totals->flat_positions += statistics->flat_positions;
                totals->cursor_peak = statistics->cursor_peak > totals->cursor_peak ? statistics->cursor_peak : totals->cursor_peak;
                totals->frame_peak = statistics->frame_peak > totals->frame_peak ? statistics->frame_peak : totals->frame_peak;
            }
            check_corpus_close(&path);
            parse_release(&parsers[1]);
        }
        failures += !valid;
        arena_release(&temporary);
        arena_release(&arena);
    }
    if (reference.parsed)
    {
        // D: the vector path (its parser and the checker's vector schedule) has the reference's result too.
        CheckCorpusPath vector = { 0 };
        check_corpus_check_policy(&vector, &parsers[1], pp, false, false);
        const bool equal = check_corpus_equal(&reference, &vector, &parsers[0], &parsers[1]);
        if (!equal)
        {
            fprintf(stderr, "test-fragments: %s: the vector path's checker result differs from the postorder reader's\n", name);
        }
        failures += !equal;
        check_corpus_close(&vector);
        parse_release(&parsers[1]);
    }
    check_corpus_close(&reference);
    parse_release(&parsers[0]);
    totals->failures += failures;
    return failures;
}

// test-checker's source fixtures (check_test_source's hook, in the fragments' unit alone): each fixture's unit, already
// preprocessed with its options, through check_fragments_test_unit, into these totals.
static CheckFragmentsTestTotals check_fragments_test_checker_totals;

static uint32_t check_fragments_test_fixture(Preprocessor* pp, const char* name)
{
    return check_fragments_test_unit(pp, name, &check_fragments_test_checker_totals);
}

static void check_fragments_test_print(const char* dataset, const CheckFragmentsTestTotals* totals);

// test-fragments --checker-fixtures: test-checker's sections of source fixtures, each with its clang oracle, whose
// fixtures check_test_source also reads with the fragment readers. Returns the failures.
static uint64_t check_fragments_test_checker_fixtures(const char* clang)
{
    CheckTest test = { .random = 133 };
    CheckTestTypes types = { 0 };
    const bool opened = check_test_types_open(&types);
    check_test_expect(&test, opened, "type-table allocation");
    if (opened)
    {
        check_test_sources(&test, clang);
        check_test_source_extensions(&test, clang);
        check_test_overload_sources(&test, clang);
        check_test_constant_sources(&test, clang);
        check_test_enum_sources(&test, clang);
        check_test_vla_sources(&test, clang);
        check_test_transparent_calls(&test, &types.table, clang);
        check_test_transparent_types(&test, clang);
        check_test_integer_extensions(&test, clang);
        check_test_pragmas(&test, clang);
        check_test_attribute_sources(&test, clang);
    }
    check_test_types_close(&types);
    printf("fragment-checker-fixtures: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    check_fragments_test_print("checker-fixtures", &check_fragments_test_checker_totals);
    return test.failures;
}

static void check_fragments_test_print(const char* dataset, const CheckFragmentsTestTotals* totals)
{
    printf("fragment-checker dataset=%s units=%" PRIu64 " accepted=%" PRIu64 " readings=%" PRIu64 " expressions=%" PRIu64 " operators=%" PRIu64
           " tree_operators=%" PRIu64 " leaves=%" PRIu64 " events=%" PRIu64 " flat_positions=%" PRIu64 " cursor_peak=%u frame_peak=%u failures=%" PRIu64 "\n",
           dataset, totals->units, totals->accepted, totals->readings, totals->expressions, totals->operators, totals->raw_operators, totals->leaves, totals->events,
           totals->flat_positions, totals->cursor_peak, totals->frame_peak, totals->failures);
}

#endif
