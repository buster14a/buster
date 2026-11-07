// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The real-driver compatibility tests' private process interface. The target-feature oracle fixtures compile in
// their own translation unit so their strings do not enter the production checker's string pool. Helpers remain
// local to the including harness; callbacks share its counters and fixture directory without sharing static state.
#ifndef BUSTER_C_DRIVER_COMPATIBILITY_TEST_H
#define BUSTER_C_DRIVER_COMPATIBILITY_TEST_H

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

typedef struct CCCompatibilityTest CCCompatibilityTest;
struct CCCompatibilityTest
{
    char directory[PATH_MAX];
    char compiler[PATH_MAX];
    const char* clang;
    uint32_t checks;
    uint32_t failures;
};

typedef struct CCCompatibilityRun CCCompatibilityRun;
struct CCCompatibilityRun
{
    int status;
    char* output;
    char* errors;
};

typedef struct CCCompatibilityHelpers CCCompatibilityHelpers;
struct CCCompatibilityHelpers
{
    void (*expect)(CCCompatibilityTest* test, bool good, const char* name);
    bool (*write)(CCCompatibilityTest* test, const char* name, const char* text);
    CCCompatibilityRun (*run)(CCCompatibilityTest* test, const char* program, const char* input, const char* const* arguments);
    void (*release)(CCCompatibilityRun* run);
};

void cc_compat_target_feature_emission(CCCompatibilityTest* test, const CCCompatibilityHelpers* helpers);

#endif
