// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Self-tests for the generator, Clang transport, output comparator and planted-difference minimizer.
// main.c includes this after the other preprocessor_fuzz files. There is no preprocessor under test yet.
// The command tests every generator family, then --fuzz cases in bounded batches; successful subsets are
// batched again so rejection fallback cannot conceal a batching bug. Temporary saved failures are removed.

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUSTER_PPF_TEST_ARENA_BYTES (128ull << 20)
#define BUSTER_PPF_TEST_BATCH 64
#define BUSTER_PPF_TEST_REPORTED 12

typedef struct PreprocessorFuzzTestTally PreprocessorFuzzTestTally;
struct PreprocessorFuzzTestTally
{
    uint64_t checks;
    uint64_t failures;
    uint64_t case_id;
};

static void preprocessor_fuzz_test_expect(PreprocessorFuzzTestTally* tally, bool condition, const char* description)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < BUSTER_PPF_TEST_REPORTED)
        {
            fprintf(stderr, "preprocessor-fuzz: %s (case=%" PRIu64 ")\n", description, tally->case_id);
        }
        tally->failures += 1;
    }
}

static bool preprocessor_fuzz_test_contains(String8 text, String8 needle)
{
    bool found = false;
    for (uint64_t offset = 0; !found && needle.length <= text.length && offset <= text.length - needle.length; offset += 1)
    {
        found = memcmp(text.pointer + offset, needle.pointer, needle.length) == 0;
    }
    return found;
}

static PreprocessorFuzzOutput preprocessor_fuzz_test_output(String8 text, bool accepted)
{
    const PreprocessorFuzzOutput result = {
        .text = text,
        .accepted = accepted,
        .valid = true,
    };
    return result;
}

static void preprocessor_fuzz_test_comparison(PreprocessorFuzzTestTally* tally, Arena* memory)
{
    const uint64_t mark = memory->length;
    const PreprocessorFuzzOutput plain = preprocessor_fuzz_test_output(BUSTER_S8("name + 17"), true);
    const PreprocessorFuzzOutput spaced = preprocessor_fuzz_test_output(BUSTER_S8("\n name\t+\n 17 \n"), true);
    const PreprocessorFuzzOutput empty = preprocessor_fuzz_test_output(BUSTER_S8(""), true);
    const PreprocessorFuzzOutput whitespace = preprocessor_fuzz_test_output(BUSTER_S8(" \n\t"), true);
    const PreprocessorFuzzOutput identifier = preprocessor_fuzz_test_output(BUSTER_S8("alpha"), true);
    const PreprocessorFuzzOutput other_identifier = preprocessor_fuzz_test_output(BUSTER_S8("beta"), true);
    const PreprocessorFuzzOutput number = preprocessor_fuzz_test_output(BUSTER_S8("123"), true);
    const PreprocessorFuzzOutput rejected = preprocessor_fuzz_test_output(BUSTER_S8("diagnostic text"), false);
    const PreprocessorFuzzOutput other_rejected = preprocessor_fuzz_test_output(BUSTER_S8("another diagnostic"), false);
    const PreprocessorFuzzOutput unknown = preprocessor_fuzz_test_output(BUSTER_S8("@"), true);
    const PreprocessorFuzzOutput invalid = { 0 };
    const PreprocessorFuzzOutput missing_text = {
        .text = {
            .length = 1,
        },
        .accepted = true,
        .valid = true,
    };
    const PreprocessorFuzzOutput oversized_text = {
        .text = {
            .pointer = "x",
            .length = BUSTER_PPF_MAX_OUTPUT_BYTES + 1,
        },
        .accepted = true,
        .valid = true,
    };
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, plain, spaced) == BUSTER_PPF_EQUAL, "whitespace changed a comparison");
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, empty, whitespace) == BUSTER_PPF_EQUAL, "empty token streams differ");
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, plain, identifier) == BUSTER_PPF_TOKEN_COUNT, "different token count was missed");
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, identifier, number) == BUSTER_PPF_TOKEN_KIND, "different token kind was missed");
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, identifier, other_identifier) == BUSTER_PPF_TOKEN_SPELLING, "different token spelling was missed");
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, plain, rejected) == BUSTER_PPF_ACCEPTANCE, "accept/reject difference was missed");
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, rejected, other_rejected) == BUSTER_PPF_EQUAL, "rejections depend on diagnostic spelling");
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, unknown, unknown) == BUSTER_PPF_EQUAL, "unknown preprocessing tokens cannot compare equal");
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, unknown, identifier) == BUSTER_PPF_TOKEN_KIND, "unknown token kind was lost");
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, plain, invalid) == BUSTER_PPF_INVALID, "infrastructure failure became a difference");
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, missing_text, rejected) == BUSTER_PPF_INVALID, "missing accepted text became a difference");
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, oversized_text, plain) == BUSTER_PPF_INVALID, "oversized accepted text was read");
    Arena no_memory = { 0 };
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(&no_memory, plain, spaced) == BUSTER_PPF_INVALID, "comparison allocation failure became a result");
    preprocessor_fuzz_test_expect(tally, memory->length == mark, "comparison retained scratch memory");

    char storage[6] = {
        'L',
        0,
        0,
        0,
        0,
        'R',
    };
    PreprocessorFuzzBuffer buffer = {
        .data = storage + 1,
        .capacity = 4,
        .valid = true,
    };
    const bool first = preprocessor_fuzz_append(&buffer, BUSTER_S8("abcd"));
    const bool overflow = preprocessor_fuzz_append(&buffer, BUSTER_S8("e"));
    preprocessor_fuzz_test_expect(tally, first && !overflow && storage[0] == 'L' && storage[5] == 'R', "bounded append overwrote its buffer");
    arena_pop_to(memory, mark);
}

