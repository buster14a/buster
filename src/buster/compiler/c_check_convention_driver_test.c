// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The calling conventions other than System V's (#137, #147) by the driver against clang, on original fixtures. clang
// 23 compiles ms_abi, vectorcall, regcall and preserve_none on x86-64 Linux with argument registers of their own, and
// preserve_most and preserve_all with System V's arguments but more registers kept by the callee; the backend compiles
// System V's alone. So -c must refuse, by both paths and naming the convention, a definition of each of the six and a
// call of each of the first four, directly and through a pointer, where clang compiles every one of them; and a call of
// a preserve_most or preserve_all function, which keeps whatever a System V caller expects, must compile and run: the
// callees built by clang, the callers by each path, values live across the calls, as clang's build of both runs.
// Include after c_check_common_driver_test.c.

static void check_test_driver_conventions(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const conventions[] = { "ms_abi", "vectorcall", "regcall", "preserve_none", "preserve_most", "preserve_all" };
    char path[512];
    char object[512];
    snprintf(path, sizeof(path), "%s/convention.c", directory);
    snprintf(object, sizeof(object), "%s/convention.o", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(conventions); c += 1)
    {
        // The definition, a direct call and a call through a pointer, each a unit of its own.
        const bool callable = c >= 4;
        char sources[3][512];
        snprintf(sources[0], sizeof(sources[0]), "int __attribute__((%s)) f(int x, long y) { return x - (int)y; }\n", conventions[c]);
        snprintf(sources[1], sizeof(sources[1]), "int __attribute__((%s)) f(int x, long y);\nint g(int n) { return f(n, 2) + n; }\n", conventions[c]);
        snprintf(sources[2], sizeof(sources[2]), "int g(int (__attribute__((%s)) *p)(int, long), int n) { return p(n, 3) * 2; }\n", conventions[c]);
        static const char* const shapes[] = { "a definition", "a direct call", "a call through a pointer" };
        for (uint32_t s = 0; s < 3; s += 1)
        {
            const bool written = parse_test_write(path, sources[s], strlen(sources[s]));
            char* text = NULL;
            char* reference[] = { (char*)clang, "-std=gnu17", "-O0", "-c", path, "-o", object, NULL };
            const int clang_status = written ? check_test_driver_spawn(reference, &text) : -1;
            free(text);
            char description[512];
            snprintf(description, sizeof(description), "conventions: clang compiles %s of %s", shapes[s], conventions[c]);
            check_test_expect(test, clang_status == 0, description);
            const bool refused = s == 0 || !callable;
            char expected[128];
            snprintf(expected, sizeof(expected), "unsupported: functions of the %s calling convention", conventions[c]);
            for (uint32_t p = 0; written && p < 2; p += 1)
            {
                char* arguments[] = { (char*)executable, "-std=gnu17", "-c", path, "-o", object, p == 1 ? "--scalar" : NULL, NULL };
                const int status = check_test_driver_spawn(arguments, &text);
                const bool named = text != NULL && strstr(text, expected) != NULL;
                snprintf(description, sizeof(description), "conventions: %s of %s by the %s path %s (status %d: %s)", shapes[s], conventions[c],
                         p == 1 ? "scalar" : "vector", refused ? "is refused by name" : "compiles", status, text != NULL ? text : "");
                check_test_expect(test, refused ? status == 1 && named : status == 0 && !named, description);
                free(text);
            }
        }
    }
    // preserve_most's and preserve_all's callees by clang, called by each path's code with values live across the calls.
    static const char callees[] =
        "int printf(const char*, ...);\n"
        "static long mix(long a, long b) { return a * 31 + b; }\n"
        "long __attribute__((preserve_most)) most(long a, int b, double c) { printf(\"most %ld %d %.1f\\n\", a, b, c); return mix(a, b) + (long)c; }\n"
        "double __attribute__((preserve_all)) all(double a, long b) { printf(\"all %.1f %ld\\n\", a, b); return a * 2 + (double)mix(b, 3); }\n";
    static const char callers[] =
        "#include <stdio.h>\n"
        "long __attribute__((preserve_most)) most(long a, int b, double c);\n"
        "double __attribute__((preserve_all)) all(double a, long b);\n"
        "int main(void) {\n"
        "    long (__attribute__((preserve_most)) *p)(long, int, double) = most;\n"
        "    long total = 0; double sum = 0;\n"
        "    for (int i = 1; i < 6; i++) { long kept = i * 7; double held = i * 0.5; total += most(kept, i, held) + p(i, -i, 1.5) + kept;\n"
        "        sum += all(held, kept) + held; }\n"
        "    printf(\"%ld %.1f\\n\", total, sum); return 0; }\n";
    char callee_path[512];
    char caller_path[512];
    char callee_object[512];
    char reference[512];
    snprintf(callee_path, sizeof(callee_path), "%s/convention-callees.c", directory);
    snprintf(caller_path, sizeof(caller_path), "%s/convention-callers.c", directory);
    snprintf(callee_object, sizeof(callee_object), "%s/convention-callees.o", directory);
    snprintf(reference, sizeof(reference), "%s/convention-clang", directory);
    const bool written = parse_test_write(callee_path, callees, sizeof(callees) - 1) && parse_test_write(caller_path, callers, sizeof(callers) - 1);
    char* text = NULL;
    char* compile[] = { (char*)clang, "-std=gnu17", "-O0", "-c", callee_path, "-o", callee_object, NULL };
    const bool callee_built = written && check_test_driver_spawn(compile, &text) == 0;
    free(text);
    char* build[] = { (char*)clang, "-std=gnu17", "-O0", "-w", caller_path, callee_object, "-o", reference, NULL };
    const bool built = callee_built && check_test_driver_spawn(build, &text) == 0;
    free(text);
    char expected[1024] = { 0 };
    check_test_expect(test, built && check_test_driver_capture(reference, expected, sizeof(expected)), "conventions: clang's build of the preserved callers runs");
    for (uint32_t p = 0; built && p < 2; p += 1)
    {
        char caller_object[512];
        char linked[512];
        snprintf(caller_object, sizeof(caller_object), "%s/convention-callers-%u.o", directory, p);
        snprintf(linked, sizeof(linked), "%s/convention-ours-%u", directory, p);
        char* arguments[] = { (char*)executable, "-std=gnu17", "-w", "-c", caller_path, "-o", caller_object, p == 1 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        char* link[] = { (char*)clang, caller_object, callee_object, "-o", linked, NULL };
        const bool linked_ok = compiled && check_test_driver_spawn(link, &text) == 0;
        free(text);
        char ran[1024] = { 0 };
        char description[2400];
        const bool running = linked_ok && check_test_driver_capture(linked, ran, sizeof(ran));
        snprintf(description, sizeof(description), "conventions: the %s path's calls of clang's preserve_most and preserve_all callees run as clang's build: %s against %s",
                 p == 1 ? "scalar" : "vector", running ? ran : "(nothing)", expected);
        check_test_expect(test, running && strcmp(ran, expected) == 0, description);
        unlink(caller_object);
        unlink(linked);
    }
    unlink(callee_path);
    unlink(caller_path);
    unlink(callee_object);
    unlink(reference);
    unlink(path);
    unlink(object);
}
