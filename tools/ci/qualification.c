// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The required CI context's fail-closed decision. The workflow supplies results,
// not exit codes: an unexpected skipped job is not a successful qualification.
// --self-test exhausts the state space against an explicit list of green cases.
// This administrative predicate is outside compiler/measurement hot paths.
//
// The `evidence` job (tools/ci/nightly.sh) must succeed in every run, and says
// whether the 9700X job was admitted. A pull request, draft or ready, never is
// (#2055): the context passes on its hosted correctness alone and says that
// native correctness and measurements run on its merged state in the nightly
// window. A merge-queue candidate (#1430) never is either: it passes on the
// full hosted contract (every row and sanitizer, its fuzz campaigns), whose
// coverage the workflow's coverage step checked job by job from the run's own
// jobs (tools/ci/nightly.c's `coverage`), with no documentation exemption.
// Every other green run with code ran the 9700X job in full, native
// correctness first.
// A nightly dispatch (backlog, snapshot) reports through its coverage record,
// not this context; one that reaches this predicate fails.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum Result Result;
enum Result
{
    RESULT_SUCCESS,
    RESULT_FAILURE,
    RESULT_CANCELLED,
    RESULT_SKIPPED,
    RESULT_INVALID,
    RESULT_COUNT
};

typedef enum Event Event;
enum Event
{
    EVENT_PR,
    EVENT_PUSH,
    EVENT_SCHEDULE,
    EVENT_DISPATCH,
    // A merge-queue candidate (#1430).
    EVENT_MERGE_GROUP,
    EVENT_INVALID,
    EVENT_COUNT
};

typedef enum Code Code;
enum Code
{
    CODE_YES,
    CODE_NO,
    CODE_UNSET,
    CODE_INVALID,
    CODE_COUNT
};

typedef enum Hardware Hardware;
enum Hardware
{
    HARDWARE_ADMIT,
    // Skipped for any reason (a draft, the nightly window, a deferral).
    HARDWARE_SKIP,
    HARDWARE_INVALID,
    HARDWARE_COUNT
};

typedef enum Purpose Purpose;
enum Purpose
{
    // Not a dispatch.
    PURPOSE_UNSET,
    PURPOSE_FULL,
    PURPOSE_NIGHTLY,
    PURPOSE_INVALID,
    PURPOSE_COUNT
};

typedef struct Qualification Qualification;
struct Qualification
{
    Event event;
    Code code;
    bool draft;
    Purpose purpose;
    Hardware hardware;
    Result changes;
    Result size;
    Result hooks;
    Result evidence;
    Result tests;
    Result fuzz;
    Result native;
    // The coverage step's outcome: a merge-queue candidate's alone, skipped elsewhere.
    Result coverage;
};

static int qualification_find(const char* value, const char* const* names, size_t count)
{
    int found = (int)count;
    if (value != NULL)
    {
        for (size_t i = 0; i < count; i += 1)
        {
            if (strcmp(value, names[i]) == 0)
            {
                found = (int)i;
            }
        }
    }
    return found;
}

static Result qualification_result(const char* name)
{
    static const char* const names[] = { "success", "failure", "cancelled", "skipped" };
    return (Result)qualification_find(getenv(name), names, sizeof(names) / sizeof(names[0]));
}

static bool qualification_no_code(const Qualification* input)
{
    // Only pre-merge PRs may claim the no-code exemption; every main push runs its hosted rows (#1271, #2057).
    bool selected = input->event == EVENT_PR;
    bool result = selected && input->changes == RESULT_SUCCESS && input->code == CODE_NO;
    result = result && input->size == RESULT_SUCCESS && input->hooks == RESULT_SUCCESS;
    result = result && input->tests == RESULT_SKIPPED && input->fuzz == RESULT_SKIPPED && input->native == RESULT_SKIPPED;
    // The evidence job skips the 9700X for every pull request (#2055).
    result = result && input->hardware == HARDWARE_SKIP && input->coverage == RESULT_SKIPPED;
    return result;
}