// Check source witnesses as well as the generator's coverage mask. Semantic transport checks below additionally
// require rejected argument/paste families to be rejected, and compare every accepted expansion with its own process.
static bool preprocessor_fuzz_test_construct(PreprocessorFuzzCase test_case, uint64_t family, bool c23)
{
    static const String8 witnesses[BUSTER_PPF_CONSTRUCT_COUNT] = {
        BUSTER_S8_INIT("#define "),
        BUSTER_S8_INIT(" * "),
        BUSTER_S8_INIT("__VA_ARGS__"),
        BUSTER_S8_INIT("...)"),
        BUSTER_S8_INIT("() "),
        BUSTER_S8_INIT(") #"),
        BUSTER_S8_INIT(" ## "),
        BUSTER_S8_INIT("(,)"),
        BUSTER_S8_INIT(") ## "),
        BUSTER_S8_INIT(",__VA_ARGS__)"),
        BUSTER_S8_INIT("#define "),
        BUSTER_S8_INIT(", ## __VA_ARGS__"),
        BUSTER_S8_INIT("#define "),
        BUSTER_S8_INIT("#define "),
        BUSTER_S8_INIT("()("),
        BUSTER_S8_INIT("(["),
        BUSTER_S8_INIT("(["),
        BUSTER_S8_INIT("({"),
        BUSTER_S8_INIT("(,)"),
        BUSTER_S8_INIT("#define "),
        BUSTER_S8_INIT("(\n("),
        BUSTER_S8_INIT("\n#undef "),
        BUSTER_S8_INIT("#elif defined "),
        BUSTER_S8_INIT(" defined"),
        BUSTER_S8_INIT("0x10ULL >= 010u"),
        BUSTER_S8_INIT("#if (0,1)"),
        BUSTER_S8_INIT("/ 0)"),
        BUSTER_S8_INIT("_Pragma("),
        BUSTER_S8_INIT("__LINE__"),
        BUSTER_S8_INIT("__COUNTER__"),
        BUSTER_S8_INIT("// ppf_comment_splice\\\ncontinued"),
        BUSTER_S8_INIT("/**/+\\\n"),
        BUSTER_S8_INIT(" ##\n"),
        BUSTER_S8_INIT(" #\n"),
        BUSTER_S8_INIT("# 1\n"),
        BUSTER_S8_INIT("(+,*)"),
        BUSTER_S8_INIT("/* ppf_random 0 */\n"),
        BUSTER_S8_INIT("/* ppf_random 1 */\n"),
        BUSTER_S8_INIT("/* ppf_random 2 */\n"),
        BUSTER_S8_INIT("/* ppf_random 3 */\n"),
        BUSTER_S8_INIT("/* ppf_random 4 */\n"),
        BUSTER_S8_INIT("/* ppf_random 5 */\n"),
        BUSTER_S8_INIT("/* ppf_random 6 */\n"),
        BUSTER_S8_INIT("/* ppf_random 7 */\n"),
        BUSTER_S8_INIT("/* ppf_random 8 */\n"),
        BUSTER_S8_INIT("/* ppf_random 9 */\n"),
        BUSTER_S8_INIT("/* ppf_random 10 */\n"),
        BUSTER_S8_INIT("/* ppf_random 11 */\n"),
    };
    bool result = preprocessor_fuzz_test_contains(test_case.source, witnesses[family]);
    if (family == 10 && c23)
    {
        result = result && preprocessor_fuzz_test_contains(test_case.source, BUSTER_S8("__VA_OPT__("));
    }
    if (family == 12 || family == 13)
    {
        bool reference_found = false;
        for (char left = 'a'; !reference_found && left <= 'h'; left += 1)
        {
            for (char right = 'a'; !reference_found && right <= 'h'; right += 1)
            {
                if ((family == 12 && left == right) || (family == 13 && left != right))
                {
                    char forward[128];
                    char backward[128];
                    const int forward_length = snprintf(forward, sizeof(forward), "#define ppf_%" PRIu64 "_%c ppf_%" PRIu64 "_%c\n",
                        test_case.id, left, test_case.id, right);
                    const int backward_length = snprintf(backward, sizeof(backward), "#define ppf_%" PRIu64 "_%c ppf_%" PRIu64 "_%c\n",
                        test_case.id, right, test_case.id, left);
                    const String8 forward_slice = {
                        .pointer = forward,
                        .length = forward_length > 0 ? (uint64_t)forward_length : 0,
                    };
                    const String8 backward_slice = {
                        .pointer = backward,
                        .length = backward_length > 0 ? (uint64_t)backward_length : 0,
                    };
                    reference_found = forward_length > 0 && (uint64_t)forward_length < sizeof(forward)
                        && backward_length > 0 && (uint64_t)backward_length < sizeof(backward)
                        && preprocessor_fuzz_test_contains(test_case.source, forward_slice)
                        && preprocessor_fuzz_test_contains(test_case.source, backward_slice);
                }
            }
        }
        result = result && reference_found;
    }
    return result;
}

