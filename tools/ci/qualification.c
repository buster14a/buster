// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The required CI context's fail-closed decision. The workflow supplies results,
// not exit codes: an unexpected skipped job is not a successful qualification.
// --self-test exhausts the state space against an explicit list of green cases.
// This administrative predicate is outside compiler/measurement hot paths.
//
// The `evidence` job (tools/ci/nightly.c) must succeed in every run, and says
// whether the 9700X job was admitted. Pull requests never are (#1272;
// tools/ci/admission.c): a pull request qualifies by its hosted results alone,
// with the 9700X excluded by policy (`skip` and `pull-request`) and its job
// skipped, and the context says so rather than claim native correctness. A
// pull request whose 9700X job ran, or whose exclusion has another reason,
// fails. Pushes to main and the daily and `full` dispatched runs need hosted
// and native correctness, fuzzing and the measurements. Merge queues and every
// other event fail: no pre-merge path but a pull request's is supported.
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
    // Skipped because the run is a pull request's: the 9700X never runs for one.
    HARDWARE_EXCLUDED,
    // Skipped for any other reason (the nightly window, a deferral, a dispatch off main's history).
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
    Purpose purpose;
    Hardware hardware;
    Result changes;
    Result size;
    Result hooks;
    Result evidence;
    Result tests;
    Result fuzz;
    Result native;
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

// A change the selector found to be documentation alone: no tests, fuzzing or 9700X job ran.
static bool qualification_no_code(const Qualification* input)
{
    bool selected = input->event == EVENT_PR || input->event == EVENT_PUSH;
    bool result = selected && input->changes == RESULT_SUCCESS && input->code == CODE_NO;
    result = result && input->size == RESULT_SUCCESS && input->hooks == RESULT_SUCCESS;
    result = result && input->tests == RESULT_SKIPPED && input->fuzz == RESULT_SKIPPED && input->native == RESULT_SKIPPED;
    // The evidence job doesn't know the selector's verdict: it excludes every pull request and admits every push.
    result = result && input->hardware == (input->event == EVENT_PR ? HARDWARE_EXCLUDED : HARDWARE_ADMIT);
    return result;
}

// A pull request's code change: its hosted rows passed and the 9700X was excluded by policy, never run.
static bool qualification_hosted_only(const Qualification* input)
{
    bool result = input->event == EVENT_PR && input->changes == RESULT_SUCCESS && input->code == CODE_YES;
    result = result && input->size == RESULT_SUCCESS && input->hooks == RESULT_SUCCESS && input->tests == RESULT_SUCCESS;
    result = result && input->hardware == HARDWARE_EXCLUDED && input->fuzz == RESULT_SKIPPED && input->native == RESULT_SKIPPED;
    return result;
}

static bool qualification_pass(const Qualification* input)
{
    const Event event = input->event;
    bool valid = event < EVENT_INVALID;
    valid = valid && (event == EVENT_DISPATCH ? input->purpose == PURPOSE_FULL : input->purpose == PURPOSE_UNSET);
    valid = valid && input->evidence == RESULT_SUCCESS;
    bool selected = event == EVENT_PUSH && input->changes == RESULT_SUCCESS && input->code == CODE_YES;
    bool periodic = (event == EVENT_SCHEDULE || event == EVENT_DISPATCH) && input->changes == RESULT_SKIPPED && input->code == CODE_UNSET;
    bool hosted = (selected || periodic) && input->size == RESULT_SUCCESS && input->hooks == RESULT_SUCCESS && input->tests == RESULT_SUCCESS;
    bool native = hosted && input->hardware == HARDWARE_ADMIT && input->native == RESULT_SUCCESS && input->fuzz == RESULT_SUCCESS;
    return valid && (qualification_no_code(input) || qualification_hosted_only(input) || native);
}