static bool qualification_pass(const Qualification* input)
{
    const Event event = input->event;
    bool valid = event < EVENT_INVALID && (event == EVENT_PR || !input->draft);
    valid = valid && (event == EVENT_DISPATCH ? input->purpose == PURPOSE_FULL : input->purpose == PURPOSE_UNSET);
    valid = valid && input->evidence == RESULT_SUCCESS;
    bool selected = (event == EVENT_PR || event == EVENT_PUSH || event == EVENT_MERGE_GROUP) && input->changes == RESULT_SUCCESS && input->code == CODE_YES;
    bool periodic = (event == EVENT_SCHEDULE || event == EVENT_DISPATCH) && input->changes == RESULT_SKIPPED && input->code == CODE_UNSET;
    bool hosted = (selected || periodic) && input->size == RESULT_SUCCESS && input->hooks == RESULT_SUCCESS && input->tests == RESULT_SUCCESS;
    // Only a merge-queue candidate runs the coverage step.
    hosted = hosted && input->coverage == (event == EVENT_MERGE_GROUP ? RESULT_SUCCESS : RESULT_SKIPPED);
    // A pull request, draft or ready, qualifies on its hosted plain rows alone: no fuzzing, no 9700X job (#2055).
    bool pull_request = event == EVENT_PR && hosted && input->hardware == HARDWARE_SKIP;
    pull_request = pull_request && input->fuzz == RESULT_SKIPPED && input->native == RESULT_SKIPPED;
    // A merge-queue candidate (#1430) on the full hosted contract and its fuzz campaigns, which its coverage step
    // checked job by job; never the 9700X job, which its merged state gets in the nightly window.
    bool candidate = event == EVENT_MERGE_GROUP && hosted && input->hardware == HARDWARE_SKIP;
    candidate = candidate && input->fuzz == RESULT_SUCCESS && input->native == RESULT_SKIPPED;
    // Every other run with code passes only through the 9700X job's native correctness and measurements.
    bool native = event != EVENT_PR && event != EVENT_MERGE_GROUP && hosted && input->hardware == HARDWARE_ADMIT && input->native == RESULT_SUCCESS;
    native = native && input->fuzz == RESULT_SUCCESS;
    return valid && (qualification_no_code(input) || pull_request || candidate || native);
}

// Each input's place in a mixed-radix index over every combination, for the self-test's table of allowed tuples.
static uint32_t qualification_index(const Qualification* input)
{
    const Result results[8] = { input->changes, input->size, input->hooks, input->evidence, input->tests, input->fuzz, input->native, input->coverage };
    uint32_t index = (uint32_t)input->event;
    index = index * CODE_COUNT + (uint32_t)input->code;
    index = index * 2 + (input->draft ? 1u : 0u);
    index = index * PURPOSE_COUNT + (uint32_t)input->purpose;
    index = index * HARDWARE_COUNT + (uint32_t)input->hardware;
    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i += 1)
    {
        index = index * RESULT_COUNT + (uint32_t)results[i];
    }
    return index;
}