static void preprocessor_fuzz_test_generation(PreprocessorFuzzTestTally* tally, Arena* memory, bool c23)
{
    const uint64_t seeds[] = {
        1,
        7,
        99,
    };
    uint64_t covered = 0;
    bool seeds_differ = false;
    for (uint64_t family = 0; family < BUSTER_PPF_CONSTRUCT_COUNT; family += 1)
    {
        const uint64_t mark = memory->length;
        tally->case_id = 10 * BUSTER_PPF_FAMILY_CYCLE + family;
        PreprocessorFuzzCase baseline = { 0 };
        const bool baseline_valid = preprocessor_fuzz_generate(memory, seeds[0], tally->case_id, c23, &baseline);
        preprocessor_fuzz_test_expect(tally, baseline_valid, "generator baseline failed");
        for (uint64_t seed_index = 0; baseline_valid && seed_index < BUSTER_ARRAY_LENGTH(seeds); seed_index += 1)
        {
            PreprocessorFuzzCase first = { 0 };
            PreprocessorFuzzCase second = { 0 };
            const bool first_valid = preprocessor_fuzz_generate(memory, seeds[seed_index], tally->case_id, c23, &first);
            const bool second_valid = preprocessor_fuzz_generate(memory, seeds[seed_index], tally->case_id, c23, &second);
            preprocessor_fuzz_test_expect(tally, first_valid && second_valid, "generator repeated call failed");
            if (first_valid && second_valid)
            {
                const bool text_equal = first.source.length == second.source.length
                    && memcmp(first.source.pointer, second.source.pointer, first.source.length) == 0;
                preprocessor_fuzz_test_expect(tally, text_equal && first.id == tally->case_id && first.id == second.id
                    && first.constructs == second.constructs && first.isolated == second.isolated, "generator is not deterministic");
                preprocessor_fuzz_test_expect(tally, first.source.length != 0 && first.source.length <= BUSTER_PPF_MAX_CASE_BYTES, "generated source exceeds its bounds");
                preprocessor_fuzz_test_expect(tally, (first.constructs & (1ull << family)) != 0
                    && (first.constructs >> BUSTER_PPF_CONSTRUCT_COUNT) == 0, "generator family coverage mask is wrong");
                covered |= first.constructs;
                char prefix[64];
                const int prefix_length = snprintf(prefix, sizeof(prefix), "ppf_%" PRIu64 "_", first.id);
                const String8 prefix_slice = {
                    .pointer = prefix,
                    .length = prefix_length > 0 ? (uint64_t)prefix_length : 0,
                };
                preprocessor_fuzz_test_expect(tally, prefix_length > 0 && (uint64_t)prefix_length < sizeof(prefix)
                    && preprocessor_fuzz_test_contains(first.source, prefix_slice), "generated case has no unique name prefix");
                preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_construct_names[family] != 0
                    && preprocessor_fuzz_construct_names[family][0] != 0, "generator family has no documented name");
                preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_test_construct(first, family, c23), "generated source lacks its construct witness");
                const bool counter = preprocessor_fuzz_test_contains(first.source, BUSTER_S8("__COUNTER__"));
                preprocessor_fuzz_test_expect(tally, !counter || first.isolated, "counter case is not isolated");
                const bool same_as_baseline = first.source.length == baseline.source.length
                    && memcmp(first.source.pointer, baseline.source.pointer, first.source.length) == 0;
                seeds_differ = seeds_differ || !same_as_baseline;
            }
        }
        arena_pop_to(memory, mark);
    }
    const uint64_t all_constructs = (1ull << BUSTER_PPF_CONSTRUCT_COUNT) - 1;
    preprocessor_fuzz_test_expect(tally, covered == all_constructs, "generator omitted a construct family");
    preprocessor_fuzz_test_expect(tally, seeds_differ, "changing the seed did not change generated source");
    Arena no_memory = { 0 };
    PreprocessorFuzzCase unchanged = {
        .id = 1234,
    };
    const bool generated = preprocessor_fuzz_generate(&no_memory, 1, 0, c23, &unchanged);
    preprocessor_fuzz_test_expect(tally, !generated && unchanged.id == 1234 && no_memory.length == 0, "generator allocation failure changed the result");
}

