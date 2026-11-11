// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// An asm operand's constraint that clang's validateOutputConstraint or validateInputConstraint refuses (#1973,
// check_asm_clang_valid): clang's "invalid output|input constraint 'C' in asm" at the constraint's string, as written (its
// concatenated pieces), by the driver against clang on original fixtures, each unit's whole output and status by the
// vector path, the scalar one and the deferred bodies' lanes, under gnu17 and gnu23: letters x86 hasn't, an output's missing '=' or '+', an output's
// x87 'f' and 'p', an output that allows neither a register nor memory, an early clobber of a read-write memory output,
// an input's '&' and '=', an empty one, a tie out of range, to a read-write output or to a missing name, and the forms clang takes.

static void check_test_driver_asm_constraints(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const units[] = {
        "void g(void) { int a; __asm__(\"\" : \"=r\"(a) : \"z\"(1)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"=z\"(a)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"=r\"(a) : \"q\"(1), \"j\"(2)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"r\"(a)); }\n",
        "void g(void) { int a; __asm__(\"\" : : \"b\"(1), \"k\"(2), \"Y\"(3)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"+&m\"(a)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"=&m\"(a)); }\n",
        "void g(void) { long double a; __asm__(\"\" : \"=f\"(a)); }\n",
        "void g(void) { long double a; __asm__(\"\" : \"=rf\"(a)); }\n",
        "void g(void) { long double a; __asm__(\"\" : \"+t\"(a)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"=rp\"(a)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"=i\"(a)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"=re\"(a)); }\n",
        "void g(void) { int a, b; __asm__(\"\" : \"+r\"(a), \"=r\"(b) : \"0\"(1)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"=r\"(a) : \"1\"(1)); }\n",
        "void g(void) { int a; __asm__(\"\" : [o] \"=r\"(a) : \"[o]\"(1), \"[p]\"(2)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"=r,m\"(a) : \"r,=r\"(1)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"=r#x,m\"(a)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"r\" \"z\"(a)); }\n",
        "void g(void) { int a; __asm__(\"\" : : \"&r\"(a)); }\n",
        "void g(void) { int a; __asm__(\"\" : : \"Yz\"(1), \"Ws\"(&a)); }\n",
        "void g(void) { int a; __asm__(\"\" : : \"Yq\"(1)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"=Yk\"(a)); }\n",
        // An empty input, and Y with a second letter x86 hasn't (#2013's review).
        "void g(void) { __asm__(\"\" : : \"\"(1)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"=Y0\"(a) : \"Y0\"(1)); }\n",
        "void g(void) { int a; __asm__(\"\" : \"=r\"(a) : \"Y0\"(1)); }\n",
        // Two bodies, which --deferred-bodies reads on lanes, whose arenas keep their constraints' bytes.
        "void g(void) { int a; __asm__(\"\" : \"=r\"(a) : \"z\"(1)); }\nvoid h(void) { int a; __asm__(\"\" : \"r\"(a)); }\n",
    };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/asm-constraints.c", directory);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < 2 * BUSTER_ARRAY_LENGTH(units); unit += 1)
    {
        const char* text = units[unit / 2];
        const char* standard = unit % 2 == 0 ? "-std=gnu17" : "-std=gnu23";
        const bool made = parse_test_write(source, text, strlen(text));
        char* arguments[] = { (char*)clang, (char*)standard, "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "asm constraints %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        // The vector path, the scalar one, and the vector path with every body read apart on two lanes (#2013's review).
        for (uint32_t mode = 0; lines && mode < 3; mode += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = mode == 1 ? "--scalar" : mode == 2 ? "--deferred-bodies" : NULL;
            arguments[6] = mode == 2 ? "--lanes" : NULL;
            arguments[7] = mode == 2 ? "2" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "asm constraints %u (%s %s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, standard, text,
                     mode == 1 ? "scalar" : mode == 2 ? "deferred bodies on two lanes" : "vector", status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    free(expected_lines);
    free(actual_lines);
    unlink(source);
}
