// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#define _GNU_SOURCE
// Original optional driver. Its output is raw evidence, not a speedup assertion.
#include "pp_pair.h"
#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool pp_pair_number(const char* text, uint32_t limit, uint32_t* result)
{
    errno = 0;
    char* end = 0;
    const unsigned long value = strtoul(text, &end, 10);
    const bool valid = text[0] != 0 && text[0] != '-' && errno == 0 && *end == 0 && value <= limit;
    if (valid)
    {
        *result = (uint32_t)value;
    }
    return valid;
}

static bool pp_pair_equal(const PpPairSample* a, const PpPairSample* b)
{
    // Time and memory usage may differ. The entire output identity and useful-work counts may not.
    return a->input_tokens == b->input_tokens && a->output_tokens == b->output_tokens && a->input_bytes == b->input_bytes &&
           a->digest == b->digest && a->token_digest == b->token_digest && a->expansions == b->expansions &&
           a->substitutions == b->substitutions && a->generated == b->generated && a->runs == b->runs &&
           memcmp(a->statistics, b->statistics, sizeof(a->statistics)) == 0;
}

static bool pp_pair_header(void)
{
    const int result = fputs("phase,round,order,variant,preprocess_ns,print_ns,release_ns,input_tokens,output_tokens,input_bytes,"
          "read_tsc,lex_tsc,refine_tsc,directives_tsc,total_tsc,digest,token_digest,history_bytes,temporary_peak_bytes,"
          "expansions,substitutions,generated,runs,file_run_tokens,file_runs,buffer_run_tokens,buffer_runs,"
          "direct_expansions,buffered_expansions,invocations,arguments,expanded_arguments,pastes,builtins,"
          "conditions,condition_tokens,long_conditions,longest_condition,directives,skipped_guards,skipped_once\n", stdout);
    return result != EOF && fflush(stdout) == 0;
}

static bool pp_pair_row(const char* phase, uint32_t round, uint32_t order, uint32_t variant, const PpPairSample* sample)
{
    printf("%s,%u,%u,%c,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64,
           phase, round, order, variant == 0 ? 'a' : 'b', sample->preprocess_ns, sample->print_ns, sample->release_ns,
           sample->input_tokens, sample->output_tokens, sample->input_bytes);
    for (uint32_t i = 0; i < BUSTER_PP_PAIR_PHASES; i += 1)
    {
        printf(",%" PRIu64, sample->ticks[i]);
    }
    printf(",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64,
           sample->digest, sample->token_digest, sample->history_bytes, sample->temporary_max_bytes,
           sample->expansions, sample->substitutions, sample->generated, sample->runs);
    for (uint32_t i = 0; i < BUSTER_PP_PAIR_STATISTICS; i += 1)
    {
        printf(",%" PRIu64, sample->statistics[i]);
    }
    putchar('\n');
    return fflush(stdout) == 0 && !ferror(stdout);
}

int main(int argc, char** argv)
{
    uint32_t rounds = 31;
    uint32_t warmups = 3;
    uint32_t cpu = 2;
    uint32_t first = 0;
    bool valid = true;
    for (uint32_t i = 1; valid && i < (uint32_t)argc; i += 1)
    {
        if (strcmp(argv[i], "--") == 0)
        {
            first = i + 1;
            break;
        }
        if (i + 1 >= (uint32_t)argc)
        {
            valid = false;
        }
        else if (strcmp(argv[i], "--rounds") == 0)
        {
            valid = pp_pair_number(argv[++i], 10000, &rounds) && rounds != 0;
        }
        else if (strcmp(argv[i], "--warmup") == 0)
        {
            valid = pp_pair_number(argv[++i], 100, &warmups);
        }
        else if (strcmp(argv[i], "--cpu") == 0)
        {
            valid = pp_pair_number(argv[++i], CPU_SETSIZE - 1, &cpu);
        }
        else
        {
            valid = false;
        }
    }
    valid = valid && first != 0 && first < (uint32_t)argc;
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: %s [--rounds N] [--warmup N] [--cpu N] -- PP_OPTIONS SOURCE\n", argv[0]);
    }
    else
    {
        cpu_set_t affinity;
        CPU_ZERO(&affinity);
        CPU_SET(cpu, &affinity);
        valid = sched_setaffinity(0, sizeof(affinity), &affinity) == 0;
        if (!valid)
        {
            perror("sched_setaffinity");
        }
        else
        {
            valid = pp_a_configure(argc - (int)first, argv + first) && pp_b_configure(argc - (int)first, argv + first);
        }
        PpPairSample expected = { 0 };
        bool have_expected = false;
        if (valid)
        {
            valid = pp_pair_header();
        }
        for (uint32_t round = 0; valid && round < warmups + rounds; round += 1)
        {
            for (uint32_t order = 0; valid && order < 2; order += 1)
            {
                const uint32_t variant = (round + order) & 1;
                PpPairSample sample = { 0 };
                valid = variant == 0 ? pp_a_run(&sample) : pp_b_run(&sample);
                if (valid && !have_expected)
                {
                    expected = sample;
                    have_expected = true;
                }
                valid = valid && pp_pair_equal(&sample, &expected);
                if (!valid)
                {
                    fprintf(stderr, "variant %c failed or changed output/work counts at round %u; no performance assertion\n",
                            variant == 0 ? 'a' : 'b', round + 1);
                }
                else
                {
                    valid = pp_pair_row(round < warmups ? "warmup" : "sample", round < warmups ? round + 1 : round - warmups + 1,
                                        order + 1, variant, &sample);
                }
            }
        }
        pp_a_release();
        pp_b_release();
        if (ferror(stdout))
        {
            fprintf(stderr, "measurement output failed: %s\n", strerror(errno));
        }
        result = valid ? 0 : 1;
    }
    return result;
}