static void preprocessor_fuzz_test_batch(PreprocessorFuzzTestTally* tally, Arena* memory, const char* clang_path, bool c23, uint64_t seed, uint64_t first_id, uint64_t count)
{
    const uint64_t mark = memory->length;
    PreprocessorFuzzCase cases[BUSTER_PPF_TEST_BATCH] = { 0 };
    PreprocessorFuzzCase accepted_cases[BUSTER_PPF_TEST_BATCH] = { 0 };
    PreprocessorFuzzOutput batched[BUSTER_PPF_TEST_BATCH] = { 0 };
    PreprocessorFuzzOutput individual[BUSTER_PPF_TEST_BATCH] = { 0 };
    PreprocessorFuzzOutput accepted_reference[BUSTER_PPF_TEST_BATCH] = { 0 };
    PreprocessorFuzzOutput accepted_batch[BUSTER_PPF_TEST_BATCH] = { 0 };
    bool valid = count <= BUSTER_PPF_TEST_BATCH;
    for (uint64_t i = 0; valid && i < count; i += 1)
    {
        valid = preprocessor_fuzz_generate(memory, seed, first_id + i, c23, &cases[i]);
    }
    preprocessor_fuzz_test_expect(tally, valid, "batch generation failed");
    if (valid)
    {
        const bool batch_valid = preprocessor_fuzz_clang(memory, clang_path, c23, cases, count, false, batched);
        const bool individual_valid = preprocessor_fuzz_clang(memory, clang_path, c23, cases, count, true, individual);
        preprocessor_fuzz_test_expect(tally, batch_valid && individual_valid, "generated cases could not run through clang");
        if (batch_valid && individual_valid)
        {
            uint64_t accepted_count = 0;
            uint64_t rejected_count = 0;
            for (uint64_t i = 0; i < count; i += 1)
            {
                tally->case_id = cases[i].id;
                preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, batched[i], individual[i]) == BUSTER_PPF_EQUAL,
                    "batch output differs from an individual process");
                const uint64_t family = preprocessor_fuzz_family(cases[i].id);
                if (family == 8 || family == 16 || family == 17 || family == 19 || (family >= 32 && family < BUSTER_PPF_TEMPLATE_FAMILIES))
                {
                    preprocessor_fuzz_test_expect(tally, !individual[i].accepted, "invalid paste or argument family was accepted");
                }
                if (individual[i].accepted)
                {
                    accepted_cases[accepted_count] = cases[i];
                    accepted_reference[accepted_count] = individual[i];
                    accepted_count += 1;
                }
                else
                {
                    rejected_count += 1;
                }
            }
            if (count >= BUSTER_PPF_CONSTRUCT_COUNT)
            {
                preprocessor_fuzz_test_expect(tally, accepted_count != 0 && rejected_count != 0, "generator did not exercise acceptance and rejection");
            }
            const bool accepted_valid = preprocessor_fuzz_clang(memory, clang_path, c23, accepted_cases, accepted_count, false, accepted_batch);
            preprocessor_fuzz_test_expect(tally, accepted_valid, "accepted-only batch failed");
            for (uint64_t i = 0; accepted_valid && i < accepted_count; i += 1)
            {
                tally->case_id = accepted_cases[i].id;
                preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, accepted_batch[i], accepted_reference[i]) == BUSTER_PPF_EQUAL,
                    "accepted-only batch changed a case");
            }
        }
    }
    arena_pop_to(memory, mark);
}

static bool preprocessor_fuzz_test_replace(Arena* memory, String8 source, String8 needle, String8 replacement, String8* result)
{
    uint64_t matches = 0;
    uint64_t position = 0;
    for (uint64_t offset = 0; needle.length != 0 && needle.length <= source.length && offset <= source.length - needle.length; offset += 1)
    {
        if (memcmp(source.pointer + offset, needle.pointer, needle.length) == 0)
        {
            matches += 1;
            position = offset;
        }
    }
    bool valid = matches == 1 && source.length - needle.length <= BUSTER_PPF_MAX_CASE_BYTES
        && replacement.length <= BUSTER_PPF_MAX_CASE_BYTES - (source.length - needle.length);
    if (valid)
    {
        const uint64_t length = source.length - needle.length + replacement.length;
        char* data = arena_push(memory, length, 1);
        valid = data != 0;
        if (valid)
        {
            memcpy(data, source.pointer, position);
            memcpy(data + position, replacement.pointer, replacement.length);
            memcpy(data + position + replacement.length, source.pointer + position + needle.length, source.length - position - needle.length);
            *result = (String8)
            {
                .pointer = data,
                .length = length,
            };
        }
    }
    return valid;
}

