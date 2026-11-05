// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A function body's redeclarations with attributes (#147): those clang drops after the entity's definition, with its
// warning, and the mutual exclusions (cold and hot, noinline and always_inline) across redeclarations, each checked
// against the declarations clang's lookup finds, a file scope's and an enclosing block's but not a closed block's
// (check_input_declaration_visible), as clang's mergeDeclAttributes does. A body read on a lane can't read nor publish
// those, so it is source order's (check_input_attribute_late). On original fixtures, the whole output, line for line,
// by both paths of -fsyntax-only and by -c with the bodies deferred on 1, 2 and 4 lanes, whose source order's reason
// --time-stages names. Include after c_check_label_attribute_driver_test.c, whose reader this takes.

static void check_test_driver_lane_attributes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
#define BUSTER_LANE_PADDING "int a(int x) { return x + 1; }\nint b(int x) { return x + 2; }\n"
#define BUSTER_LANE_TAIL "int c(int x) { return x + 3; }\nint d(int x) { return x + 4; }\n"
    static const struct
    {
        const char* source;
        // Whether a body's redeclaration sends the unit to source order (otherwise another reason, or none, does).
        bool replayed;
    } fixtures[] = {
        { "void g(void) {}\n" BUSTER_LANE_PADDING "void f(void) { void g(void) __attribute__((cold)); g(); }\n" BUSTER_LANE_TAIL, true },
        { "void h(void) __attribute__((hot));\n" BUSTER_LANE_PADDING "void k(void) { void h(void) __attribute__((cold)); h(); }\n" BUSTER_LANE_TAIL, true },
        { "void h(void) __attribute__((noinline));\n" BUSTER_LANE_PADDING "void k(void) { void h(void) __attribute__((always_inline)); h(); }\n" BUSTER_LANE_TAIL,
          true },
        { "void h(void) {}\n" BUSTER_LANE_PADDING "void k(void) { void h(void) __attribute__((cold)); void h(void) __attribute__((cold)); }\n", true },
        { "int v = 1;\n" BUSTER_LANE_PADDING "int f(void) { extern int v __attribute__((aligned(16))); return v; }\n", true },
        { "void h(void) __attribute__((hot));\n" BUSTER_LANE_PADDING "void k(void) { void h(void) __attribute__((hot)); h(); }\n", true },
        // A block's attributes reach neither the definition nor a later file-scope declaration, nor another body's or a
        // closed block's, but an enclosing block's do; with no visible declaration, an earlier block's extern is found.
        { "void g(void);\n" BUSTER_LANE_PADDING "void f(void) { void g(void) __attribute__((cold)); g(); }\n" BUSTER_LANE_TAIL
          "void g(void) {}\nvoid g(void) __attribute__((cold));\n", true },
        { "void h(void);\n" BUSTER_LANE_PADDING "void k(void) { void h(void) __attribute__((hot)); h(); }\n" BUSTER_LANE_TAIL
          "void h(void) __attribute__((cold));\n", true },
        { "void h(void);\nvoid k(void) { void h(void) __attribute__((hot)); }\n" BUSTER_LANE_PADDING "void m(void) { void h(void) __attribute__((cold)); }\n", true },
        { "void h(void);\n" BUSTER_LANE_PADDING "void k(void) { { void h(void) __attribute__((hot)); } void h(void) __attribute__((cold)); }\n", true },
        { "void h(void);\n" BUSTER_LANE_PADDING "void k(void) { void h(void) __attribute__((hot)); { void h(void) __attribute__((cold)); } }\n", true },
        { "void h(void) __attribute__((hot));\n" BUSTER_LANE_PADDING "void k(void) { void h(void); { void h(void) __attribute__((cold)); } }\n", true },
        { "void k(void) { extern void h(void) __attribute__((hot)); }\nvoid m(void) { void h(void) __attribute__((cold)); }\n" BUSTER_LANE_PADDING, false },
        // A block's first declaration, which a later file-scope one links to: the definition inherits its attributes (#1446's
        // review), and they exclude a later one's, in another body too.
        { "void f(void) { extern void g(void) __attribute__((cold)); }\n" BUSTER_LANE_PADDING "void g(void) {}\nvoid g(void) __attribute__((cold));\n", false },
        { "void f(void) { extern void g(void) __attribute__((cold)); }\n" BUSTER_LANE_PADDING "void g(void) {}\nvoid g(void) __attribute__((used));\n", false },
        { "void k(void) { extern void h(void) __attribute__((hot)); }\nvoid h(void);\n" BUSTER_LANE_PADDING
          "void m(void) { void h(void) __attribute__((cold)); }\n", false },
        { "void k(void) { extern void h(void) __attribute__((hot)); }\n" BUSTER_LANE_PADDING "void h(void) __attribute__((cold));\n", false },
        // The same, after an earlier late attribute made the masks, which then take the block's first declaration as it
        // binds (#1446's second review).
        { "void f(void) {}\nvoid f(void) __attribute__((cold));\nvoid k(void) { extern void g(void) __attribute__((hot)); }\n" BUSTER_LANE_PADDING
          "void g(void) {}\nvoid g(void) __attribute__((hot));\n", false },
        // A redeclaration without such attributes stays on its lane.
        { "void h(void) __attribute__((hot));\n" BUSTER_LANE_PADDING "void k(void) { void h(void); h(); }\n" BUSTER_LANE_TAIL, false },
    };
#undef BUSTER_LANE_PADDING
#undef BUSTER_LANE_TAIL
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/lane-attributes.o", directory);
    for (uint32_t f = 0; expected_lines != NULL && actual_lines != NULL && f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        snprintf(source, sizeof(source), "%s/lane-attributes-%u.c", directory, f);
        const bool made = parse_test_write(source, fixtures[f].source, strlen(fixtures[f].source));
        char* oracle_arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", source, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(oracle_arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        check_test_expect(test, made && lines, "lane attributes: fixture written and clang's lines read");
        // Both paths of -fsyntax-only, then -c with the bodies deferred on 1, 2 and 4 lanes.
        static const char* const lanes[] = { "1", "2", "4" };
        for (uint32_t run = 0; lines && run < 2 + BUSTER_ARRAY_LENGTH(lanes); run += 1)
        {
            char* syntax[] = { (char*)executable, "-std=gnu17", "-fsyntax-only", source, run == 1 ? "--scalar" : NULL, NULL };
            char* compile[] = { (char*)executable, "-std=gnu17", "-c", "-o", object, "--deferred-bodies", "--time-stages", "--lanes",
                                (char*)(run >= 2 ? lanes[run - 2] : "1"), source, NULL };
            char* actual = NULL;
            const int status = check_test_driver_spawn(run < 2 ? syntax : compile, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            // On more than one lane, the source order's reason, where a body's redeclaration decides it.
            const bool reason = run < 3 || !fixtures[f].replayed ||
                                (actual != NULL && strstr(actual, "\ntime-check\tdeferred-merged\ta body's redeclaration has attributes its entity's other "
                                                                  "declarations decide\n") != NULL);
            char description[4096];
            snprintf(description, sizeof(description), "lane attributes %u, %s%s: status %d (clang %d), lines\n%s(clang\n%s)", f,
                     run == 0 ? "vector" : run == 1 ? "scalar" : "-c on lanes ", run >= 2 ? lanes[run - 2] : "", status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && reason && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
        unlink(source);
        unlink(object);
    }
    free(expected_lines);
    free(actual_lines);
}
