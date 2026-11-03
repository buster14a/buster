// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The required CI context's fail-closed decision. The workflow supplies results,
// not exit codes: an unexpected skipped job is not a successful qualification.
// --self-test exhausts the state space against an explicit list of green cases.
// This administrative predicate is outside compiler/measurement hot paths.

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

typedef struct Qualification Qualification;
struct Qualification
{
    Event event;
    Code code;
    bool draft;
    Result changes;
    Result size;
    Result hooks;
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

static bool qualification_no_code(const Qualification* input)
{
    bool selected = input->event == EVENT_PR || input->event == EVENT_PUSH;
    bool result = selected && input->changes == RESULT_SUCCESS && input->code == CODE_NO;
    result = result && input->size == RESULT_SUCCESS && input->hooks == RESULT_SUCCESS;
    result = result && input->tests == RESULT_SKIPPED && input->fuzz == RESULT_SKIPPED && input->native == RESULT_SKIPPED;
    return result;
}

static bool qualification_pass(const Qualification* input)
{
    const Event event = input->event;
    bool valid = event < EVENT_INVALID && (event == EVENT_PR || !input->draft);
    bool selected = (event == EVENT_PR || event == EVENT_PUSH) && input->changes == RESULT_SUCCESS && input->code == CODE_YES;
    bool periodic = (event == EVENT_SCHEDULE || event == EVENT_DISPATCH) && input->changes == RESULT_SKIPPED && input->code == CODE_UNSET;
    bool native = (selected || periodic) && input->size == RESULT_SUCCESS && input->hooks == RESULT_SUCCESS;
    native = native && input->tests == RESULT_SUCCESS && input->native == RESULT_SUCCESS;
    native = native && (input->fuzz == RESULT_SUCCESS || (event == EVENT_PR && input->fuzz == RESULT_SKIPPED));
    native = native && !(event == EVENT_PR && input->draft);
    return valid && (qualification_no_code(input) || native);
}

static bool qualification_same(const Qualification* left, const Qualification* right)
{
    bool result = left->event == right->event && left->code == right->code && left->draft == right->draft;
    result = result && left->changes == right->changes && left->size == right->size && left->hooks == right->hooks;
    result = result && left->tests == right->tests && left->fuzz == right->fuzz && left->native == right->native;
    return result;
}

static int qualification_test(void)
{
    // These are all allowed tuples, independently of the predicate's branches.
    // The only green draft is the explicit no-code exemption, not native coverage.
    const Qualification allowed[] = {
        { EVENT_PR, CODE_YES, false, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED, RESULT_SUCCESS },
        { EVENT_PR, CODE_YES, false, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS },
        { EVENT_PUSH, CODE_YES, false, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS },
        { EVENT_SCHEDULE, CODE_UNSET, false, RESULT_SKIPPED, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS },
        { EVENT_DISPATCH, CODE_UNSET, false, RESULT_SKIPPED, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS },
        { EVENT_PR, CODE_NO, false, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED, RESULT_SKIPPED, RESULT_SKIPPED },
        { EVENT_PR, CODE_NO, true, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED, RESULT_SKIPPED, RESULT_SKIPPED },
        { EVENT_PUSH, CODE_NO, false, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SUCCESS, RESULT_SKIPPED, RESULT_SKIPPED, RESULT_SKIPPED },
    };
    uint32_t checks = 0;
    uint32_t failures = 0;
    // Six job results, five values each (including missing/unknown).
    const uint32_t combinations = 15625;
    for (int event = 0; event < EVENT_COUNT; event += 1)
    {
        for (int code = 0; code < CODE_COUNT; code += 1)
        {
            for (int draft = 0; draft < 2; draft += 1)
            {
                for (uint32_t encoded = 0; encoded < combinations; encoded += 1)
                {
                    uint32_t remaining = encoded;
                    Result values[6];
                    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i += 1)
                    {
                        values[i] = (Result)(remaining % RESULT_COUNT);
                        remaining /= RESULT_COUNT;
                    }
                    Qualification input = {
                        .event = (Event)event,
                        .code = (Code)code,
                        .draft = draft != 0,
                        .changes = values[0],
                        .size = values[1],
                        .hooks = values[2],
                        .tests = values[3],
                        .fuzz = values[4],
                        .native = values[5],
                    };
                    bool expected = false;
                    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i += 1)
                    {
                        expected |= qualification_same(&input, &allowed[i]);
                    }
                    checks += 1;
                    if (qualification_pass(&input) != expected)
                    {
                        failures += 1;
                        if (failures <= 5)
                        {
                            fprintf(stderr, "qualification: unexpected result for event=%d code=%d draft=%d results=%u\n", event, code, draft, encoded);
                        }
                    }
                }
            }
        }
    }
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
        static const char* const events[] = { "pull_request", "push", "schedule", "workflow_dispatch" };
        static const char* const codes[] = { "true", "false", "" };
        static const char* const drafts[] = { "false", "true" };
        const int draft = qualification_find(getenv("PR_DRAFT"), drafts, sizeof(drafts) / sizeof(drafts[0]));
        Qualification input = {
            .event = (Event)qualification_find(getenv("EVENT_NAME"), events, sizeof(events) / sizeof(events[0])),
            .code = (Code)qualification_find(getenv("CODE_CHANGED"), codes, sizeof(codes) / sizeof(codes[0])),
            .draft = draft == 1,
            .changes = qualification_result("CHANGES_RESULT"),
            .size = qualification_result("SIZE_RESULT"),
            .hooks = qualification_result("DCO_RESULT"),
            .tests = qualification_result("TEST_RESULT"),
            .fuzz = qualification_result("FUZZ_RESULT"),
            .native = qualification_result("BENCHMARK_RESULT"),
        };
        bool passed = draft < 2 && qualification_pass(&input);
        status = passed ? 0 : 1;
        if (passed && qualification_no_code(&input))
        {
            puts("Documentation-only change; explicit no-code exemption, not native qualification.");
        }
        else if (passed)
        {
            puts("Hosted and native correctness and required measurements passed.");
        }
        else if (input.event == EVENT_PR && input.draft)
        {
            fputs("::error::Draft code change: hardware qualification withheld. Mark ready for review to qualify the current revision.\n", stderr);
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
