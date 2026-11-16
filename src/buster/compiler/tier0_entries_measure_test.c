// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// measure-tier0-entries' checks (#2874, tier0_entries_measure.c). First the count's rules on samples made here, where
// the answer is known: a pattern that follows one input is decided by it alone; one that follows two inputs together
// by the pair and by neither; an odd group in a key leaves the key's entries undecided by every input and is one
// exception; and an odd group that a second input sets apart is decided at two inputs and one exception at one. Then
// the census of the fixture under the fixture root, tests/tier0-entries/entries.c: every typed entry is in one group,
// every record an entry's or the frame's, every head that takes a template has its template's records, the call and
// the copy are deferred by their kind and one division by its depth, and a compare with zero and its branch are one
// group of three entries. Its report must have the gate's lines, the totals last. Include after tier0_entries_measure.c; test-checker calls this
// through its forward declaration.
#ifndef BUSTER_TIER0_ENTRIES_MEASURE_TEST_INCLUDED
#define BUSTER_TIER0_ENTRIES_MEASURE_TEST_INCLUDED

// The search over `count` samples made by a case: its row over every input.
static bool tier0_entries_test_search(const Tier0CensusSample* samples, uint32_t count, Tier0CensusRow* row)
{
    uint64_t entries = 0;
    for (uint32_t m = 0; m < count; m += 1)
    {
        entries += samples[m].weight;
    }
    Tier0CensusSearch search = { .cells = calloc(1024, sizeof(Tier0CensusCell)), .pairs = calloc(1024, sizeof(Tier0CensusPair)), .capacity = 1024 };
    const bool room = search.cells != 0 && search.pairs != 0;
    memset(row, 0, sizeof(*row));
    if (room)
    {
        tier0_census_search(&search, samples, count, BUSTER_TIER0_CENSUS_RAW, entries, row);
    }
    free(search.pairs);
    free(search.cells);
    return room;
}

