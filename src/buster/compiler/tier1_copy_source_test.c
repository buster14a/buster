// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2818's original C fixture through the production driver. Included only by tier1_pipeline_test.c. The fixture's
// byte model and Clang's execution check semantics; assembly checks make disabling the late pairing pass fail too.

static bool tier1_copy_source_output(const char* path, char* bytes, size_t capacity)
{
    FILE* file = fopen(path, "r");
    bool ok = file != 0;
    if (ok)
    {
        ok = fgets(bytes, capacity, file) != 0 && fgetc(file) == EOF && !ferror(file);
        ok = fclose(file) == 0 && ok;
    }
    return ok;
}

static bool tier1_copy_source_shape(const char* path, const char* symbol, uint32_t* halves, uint32_t* cached_calls)
{
    FILE* file = fopen(path, "r");
    bool ok = file != 0;
    bool found = false;
    bool body = false;
    *halves = 0;
    *cached_calls = 0;
    if (ok)
    {
        char label[128];
        snprintf(label, sizeof(label), "%s:", symbol);
        char line[1024];
        while (fgets(line, sizeof(line), file) != 0)
        {
            if (strstr(line, label) != 0)
            {
                found = true;
                body = true;
            }
            else if (body && strstr(line, ".size") != 0 && strstr(line, symbol) != 0)
            {
                body = false;
            }
            if (body)
            {
                *halves += strstr(line, "movhps") != 0;
                *cached_calls += strstr(line, "call") != 0 && strstr(line, "copy_cached_64") != 0;
            }
        }
        ok = found && !ferror(file);
        ok = fclose(file) == 0 && ok;
    }
    return ok;
}

static void tier1_pipeline_copy_source(Tier1PipelineTest* test, const char* clang)
{
    char directory[4096];
    char binary[4096];
    char output[4096];
    char assembly[4096];
    const int length = snprintf(directory, sizeof(directory), "%s/buster-copy-halves-XXXXXX", oracle_temporary_directory());
    bool ok = length > 0 && (uint64_t)length < sizeof(directory) - 32 && mkdtemp(directory) != 0;
    const bool created = ok;
    const uint64_t failures = test->failures;
    tier1_pipeline_expect(test, ok, "create the local-copy C regression directory");
    if (ok)
    {
        snprintf(binary, sizeof(binary), "%s/program", directory);
        snprintf(output, sizeof(output), "%s/output.txt", directory);
        snprintf(assembly, sizeof(assembly), "%s/copies.s", directory);
    }
    char expected[128] = { 0 };
    char* const source = "tests/tier1/copy-halves.c";
    if (ok)
    {
        char* const compile[] = { (char*)clang, "-std=gnu17", "-O2", "-march=x86-64", "-o", binary, source, 0 };
        const Tier1PipelineProcess compiled = tier1_pipeline_process(compile, 0, 30000);
        ok = tier1_pipeline_expect(test, tier1_pipeline_process_ok(&compiled), "Clang compiles the independent local-copy C fixture");
    }
    if (ok)
    {
        char* const run[] = { binary, 0 };
        const Tier1PipelineProcess ran = tier1_pipeline_process(run, output, 10000);
        ok = tier1_pipeline_expect(test, tier1_pipeline_process_ok(&ran) && tier1_copy_source_output(output, expected, sizeof(expected)) &&
                                  strstr(expected, "copy-halves: 0 failures, ") == expected,
                                  "Clang agrees with every byte formula and destination guard");
    }
    static const char* const targets[] = {
        "-march=x86-64",
        "-march=x86-64-v3",
        "-march=x86-64-v4",
    };
    for (uint32_t target = 0; ok && target < BUSTER_ARRAY_LENGTH(targets); target += 1)
    {
        for (uint32_t remat = 0; ok && remat < 2; remat += 1)
        {
            char* compile[] = { "/proc/self/exe", "--tier1", "--tier1-strict", "--verify", "-std=gnu17", (char*)targets[target],
                                "-o", binary, source, remat == 0 ? "--tier1-no-rematerialize" : 0, 0 };
            const Tier1PipelineProcess compiled = tier1_pipeline_process(compile, 0, 30000);
            ok = tier1_pipeline_expect(test, tier1_pipeline_process_ok(&compiled), "all copy fixture functions compile at Tier 1 and verify");
            if (ok)
            {
                char actual[128] = { 0 };
                char* const run[] = { binary, 0 };
                const Tier1PipelineProcess ran = tier1_pipeline_process(run, output, 10000);
                ok = tier1_pipeline_expect(test, tier1_pipeline_process_ok(&ran) && tier1_copy_source_output(output, actual, sizeof(actual)) &&
                                          strcmp(actual, expected) == 0,
                                          "local/cold/self/volatile copies, odd tails and overlapping assignments match Clang");
            }
        }
        if (ok)
        {
            // One lane emits the rooted candidate before its later caller, exposing any mutation of the cached summary.
            char* const compile[] = { "/proc/self/exe", "--tier1", "--tier1-strict", "--verify", "-std=gnu17", (char*)targets[target],
                                      "--lanes", "1", "-S", "-o", assembly, source, 0 };
            const Tier1PipelineProcess compiled = tier1_pipeline_process(compile, 0, 30000);
            static const char* const symbols[] = { "copy_local_64", "copy_cached_64", "consume_cached_64", "copy_unused_64" };
            for (uint32_t s = 0; ok && s < BUSTER_ARRAY_LENGTH(symbols); s += 1)
            {
                uint32_t halves = 0;
                uint32_t cached_calls = 0;
                const bool shape = tier1_pipeline_process_ok(&compiled) && tier1_copy_source_shape(assembly, symbols[s], &halves, &cached_calls);
                const uint32_t expected_halves = s == 2 ? 0 : 4;
                ok = tier1_pipeline_expect(test, shape && halves == expected_halves && cached_calls == 0,
                                          s == 0 ? "the 64-byte local copy has four paired high-half loads at every target" :
                                          s == 1 ? "the cached candidate's own code pairs on an owned copy of its scalar summary" :
                                          s == 2 ? "the caller expands the unchanged scalar summary and SROA removes unread fields before pairing" :
                                                   "paired copies to an unobserved aggregate destination still emit");
                if (!ok)
                {
                    fprintf(stderr, "copy-halves: %s has %u high-half loads in %s (expected %u), %u cached calls\n",
                            targets[target], halves, symbols[s], expected_halves, cached_calls);
                }
            }
        }
    }
    if (created)
    {
        if (ok && failures == test->failures)
        {
            unlink(binary);
            unlink(output);
            unlink(assembly);
            rmdir(directory);
        }
        else
        {
            fprintf(stderr, "copy-halves: retained C regression files in %s\n", directory);
        }
    }
}
