// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's library builtins through the driver, against clang as an external oracle (c_builtin_library.c): the implicit
// declarations of every plain library name, in each language mode and with -fno-builtin, -fno-builtin-NAME and
// -ffreestanding, whose diagnostics must be clang's line for line; programs that call library functions declared so,
// fold plain declarations' calls or not as those options say, and abort, which must run as clang's builds do; and the
// library builtins the lowering refuses, by name. Include after c_check_fortify_driver_test.c, whose runner it takes.

// The lines of a compiler's diagnostics that implicit declarations make (clang's ext_implicit_lib_function_decl and its
// C99 form, the header's note, and the ordinary implicit declarations' and undeclared identifiers' errors), in order,
// into `lines`; false where they don't fit.
static bool check_test_library_lines(const char* text, char* lines, size_t capacity)
{
    static const char* const parts[] = {
        "library function",
        "include the header",
        "undeclared function",
        "implicit declaration of function",
        "undeclared identifier",
    };
    size_t length = 0;
    bool fits = capacity != 0;
    lines[0] = 0;
    for (const char* line = text; fits && line != NULL && *line != 0;)
    {
        const char* end = strchr(line, '\n');
        const size_t size = end != NULL ? (size_t)(end - line) : strlen(line);
        bool kept = false;
        for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(parts); p += 1)
        {
            const char* found = strstr(line, parts[p]);
            kept |= found != NULL && found < line + size;
        }
        fits = !kept || length + size + 1 < capacity;
        if (kept && fits)
        {
            memcpy(lines + length, line, size);
            length += size;
            lines[length++] = '\n';
            lines[length] = 0;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    return fits;
}

// The first line where two texts differ, for a failure's description.
static void check_test_library_difference(const char* a, const char* b, char* difference, size_t capacity)
{
    size_t at = 0;
    while (a[at] != 0 && a[at] == b[at])
    {
        at += 1;
    }
    while (at > 0 && a[at - 1] != '\n')
    {
        at -= 1;
    }
    const char* a_end = strchr(a + at, '\n');
    const char* b_end = strchr(b + at, '\n');
    snprintf(difference, capacity, "clang \"%.*s\", ours \"%.*s\"", (int)(a_end != NULL ? a_end - (a + at) : (ptrdiff_t)strlen(a + at)), a + at,
             (int)(b_end != NULL ? b_end - (b + at) : (ptrdiff_t)strlen(b + at)), b + at);
}

// Every plain library name called once, undeclared, in each configuration, then non-call uses of names before their
// calls: clang creates the builtin's declaration where its name is looked up, call or not, and its diagnostic says
// "call" either way. Each configuration's lines must be clang's, and its status, by both paths: some lines, but none
// where -Wno-implicit-function-declaration turns the diagnostics off (`quiet`).
static void check_test_library_implicit(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* standard;
        const char* first;
        const char* second;
        bool quiet;
    } configurations[] = {
        { "-std=gnu89", NULL, NULL },
        { "-std=c89", NULL, NULL },
        { "-std=c99", NULL, NULL },
        { "-std=gnu17", NULL, NULL },
        { "-std=c17", NULL, NULL },
        { "-std=gnu23", NULL, NULL },
        { "-std=gnu17", "-fno-builtin", NULL },
        { "-std=gnu89", "-fno-builtin-sin", "-fno-builtin-strlen" },
        { "-std=gnu17", "-ffreestanding", NULL },
        { "-std=gnu17", "-ffreestanding", "-fbuiltin" },
        { "-std=gnu17", "-fno-builtin", "-fbuiltin" },
        { "-std=gnu17", "-Wno-implicit-function-declaration", NULL, true },
    };
    static const char uses[] = "double (*pointer)(double) = cos;\nint f(void)\n{\n    unsigned long (*length)(const char *) = &strlen;\n"
                               "    return (int)length(\"abc\") + (int)pointer(0.0) + (int)strlen(\"de\") + abs(-1);\n}\n";
    const uint64_t capacity = 64 + (uint64_t)BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT * (BUSTER_CBUILTIN_LIBRARY_PLAIN_MAX_NAME + 8);
    char* calls = malloc(capacity);
    uint64_t length = calls != NULL ? (uint64_t)snprintf(calls, capacity, "void f(void)\n{\n") : 0;
    for (uint32_t p = 0; calls != NULL && p < BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT; p += 1)
    {
        length += (uint64_t)snprintf(calls + length, capacity - length, "    %s();\n", cbuiltin_library_plains[p].name);
    }
    length += calls != NULL ? (uint64_t)snprintf(calls + length, capacity - length, "}\n") : 0;
    char paths[2][512];
    snprintf(paths[0], sizeof(paths[0]), "%s/library-calls.c", directory);
    snprintf(paths[1], sizeof(paths[1]), "%s/library-uses.c", directory);
    const bool written = calls != NULL && length < capacity && parse_test_write(paths[0], calls, length) && parse_test_write(paths[1], uses, sizeof(uses) - 1);
    check_test_expect(test, written, "library implicit declarations: fixtures written");
    enum { LINES = 1 << 20 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    for (uint32_t c = 0; written && expected_lines != NULL && actual_lines != NULL && c < BUSTER_ARRAY_LENGTH(configurations); c += 1)
    {
        for (uint32_t f = 0; f < 2; f += 1)
        {
            char* arguments[12] = { (char*)clang, (char*)configurations[c].standard, "-fno-caret-diagnostics", "-ferror-limit=0", "-fsyntax-only", paths[f] };
            uint32_t count = 6;
            if (configurations[c].first != NULL)
            {
                arguments[count++] = (char*)configurations[c].first;
            }
            if (configurations[c].second != NULL)
            {
                arguments[count++] = (char*)configurations[c].second;
            }
            char* expected = NULL;
            const int oracle = check_test_driver_spawn(arguments, &expected);
            const bool kept = expected != NULL && check_test_library_lines(expected, expected_lines, LINES);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                char* actual = NULL;
                arguments[0] = (char*)executable;
                arguments[count] = scalar != 0 ? "--scalar" : NULL;
                arguments[count + 1] = NULL;
                const int status = check_test_driver_spawn(arguments, &actual);
                const bool same = kept && actual != NULL && check_test_library_lines(actual, actual_lines, LINES) &&
                                  strcmp(expected_lines, actual_lines) == 0 && (expected_lines[0] == 0) == configurations[c].quiet;
                char difference[512] = { 0 };
                if (kept && actual != NULL && !same)
                {
                    check_test_library_difference(expected_lines, actual_lines, difference, sizeof(difference));
                }
                char description[768];
                snprintf(description, sizeof(description), "library implicit declarations %s %s %s %s, %s: status %d (clang %d), %s",
                         configurations[c].standard, configurations[c].first != NULL ? configurations[c].first : "",
                         configurations[c].second != NULL ? configurations[c].second : "", f == 0 ? "calls" : "uses", scalar != 0 ? "scalar" : "vector",
                         status, oracle, difference);
                check_test_expect(test, oracle >= 0 && status == oracle && same, description);
                free(actual);
            }
            free(expected);
        }
    }
    free(actual_lines);
    free(expected_lines);
    free(calls);
    unlink(paths[0]);
    unlink(paths[1]);
}

