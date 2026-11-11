// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

static void check_ownership_structure_fixtures(void)
{
    static const OwnershipUnits units = {
        .names = { "main.c", "main_unit_backend.c", "main_unit_frontend.c" },
        .count = 3,
    };
    static const char base[] = "static int shared(void) { return 1; }\n";
    const OwnershipStructure expected = ownership_structure(&units, 7, base);
    static const struct
    {
        const char* label;
        const char* source;
        bool equal;
        bool valid;
    } cases[] = {
        { "body edit", "static int shared(void) { return 2; }\n", true, true },
        { "whitespace and comments", "static /* shared */ int shared ( void ) { return 1; }\n", true, true },
        { "literal braces and directives", "static int shared(void) { const char* p = \"} #include x\"; return p[0]; }\n", true, true },
        { "commented directive", "/* #include \"fake.inc\" */\nstatic int shared(void) { return 1; }\n", true, true },
        { "new shared function", "static int shared(void) { return 1; }\nstatic int leak(void) { return 3; }\n", false, true },
        { "new shared prototype", "static int shared(void) { return 1; }\nstatic int leak(void);\n", false, true },
        { "new shared data", "static int shared(void) { return 1; }\nstatic const char data[] = \"private\";\n", false, true },
        { "new shared macro", "static int shared(void) { return 1; }\n#define BUSTER_PRIVATE_DATA 3\n", false, true },
        { "correct private function", "static int shared(void) { return 1; }\n#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)\nstatic int private(void) { return 2; }\n#endif\n", true, true },
        { "standalone fallback", "static int shared(void) { return 1; }\n#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_BACKEND\nstatic int private(void) { return 2; }\n#endif\n", true, true },
        { "private macro", "static int shared(void) { return 1; }\n#if BUSTER_UNIT == BUSTER_UNIT_BACKEND\n#define BUSTER_PRIVATE_DATA 3\n#endif\n", true, true },
        { "private elif", "static int shared(void) { return 1; }\n#if 0\n#elif BUSTER_UNIT == BUSTER_UNIT_BACKEND\nstatic int private(void) { return 2; }\n#endif\n", true, true },
        { "private conjunction", "static int shared(void) { return 1; }\n#if defined(BUSTER_UNIT) && (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && !BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND))\nstatic int private(void) { return 2; }\n#endif\n", true, true },
        { "widened private span", "static int shared(void) { return 1; }\n#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) || BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)\nstatic int leak(void) { return 3; }\n#endif\n", false, true },
        { "negated owner", "static int shared(void) { return 1; }\n#if BUSTER_UNIT != BUSTER_UNIT_BACKEND\nstatic int leak(void) { return 3; }\n#endif\n", false, true },
        { "unknown host predicate", "static int shared(void) { return 1; }\n#if BUSTER_HOST_VECTOR\nstatic int leak(void) { return 3; }\n#endif\n", false, true },
        { "unknown host else", "static int shared(void) { return 1; }\n#if BUSTER_HOST_VECTOR\n#else\nstatic int leak(void) { return 3; }\n#endif\n", false, true },
        { "arbitrary quoted include", "static int shared(void) { return 1; }\n#include \"data/arbitrary.blob\"\n", false, true },
        { "comment-spaced include", "static int shared(void) { return 1; }\n# /* before */ include/* after */\"data/arbitrary.blob\"\n", false, true },
        { "system include", "static int shared(void) { return 1; }\n#include <stdio.h>\n", false, true },
        { "macro include", "static int shared(void) { return 1; }\n#include BUSTER_PRIVATE_DATA\n", false, true },
        { "include inside helper", "static int shared(void) {\n#include \"data/arbitrary.blob\"\nreturn 1; }\n", false, true },
        { "unknown selector", "static int shared(void) { return 1; }\n#if BUSTER_IN_UNIT(BUSTER_UNIT_MISSING)\nstatic int leak(void) { return 3; }\n#endif\n", false, false },
        { "incomplete conditional", "static int shared(void) { return 1; }\n#if BUSTER_HOST_VECTOR\n", false, false },
        { "incomplete include", "static int shared(void) { return 1; }\n#include \"unterminated\n", false, false },
        { "incomplete body", "static int shared(void) { return 1;\n", false, false },
    };
    if (!expected.valid || expected.declarations != 1)
    {
        fail("invalid base shared-structure fixture");
    }
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
    {
        const OwnershipStructure actual = ownership_structure(&units, 7, cases[i].source);
        if (actual.valid != cases[i].valid || (actual.valid && (actual.hash == expected.hash) != cases[i].equal))
        {
            fail("shared-structure fixture '%s': valid %u, shared entries %u, fingerprint %016llx", cases[i].label,
                 actual.valid, actual.declarations, (unsigned long long)actual.hash);
        }
    }
    const OwnershipStructure aggregate = ownership_structure(&units, 7, "typedef struct Input { int a; } Input;\n");
    const OwnershipStructure grown = ownership_structure(&units, 7, "typedef struct Input { int a; int private_fixture; } Input;\n");
    if (!aggregate.valid || !grown.valid || aggregate.hash == grown.hash)
    {
        fail("shared aggregate member growth escaped the structural invariant");
    }
    pass("27 shared-structure cases preserve private islands/body edits and reject shared growth, widened spans and malformed roots");
}