static int qualification_test(void)
{
    // These are all allowed tuples, independently of the predicate's branches.
    // A pull request, draft or ready, is green on its hosted rows with the 9700X
    // job and fuzzing skipped (#2055); a merge-queue candidate on its hosted
    // rows, its fuzz campaigns and its coverage step, with the 9700X job
    // skipped (#1430); no other green tuple with code skipped the 9700X job.
    // Only a candidate's coverage step runs.
    const Qualification allowed[] = {
        { EVENT_PR, CODE_YES, false, PURPOSE_UNSET, HARDWARE_SKIP, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED, RESULT_SKIPPED, RESULT_SKIPPED },
        { EVENT_PR, CODE_YES, true, PURPOSE_UNSET, HARDWARE_SKIP, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED, RESULT_SKIPPED, RESULT_SKIPPED },
        { EVENT_PUSH, CODE_YES, false, PURPOSE_UNSET, HARDWARE_ADMIT, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED },
        { EVENT_SCHEDULE, CODE_UNSET, false, PURPOSE_UNSET, HARDWARE_ADMIT, RESULT_SKIPPED, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED },
        { EVENT_DISPATCH, CODE_UNSET, false, PURPOSE_FULL, HARDWARE_ADMIT, RESULT_SKIPPED, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED },
        { EVENT_MERGE_GROUP, CODE_YES, false, PURPOSE_UNSET, HARDWARE_SKIP, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED, RESULT_SUCCESS },
        { EVENT_PR, CODE_NO, false, PURPOSE_UNSET, HARDWARE_SKIP, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED, RESULT_SKIPPED, RESULT_SKIPPED, RESULT_SKIPPED },
        { EVENT_PR, CODE_NO, true, PURPOSE_UNSET, HARDWARE_SKIP, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED, RESULT_SKIPPED, RESULT_SKIPPED, RESULT_SKIPPED },
    };
    // Eight job and step results, five values each (including missing/unknown), and every event, selector output,
    // draft state, dispatch purpose and hardware decision.
    const uint32_t results = 390625;
    const uint32_t total = EVENT_COUNT * CODE_COUNT * 2 * PURPOSE_COUNT * HARDWARE_COUNT * results;
    uint8_t* expected = calloc((total + 7) / 8, 1);
    uint32_t checks = 0;
    uint32_t failures = 0;
    for (size_t i = 0; expected != NULL && i < sizeof(allowed) / sizeof(allowed[0]); i += 1)
    {
        const uint32_t index = qualification_index(&allowed[i]);
        expected[index / 8] |= (uint8_t)(1u << (index % 8));
    }
    for (uint32_t encoded = 0; expected != NULL && encoded < total; encoded += 1)
    {
        uint32_t remaining = encoded;
        Result values[8];
        for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i += 1)
        {
            values[7 - i] = (Result)(remaining % RESULT_COUNT);
            remaining /= RESULT_COUNT;
        }
        const Hardware hardware = (Hardware)(remaining % HARDWARE_COUNT);
        remaining /= HARDWARE_COUNT;
        const Purpose purpose = (Purpose)(remaining % PURPOSE_COUNT);
        remaining /= PURPOSE_COUNT;
        const bool draft = remaining % 2 != 0;
        remaining /= 2;
        const Code code = (Code)(remaining % CODE_COUNT);
        remaining /= CODE_COUNT;
        Qualification input = {
            .event = (Event)remaining,
            .code = code,
            .draft = draft,
            .purpose = purpose,
            .hardware = hardware,
            .changes = values[0],
            .size = values[1],
            .hooks = values[2],
            .evidence = values[3],
            .tests = values[4],
            .fuzz = values[5],
            .native = values[6],
            .coverage = values[7],
        };
        const bool want = (expected[encoded / 8] >> (encoded % 8)) & 1u;
        checks += 1;
        if (qualification_index(&input) != encoded || qualification_pass(&input) != want)
        {
            failures += 1;
            if (failures <= 5)
            {
                fprintf(stderr, "qualification: unexpected result for combination %u\n", encoded);
            }
        }
    }
    // The table holds exactly the allowed tuples: none repeated, none lost.
    uint32_t allowed_count = 0;
    for (uint32_t i = 0; expected != NULL && i < (total + 7) / 8; i += 1)
    {
        allowed_count += (uint32_t)__builtin_popcount(expected[i]);
    }
    failures += expected == NULL || allowed_count != sizeof(allowed) / sizeof(allowed[0]);
    free(expected);
    printf("qualification --self-test: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}

int main(int argc, char** argv)
{
    int status = 2;
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
    {
        status = qualification_test();
    }
    else if (argc == 1)
    {
        static const char* const events[] = { "pull_request", "push", "schedule", "workflow_dispatch", "merge_group" };
        static const char* const codes[] = { "true", "false", "" };
        static const char* const drafts[] = { "false", "true" };
        static const char* const purposes[] = { "", "full", "nightly" };
        const int draft = qualification_find(getenv("PR_DRAFT"), drafts, sizeof(drafts) / sizeof(drafts[0]));
        const char* hardware = getenv("HARDWARE");
        const bool admit = hardware != NULL && strcmp(hardware, "admit") == 0;
        const bool skip = hardware != NULL && strcmp(hardware, "skip") == 0;
        const char* purpose = getenv("PURPOSE");
        const char* nightly = purpose != NULL && (strcmp(purpose, "backlog") == 0 || strcmp(purpose, "snapshot") == 0) ? "nightly" : purpose;
        Qualification input = {
            .event = (Event)qualification_find(getenv("EVENT_NAME"), events, sizeof(events) / sizeof(events[0])),
            .code = (Code)qualification_find(getenv("CODE_CHANGED"), codes, sizeof(codes) / sizeof(codes[0])),
            .draft = draft == 1,
            .purpose = (Purpose)qualification_find(nightly != NULL ? nightly : "", purposes, sizeof(purposes) / sizeof(purposes[0])),
            .hardware = admit ? HARDWARE_ADMIT : skip ? HARDWARE_SKIP : HARDWARE_INVALID,
            .changes = qualification_result("CHANGES_RESULT"),
            .size = qualification_result("SIZE_RESULT"),
            .hooks = qualification_result("DCO_RESULT"),
            .evidence = qualification_result("EVIDENCE_RESULT"),
            .tests = qualification_result("TEST_RESULT"),
            .fuzz = qualification_result("FUZZ_RESULT"),
            .native = qualification_result("BENCHMARK_RESULT"),
            .coverage = qualification_result("COVERAGE_RESULT"),
        };
        bool passed = draft < 2 && qualification_pass(&input);
        status = passed ? 0 : 1;
        if (passed && qualification_no_code(&input))
        {
            puts("Documentation-only change; explicit no-code exemption, not native qualification.");
        }
        else if (passed && input.event == EVENT_MERGE_GROUP)
        {
            puts("The merge-queue candidate passed the full hosted contract: every row and sanitizer, the vector pool and the fuzz campaigns, checked job by job. The 9700X wasn't used (#1430): native correctness and measurements run on its first-parent state in the nightly window.");
        }
        else if (passed && input.event == EVENT_PR)
        {
            puts("Hosted correctness passed. Pull requests don't use the 9700X (#2055): native correctness and measurements were NOT run here; they run on the merged first-parent state in the nightly window.");
        }
        else if (passed)
        {
            puts("Hosted and native correctness and required measurements passed.");
        }
        else
        {
            fputs("::error::Qualification failed: required results or selector outputs are missing, skipped, cancelled, failed or inconsistent.\n", stderr);
        }
    }
    else
    {
        fputs("usage: qualification [--self-test]\n", stderr);
    }
    return status;
}
