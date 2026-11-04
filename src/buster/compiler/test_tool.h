// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// An external tool a cross-check spawns, probed once (#1129): whether PATH finds it, the dotted version its --version
// prints, the minimum the cross-check needs and whether it may be used. A missing or older tool makes that section print
// "skipped: needs NAME >= V, found W" rather than a bare failure; a compatible tool's wrong output still fails.
#ifndef BUSTER_TEST_TOOL_INCLUDED
#define BUSTER_TEST_TOOL_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// A version's dotted components (major, minor, patch, and one more for a distribution's suffix) and the bytes a display
// or a diagnostic keeps.
#define BUSTER_TEST_TOOL_COMPONENTS 4
#define BUSTER_TEST_TOOL_TEXT_BYTES 64

typedef struct TestTool TestTool;
struct TestTool
{
    const char* name;
    const char* command;
    bool found;
    bool version_parsed;
    bool timed_out;
    bool failed;
    bool available;
    uint32_t version[BUSTER_TEST_TOOL_COMPONENTS];
    uint32_t version_count;
    uint32_t minimum[BUSTER_TEST_TOOL_COMPONENTS];
    uint32_t minimum_count;
    char version_text[BUSTER_TEST_TOOL_TEXT_BYTES];
    char unavailable[BUSTER_TEST_TOOL_TEXT_BYTES];
};

// Probes `command` (or `name` where command is 0) with --version, and compares it with `minimum`'s first
// `minimum_count` components (0 for no minimum). Never starts the tool again. A tool whose --version doesn't end within
// its deadline has its whole process group killed, and one whose --version exits nonzero is reported unavailable
// whether or not a minimum is required.
static TestTool test_tool_probe(const char* name, const char* command, const uint32_t* minimum, uint32_t minimum_count);

// Whether a probe found a tool whose --version succeeded and whose version meets its minimum, or any such tool without
// one.
static bool test_tool_available(const TestTool* tool);

// Prints the section's skip to `stream` (stdout for test_tool_skip): the tool, its minimum and what was found.
static void test_tool_skip_to(FILE* stream, const TestTool* tool, const char* section);
static void test_tool_skip(const TestTool* tool, const char* section);

// The helper's own checks: parsing, comparison, a fake tool's version, a missing tool, an unreadable version, a failing
// --version with and without a minimum, a hanging tool killed at its deadline, and the exact skip text. Prints a line
// and returns the failures. test-tool refuses any argument with a usage error rather than running these.
static uint32_t test_tool_test(void);

#endif
