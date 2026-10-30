// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #804 regression fixtures. Clang is an external declaration, diagnostic and execution oracle.

static void check_test_driver_implicit_library(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    typedef struct CheckImplicitLibraryCase
    {
        const char* source;
        const char* standard;
        const char* first;
        const char* second;
    } CheckImplicitLibraryCase;
    static const CheckImplicitLibraryCase cases[] =
    {
        { "int f(void){return printf(\"hi\\n\");}", "-std=c89", NULL, NULL },
        { "int f(void){return strlen(\"abc\")-3;}", "-std=c89", NULL, NULL },
        { "int f(void){return abs(-1)-1;}", "-std=c89", NULL, NULL },
        { "int f(void){return malloc(4)==0;}", "-std=c89", NULL, NULL },
        { "int f(void){char b[4];return memset(b,0,4)==0;}", "-std=c89", NULL, NULL },
        { "int f(void){return exit(0),0;}", "-std=c89", NULL, NULL },
        { "int f(void){char b[4];return snprintf(b,4,\"x\")-1;}", "-ansi", NULL, NULL },
        { "int f(void){return toupper(97)-65;}", "-std=gnu89", NULL, NULL },
        { "int f(void){return abs(-1);}int g(void){return abs(-2);}", "-std=c89", NULL, NULL },
        { "int f(void){abs(-1);{int abs=2;return abs;}}", "-std=c89", NULL, NULL },
        { "int f(void){abs(-1);int abs=2;return abs;}", "-std=c89", NULL, NULL },
        { "int f(int (*abs)(int)){return abs(-1);}", "-std=c89", NULL, NULL },
        { "int abs(int);int f(void){return abs(-1);}", "-std=c89", NULL, NULL },
        { "int f(void){return abs(-1);}int abs(int);", "-std=c89", NULL, NULL },
        { "int f(void){return abs(-1);}long abs(long);", "-std=c89", NULL, NULL },
        { "int f(void){return abs(-1);}long abs(long);", "-std=c89", "-Werror=incompatible-library-redeclaration", NULL },
        { "int f(void){return abs(-1);}long abs(long);", "-std=c89", "-Wno-incompatible-library-redeclaration", NULL },
        { "int f(void){return abs(-1);}static int abs(int n){return n;}", "-std=c89", NULL, NULL },
        { "int f(void){return abs(1,2);}", "-std=c89", NULL, NULL },
        { "int f(void){return abs();}", "-std=c89", NULL, NULL },
        { "int f(void){return sizeof(vprintf(\"x\",(void*)0));}", "-std=c89", NULL, NULL },
        { "int f(void){return sizeof(fwrite(\"x\",1,1,(void*)0));}", "-std=c89", NULL, NULL },
        { "int f(void){return sizeof(strdup(\"x\"));}", "-std=c89", NULL, NULL },
        { "int f(void){return sizeof(strdup(\"x\"));}", "-std=gnu89", NULL, NULL },
        { "int f(void){return sizeof(strdup);}", "-std=gnu89", NULL, NULL },
        { "int f(void){return sizeof(strdup);}", "-std=c89", NULL, NULL },
        { "int f(void){return abs(-1);}", "-std=c99", NULL, NULL },
        { "int f(void){return abs(-1);}", "-std=c23", "-Wno-implicit-function-declaration", NULL },
        { "int f(void){return abs(-1);}", "-std=c89", "-Werror", NULL },
        { "int f(void){return abs(-1);}", "-std=c89", "-Werror", "-Wno-error=implicit-function-declaration" },
        { "int f(void){return abs(-1);}", "-std=c89", "-Wno-error=implicit-function-declaration", "-Werror" },
        { "int f(void){return abs(-1);}", "-std=c89", "-Wno-implicit-function-declaration", NULL },
        { "int f(void){return abs(-1);}", "-std=c89", "-Wno-implicit-function-declaration", "-Wimplicit-function-declaration" },
        { "int f(void){return abs(-1);}", "-std=c89", "-Werror=implicit-function-declaration", "-w" },
        { "int f(void){return sizeof(malloc(4));}", "-std=c89", "-fno-builtin", NULL },
        { "int f(void){return sizeof(malloc(4));}", "-std=c89", "-fno-builtin-malloc", NULL },
        { "int f(void){return sizeof(malloc(4));}", "-std=c89", "-fno-builtin-mallocx", NULL },
        { "int f(void){return sizeof(malloc(4));}", "-std=c89", "-fno-builtin", "-fbuiltin" },
        { "int f(void){return sizeof(malloc(4));}", "-std=c89", "-fbuiltin", "-fno-builtin" },
        { "int f(void){return sizeof(malloc(4));}", "-std=c89", "-ffreestanding", NULL },
        { "int f(void){return sizeof(malloc(4));}", "-std=c89", "-ffreestanding", "-fbuiltin" },
        { "int f(void){return sizeof(malloc(4));}", "-std=c89", "-ffreestanding", "-fhosted" },
        { "int f(void){return __builtin_popcount(7);}", "-std=c89", "-fno-builtin", NULL },
        { "#pragma clang diagnostic ignored \"-Wimplicit-function-declaration\"\nint f(void){return abs(-1);}", "-std=c89", NULL, NULL },
        { "#pragma clang diagnostic error \"-Wimplicit-function-declaration\"\nint f(void){return abs(-1);}", "-std=c89", NULL, NULL },
        { "#include <stdio.h>\nint f(void){char b[4];return snprintf(b,4,\"x\")-1;}", "-ansi", NULL, NULL },
    };
    static const struct
    {
        CheckImplicitLibraryCase input;
        bool success;
    } scope_cases[] =
    {
        { { "int f(void){return abs(-1);}typedef int abs;", "-std=c89", NULL, NULL }, false },
        { { "int f(void){return abs(-1);}typedef int abs;", "-std=gnu89", NULL, NULL }, false },
        { { "int f(void){return abs(-1);}enum E{abs=1};", "-std=c89", NULL, NULL }, false },
        { { "int f(void){return abs(-1);}enum E{abs=1};", "-std=gnu89", NULL, NULL }, false },
        { { "int f(void){{abs(-1);}return 0;}typedef int abs;", "-std=c89", NULL, NULL }, false },
        { { "int f(void){extern int abs(int);return abs(-1);}int g(void){abs(-2);{extern int abs(int);}return 0;}typedef int abs;", "-std=gnu89", NULL, NULL }, false },
        { { "int f(void){return abs(-1);}int g(int abs);typedef int abs;", "-std=c89", NULL, NULL }, false },
        { { "int f(void){return abs(-1);}int g(void){typedef int abs;abs n=2;return n;}", "-std=c89", NULL, NULL }, true },
        { { "int f(void){return abs(-1);}int g(void){{enum E{abs=2};}return abs(-2);}", "-std=gnu89", NULL, NULL }, true },
        { { "int f(void){abs(-1);{int abs=2;abs++;}return abs(-2);}", "-std=c89", NULL, NULL }, true },
        { { "int f(void){return abs(-1);}int (*p)(int)=abs;", "-std=c89", NULL, NULL }, true },
        { { "int f(void){return abs(-1);}typedef int abs;", "-std=c89", "-fno-builtin", NULL }, true },
        { { "int f(void){return abs(-1);}typedef int abs;", "-std=gnu89", "-ffreestanding", NULL }, true },
        { { "int f(void){typedef int abs;abs n=2;return n;}int g(void){return abs(-1);}", "-std=c89", NULL, NULL }, true },
        { { "int f(void){enum E{abs=2};return abs;}int g(void){return abs(-1);}", "-std=gnu89", NULL, NULL }, true },
    };
    char source[512];
    char objects[2][512];
    snprintf(source, sizeof(source), "%s/implicit-library.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/implicit-library-scalar.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/implicit-library-vector.o", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases) + BUSTER_ARRAY_LENGTH(scope_cases); c += 1)
    {
        const bool scope = c >= BUSTER_ARRAY_LENGTH(cases);
        const uint32_t scope_case = scope ? c - BUSTER_ARRAY_LENGTH(cases) : 0;
        const CheckImplicitLibraryCase* input = scope ? &scope_cases[scope_case].input : &cases[c];
        const bool written = parse_test_write(source, input->source, strlen(input->source));
        check_test_expect(test, written, "implicit library fixture is written");
        for (uint32_t mode = 0; written && mode < 2; mode += 1)
        {
            char* expected = NULL;
            char* arguments[16] = { (char*)clang, (char*)input->standard, "-fno-caret-diagnostics",
                                   mode == 0 ? "-fsyntax-only" : "-c", source, "-o", objects[0] };
            uint32_t count = 7;
            if (input->first != NULL)
            {
                arguments[count++] = (char*)input->first;
            }
            if (input->second != NULL)
            {
                arguments[count++] = (char*)input->second;
            }
            const int oracle = check_test_driver_spawn(arguments, &expected);
            if (scope)
            {
                check_test_expect(test, oracle >= 0 && (oracle == 0) == scope_cases[scope_case].success,
                                  "implicit library scope control has its expected Clang acceptance");
            }
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                arguments[0] = (char*)executable;
                arguments[6] = objects[scalar];
                arguments[count] = scalar == 0 ? "--scalar" : NULL;
                arguments[count + 1] = NULL;
                unlink(objects[scalar]);
                char* actual = NULL;
                const int status = check_test_driver_spawn(arguments, &actual);
                bool same = expected != NULL && actual != NULL && oracle >= 0 && status == oracle;
                static const char* const groups[] = { "-Wimplicit-function-declaration]", "-Wincompatible-library-redeclaration]", "-Wbuiltin-requires-header]" };
                for (uint32_t group = 0; same && group < BUSTER_ARRAY_LENGTH(groups); group += 1)
                {
                    same = check_test_implicit_count(expected, groups[group]) == check_test_implicit_count(actual, groups[group]);
                }
                char description[128];
                snprintf(description, sizeof(description), "implicit library case %u mode %u path %u matches Clang", c, mode, scalar);
                check_test_expect(test, same, description);
                if (!same)
                {
                    printf("  oracle status %d: %s\n  actual status %d: %s\n", oracle, expected != NULL ? expected : "", status, actual != NULL ? actual : "");
                }
                check_test_expect(test, mode == 0 || (access(objects[scalar], F_OK) == 0) == (status == 0),
                                  "implicit library diagnostics govern object publication");
                free(actual);
            }
            if (mode != 0 && oracle == 0)
            {
                check_test_expect(test, check_test_driver_same_files(objects[0], objects[1]), "implicit library objects match across scalar and vector paths");
            }
            free(expected);
            unlink(objects[0]);
            unlink(objects[1]);
        }
    }
    static const char* const native[] =
    {
        "int main(void){char *p=malloc(8);int r;if(!p)return 1;memset(p,0,8);r=snprintf(p,8,\"%d\",abs(-7));"
        "r=r!=1||strlen(p)!=1||toupper(97)!=65||p[0]!='7';free(p);return r;}",
        "int main(void){return printf(\"hi\\n\")!=3;}",
        "int main(void){exit(0);}",
        "int main(void){return sizeof(malloc(4));}",
        "int main(void){return sizeof(strlen(\"x\"));}",
        "int main(void){return sizeof(strdup(\"x\"));}",
    };
    static const char* const options[] = { NULL, "-fno-builtin", "-fno-builtin-malloc", "-fno-builtin-strlen", "-ffreestanding" };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(native); c += 1)
    {
        const bool written = parse_test_write(source, native[c], strlen(native[c]));
        const uint32_t variants = c >= 3 ? BUSTER_ARRAY_LENGTH(options) : 1;
        for (uint32_t option = 0; written && option < variants; option += 1)
        {
            int expected = -1;
            for (uint32_t route = 0; route < 3; route += 1)
            {
                char* arguments[12] = { (char*)(route == 0 ? clang : executable), "-std=gnu89", "-w", source, "-o", objects[0] };
                uint32_t count = 6;
                if (options[option] != NULL)
                {
                    arguments[count++] = (char*)options[option];
                }
                arguments[count] = route == 1 ? "--scalar" : NULL;
                char* diagnostics = NULL;
                const int built = check_test_driver_spawn(arguments, &diagnostics);
                check_test_expect(test, built == 0, "implicit library native fixture builds");
                if (built != 0)
                {
                    printf("  native case %u option %u route %u: %s\n", c, option, route, diagnostics != NULL ? diagnostics : "");
                }
                free(diagnostics);
                diagnostics = NULL;
                char* run[] = { objects[0], NULL };
                const int status = built == 0 ? check_test_driver_spawn(run, &diagnostics) : -1;
                if (route == 0)
                {
                    expected = status;
                }
                char description[128];
                snprintf(description, sizeof(description), "implicit library native case %u option %u route %u result %d equals Clang %d", c, option, route, status, expected);
                check_test_expect(test, status >= 0 && status == expected, description);
                free(diagnostics);
                unlink(objects[0]);
            }
        }
    }
    unlink(source);
}