// Programs built by clang and by this executable, by both paths, that must print the same and exit alike: C89's calls
// of library functions declared implicitly, with their rows' prototypes (sin's double, sprintf's variadic call); a
// plain declaration's strcmp of constants, which folds to clang's -1 where the name is a builtin and calls the library's
// otherwise, by -fno-builtin, -fno-builtin-strcmp and -ffreestanding; __builtin_strcmp's fold, which no option turns
// off; and __builtin_abort.
static void check_test_library_runs(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
        const char* standard;
        const char* option;
    } programs[] = {
        {
            "C89's implicit library declarations",
            "int main(void){char b[64];double d=sin(0.5)+sqrt(2.0);long n=labs(-7);unsigned long l=strlen(\"hello\");\n"
            "sprintf(b,\"%.6f %ld %lu %d\",d,n,l,abs(-3));puts(b);return (int)l+(int)fmax(1.0,2.0)*10;}\n",
            "-std=gnu89",
            NULL,
        },
        {
            "C99's implicit library declarations, made warnings",
            "int main(void){char b[64];double d=cos(0.25)*pow(2.0,10.0);snprintf(b,sizeof(b),\"%.3f\",d);puts(b);return (int)strlen(b);}\n",
            "-std=gnu17",
            "-Wno-error=implicit-function-declaration",
        },
        {
            "a plain declaration's strcmp of constants folds",
            "int strcmp(const char*,const char*);int main(void){return strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            NULL,
        },
        {
            "-fno-builtin keeps a plain declaration's strcmp a call",
            "int strcmp(const char*,const char*);int main(void){return strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            "-fno-builtin",
        },
        {
            "-fno-builtin-strcmp keeps a plain declaration's strcmp a call",
            "int strcmp(const char*,const char*);int main(void){return strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            "-fno-builtin-strcmp",
        },
        {
            "-ffreestanding keeps a plain declaration's strcmp a call",
            "int strcmp(const char*,const char*);int main(void){return strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            "-ffreestanding",
        },
        {
            "__builtin_strcmp folds whatever the options",
            "int main(void){return __builtin_strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            "-fno-builtin",
        },
        {
            "__builtin_abort",
            "int main(int argc,char**argv){if(argc>0)__builtin_abort();return 3;}\n",
            "-std=gnu17",
            NULL,
        },
    };
    char source[512];
    char built[3][512];
    snprintf(source, sizeof(source), "%s/library-run.c", directory);
    for (uint32_t b = 0; b < 3; b += 1)
    {
        snprintf(built[b], sizeof(built[b]), "%s/library-run-%u", directory, b);
    }
    for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(programs); p += 1)
    {
        const bool written = parse_test_write(source, programs[p].source, strlen(programs[p].source));
        int statuses[3] = { -1, -1, -1 };
        char* outputs[3] = { NULL, NULL, NULL };
        for (uint32_t b = 0; written && b < 3; b += 1)
        {
            char* arguments[10] = { (char*)(b == 0 ? clang : executable), (char*)programs[p].standard, "-w", source, "-o", built[b], "-lm" };
            uint32_t count = 7;
            if (programs[p].option != NULL)
            {
                arguments[count++] = (char*)programs[p].option;
            }
            if (b == 2)
            {
                arguments[count++] = "--scalar";
            }
            arguments[count] = NULL;
            char* diagnostics = NULL;
            unlink(built[b]);
            const int compiled = check_test_driver_spawn(arguments, &diagnostics);
            free(diagnostics);
            if (compiled == 0)
            {
                // The status of a run, then, for a program that exits (not __builtin_abort's, whose core the runner turns
                // off), what a second run prints.
                char* run[] = { built[b], NULL };
                statuses[b] = check_test_fortify_execute(built[b]);
                if (statuses[b] >= 0 && statuses[b] < 128)
                {
                    check_test_driver_spawn_stream(run, &outputs[b], true);
                }
                else
                {
                    outputs[b] = strdup("");
                }
            }
        }
        for (uint32_t b = 1; written && b < 3; b += 1)
        {
            char description[384];
            snprintf(description, sizeof(description), "%s, %s: exit %d and output \"%s\" (clang %d, \"%s\")", programs[p].name, b == 2 ? "scalar" : "vector",
                     statuses[b], outputs[b] != NULL ? outputs[b] : "", statuses[0], outputs[0] != NULL ? outputs[0] : "");
            check_test_expect(test, statuses[0] >= 0 && statuses[b] == statuses[0] && outputs[0] != NULL && outputs[b] != NULL &&
                                    strcmp(outputs[0], outputs[b]) == 0, description);
        }
        check_test_expect(test, written, "library programs: source written");
        for (uint32_t b = 0; b < 3; b += 1)
        {
            free(outputs[b]);
            unlink(built[b]);
        }
    }
    unlink(source);
}