static void preprocessor_fuzz_test_rejection_coverage(PreprocessorFuzzTestTally* tally, Arena* memory, const char* clang_path, bool c23)
{
    static const uint32_t families[] =
    {
        8,
        32,
        33,
        34,
        35,
    };
    static const String8 faults[] =
    {
        BUSTER_S8_INIT(" ## "),
        BUSTER_S8_INIT(" ##\n"),
        BUSTER_S8_INIT(" #\n"),
        BUSTER_S8_INIT("# 1\n"),
        BUSTER_S8_INIT("(+,*)\n"),
    };
    static const String8 repairs[] =
    {
        BUSTER_S8_INIT(" "),
        BUSTER_S8_INIT("\n"),
        BUSTER_S8_INIT("\n"),
        BUSTER_S8_INIT("1\n"),
        BUSTER_S8_INIT("(+,=)\n"),
    };
    static const uint64_t seeds[] =
    {
        1,
        7,
        99,
    };
    for (uint64_t seed = 0; seed < BUSTER_ARRAY_LENGTH(seeds); seed += 1)
    {
        for (uint64_t variant = 0; variant < BUSTER_ARRAY_LENGTH(families); variant += 1)
        {
            const uint64_t mark = memory->length;
            PreprocessorFuzzCase cases[2] = { 0 };
            tally->case_id = families[variant];
            bool valid = preprocessor_fuzz_generate(memory, seeds[seed], tally->case_id, c23, cases);
            cases[1] = cases[0];
            valid = valid && preprocessor_fuzz_test_replace(memory, cases[0].source, faults[variant], repairs[variant], &cases[1].source);
            preprocessor_fuzz_test_expect(tally, valid, "rejection case does not contain exactly one independent fault");
            PreprocessorFuzzOutput outputs[2] = { 0 };
            valid = valid && preprocessor_fuzz_clang(memory, clang_path, c23, cases, 2, true, outputs);
            preprocessor_fuzz_test_expect(tally, valid, "independent rejection coverage could not run through clang");
            if (valid)
            {
                preprocessor_fuzz_test_expect(tally, !outputs[0].accepted && outputs[1].accepted,
                    "repairing the sole invalid operator did not change rejection to acceptance");
                preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, outputs[0], outputs[1]) == BUSTER_PPF_ACCEPTANCE,
                    "incorrect acceptance of one invalid operator form would be hidden");
            }
            arena_pop_to(memory, mark);
        }
    }
}

static void preprocessor_fuzz_test_arithmetic_coverage(PreprocessorFuzzTestTally* tally, Arena* memory, const char* clang_path, bool c23)
{
    static const uint64_t seeds[] =
    {
        1,
        7,
        99,
    };
    const String8 expected_text = BUSTER_S8("100 200 101 201 102 202 103 203 104 204 105 205 106 206 "
        "107 207 108 208 109 209 110 210 111 211 112 212");
    for (uint64_t seed = 0; seed < BUSTER_ARRAY_LENGTH(seeds); seed += 1)
    {
        const uint64_t mark = memory->length;
        PreprocessorFuzzCase cases[BUSTER_PPF_ARITHMETIC_COUNT + 1] = { 0 };
        tally->case_id = 24;
        bool valid = preprocessor_fuzz_generate(memory, seeds[seed], tally->case_id, c23, cases);
        for (uint32_t operation = 0; valid && operation < BUSTER_PPF_ARITHMETIC_COUNT; operation += 1)
        {
            char marker[64];
            const int marker_length = snprintf(marker, sizeof(marker), "/* ppf_arithmetic_%u */ ", operation);
            valid = marker_length > 0 && (uint64_t)marker_length < sizeof(marker);
            uint64_t found = 0;
            String8 definition = { 0 };
            for (uint64_t offset = 0; valid && (uint64_t)marker_length <= cases[0].source.length
                && offset <= cases[0].source.length - (uint64_t)marker_length; offset += 1)
            {
                if (memcmp(cases[0].source.pointer + offset, marker, (size_t)marker_length) == 0)
                {
                    uint64_t end = offset + (uint64_t)marker_length;
                    while (end < cases[0].source.length && cases[0].source.pointer[end] != '\n')
                    {
                        end += 1;
                    }
                    definition.pointer = cases[0].source.pointer + offset;
                    definition.length = end - offset;
                    found += 1;
                }
            }
            cases[operation + 1] = cases[0];
            valid = valid && found == 1 && preprocessor_fuzz_test_replace(memory, cases[0].source, definition, BUSTER_S8("0"), &cases[operation + 1].source);
        }
        preprocessor_fuzz_test_expect(tally, valid, "arithmetic case lacks an independent expression for each operator");
        PreprocessorFuzzOutput outputs[BUSTER_PPF_ARITHMETIC_COUNT + 1] = { 0 };
        valid = valid && preprocessor_fuzz_clang(memory, clang_path, c23, cases, BUSTER_ARRAY_LENGTH(cases), false, outputs);
        preprocessor_fuzz_test_expect(tally, valid, "arithmetic sensitivity cases could not run through clang");
        if (valid)
        {
            PreprocessorFuzzTokens tokens = { 0 };
            const bool tokenized = outputs[0].accepted && preprocessor_fuzz_tokenize(memory, outputs[0].text, &tokens);
            const uint64_t expected_count = 2 * BUSTER_PPF_ARITHMETIC_COUNT;
            preprocessor_fuzz_test_expect(tally, tokenized && tokens.count >= expected_count, "arithmetic output lacks both branch outcomes");
            if (tokenized && tokens.count >= expected_count)
            {
                const uint64_t start = tokens.starts[tokens.count - expected_count];
                const String8 actual_text =
                {
                    .pointer = outputs[0].text.pointer + start,
                    .length = outputs[0].text.length - start,
                };
                const PreprocessorFuzzOutput actual = preprocessor_fuzz_test_output(actual_text, true);
                const PreprocessorFuzzOutput expected = preprocessor_fuzz_test_output(expected_text, true);
                preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, actual, expected) == BUSTER_PPF_EQUAL,
                    "arithmetic predicates did not produce a true and a false outcome for every operator");
            }
            for (uint32_t operation = 0; operation < BUSTER_PPF_ARITHMETIC_COUNT; operation += 1)
            {
                const PreprocessorFuzzDifference difference = preprocessor_fuzz_compare(memory, outputs[0], outputs[operation + 1]);
                preprocessor_fuzz_test_expect(tally, outputs[operation + 1].accepted && difference != BUSTER_PPF_INVALID
                    && difference != BUSTER_PPF_EQUAL, "zeroing an arithmetic operator result did not change output tokens");
            }
        }
        arena_pop_to(memory, mark);
    }
}

