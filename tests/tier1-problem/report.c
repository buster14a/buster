// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #938's harness's report of buster's allocations (#939): each problem file decoded, adapted and allocated by both of
// buster's allocator paths, every allocation reported only past its certificate (tier1_problem_allocate). A line per
// file, its function and its one terminal reason, the allocations' sizes where it was accepted, then the tally. The exit
// status is 1 where an allocation fails its certificate, a file can't be read or none is given, else 0: a refusal is an
// outcome the harness reports, not a failure.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "../../src/buster/compiler/tier1_problem.c"
#include "../../src/buster/compiler/lane.c"
#include "../../src/buster/compiler/tier1_allocate.c"
#include "../../src/buster/compiler/tier1_certificate.c"
#include "../../src/buster/compiler/tier1_certificate_interval.c"
#include "../../src/buster/compiler/tier1_problem_intervals.c"

// The function's name: the snapshot's last metadata field.
#define BUSTER_TP_REPORT_FUNCTION (BUSTER_TP_META_COUNT - 1)
#define BUSTER_TP_REPORT_FILE_BYTES (UINT32_C(1) << 24)

int main(int argc, char** argv)
{
    static _Alignas(64) uint8_t memory[UINT32_C(1) << 26];
    uint8_t* bytes = malloc(BUSTER_TP_REPORT_FILE_BYTES);
    Tier1ProblemTally tally = { 0 };
    bool failed = argc < 2 || bytes == 0;
    if (argc < 2)
    {
        fprintf(stderr, "usage: tier1-problem-report FILE...\n");
    }
    for (int k = 1; bytes != 0 && k < argc; k += 1)
    {
        FILE* file = fopen(argv[k], "rb");
        const size_t size = file != 0 ? fread(bytes, 1, BUSTER_TP_REPORT_FILE_BYTES, file) : 0;
        const bool whole = file != 0 && ferror(file) == 0 && feof(file) != 0;
        if (file != 0)
        {
            fclose(file);
        }
        if (!whole)
        {
            printf("%s\tunreadable\n", argv[k]);
            failed = true;
        }
        else
        {
            Tier1Problem problem = { 0 };
            Arena arena = { .base = memory, .length = 0, .capacity = sizeof(memory) };
            Tier1ProblemAllocation made = { 0 };
            const Tier1ProblemReason decoded = tier1_problem_decode(bytes, size, &problem);
            const Tier1ProblemReason reason = decoded == TIER1_PROBLEM_OK ? tier1_problem_allocate(&problem, &arena, &made) : decoded;
            tier1_problem_tally(&tally, reason);
            const Tier1ProblemText name = decoded == TIER1_PROBLEM_OK ? problem.metadata[BUSTER_TP_REPORT_FUNCTION] : (Tier1ProblemText){ 0 };
            printf("%s\t%.*s\t%s", argv[k], (int)name.size, name.data != 0 ? (const char*)name.data : "", tier1_problem_reason_name(reason));
            if (reason == TIER1_PROBLEM_OK)
            {
                printf("\tinstructions=%" PRIu32 "\toperands=%" PRIu32 "\tintervals=%" PRIu32 "\tfragments=%" PRIu32 "/%" PRIu32 "\tspills=%" PRIu32 "/%" PRIu32 "\tcertified=2", made.stats.original_instructions, made.stats.original_operands,
                       made.input.count, made.allocations[0].fragment_count, made.allocations[1].fragment_count, made.allocations[0].spill_count, made.allocations[1].spill_count);
            }
            printf("\n");
            failed |= reason == TIER1_PROBLEM_CERTIFICATE_REJECTED;
        }
    }
    printf("tier1-problem-report: functions=%" PRIu64 "\taccepted=%" PRIu64 "\trejected=%" PRIu64, tally.functions, tally.accepted, tally.rejected);
    for (uint32_t r = 1; r < TIER1_PROBLEM_REASON_COUNT; r += 1)
    {
        if (tally.reasons[r] != 0)
        {
            printf("\t%s=%" PRIu64, tier1_problem_reason_name((Tier1ProblemReason)r), tally.reasons[r]);
        }
    }
    printf("\n");
    free(bytes);
    return failed ? 1 : 0;
}
