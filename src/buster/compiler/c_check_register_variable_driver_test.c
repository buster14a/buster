// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Global register variables and explicit register variables' labels (#1846, the owner's decision on #1447), against
// clang by the driver: Sema's errors (check_input_register_label: a file-scope `register` without a label, a label
// naming no register clang knows, a local's too, one unsuitable for a global, of another size than the variable, and a
// type that is neither an integer nor a pointer; the address of one; and a global's operand of a constraint that allows
// memory, check_asm_operand_error), each unit's whole output and status by the vector path, the scalar one and the
// deferred bodies' lanes, under gnu17 and gnu23; then a program that reads rsp, rbp, esp and ebp through them, writes
// rsp's back, and names them in templates, compiled by the direct path, --scalar and --tier1 with and without it, each
// printing what clang -O0's build prints.

static void check_test_driver_register_variables(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const units[] = {
        "register long sp; long f(void) { return sp; }\n",
        "register long gr __asm__(\"r12\");\n",
        "register int sp __asm__(\"rsp\");\n",
        "register double sp __asm__(\"rbx\");\n",
        "register double sp __asm__(\"foo\");\n",
        "register char *sp __asm__(\"esp\");\n",
        "register long sp __asm__(\"%rsp\");\n",
        "struct S { long a; };\nregister struct S sp __asm__(\"rsp\");\n",
        "register _Atomic long sp __asm__(\"rsp\");\n",
        "register long sp[1] __asm__(\"rsp\");\n",
        "register long sp __asm__(\"rsp\");\nlong *f(void) { return &sp; }\n",
        "void f(void) { register long q __asm__(\"foo\"); (void)q; }\n",
        "register long a __asm__(\"180\");\nregister long b __asm__(\"181\");\nregister long c __asm__(\"0x10\");\nregister long d __asm__(\"r16d\");\n",
        "register long a __asm__(\"st(1)\");\nregister long b __asm__(\"cr1\");\nregister long c __asm__(\"sil\");\nregister long d __asm__(\"dr7\");\n",
        "register double a __asm__(\"rbx\");\nregister double b __asm__(\"foo\");\nregister long c;\nregister long d;\nlong f(void) { return c + d; }\n",
        "void f(void) { register long a __asm__(\"dr7\"); register long b __asm__(\"%rsp\"); register long c __asm__(\"sil\"); (void)a; (void)b; (void)c; }\n",
        "register unsigned long sp __asm__(\"rsp\");\nvoid f(void) { __asm__(\"\" : \"=m\"(sp)); }\n",
        "register unsigned long sp __asm__(\"rsp\");\nvoid f(void) { __asm__(\"\" : : \"rm\"(sp)); }\n",
        "register unsigned long sp __asm__(\"rsp\");\nvoid f(void) { __asm__(\"\" : \"+rm\"(sp)); }\n",
        "register unsigned long sp __asm__(\"rsp\") = 4;\nregister int e __asm__(\"esp\");\nregister void *b __asm__(\"rbp\");\nregister unsigned f __asm__(\"ebp\");\n"
        "unsigned long g(void) { return sp + (unsigned long)e + (unsigned long)b + f; }\n",
    };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/register-variables.c", directory);
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
        snprintf(description, sizeof(description), "register variables %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t mode = 0; lines && mode < 3; mode += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = mode == 1 ? "--scalar" : mode == 2 ? "--deferred-bodies" : NULL;
            arguments[6] = mode == 2 ? "--lanes" : NULL;
            arguments[7] = mode == 2 ? "2" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "register variables %u (%s %s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, standard, text,
                     mode == 1 ? "scalar" : mode == 2 ? "deferred bodies on two lanes" : "vector", status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    free(expected_lines);
    free(actual_lines);
    unlink(source);
    // The program: rsp read near a local's address, written back with its own value (the assignment's value too), updated,
    // named
    // by templates as an input and as `+` and `=` outputs; rbp read as a frame pointer above a local and named by a
    // template; esp and ebp as rsp's and rbp's low halves.
    static const char program[] =
        "#include <stdio.h>\n"
        "register unsigned long sp __asm__(\"rsp\");\n"
        "register void *bp __asm__(\"rbp\");\n"
        "register unsigned int esp4 __asm__(\"esp\");\n"
        "register int ebp4 __asm__(\"ebp\");\n"
        "static int near(void) { char local; long d = (long)((unsigned long)&local - sp); return d > -4096 && d < 4096; }\n"
        "static int kept(void) { unsigned long before = sp; sp = sp; unsigned long r = (sp = before); return r == before && sp == before; }\n"
        "static int named(void) { unsigned long r; __asm__(\"mov %1, %0\" : \"=r\"(r) : \"r\"(sp)); __asm__ volatile(\"\" : \"+r\"(sp)); __asm__(\"\" : \"=r\"(sp)); return r == sp; }\n"
        "static int frame(void) { char local; void *r; __asm__(\"mov %1, %0\" : \"=r\"(r) : \"r\"(bp)); long d = (long)((char *)r - &local); return d >= 0 && d < 4096 && r == bp; }\n"
        "static int halves(void) { return esp4 == (unsigned int)sp && (unsigned int)ebp4 == (unsigned int)(unsigned long)bp; }\n"
        // A signed one's assignment of a local's value, and its updates (#2432): compound assignments, increments and
        // decrements of both, the expressions' values the new ones and a postfix one's the old one.
        "register long ssp __asm__(\"rsp\");\n"
        "static int assigned(void) { long a = ssp; ssp = a; long r = (ssp = a); return ssp == a && r == a; }\n"
        "static int updated(int d) { long a = ssp; ssp -= 64; long b = ssp; long s = (ssp += 64); long o = ssp--; long n = --ssp; ssp++; ++ssp; long c = ssp;\n"
        "    ssp &= ~0L; unsigned long u = sp; sp -= (unsigned)d * 16u; unsigned long v = sp; sp += (unsigned)d * 16u;\n"
        "    return a - b == 64 && s == a && o == a && a - n == 2 && c == a && u - v == (unsigned long)d * 16 && sp == u; }\n"
        "int main(void) { printf(\"%d %d %d %d %d %d %d\\n\", near(), kept(), named(), frame(), halves(), assigned(), updated(3)); return 0; }\n";
    char unit[PATH_MAX];
    char reference[PATH_MAX];
    char built[PATH_MAX];
    snprintf(unit, sizeof(unit), "%s/global-registers.c", directory);
    snprintf(reference, sizeof(reference), "%s/global-registers-clang", directory);
    snprintf(built, sizeof(built), "%s/global-registers", directory);
    const bool written = parse_test_write(unit, program, sizeof(program) - 1);
    char* reference_arguments[] = { (char*)clang, "-O0", "-w", "-o", reference, unit, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    char expected[256] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the global register variables' program runs");
    static const char* const option_names[] = { "the direct path", "--scalar", "--tier1", "--tier1 --scalar" };
    for (uint32_t path = 0; ran && path < 4; path += 1)
    {
        char* plain[] = { (char*)executable, "--verify", "-w", "-o", built, unit, NULL };
        char* scalar[] = { (char*)executable, "--verify", "--scalar", "-w", "-o", built, unit, NULL };
        char* tier1[] = { (char*)executable, "--verify", "--tier1", "-w", "-o", built, unit, NULL };
        char* tier1_scalar[] = { (char*)executable, "--verify", "--tier1", "--scalar", "-w", "-o", built, unit, NULL };
        char* const* arguments = path == 0 ? plain : path == 1 ? scalar : path == 2 ? tier1 : tier1_scalar;
        char* output = NULL;
        const bool compiled = check_test_driver_spawn((char**)arguments, &output) == 0;
        free(output);
        char actual[256] = { 0 };
        char description[256];
        snprintf(description, sizeof(description), "global register variables by %s print clang -O0's build's output", option_names[path]);
        check_test_expect(test, compiled && check_test_driver_capture(built, actual, sizeof(actual)) && strcmp(actual, expected) == 0, description);
        unlink(built);
    }
    unlink(reference);
    unlink(unit);
}