static void preprocessor_fuzz_test_oracle(PreprocessorFuzzTestTally* tally, Arena* memory, const char* clang_path, bool c23)
{
    const uint64_t mark = memory->length;
    const PreprocessorFuzzCase cases[] = {
        {
            .source = BUSTER_S8_INIT("#define ppf_10000_a 17\nppf_10000_a\n#undef ppf_10000_a\n"),
            .id = 10000,
        },
        {
            .source = BUSTER_S8_INIT("#ifdef ppf_10000_a\n#error leaked_macro\n#endif\nppf_10000_a\n"),
            .id = 10001,
        },
        {
            .source = BUSTER_S8_INIT("__LINE__ __FILE__\n"),
            .id = 10002,
        },
        {
            .source = BUSTER_S8_INIT("#line 81 \"case-private.c\"\n__LINE__\n"),
            .id = 10003,
        },
        {
            .source = BUSTER_S8_INIT("__LINE__ __FILE__\n"),
            .id = 10004,
        },
        {
            .source = BUSTER_S8_INIT("__COUNTER__ __COUNTER__\n"),
            .id = 10005,
            .isolated = true,
        },
        {
            .source = BUSTER_S8_INIT("__COUNTER__ __COUNTER__\n"),
            .id = 10006,
            .isolated = true,
        },
        {
            .source = BUSTER_S8_INIT("\"BUSTER_PPF_END_0\" BUSTER_PPF_END_0_suffix\n"),
            .id = 10007,
        },
        {
            .source = BUSTER_S8_INIT("#warning ppf_10008_message\n23\n"),
            .id = 10008,
        },
    };
    const String8 expected[] = {
        BUSTER_S8_INIT("17"),
        BUSTER_S8_INIT("ppf_10000_a"),
        BUSTER_S8_INIT("1 \"ppf_10002.c\""),
        BUSTER_S8_INIT("81"),
        BUSTER_S8_INIT("1 \"ppf_10004.c\""),
        BUSTER_S8_INIT("0 1"),
        BUSTER_S8_INIT("0 1"),
        BUSTER_S8_INIT("\"BUSTER_PPF_END_0\" BUSTER_PPF_END_0_suffix"),
        BUSTER_S8_INIT("23"),
    };
    PreprocessorFuzzOutput outputs[BUSTER_ARRAY_LENGTH(cases)] = { 0 };
    const bool valid = preprocessor_fuzz_clang(memory, clang_path, c23, cases, BUSTER_ARRAY_LENGTH(cases), false, outputs);
    preprocessor_fuzz_test_expect(tally, valid, "isolation fixture batch failed");
    for (uint64_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        tally->case_id = cases[i].id;
        const PreprocessorFuzzOutput reference = preprocessor_fuzz_test_output(expected[i], true);
        preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_compare(memory, outputs[i], reference) == BUSTER_PPF_EQUAL,
            "case isolation, line identity, counter or marker token changed");
    }
    const PreprocessorFuzzCase rejection_cases[] = {
        {
            .source = BUSTER_S8_INIT("#error ppf_10009_rejection\n"),
            .id = 10009,
        },
        {
            .source = BUSTER_S8_INIT("42\n"),
            .id = 10010,
        },
    };
    PreprocessorFuzzOutput rejected[2] = { 0 };
    const bool recovered = preprocessor_fuzz_clang(memory, clang_path, c23, rejection_cases, 2, false, rejected);
    preprocessor_fuzz_test_expect(tally, recovered && rejected[0].valid && !rejected[0].accepted, "source rejection became an infrastructure failure");
    const PreprocessorFuzzOutput forty_two = preprocessor_fuzz_test_output(BUSTER_S8("42"), true);
    preprocessor_fuzz_test_expect(tally, recovered && preprocessor_fuzz_compare(memory, rejected[1], forty_two) == BUSTER_PPF_EQUAL,
        "rejected batch did not recover its accepted case");

    const uint64_t failure_mark = memory->length;
    PreprocessorFuzzOutput failed = { 0 };
    const bool launched = preprocessor_fuzz_clang(memory, "/this/path/does/not/exist/buster-clang", c23, cases, 1, false, &failed);
    preprocessor_fuzz_test_expect(tally, !launched && !failed.valid && memory->length == failure_mark, "failed clang launch became source rejection or leaked arena memory");
    // These deliberate violations of the generator's reserved marker namespace must fail the transport,
    // rather than being reported as a preprocessor rejection or assigned to the wrong case.
    const PreprocessorFuzzCase broken_markers[] = {
        {
            .source = BUSTER_S8_INIT("BUSTER_PPF_END_0\n"),
            .id = 10011,
        },
        {
            .source = BUSTER_S8_INIT("#define BUSTER_PPF_END_0 consumed\n13\n"),
            .id = 10012,
        },
        {
            .source = BUSTER_S8_INIT("BUSTER_PPF_END_1\n"),
            .id = 10013,
        },
    };
    for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(broken_markers); i += 1)
    {
        tally->case_id = broken_markers[i].id;
        const bool marker_valid = preprocessor_fuzz_clang(memory, clang_path, c23, &broken_markers[i], 1, false, &failed);
        preprocessor_fuzz_test_expect(tally, !marker_valid && !failed.valid && memory->length == failure_mark,
            "extra, missing or out-of-order marker was accepted");
    }
    arena_pop_to(memory, mark);
}

