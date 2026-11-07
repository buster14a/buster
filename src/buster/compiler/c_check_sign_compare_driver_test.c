// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1391 source regressions. Include after the shared driver diagnostic-count and line-filter helpers.

static void check_test_driver_sign_compare(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
        uint32_t warnings;
        const char* option;
    } cases[] =
    {
        { "reported unsigned char against unsigned long", "typedef unsigned char Byte;int f(Byte b,unsigned long m){return b<=m;}\n", 0, NULL },
        { "reported unsigned short against unsigned long", "int f(unsigned short b,unsigned long m){return b<m;}\n", 0, NULL },
        { "unsigned char all operators against unsigned int", "int f(unsigned char b,unsigned m){return (b<m)+(b<=m)+(b>m)+(b>=m)+(b==m)+(b!=m);}\n", 0, NULL },
        { "unsigned int all operators against unsigned char", "int f(unsigned m,unsigned char b){return (m<b)+(m<=b)+(m>b)+(m>=b)+(m==b)+(m!=b);}\n", 0, NULL },
        { "unsigned short all operators against unsigned long", "int f(unsigned short b,unsigned long m){return (b<m)+(b<=m)+(b>m)+(b>=m)+(b==m)+(b!=m);}\n", 0, NULL },
        { "unsigned long all operators against unsigned short", "int f(unsigned long m,unsigned short b){return (m<b)+(m<=b)+(m>b)+(m>=b)+(m==b)+(m!=b);}\n", 0, NULL },
        { "qualified typedef keeps unsigned range", "typedef unsigned char Byte;int f(const volatile Byte b,unsigned long m){return b<m;}\n", 0, NULL },
        { "unsigned short member keeps unsigned range", "struct S{unsigned short n;};int f(const struct S*p,unsigned long m){return p->n<m;}\n", 0, NULL },
        { "unsigned char subscript keeps unsigned range", "int f(const unsigned char*p,unsigned m){return p[0]<m;}\n", 0, NULL },
        { "unsigned char call result keeps unsigned range", "unsigned char narrow(void);int f(unsigned long m){return narrow()<m;}\n", 0, NULL },
        { "unsigned plain char keeps unsigned range", "int f(char b,unsigned long m){return b<m;}\n", 0, "-funsigned-char" },
        { "unsigned narrowing cast keeps unsigned range", "int f(int b,unsigned long m){return (unsigned char)b<m;}\n", 0, NULL },
        { "unsigned short narrowing cast keeps unsigned range", "int f(int b,unsigned m){return (unsigned short)b<m;}\n", 0, NULL },
        { "boolean keeps its nonnegative type", "int f(_Bool b,unsigned long m){return b<m;}\n", 0, NULL },
        { "signed char can be negative", "int f(signed char b,unsigned long m){return b<m;}\n", 1, NULL },
        { "signed short can be negative", "int f(short b,unsigned m){return b<m;}\n", 1, NULL },
        { "reported signed int control", "int f(int b,unsigned long m){return b<m;}\n", 1, NULL },
        { "signed long can be negative", "int f(long b,unsigned long m){return b<m;}\n", 1, NULL },
        { "unsigned long against signed int still warns", "int f(unsigned long m,int b){return m<b;}\n", 1, NULL },
        { "signed int all operators still warn", "int f(int b,unsigned m){return (b<m)+(b<=m)+(b>m)+(b>=m)+(b==m)+(b!=m);}\n", 6, NULL },
        { "explicit signed widening cast keeps signed range", "int f(unsigned char b,unsigned long m){return (int)b<m;}\n", 1, NULL },
        { "explicit signed narrowing cast keeps signed range", "int f(unsigned char b,unsigned long m){return (signed char)b<m;}\n", 1, NULL },
        { "unsigned cast beneath signed cast is not enough", "int f(int b,unsigned long m){return (int)(unsigned char)b<m;}\n", 1, NULL },
        { "negated narrow unsigned can be negative", "int f(unsigned char b,unsigned long m){return -b<m;}\n", 1, NULL },
        { "complemented narrow unsigned can be negative", "int f(unsigned short b,unsigned m){return ~b<m;}\n", 1, NULL },
        { "subtraction from narrow unsigned can be negative", "int f(unsigned short b,unsigned m){return b-1<m;}\n", 1, NULL },
        { "nonnegative constant remains quiet", "int f(unsigned long m){return 3<m;}\n", 0, NULL },
        { "negative constant still warns", "int f(unsigned long m){return -1<m;}\n", 1, NULL },
        { "wider signed type represents unsigned int", "int f(unsigned b,long m){return b<m;}\n", 0, NULL },
        { "wide signed and unsigned remain different", "int f(unsigned long b,long m){return b<m;}\n", 1, NULL },
        { "both narrow operands promote to signed int", "int f(signed char b,unsigned short m){return b<m;}\n", 0, NULL },
    };
    static const char* const policies[] = { NULL, "-Werror=sign-compare", "-Wno-sign-compare", "-w" };
    char source[PATH_MAX];
    char object[PATH_MAX];
    snprintf(source, sizeof(source), "%s/sign-compare.c", directory);
    snprintf(object, sizeof(object), "%s/sign-compare.o", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        for (uint32_t policy = 0; policy < BUSTER_ARRAY_LENGTH(policies); policy += 1)
        {
            if (!check_test_driver_takes())
            {
                continue;
            }
            // Two independent bodies and -c permit actual deferred admission, unlike one-file -fsyntax-only.
            char unit[4096];
            const int length = snprintf(unit, sizeof(unit), "%sint sign_compare_anchor(void){return 0;}\n", cases[c].source);
            const bool written = length >= 0 && (size_t)length < sizeof(unit) && parse_test_write(source, unit, (size_t)length);
            char description[256];
            snprintf(description, sizeof(description), "sign compare %s policy=%u fixture is written", cases[c].name, policy);
            check_test_expect(test, written, description);
            const uint32_t errors = policy == 1 ? cases[c].warnings : 0;
            const uint32_t warnings = policy == 0 ? cases[c].warnings : 0;
            const int expected_status = errors != 0 ? 1 : 0;
            char reference[4096] = { 0 };
            bool reference_fits = false;
            for (uint32_t route = 0; written && route < 4; route += 1)
            {
                char* arguments[24] = { (char*)(route == 0 ? clang : executable != NULL ? executable : "/proc/self/exe"),
                                        "-std=gnu17", "-c", "-fno-caret-diagnostics", "-fno-color-diagnostics",
                                        "-Wall", "-Wextra", "-Wsign-compare", source, "-o", object };
                uint32_t used = 11;
                if (cases[c].option != NULL)
                {
                    arguments[used++] = (char*)cases[c].option;
                }
                if (policies[policy] != NULL)
                {
                    arguments[used++] = (char*)policies[policy];
                }
                if (route == 2)
                {
                    arguments[used++] = "--scalar";
                }
                else if (route == 3)
                {
                    arguments[used++] = "--deferred-bodies";
                    arguments[used++] = "--lanes";
                    arguments[used++] = "2";
                    arguments[used++] = "--worker-budget";
                    arguments[used++] = "2";
                    arguments[used++] = "--time-stages";
                }
                arguments[used] = NULL;
                unlink(object);
                char* text = NULL;
                const int status = check_test_driver_spawn(arguments, &text);
                char lines[4096] = { 0 };
                const bool fit = check_test_driver_common_output(text != NULL ? text : "", lines, sizeof(lines));
                const CheckDriverDiagnosticCounts counts = check_test_driver_diagnostic_counts(text);
                if (route == 0)
                {
                    reference_fits = fit;
                    memcpy(reference, lines, strlen(lines) + 1);
                }
                const bool same = route == 0 || (reference_fits && strcmp(reference, lines) == 0);
                const bool deferred = route != 3 || (text != NULL && strstr(text, "time-check\tdeferred-lanes\t2\n") != NULL &&
                                                     strstr(text, "time-check\tdeferred-merged\tyes\n") != NULL);
                const bool good = fit && text != NULL && status == expected_status && counts.warnings == warnings &&
                                  counts.errors == errors && counts.fatal == 0 && counts.notes == 0 && same && deferred &&
                                  (access(object, F_OK) == 0) == (status == 0);
                snprintf(description, sizeof(description), "sign compare %s policy=%u route=%u: independent counts, status, admission and Clang lines",
                         cases[c].name, policy, route);
                check_test_expect(test, good, description);
                if (!good)
                {
                    fprintf(stderr, "  status %d expected %d; warnings %u expected %u; errors %u expected %u; notes %u; deferred %u\nClang lines:\n%s\nActual output:\n%s\n",
                            status, expected_status, counts.warnings, warnings, counts.errors, errors, counts.notes, deferred,
                            reference, text != NULL ? text : "(missing)");
                }
                free(text);
            }
        }
    }
    unlink(object);
    unlink(source);
}