// Each input's place in a mixed-radix index over every combination, for the self-test's table of allowed tuples.
static uint32_t qualification_index(const Qualification* input)
{
    const Result results[7] = { input->changes, input->size, input->hooks, input->evidence, input->tests, input->fuzz, input->native };
    uint32_t index = (uint32_t)input->event;
    index = index * CODE_COUNT + (uint32_t)input->code;
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
    // A pull request's are hosted alone, the 9700X excluded and its job skipped;
    // every other green tuple with code ran the 9700X job and fuzzing.
    const Qualification allowed[] = {
        { EVENT_PR, CODE_YES, PURPOSE_UNSET, HARDWARE_EXCLUDED, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED, RESULT_SKIPPED },
        { EVENT_PR, CODE_NO, PURPOSE_UNSET, HARDWARE_EXCLUDED, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED, RESULT_SKIPPED, RESULT_SKIPPED },
        { EVENT_PUSH, CODE_YES, PURPOSE_UNSET, HARDWARE_ADMIT, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS },
        { EVENT_PUSH, CODE_NO, PURPOSE_UNSET, HARDWARE_ADMIT, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED, RESULT_SKIPPED, RESULT_SKIPPED },
        { EVENT_SCHEDULE, CODE_UNSET, PURPOSE_UNSET, HARDWARE_ADMIT, RESULT_SKIPPED, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS },
        { EVENT_DISPATCH, CODE_UNSET, PURPOSE_FULL, HARDWARE_ADMIT, RESULT_SKIPPED, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS },
    };
    // Seven job results, five values each (including missing/unknown), and every event (a merge queue's and any
    // other unknown one among them), selector output, dispatch purpose and hardware decision.
    const uint32_t results = 78125;
    const uint32_t total = EVENT_COUNT * CODE_COUNT * PURPOSE_COUNT * HARDWARE_COUNT * results;
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
        Result values[7];
        for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i += 1)
        {
            values[6 - i] = (Result)(remaining % RESULT_COUNT);
            remaining /= RESULT_COUNT;
        }
        const Hardware hardware = (Hardware)(remaining % HARDWARE_COUNT);
        remaining /= HARDWARE_COUNT;
        const Purpose purpose = (Purpose)(remaining % PURPOSE_COUNT);
        remaining /= PURPOSE_COUNT;
        const Code code = (Code)(remaining % CODE_COUNT);
        remaining /= CODE_COUNT;
        Qualification input = {
            .event = (Event)remaining,
            .code = code,
            .purpose = purpose,
            .hardware = hardware,
            .changes = values[0],
            .size = values[1],
            .hooks = values[2],
            .evidence = values[3],
            .tests = values[4],
            .fuzz = values[5],
            .native = values[6],
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
        // merge_group, and any other event, is no supported path: it maps to EVENT_INVALID and fails.
        static const char* const events[] = { "pull_request", "push", "schedule", "workflow_dispatch" };
        static const char* const codes[] = { "true", "false", "" };
        static const char* const purposes[] = { "", "full", "nightly" };
        const char* hardware = getenv("HARDWARE");
        const char* reason = getenv("HARDWARE_REASON");
        const bool admit = hardware != NULL && strcmp(hardware, "admit") == 0;
        const bool skip = hardware != NULL && strcmp(hardware, "skip") == 0;
        const bool excluded = skip && reason != NULL && strcmp(reason, "pull-request") == 0;
        const char* purpose = getenv("PURPOSE");
        const char* nightly = purpose != NULL && (strcmp(purpose, "backlog") == 0 || strcmp(purpose, "snapshot") == 0) ? "nightly" : purpose;
        Qualification input = {
            .event = (Event)qualification_find(getenv("EVENT_NAME"), events, sizeof(events) / sizeof(events[0])),
            .code = (Code)qualification_find(getenv("CODE_CHANGED"), codes, sizeof(codes) / sizeof(codes[0])),
            .purpose = (Purpose)qualification_find(nightly != NULL ? nightly : "", purposes, sizeof(purposes) / sizeof(purposes[0])),
            .hardware = admit ? HARDWARE_ADMIT : excluded ? HARDWARE_EXCLUDED : skip ? HARDWARE_SKIP : HARDWARE_INVALID,
            .changes = qualification_result("CHANGES_RESULT"),
            .size = qualification_result("SIZE_RESULT"),
            .hooks = qualification_result("DCO_RESULT"),
            .evidence = qualification_result("EVIDENCE_RESULT"),
            .tests = qualification_result("TEST_RESULT"),
            .fuzz = qualification_result("FUZZ_RESULT"),
            .native = qualification_result("BENCHMARK_RESULT"),
        };
        const bool passed = qualification_pass(&input);
        status = passed ? 0 : 1;
        const char* message = "Qualification failed: required results or selector outputs are missing, skipped, cancelled, failed or inconsistent.";
        if (passed && qualification_no_code(&input))
        {
            message = input.event == EVENT_PR ? "Documentation-only change: explicit no-code exemption. Native correctness was not run: pull requests never run on the 9700X (#1272)." : "Documentation-only change: explicit no-code exemption, not native qualification.";
        }
        else if (passed && qualification_hosted_only(&input))
        {
            message = "Hosted correctness passed. Native correctness was NOT run: pull requests never run on the 9700X (#1272); it runs after the merge, on main's push and the scheduled runs.";
        }
        else if (passed)
        {
            message = "Hosted and native correctness and required measurements passed.";
        }
        else if (input.event == EVENT_PR && input.native != RESULT_SKIPPED)
        {
            message = "Qualification failed: a pull request's run reached the 9700X job, which pull requests must never do (#1272).";
        }
        else if (input.event == EVENT_INVALID)
        {
            message = "Qualification failed: this event is no supported qualification path (a merge queue's included); pull requests qualify by their own hosted runs.";
        }
        if (passed)
        {
            puts(message);
        }
        else
        {
            fprintf(stderr, "::error::%s\n", message);
        }
        const char* summary_path = getenv("GITHUB_STEP_SUMMARY");
        FILE* summary = summary_path != NULL && summary_path[0] != 0 ? fopen(summary_path, "a") : NULL;
        if (summary != NULL)
        {
            fprintf(summary, "### Qualification\n\n%s\n", message);
            fclose(summary);
        }
    }
    else
    {
        fputs("usage: qualification [--self-test]\n", stderr);
    }
    return status;
}