static void preprocessor_fuzz_test_saved(PreprocessorFuzzTestTally* tally, String8 source)
{
    char directory[] = "/tmp/buster-preprocessor-fuzz-test-XXXXXX";
    const int previous_directory = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const bool created = previous_directory >= 0 && mkdtemp(directory) != 0;
    const bool changed = created && chdir(directory) == 0;
    preprocessor_fuzz_test_expect(tally, changed, "temporary failure directory could not be created");
    if (changed)
    {
        const bool saved = preprocessor_fuzz_save(source, 17);
        preprocessor_fuzz_test_expect(tally, saved, "minimized case was not saved");
        if (saved)
        {
            preprocessor_fuzz_test_expect(tally, !preprocessor_fuzz_save(BUSTER_S8("replacement"), 17), "existing failure was overwritten");
            const int file = open("fuzz_failure_17.c", O_RDONLY | O_CLOEXEC);
            char contents[BUSTER_PPF_MAX_CASE_BYTES + 1];
            uint64_t length = 0;
            bool read_ok = file >= 0;
            bool complete = false;
            while (read_ok && !complete && length < sizeof(contents))
            {
                const ssize_t count = read(file, contents + length, sizeof(contents) - length);
                if (count > 0)
                {
                    length += (uint64_t)count;
                }
                else if (count == 0)
                {
                    complete = true;
                }
                else if (errno != EINTR)
                {
                    read_ok = false;
                }
            }
            if (file >= 0)
            {
                close(file);
            }
            preprocessor_fuzz_test_expect(tally, read_ok && complete && length == source.length
                && memcmp(contents, source.pointer, length) == 0, "saved failure did not round-trip");
            preprocessor_fuzz_test_expect(tally, unlink("fuzz_failure_17.c") == 0, "saved failure was not removed");
        }
        preprocessor_fuzz_test_expect(tally, fchdir(previous_directory) == 0, "original directory was not restored");
    }
    if (previous_directory >= 0)
    {
        close(previous_directory);
    }
    if (created)
    {
        preprocessor_fuzz_test_expect(tally, rmdir(directory) == 0, "temporary failure directory was not removed");
    }
}

static void preprocessor_fuzz_test_minimizer(PreprocessorFuzzTestTally* tally, Arena* memory)
{
    const uint64_t mark = memory->length;
    const String8 noise = BUSTER_S8("discarded\n/* BUSTER_PPF_LEFT BUSTER_PPF_RIGHT */\n"
        "BUSTER_PPF_LEFT + removable\n\"BUSTER_PPF_RIGHT\"\nmore_noise BUSTER_PPF_RIGHT\n");
    const String8 exact = BUSTER_S8("BUSTER_PPF_LEFT BUSTER_PPF_RIGHT");
    const String8 false_matches[] = {
        BUSTER_S8_INIT("/* BUSTER_PPF_LEFT BUSTER_PPF_RIGHT */"),
        BUSTER_S8_INIT("\"BUSTER_PPF_LEFT\" BUSTER_PPF_RIGHT"),
        BUSTER_S8_INIT("BUSTER_PPF_LEFT_suffix BUSTER_PPF_RIGHT"),
        BUSTER_S8_INIT("BUSTER_PPF_LEFT"),
    };
    preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_planted(memory, noise), "planted mismatch is not present");
    for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(false_matches); i += 1)
    {
        preprocessor_fuzz_test_expect(tally, !preprocessor_fuzz_planted(memory, false_matches[i]), "planted adapter matched a comment, string, substring or missing token");
    }
    const PreprocessorFuzzOracle planted = {
        .kind = BUSTER_PPF_ORACLE_PLANTED,
    };
    String8 minimized = { 0 };
    const bool reduced = preprocessor_fuzz_minimize(memory, &planted, noise, &minimized);
    preprocessor_fuzz_test_expect(tally, reduced, "planted mismatch did not minimize");
    if (reduced)
    {
        const PreprocessorFuzzOutput actual = preprocessor_fuzz_test_output(minimized, true);
        const PreprocessorFuzzOutput expected = preprocessor_fuzz_test_output(exact, true);
        preprocessor_fuzz_test_expect(tally, minimized.length < noise.length
            && preprocessor_fuzz_compare(memory, actual, expected) == BUSTER_PPF_EQUAL, "minimizer did not reach the expected two-token minimum");
        preprocessor_fuzz_test_expect(tally, preprocessor_fuzz_planted(memory, minimized), "minimization lost the difference");
        preprocessor_fuzz_test_saved(tally, minimized);
    }
    const uint64_t failure_mark = memory->length;
    String8 unchanged = BUSTER_S8("unchanged");
    const String8 before = unchanged;
    const bool equal_minimized = preprocessor_fuzz_minimize(memory, &planted, BUSTER_S8("BUSTER_PPF_LEFT"), &unchanged);
    preprocessor_fuzz_test_expect(tally, !equal_minimized && memory->length == failure_mark
        && unchanged.pointer == before.pointer && unchanged.length == before.length, "failed minimization changed its result or arena");
    Arena no_memory = { 0 };
    const bool no_memory_minimized = preprocessor_fuzz_minimize(&no_memory, &planted, exact, &unchanged);
    preprocessor_fuzz_test_expect(tally, !no_memory_minimized && no_memory.length == 0
        && unchanged.pointer == before.pointer && unchanged.length == before.length, "minimizer allocation failure changed its result");
    arena_pop_to(memory, mark);
}