static void check_test_tier0_entries_rules(CheckTest* test)
{
    enum { SAMPLES = 64 };
    Tier0CensusSample samples[SAMPLES];
    Tier0CensusRow row;
    const uint64_t class = 1ull << TIER0_CENSUS_CLASS;
    const uint64_t depth = 1ull << TIER0_CENSUS_DEPTH;
    const uint64_t home = 1ull << TIER0_CENSUS_HOME;
    // The pattern follows the class; the depth and the home vary beside it.
    memset(samples, 0, sizeof(samples));
    for (uint32_t m = 0; m < SAMPLES; m += 1)
    {
        samples[m].inputs[TIER0_CENSUS_CLASS] = (uint8_t)(m % 4);
        samples[m].inputs[TIER0_CENSUS_DEPTH] = (uint8_t)(m % 5);
        samples[m].inputs[TIER0_CENSUS_HOME] = (uint8_t)(m % 3);
        samples[m].pattern = m % 4;
        samples[m].weight = 1;
    }
    bool ran = tier0_entries_test_search(samples, SAMPLES, &row);
    check_test_expect(test, ran && row.set == class && row.inputs == 1 && row.rows == 4 && row.undecided == 0 && row.exceptions == 0,
                      "tier0 entries: a pattern that follows one input has that input as its whole key");
    check_test_expect(test, ran && row.within[BUSTER_TIER0_CENSUS_CONFLICTS][0] == 0 && row.within[BUSTER_TIER0_CENSUS_CONFLICTS][1] == SAMPLES &&
                                row.within[BUSTER_TIER0_CENSUS_EXCEPTIONS][0] == SAMPLES / 4 && row.gate_sets[BUSTER_TIER0_CENSUS_CONFLICTS] == class,
                      "tier0 entries: no input decides none of its groups, one input all of them, and the commonest pattern is a quarter's");
    // The pattern follows the depth and the home together: neither alone decides a group.
    memset(samples, 0, sizeof(samples));
    for (uint32_t m = 0; m < SAMPLES; m += 1)
    {
        const uint32_t a = m & 1;
        const uint32_t b = m >> 1 & 1;
        samples[m].inputs[TIER0_CENSUS_DEPTH] = (uint8_t)a;
        samples[m].inputs[TIER0_CENSUS_HOME] = (uint8_t)b;
        samples[m].inputs[TIER0_CENSUS_CLASS] = (uint8_t)(m >> 2 & 1);
        samples[m].pattern = a ^ b;
        samples[m].weight = 2;
    }
    ran = tier0_entries_test_search(samples, SAMPLES, &row);
    check_test_expect(test, ran && row.set == (depth | home) && row.inputs == 2 && row.rows == 4 && row.undecided == 0,
                      "tier0 entries: a pattern that follows two inputs together has the pair as its whole key");
    check_test_expect(test, ran && row.within[BUSTER_TIER0_CENSUS_CONFLICTS][1] == 0 && row.within[BUSTER_TIER0_CENSUS_CONFLICTS][2] == 2 * SAMPLES,
                      "tier0 entries: neither input of a pair decides a group alone, and the pair decides them all");
    // An odd group with its key's inputs: no input decides the key's entries, and the odd one is the exception.
    memset(samples, 0, sizeof(samples));
    for (uint32_t m = 0; m < SAMPLES; m += 1)
    {
        samples[m].inputs[TIER0_CENSUS_CLASS] = (uint8_t)(m % 2);
        samples[m].pattern = m % 2;
        samples[m].weight = 1;
    }
    samples[SAMPLES - 2].pattern = 7;
    ran = tier0_entries_test_search(samples, SAMPLES, &row);
    check_test_expect(test, ran && row.undecided == SAMPLES / 2 && row.exceptions == 1 && row.set == class,
                      "tier0 entries: an odd group leaves its key's entries undecided by every input, and is the one exception");
    check_test_expect(test, ran && row.within[BUSTER_TIER0_CENSUS_CONFLICTS][1] == SAMPLES / 2 && row.within[BUSTER_TIER0_CENSUS_EXCEPTIONS][1] == SAMPLES - 1,
                      "tier0 entries: the other key's entries are decided, and every entry but the odd one has its key's commonest pattern");
    // An odd group that the depth sets apart: decided at two inputs, an exception at one.
    samples[SAMPLES - 2].inputs[TIER0_CENSUS_DEPTH] = 9;
    ran = tier0_entries_test_search(samples, SAMPLES, &row);
    check_test_expect(test, ran && row.undecided == 0 && row.set == (class | depth) && row.inputs == 2 && row.rows == 3,
                      "tier0 entries: an odd group a second input sets apart is decided by the two");
    check_test_expect(test, ran && row.within[BUSTER_TIER0_CENSUS_CONFLICTS][1] == SAMPLES / 2 && row.within[BUSTER_TIER0_CENSUS_CONFLICTS][2] == SAMPLES &&
                                row.within[BUSTER_TIER0_CENSUS_EXCEPTIONS][1] == SAMPLES - 1 && row.within[BUSTER_TIER0_CENSUS_EXCEPTIONS][2] == SAMPLES,
                      "tier0 entries: one input leaves the odd group's key undecided and the group an exception; two leave neither");
}

// The census of `path` with its report captured: whether it ran, the census (the caller's to end) and the report
// (malloc'd, the caller's to free).
static bool tier0_entries_test_run(char* path, Tier0Census* census, Tier0CensusResult* result, char** report)
{
    FILE* capture = tmpfile();
    fflush(stdout);
    fflush(stderr);
    const int saved[2] = { dup(STDOUT_FILENO), dup(STDERR_FILENO) };
    const int null = open("/dev/null", O_WRONLY);
    bool ran = capture != 0 && saved[0] >= 0 && saved[1] >= 0 && null >= 0 && dup2(fileno(capture), STDOUT_FILENO) >= 0 && dup2(null, STDERR_FILENO) >= 0;
    char* options[] = { "-nostdinc" };
    char* files[] = { path };
    const Tier0CensusSources sources = { .files = files, .file_count = 1, .options = options, .option_count = 1 };
    ran = ran && tier0_census_files(census, &sources, 1, true, BUSTER_TYPED_CALL, 8, false, result);
    fflush(stdout);
    fflush(stderr);
    ran = saved[0] >= 0 && dup2(saved[0], STDOUT_FILENO) >= 0 && ran;
    ran = saved[1] >= 0 && dup2(saved[1], STDERR_FILENO) >= 0 && ran;
    const long length = ran && fseek(capture, 0, SEEK_END) == 0 ? ftell(capture) : -1;
    char* text = length >= 0 && length < (1l << 24) && fseek(capture, 0, SEEK_SET) == 0 ? calloc((size_t)length + 1, 1) : 0;
    ran = text != 0 && fread(text, 1, (size_t)length, capture) == (size_t)length;
    for (uint32_t i = 0; i < 2; i += 1)
    {
        if (saved[i] >= 0)
        {
            close(saved[i]);
        }
    }
    if (null >= 0)
    {
        close(null);
    }
    if (capture != 0)
    {
        fclose(capture);
    }
    *report = text;
    return ran;
}