static void check_ownership_include_fixtures(void)
{
    static const OwnershipUnits units = {
        .names = { "main.c", "main_unit_backend.c", "main_unit_frontend.c" },
        .count = 3,
    };
    static const OwnershipException shared[] = {
        { "src/buster/compiler/shared_test.c", "main_unit_backend.c main_unit_frontend.c", "shared helper negative fixture", 0 },
    };
    OwnershipGraph graph = {
        .files = {
            { .path = "src/buster/compiler/shared_test.c", .owners = 6, .fixture = true },
            { .path = "src/buster/compiler/nested/input.h", .owners = 6 },
            { .path = "src/buster/compiler/data/bytes.blob", .owners = 6 },
            { .path = "src/buster/compiler/production.c", .owners = 7 },
            { .path = "fixtures/raw.bin", .owners = 6 },
        },
        .count = 5,
    };
    bool valid = ownership_mark_includes(&graph, &graph.files[0],
        "// #include \"production.c\"\nconst char* p = \"#include production.c\";\n# /* before */ include/* after */\"nested/./input.h\"\n");
    valid = ownership_mark_includes(&graph, &graph.files[1], "#include \\\n \"../data/bytes.blob\"\n") && valid;
    valid = ownership_mark_includes(&graph, &graph.files[0], "#include \"../../../fixtures/raw.bin\"\n") && valid;
    if (!valid || !graph.files[1].fixture || !graph.files[2].fixture || graph.files[3].fixture || !graph.files[4].fixture)
    {
        fail("literal fixture closure missed an arbitrary nested/outside path or accepted a commented include");
    }
    OwnershipIssue issue = ownership_check(&units, &graph, shared, 1);
    if (issue.reason == 0 || strcmp(issue.reason, "private fixture has multiple unity owners") != 0 || strcmp(issue.path, "src/buster/compiler/nested/input.h") != 0)
    {
        fail("a shared root exempted its arbitrary header descendant");
    }
    graph.files[1].owners = 2;
    issue = ownership_check(&units, &graph, shared, 1);
    if (issue.reason == 0 || strcmp(issue.path, "src/buster/compiler/data/bytes.blob") != 0)
    {
        fail("a shared root exempted its arbitrary grandchild data");
    }
    graph.files[2].owners = 2;
    graph.files[4].owners = 2;
    issue = ownership_check(&units, &graph, shared, 1);
    if (issue.reason != 0 || ownership_mark_includes(&graph, &graph.files[0], "#include \"unterminated\n"))
    {
        fail("private descendants failed ownership or malformed include data passed");
    }
    graph.files[4].path = "shared_test.c";
    graph.files[4].owners = 6;
    issue = ownership_check(&units, &graph, shared, 1);
    if (issue.reason == 0 || strcmp(issue.path, "shared_test.c") != 0)
    {
        fail("a fixture outside the compiler directory inherited a same-basename exception");
    }
    char normalized[PATH_MAX];
    if (ownership_normalize("../outside", normalized) || ownership_normalize("/absolute", normalized))
    {
        fail("literal include normalization accepted a repository escape");
    }
    pass("literal include closure checks exact ownership of arbitrary headers, fragments, generated data and outside descendants");
}

static void check_ownership_exception_fixtures(void)
{
    static const OwnershipUnits units = {
        .names = { "main.c", "main_unit_backend.c", "main_unit_frontend.c" },
        .count = 3,
    };
    const OwnershipGraph graph = {
        .files = { { .path = "src/buster/compiler/shared_test.c", .owners = 6, .fixture = true } },
        .count = 1,
    };
    static const OwnershipException cases[] = {
        { "src/buster/compiler/shared_test.c", 0, "no automatic all-unit exception", 0 },
        { "src/buster/compiler/shared_test.c", "main_unit_backend.c main_unit_frontend.c", 0, 0 },
        { "src/buster/compiler/shared_test.c", "main_unit_backend.c main_unit_backend.c", "duplicate owner", 0 },
        { "src/buster/compiler/shared_test.c", "main_unit_backend.c main_unit_unknown.c", "unknown owner", 0 },
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
    {
        const OwnershipIssue issue = ownership_check(&units, &graph, &cases[i], 1);
        if (issue.reason == 0)
        {
            fail("invalid exact-sharing exception %u was accepted", i);
        }
    }
    pass("shared exceptions require a reason and explicit, unique, known owners");
}
