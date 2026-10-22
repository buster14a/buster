// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Tests for diagnostic.c: radix/insertion agreement including every short tail against guard pages, stable ties,
// all 128 key bits, lane-local ownership, reversed completion, grouped notes, fatal cutoff and bounded failures.

#define BUSTER_DIAGNOSTIC_TEST_JOBS 129
#define BUSTER_DIAGNOSTIC_TEST_RECORDS (BUSTER_DIAGNOSTIC_TEST_JOBS * 3)

static uint64_t diagnostic_test_random(uint64_t* state)
{
    *state = *state * 6364136223846793005ull + 1442695040888963407ull;
    return *state;
}

static bool diagnostic_test_sort(uint64_t seed)
{
    bool result = true;
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    for (uint32_t trial = 0; result && trial < 323; trial += 1)
    {
        const uint32_t count = trial < 320 ? trial : 4095 + trial - 320;
        const uint64_t bytes = (uint64_t)count * sizeof(DiagnosticOrder);
        const uint64_t readable = (bytes + page - 1) / page * page;
        const uint64_t mapped = readable + 2 * page;
        uint8_t* memory[2] = { 0 };
        DiagnosticOrder* lists[2] = { 0 };
        for (uint32_t i = 0; i < 2; i += 1)
        {
            memory[i] = mmap(0, mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            result = result && memory[i] != MAP_FAILED;
            if (memory[i] != MAP_FAILED)
            {
                result = result && (readable == 0 || mprotect(memory[i] + page, readable, PROT_READ | PROT_WRITE) == 0);
                lists[i] = (DiagnosticOrder*)(memory[i] + page + readable - bytes);
            }
        }
        DiagnosticOrder* expected = calloc(count + 1, sizeof(DiagnosticOrder));
        result = result && expected != 0;
        for (uint32_t pattern = 0; result && pattern < 5; pattern += 1)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint64_t a = diagnostic_test_random(&seed);
                const uint64_t b = diagnostic_test_random(&seed);
                lists[0][i] = (DiagnosticOrder)
                {
                    .source = pattern == 0 ? UINT64_MAX : pattern == 1 ? count - i : pattern == 2 ? i : pattern == 4 ? UINT64_MAX - a % 4 : a,
                    .sequence = pattern == 0 ? 0 : pattern >= 3 ? b : b % 4,
                    .record = i,
                };
                expected[i] = lists[0][i];
            }
            diagnostic_sort_scalar(expected, count);
            diagnostic_sort(lists[0], count, lists[1]);
            for (uint32_t i = 0; result && i < count; i += 1)
            {
                result = lists[0][i].source == expected[i].source && lists[0][i].sequence == expected[i].sequence &&
                         lists[0][i].record == expected[i].record;
            }
        }
        free(expected);
        for (uint32_t i = 0; i < 2; i += 1)
        {
            if (memory[i] != MAP_FAILED)
            {
                munmap(memory[i], mapped);
            }
        }
    }
    printf("diagnostic sort: tails, guard pages, stable ties and full-width keys: %s\n", result ? "pass" : "FAIL");
    return result;
}

typedef struct DiagnosticTestLanes DiagnosticTestLanes;
struct DiagnosticTestLanes
{
    DiagnosticBuffer buffers[8];
    bool reverse;
    bool passed[8];
};

static void diagnostic_test_lane(void* argument)
{
    DiagnosticTestLanes* test = argument;
    DiagnosticBuffer* buffer = &test->buffers[lane_index()];
    bool passed = true;
    for (uint32_t slot = lane_index(); slot < BUSTER_DIAGNOSTIC_TEST_JOBS; slot += lane_count())
    {
        const uint32_t index = test->reverse ? BUSTER_DIAGNOSTIC_TEST_JOBS - 1 - slot : slot;
        DiagnosticJob job =
        {
            .unit = index / 4,
            .index = index,
        };
        if ((index + lane_count()) % 5 == 0)
        {
            const struct timespec delay = { .tv_nsec = 100000 };
            nanosleep(&delay, 0);
        }
        for (uint32_t i = 0; i < 3; i += 1)
        {
            char text[128];
            const int length = snprintf(text, sizeof(text), "unit %u job %u message %u\n  note for %u/%u\n", job.unit, index, i, index, i);
            const bool fatal = (index == 9 || index == 14) && i == 1;
            const bool stopped = job.stopped;
            const String8 message =
            {
                .pointer = text,
                .length = (uint64_t)length,
            };
            const bool appended = diagnostic_append(buffer, &job, 100 + index % 4, fatal ? DIAGNOSTIC_FATAL : DIAGNOSTIC_WARNING, message);
            passed = passed && appended != stopped;
            memset(text, 0xa5, sizeof(text));
        }
    }
    test->passed[lane_index()] = passed;
}

