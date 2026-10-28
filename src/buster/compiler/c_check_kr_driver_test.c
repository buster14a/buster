// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original CLI/source/system-policy controls. Clang independently supplies counts, status and the emission barrier.

static void check_test_driver_kr_policy(CheckTest* test, const char* clang, const char* executable, const char* directory, bool lazy)
{
    static const char definition[] = "int f(a)int a;{return a;}\n";
    static const char arity[] = "#pragma clang diagnostic ignored \"-Wdeprecated-non-prototype\"\n"
                                "int f(a)int a;{return a;}int g(void){return f(1,2);}\n";
    static const struct
    {
        const char* source;
        const char* first;
        const char* second;
        bool system;
        uint32_t warnings;
        uint32_t errors;
    } cases[] = {
        { definition, NULL, NULL, false, 1, 0 },
        { definition, "-Wno-deprecated-non-prototype", NULL, false, 0, 0 },
        { definition, "-Werror", NULL, false, 0, 1 },
        { definition, "-Werror=deprecated-non-prototype", NULL, false, 0, 1 },
        { definition, "-Werror", "-Wno-error=deprecated-non-prototype", false, 1, 0 },
        { definition, "-Wno-error=deprecated-non-prototype", "-Werror", false, 1, 0 },
        { definition, "-Werror=deprecated-non-prototype", "-Wno-error", false, 0, 1 },
        { definition, "-w", "-Werror=deprecated-non-prototype", false, 0, 0 },
        { definition, "-pedantic-errors", NULL, false, 1, 0 },
        { "#pragma clang diagnostic warning \"-Wdeprecated-non-prototype\"\nint f(a)int a;{return a;}\n", "-Werror", NULL, false, 1, 0 },
        { "#pragma clang diagnostic error \"-Wdeprecated-non-prototype\"\nint f(a)int a;{return a;}\n", "-Wno-deprecated-non-prototype", NULL, false, 0, 1 },
        { definition, NULL, NULL, true, 0, 0 },
        { definition, "-Wsystem-headers", NULL, true, 1, 0 },
        { definition, "-Wsystem-headers", "-Wno-system-headers", true, 0, 0 },
        { definition, "-Wno-system-headers", "-Wsystem-headers", true, 1, 0 },
        { "#pragma clang diagnostic error \"-Wdeprecated-non-prototype\"\nint f(a)int a;{return a;}\n", "-Wsystem-headers", NULL, true, 0, 1 },
        { "int f();int g(void){return f(1);}\n", "-Werror", NULL, false, 0, 1 },
        { arity, "-Wno-deprecated-non-prototype", NULL, false, 1, 0 },
        { arity, "-Werror", NULL, false, 0, 1 },
        { "#pragma clang diagnostic ignored \"-Wdeprecated-non-prototype\"\nint f(a)int a;{return a;}int g(void){return sizeof((&f)(1,2));}\n", "-Werror", NULL, false, 0, 1 },
    };
    char source[512];
    char header[512];
    char objects[2][512];
    snprintf(source, sizeof(source), "%s/kr-policy.c", directory);
    snprintf(header, sizeof(header), "%s/kr-policy.h", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/kr-policy-scalar.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/kr-policy-vector.o", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const char* unit = cases[c].system ? "#include <kr-policy.h>\n" : cases[c].source;
        const bool ready = parse_test_write(source, unit, strlen(unit)) && parse_test_write(header, cases[c].source, strlen(cases[c].source));
        check_test_expect(test, ready, "K&R policy fixture is written");
        for (uint32_t mode = 0; ready && mode < 2; mode += 1)
        {
            char* expected = NULL;
            char* arguments[16] = { (char*)clang, "-std=gnu17", "-fno-caret-diagnostics", mode == 0 ? "-fsyntax-only" : "-c",
                                    source, "-o", objects[0], "-isystem", (char*)directory };
            uint32_t count = 9;
            if (lazy)
            {
                arguments[count++] = "-flazy-system-headers";
            }
            if (cases[c].first != NULL)
            {
                arguments[count++] = (char*)cases[c].first;
            }
            if (cases[c].second != NULL)
            {
                arguments[count++] = (char*)cases[c].second;
            }
            const int oracle = check_test_driver_spawn(arguments, &expected);
            check_test_expect(test, oracle == (cases[c].errors != 0) && expected != NULL &&
                              check_test_implicit_count(expected, ": warning:") == cases[c].warnings &&
                              check_test_implicit_count(expected, ": error:") == cases[c].errors,
                              "Clang establishes the independent K&R policy status and exact counts");
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                char* actual = NULL;
                arguments[0] = (char*)executable;
                arguments[6] = objects[scalar];
                arguments[count] = scalar == 0 ? "--scalar" : NULL;
                arguments[count + 1] = NULL;
                unlink(objects[scalar]);
                const int status = check_test_driver_spawn(arguments, &actual);
                char description[128];
                snprintf(description, sizeof(description), "K&R policy case %u %s %s matches Clang's status and exact counts", c,
                         scalar == 0 ? "scalar" : "vector", mode == 0 ? "syntax" : "object");
                check_test_expect(test, oracle >= 0 && status == oracle && actual != NULL &&
                                  check_test_implicit_count(actual, ": warning:") == cases[c].warnings &&
                                  check_test_implicit_count(actual, ": error:") == cases[c].errors, description);
                check_test_expect(test, mode == 0 || (access(objects[scalar], F_OK) == 0) == (status == 0),
                                  "K&R warning promotion blocks object publication and permitted warnings retain emission");
                free(actual);
            }
            free(expected);
            unlink(objects[0]);
            unlink(objects[1]);
        }
    }
    unlink(source);
    unlink(header);
}