// The library builtins the checker types and the lowering refuses: -c fails naming each, by both paths, and writes no
// object; their __has_builtin is 0, which the same unit's #if checks.
static void check_test_library_refusals(CheckTest* test, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
    } refusals[] = {
        { "__builtin_alloca_with_align", "void* f(void){return __builtin_alloca_with_align(16,64);}\n" },
        { "__builtin_alloca_uninitialized", "void* f(unsigned long n){return __builtin_alloca_uninitialized(n);}\n" },
        { "__builtin_nans", "double f(void){return __builtin_nans(\"\");}\n" },
        { "__builtin_nansl", "long double f(void){return __builtin_nansl(\"1\");}\n" },
    };
    char source[512];
    char object[512];
    snprintf(source, sizeof(source), "%s/library-refusal.c", directory);
    snprintf(object, sizeof(object), "%s/library-refusal.o", directory);
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(refusals); r += 1)
    {
        char text[512];
        const int length = snprintf(text, sizeof(text), "#if __has_builtin(%s)\n#error advertised\n#endif\n%s", refusals[r].name, refusals[r].source);
        const bool written = length > 0 && (size_t)length < sizeof(text) && parse_test_write(source, text, (uint64_t)length);
        for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
        {
            char* diagnostics = NULL;
            char* arguments[] = { (char*)executable, "-std=gnu17", "-c", source, "-o", object, scalar != 0 ? "--scalar" : NULL, NULL };
            unlink(object);
            const int status = check_test_driver_spawn(arguments, &diagnostics);
            char expected[128];
            snprintf(expected, sizeof(expected), "unsupported: %s", refusals[r].name);
            char description[384];
            snprintf(description, sizeof(description), "%s is refused by name at -c (%s): status %d, \"%.160s\"", refusals[r].name, scalar != 0 ? "scalar" : "vector",
                     status, diagnostics != NULL ? diagnostics : "");
            check_test_expect(test, status == 1 && diagnostics != NULL && strstr(diagnostics, expected) != NULL && strstr(diagnostics, "advertised") == NULL &&
                                    access(object, F_OK) != 0, description);
            free(diagnostics);
        }
    }
    unlink(source);
    unlink(object);
}

static void check_test_driver_library(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_library_implicit(test, clang, executable, directory);
    check_test_library_runs(test, clang, executable, directory);
    check_test_library_refusals(test, executable, directory);
}