static bool diagnostic_test_lanes(void)
{
    static const uint32_t counts[] =
    {
        1,
        2,
        3,
        8,
    };
    Arena merged = arena_reserve(1 << 20);
    char* first = 0;
    size_t first_length = 0;
    bool result = merged.base != 0;
    for (uint32_t mode = 0; result && mode < 4; mode += 1)
    {
        for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(counts); c += 1)
        {
            DiagnosticTestLanes test = { .reverse = (mode & 1) != 0 };
            Diagnostic records[8][BUSTER_DIAGNOSTIC_TEST_RECORDS];
            Arena texts[8] = { 0 };
            for (uint32_t i = 0; i < counts[c]; i += 1)
            {
                texts[i] = arena_reserve(1 << 16);
                result = result && texts[i].base != 0;
                test.buffers[i] = (DiagnosticBuffer)
                {
                    .records = records[i],
                    .text = &texts[i],
                    .capacity = BUSTER_DIAGNOSTIC_TEST_RECORDS,
                };
            }
            result = result && lane_run(counts[c], diagnostic_test_lane, &test, 0);
            for (uint32_t i = 0; i < counts[c]; i += 1)
            {
                result = result && test.passed[i];
            }
            const Diagnostics diagnostics = diagnostic_merge(test.buffers, counts[c], &merged, mode >= 2);
            result = result && diagnostics.success && diagnostics.count == 29;
            char* text = 0;
            size_t length = 0;
            FILE* stream = open_memstream(&text, &length);
            result = diagnostic_write(&diagnostics, stream) && result;
            if (stream != 0)
            {
                result = fclose(stream) == 0 && result;
            }
            if (mode == 0 && c == 0)
            {
                first = text;
                first_length = length;
                result = result && strstr(first, "job 9 message 1\n  note for 9/1\n") != 0 && strstr(first, "job 9 message 2") == 0 &&
                         strstr(first, "job 10 ") == 0;
            }
            else
            {
                result = result && text != 0 && length == first_length && memcmp(text, first, length) == 0;
                free(text);
            }
            for (uint32_t i = 0; i < counts[c]; i += 1)
            {
                arena_release(&texts[i]);
            }
            arena_pop_to(&merged, 0);
        }
    }
    free(first);
    arena_release(&merged);
    printf("diagnostic lanes: 1/2/3/8, forward/reverse, notes and earliest fatal: %s\n", result ? "pass" : "FAIL");
    return result;
}

static bool diagnostic_test_limits(void)
{
    Arena text = arena_reserve(4096);
    Arena merged = arena_reserve(4096);
    Diagnostic records[2];
    DiagnosticBuffer buffer =
    {
        .records = records,
        .text = &text,
        .capacity = 2,
    };
    DiagnosticJob job =
    {
        .unit = UINT32_MAX,
        .index = UINT32_MAX,
        .next = UINT32_MAX - 1,
    };
    bool result = text.base != 0 && merged.base != 0;
    result = result && diagnostic_append(&buffer, &job, UINT32_MAX, DIAGNOSTIC_ERROR, BUSTER_S8("last\n")) &&
             records[0].source == UINT64_MAX && records[0].sequence == UINT64_MAX - 1;
    result = result && !diagnostic_append(&buffer, &job, 0, DIAGNOSTIC_ERROR, BUSTER_S8("full\n")) && buffer.failed;
    Diagnostics diagnostics = diagnostic_merge(&buffer, 1, &merged, false);
    result = result && !diagnostics.success && diagnostics.count == 0 && merged.length == 0;
    FILE* stream = tmpfile();
    result = result && stream != 0 && !diagnostic_write(&diagnostics, stream) && ftell(stream) == 0;
    if (stream != 0)
    {
        fclose(stream);
    }
    buffer = (DiagnosticBuffer)
    {
        .records = records,
        .text = &text,
        .capacity = 0,
    };
    job = (DiagnosticJob){ 0 };
    result = result && !diagnostic_append(&buffer, &job, 0, DIAGNOSTIC_ERROR, BUSTER_S8("no records")) && buffer.failed && buffer.count == 0;
    buffer = (DiagnosticBuffer)
    {
        .records = records,
        .text = &text,
        .capacity = 1,
    };
    job = (DiagnosticJob){ 0 };
    text.length = text.capacity;
    result = result && !diagnostic_append(&buffer, &job, 0, DIAGNOSTIC_ERROR, BUSTER_S8("no text room")) && buffer.failed && buffer.count == 0;
    buffer = (DiagnosticBuffer)
    {
        .records = records,
        .text = &text,
        .capacity = 1,
    };
    result = result && diagnostic_append(&buffer, &job, 0, DIAGNOSTIC_NOTE, (String8){ 0 });
    merged.length = merged.capacity;
    diagnostics = diagnostic_merge(&buffer, 1, &merged, false);
    result = result && !diagnostics.success && diagnostics.count == 0 && merged.length == merged.capacity;
    diagnostics = diagnostic_merge(0, 0, &merged, false);
    result = result && diagnostics.success && diagnostics.count == 0;
    arena_release(&text);
    arena_release(&merged);
    printf("diagnostic limits: full keys, record/text/merge exhaustion and empty output: %s\n", result ? "pass" : "FAIL");
    return result;
}

static int run_diagnostic_tests(void)
{
    bool result = diagnostic_test_sort(153);
    result = diagnostic_test_lanes() && result;
    result = diagnostic_test_limits() && result;
    return result ? 0 : 1;
}