static __attribute__((noinline)) void check_test_tier0_entries(CheckTest* test)
{
    check_test_tier0_entries_rules(test);
    char path[512];
    const int length = snprintf(path, sizeof(path), "%s/tier0-entries/entries.c", check_test_fixture_root);
    Tier0Census census;
    Tier0CensusResult result;
    memset(&census, 0, sizeof(census));
    memset(&result, 0, sizeof(result));
    char* report = 0;
    const bool ran = length > 0 && length < (int)sizeof(path) && tier0_entries_test_run(path, &census, &result, &report);
    check_test_expect(test, ran && census.files == 1 && census.failures == 0 && census.left_out == 0 && census.refused == 0 && census.functions == 9,
                      "tier0 entries: the fixture's nine functions are counted");
    uint64_t entries = 0;
    uint64_t records = 0;
    uint64_t states[TIER0_CENSUS_STATES] = { 0 };
    uint32_t widest = 0;
    uint32_t deferred_kinds = 0;
    bool division = false;
    bool compare = false;
    uint32_t near_pattern = UINT32_MAX;
    uint32_t far_pattern = UINT32_MAX;
    for (uint64_t s = 0; ran && s < census.sample_count; s += 1)
    {
        const Tier0CensusSample* sample = &census.samples[s];
        entries += sample->weight;
        records += sample->records;
        states[sample->state] += 1;
        widest = sample->weight > widest ? sample->weight : widest;
        deferred_kinds |= sample->state == TIER0_CENSUS_DEFERRED_KIND && sample->kind == BUSTER_TYPED_CALL ? 1 : 0;
        deferred_kinds |= sample->state == TIER0_CENSUS_DEFERRED_KIND && sample->kind == BUSTER_TYPED_COPY ? 2 : 0;
        division = division || (sample->state == TIER0_CENSUS_DEFERRED_DEPTH && sample->kind == BUSTER_TYPED_DIV);
        // The loop's condition: the compare's value, then CONST 0, NE and BRANCH_FALSE as one group, a compare with a
        // constant and its branch (the lowering tests a condition's value against zero).
        compare = compare || (sample->kind == BUSTER_TYPED_BRANCH_FALSE && sample->weight == 3 && sample->inputs[TIER0_CENSUS_KEY_KIND] == BUSTER_TYPED_NE &&
                              sample->inputs[TIER0_CENSUS_VARIANT] == BUSTER_TIER0_VARIANT_BRANCH_FALSE + BUSTER_TIER0_VARIANT_CONST);
        // `p + 3` and `p + 20` of 8-byte elements: the constants are of one class (2 to 127) and the products, 24 and
        // 160, of two (the classes of tier0_census_value_class, plus one).
        const bool addition = sample->kind == BUSTER_TYPED_PTR_ADD && sample->inputs[TIER0_CENSUS_VALUE] == 3 && sample->inputs[TIER0_CENSUS_VALUE_1] == 3 &&
                              sample->inputs[TIER0_CENSUS_SCALE] == 4;
        near_pattern = addition && sample->inputs[TIER0_CENSUS_PRODUCT] == 4 ? sample->pattern : near_pattern;
        far_pattern = addition && sample->inputs[TIER0_CENSUS_PRODUCT] == 6 ? sample->pattern : far_pattern;
    }
    check_test_expect(test, ran && entries == census.entries && census.sample_count == census.heads && entries != 0,
                      "tier0 entries: every typed entry is in one group");
    check_test_expect(test, ran && records + census.frame_records == census.records, "tier0 entries: every record is an entry's, a prologue's or an epilogue's");
    check_test_expect(test, ran && states[TIER0_CENSUS_OTHER] == 0 && states[TIER0_CENSUS_TEMPLATED] != 0,
                      "tier0 entries: every head that takes a template has its template's records");
    check_test_expect(test, ran && deferred_kinds == 3 && states[TIER0_CENSUS_DEFERRED_KIND] == 2, "tier0 entries: the call and the copy are deferred by their kind");
    check_test_expect(test, ran && division && states[TIER0_CENSUS_DEFERRED_DEPTH] == 1, "tier0 entries: the division whose operands aren't in rax's place is deferred by its depth");
    check_test_expect(test, ran && compare && widest == 3, "tier0 entries: a compare with zero and its branch are one group of three entries, the widest");
    check_test_expect(test, ran && near_pattern != UINT32_MAX && far_pattern != UINT32_MAX && near_pattern != far_pattern,
                      "tier0 entries: two pointer additions differ by their scaled constants' class alone, and in their patterns");
    check_test_expect(test, ran && result.decided[0] == census.entries && result.decided[1] == census.entries && result.undecided[0] == 0 &&
                                result.common[0] == census.entries && result.rows[0] != 0,
                      "tier0 entries: a key of the gate's bytes decides every entry of the fixture");
    static const char* const lines[] = {
        "tier0-census: 9 functions, ",
        "tier0-census: keys=raw: gate: a key of 8 bytes or fewer decides ",
        "tier0-census: keys=fused: gate: a key of 8 bytes or fewer decides ",
        "tier0-census: keys=raw kind=CALL entries=1 ",
        "tier0-census: list entries=1 key=- pattern=",
        " totals files=1 left_out=0 failures=0 census=reported\n",
    };
    bool reported = ran && report != 0;
    for (uint32_t i = 0; reported && i < BUSTER_ARRAY_LENGTH(lines); i += 1)
    {
        const char* found = strstr(report, lines[i]);
        reported = found != 0 && strstr(found + 1, lines[i]) == 0;
    }
    const char* totals = report != 0 ? strstr(report, "tier0-census: totals ") : 0;
    check_test_expect(test, reported && totals != 0 && strchr(totals, '\n') == report + strlen(report) - 1, "tier0 entries: the report has the gate's lines, the totals last");
    // The command's own options: an unknown kind to list, no file and a set without a file are refused; two sets of the
    // fixture are one census of twice its entries.
    char* unknown[] = { "--list", "NOTHING", path };
    char* none[] = { "--rows" };
    char* empty[] = { path, "--next", "-nostdinc" };
    char* twice[] = { "-nostdinc", path, "--next", "-nostdinc", path };
    fflush(stdout);
    fflush(stderr);
    const int saved = dup(STDERR_FILENO);
    const int null = open("/dev/null", O_WRONLY);
    const bool quiet = saved >= 0 && null >= 0 && dup2(null, STDERR_FILENO) >= 0;
    const int refused = quiet ? run_tier0_entries_measure(3, unknown) + run_tier0_entries_measure(1, none) + run_tier0_entries_measure(3, empty) : 0;
    fflush(stderr);
    const bool restored = saved >= 0 && dup2(saved, STDERR_FILENO) >= 0;
    if (saved >= 0)
    {
        close(saved);
    }
    if (null >= 0)
    {
        close(null);
    }
    check_test_expect(test, quiet && restored && refused == 3, "tier0 entries: an unknown kind to list, a run without a file and a set without one are refused");
    const uint64_t once = census.entries;
    free(report);
    if (ran)
    {
        tier0_census_end(&census);
    }
    // Two sets of the fixture.
    char* files_twice[] = { twice[1], twice[4] };
    char* options_twice[] = { twice[0], twice[3] };
    const Tier0CensusSources both[] = {
        { .files = files_twice, .file_count = 1, .options = options_twice, .option_count = 1 },
        { .files = files_twice + 1, .file_count = 1, .options = options_twice + 1, .option_count = 1 },
    };
    Tier0Census doubled;
    memset(&doubled, 0, sizeof(doubled));
    const bool again = ran && tier0_census_files(&doubled, both, 2, false, BUSTER_TYPED_KINDS, 8, true, &result);
    check_test_expect(test, again && doubled.files == 2 && doubled.entries == 2 * once && doubled.functions == 18, "tier0 entries: two sets of files are one census");
    if (again)
    {
        tier0_census_end(&doubled);
    }
}

#endif