static bool preprocessor_fuzz_test_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    errno = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    const bool valid = text[0] >= '0' && text[0] <= '9' && end != text && *end == 0 && errno == 0;
    if (valid)
    {
        *value = parsed;
    }
    return valid;
}

static int run_preprocessor_fuzz_tests(int argc, char** argv)
{
    uint64_t count = 64;
    uint64_t seed = 1;
    const char* clang_path = "clang";
    bool c23 = false;
    bool valid = true;
    for (int argument = 0; argument < argc; argument += 1)
    {
        if (strcmp(argv[argument], "--fuzz") == 0 && argument + 1 < argc)
        {
            argument += 1;
            valid = preprocessor_fuzz_test_parse_u64(argv[argument], &count) && valid;
        }
        else if (strcmp(argv[argument], "--seed") == 0 && argument + 1 < argc)
        {
            argument += 1;
            valid = preprocessor_fuzz_test_parse_u64(argv[argument], &seed) && valid;
        }
        else if (strcmp(argv[argument], "--clang") == 0 && argument + 1 < argc)
        {
            argument += 1;
            clang_path = argv[argument];
        }
        else if (strcmp(argv[argument], "--std") == 0 && argument + 1 < argc)
        {
            argument += 1;
            c23 = strcmp(argv[argument], "c23") == 0;
            valid = valid && (c23 || strcmp(argv[argument], "c17") == 0);
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-preprocessor-fuzz [--fuzz N] [--seed N] [--clang PATH] [--std c17|c23]\n");
    }
    else if (!preprocessor_fuzz_clang_version(clang_path))
    {
        fprintf(stderr, "preprocessor-fuzz: --clang must name an executable clang 23\n");
    }
    else
    {
        // Batched and individual processes must give __DATE__ and __TIME__ alike.
        setenv("SOURCE_DATE_EPOCH", BUSTER_PPF_EPOCH, 1);
        Arena memory = arena_reserve(BUSTER_PPF_TEST_ARENA_BYTES);
        PreprocessorFuzzTestTally tally = { 0 };
        preprocessor_fuzz_test_expect(&tally, memory.base != 0, "test arena reservation failed");
        if (memory.base != 0)
        {
            preprocessor_fuzz_test_comparison(&tally, &memory);
            preprocessor_fuzz_test_generation(&tally, &memory, c23);
            preprocessor_fuzz_test_minimizer(&tally, &memory);
            preprocessor_fuzz_test_oracle(&tally, &memory, clang_path, c23);
            preprocessor_fuzz_test_rejection_coverage(&tally, &memory, clang_path, c23);
            preprocessor_fuzz_test_arithmetic_coverage(&tally, &memory, clang_path, c23);
            const uint64_t actual_count = count < BUSTER_PPF_CONSTRUCT_COUNT ? BUSTER_PPF_CONSTRUCT_COUNT : count;
            uint64_t first_id = 0;
            while (first_id < actual_count)
            {
                const uint64_t remaining = actual_count - first_id;
                const uint64_t batch_count = remaining < BUSTER_PPF_TEST_BATCH ? remaining : BUSTER_PPF_TEST_BATCH;
                preprocessor_fuzz_test_batch(&tally, &memory, clang_path, c23, seed, first_id, batch_count);
                first_id += batch_count;
            }
        }
        printf("preprocessor-fuzz: %" PRIu64 " checks, %" PRIu64 " failures (seed=%" PRIu64 ", fuzz=%" PRIu64 ", %s)\n",
            tally.checks, tally.failures, seed, count, c23 ? "c23" : "c17");
        result = tally.failures == 0 ? 0 : 1;
        arena_release(&memory);
    }
    return result;
}
