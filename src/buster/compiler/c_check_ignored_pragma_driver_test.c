// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source fixtures for #810. Clang is an external policy oracle, not an implementation input. Both
// checker paths must agree on acceptance and diagnostic groups; rejected compilations must not emit objects.

static uint32_t check_test_pragma_diagnostics(const char* text, const char* group)
{
    uint32_t count = 0;
    for (const char* at = text; at != NULL && (at = strstr(at, group)) != NULL; at += strlen(group))
    {
        count += 1;
    }
    return count;
}

static void check_test_driver_pragma_system(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char header_text[] = "#pragma buster_header\n#define PRAGMA_FROM_HEADER _Pragma(\"buster_macro\")\n";
    static const char source_text[] = "#include <ignored-pragma-system.h>\nint main(void){return PRAGMA_FROM_HEADER 0;}\n";
    char header[512];
    char source[512];
    snprintf(header, sizeof(header), "%s/ignored-pragma-system.h", directory);
    snprintf(source, sizeof(source), "%s/ignored-pragma-system.c", directory);
    const bool written = parse_test_write(header, header_text, sizeof(header_text) - 1) && parse_test_write(source, source_text, sizeof(source_text) - 1);
    check_test_expect(test, written, "pragma system-header and user expansion fixtures are written");
    for (uint32_t system = 0; written && system < 3; system += 1)
    {
        char* arguments[16] = { (char*)clang, "-fsyntax-only", "-fno-caret-diagnostics", "-Werror=unknown-pragmas", source, "-isystem", (char*)directory };
        uint32_t count = 7;
        if (system != 0)
        {
            arguments[count++] = "-Wsystem-headers";
        }
        if (system == 2)
        {
            arguments[count++] = "-Wno-system-headers";
        }
        char* expected = NULL;
        const int oracle = check_test_driver_spawn(arguments, &expected);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            for (uint32_t lazy = 0; lazy < 2; lazy += 1)
            {
                arguments[0] = (char*)executable;
                uint32_t extra = count;
                if (scalar != 0)
                {
                    arguments[extra++] = "--scalar";
                }
                if (lazy != 0)
                {
                    arguments[extra++] = "-flazy-system-headers";
                }
                arguments[extra] = NULL;
                char* actual = NULL;
                const int result = check_test_driver_spawn(arguments, &actual);
                const uint32_t observed = check_test_pragma_diagnostics(actual, "-Wunknown-pragmas]");
                const bool equal = oracle != 0 && result != 0 && observed == (system == 1 ? 2u : 1u) &&
                                   observed == check_test_pragma_diagnostics(expected, "-Wunknown-pragmas]");
                check_test_expect(test, equal, "pragma warning visibility uses the directive or macro invocation, not its neighboring C token");
                if (!equal)
                {
                    fprintf(stderr, "pragma system case %u scalar %u lazy %u: Clang %d (%s), checker %d (%s)\n", system, scalar, lazy,
                            oracle, expected != NULL ? expected : "", result, actual != NULL ? actual : "");
                }
                free(actual);
            }
        }
        free(expected);
    }
    unlink(header);
    unlink(source);
}

static void check_test_driver_pragma_execution(CheckTest* test, const char* executable, const char* directory)
{
    static const char source_text[] =
        "#pragma ident \"tooling\"\n#pragma GCC push_options\n#pragma GCC optimize(\"O3\")\n"
        "#pragma comment(lib, \"m\")\nint main(void){int sum=0;\n#pragma omp parallel for\n"
        "for(int i=0;i<4;i++)sum+=i;return sum!=6;}\n#pragma GCC pop_options\n";
    char source[512];
    char binary[512];
    char preprocessed[512];
    snprintf(source, sizeof(source), "%s/ignored-pragma-execute.c", directory);
    snprintf(binary, sizeof(binary), "%s/ignored-pragma-execute", directory);
    snprintf(preprocessed, sizeof(preprocessed), "%s/ignored-pragma-execute.i", directory);
    const bool written = parse_test_write(source, source_text, sizeof(source_text) - 1);
    check_test_expect(test, written, "ignored pragma execution fixture is written");
    for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
    {
        char* text = NULL;
        char* arguments[] = { (char*)executable, source, "-o", binary, scalar != 0 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        text = NULL;
        char* run[] = { binary, NULL };
        check_test_expect(test, compiled && check_test_driver_spawn(run, &text) == 0, "ignored pragmas preserve native serial execution");
        free(text);
        text = NULL;
        unlink(preprocessed);
        char* preprocess[] = { (char*)executable, "-E", "-P", "-Werror=unknown-pragmas", source, "-o", preprocessed, scalar != 0 ? "--scalar" : NULL, NULL };
        const int status = check_test_driver_spawn(preprocess, &text);
        free(text);
        char captured[4096] = { 0 };
        FILE* file = status == 0 ? fopen(preprocessed, "rb") : NULL;
        const size_t length = file != NULL ? fread(captured, 1, sizeof(captured) - 1, file) : 0;
        const bool read = file != NULL && !ferror(file) && feof(file);
        captured[length] = 0;
        if (file != NULL)
        {
            fclose(file);
        }
        check_test_expect(test, read && strstr(captured, "#pragma omp") != NULL && strstr(captured, "#pragma GCC optimize") != NULL,
                          "preprocess-only output retains pragmas without running semantic warning policy");
    }
    unlink(source);
    unlink(binary);
    unlink(preprocessed);
}

static void check_test_driver_ignored_pragmas(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* source;
        const char* first;
        const char* second;
        const char* third;
    } cases[] = {
        { "#pragma buster_unknown\nint main(void){return 0;}\n", NULL, NULL, NULL },
        { "#pragma buster_unknown\nint main(void){return 0;}\n", "-Wall", NULL, NULL },
        { "#pragma buster_unknown\nint main(void){return 0;}\n", "-Wunknown-pragmas", "-Werror", NULL },
        { "#pragma buster_unknown\nint main(void){return 0;}\n", "-Werror=unknown-pragmas", "-w", NULL },
        { "#pragma buster_unknown\nint main(void){return 0;}\n", "-Wall", "-Wno-pragmas", NULL },
        { "#pragma buster_unknown\nint main(void){return 0;}\n", "-Werror=unknown-pragmas", "-Wno-error=unknown-pragmas", NULL },
        { "#pragma buster_unknown\nint main(void){return 0;}\n", "-Wno-unknown-pragmas", "-Wpragmas", NULL },
        { "#pragma ident \"tooling\"\n#pragma GCC push_options\n#pragma GCC optimize(\"O3\")\n#pragma GCC ivdep\n#pragma GCC pop_options\nint main(void){return 0;}\n", "-Wall", NULL, NULL },
        { "#pragma clang buster_unknown\n#pragma GCC buster_unknown\n#pragma STDC buster_unknown\nint main(void){return 0;}\n", NULL, NULL, NULL },
        { "#pragma clang buster_unknown\n#pragma GCC buster_unknown\n#pragma STDC buster_unknown\nint main(void){return 0;}\n", "-Werror=pragmas", NULL, NULL },
        { "#pragma align 4\nint main(void){return 0;}\n", NULL, NULL, NULL },
        { "#pragma align 4\nint main(void){return 0;}\n", "-Werror=ignored-pragmas", NULL, NULL },
        { "#pragma align 4\nint main(void){return 0;}\n", "-Wno-ignored-pragmas", "-Werror", NULL },
        { "#pragma comment(lib, \"m\")\nint main(void){return 0;}\n", "-Wall", "-Werror", NULL },
        { "#pragma comment(user, \"tooling\")\nint main(void){return 0;}\n", "-Werror=ignored-pragmas", NULL, NULL },
        { "int main(void){int sum=0;\n#pragma omp parallel for\nfor(int i=0;i<4;i++)sum+=i;return sum!=6;}\n", NULL, NULL, NULL },
        { "int main(void){int sum=0;\n#pragma omp parallel for\nfor(int i=0;i<4;i++)sum+=i;return sum!=6;}\n", "-Wall", "-Werror", NULL },
        { "int main(void){\n#pragma omp parallel\n{}return 0;}\n", "-Wsource-uses-openmp", NULL, NULL },
        { "int main(void){\n#pragma omp parallel\n{}return 0;}\n", "-Werror=source-uses-openmp", NULL, NULL },
        { "int main(void){\n#pragma omp parallel\n{}return 0;}\n", "-Wopenmp", "-Werror", NULL },
        { "int main(void){\n#pragma omp parallel\n{}return 0;}\n", "-fopenmp", "-fno-openmp", NULL },
        { "#define _OPENMP 202111\nint main(void){\n#pragma omp parallel\n{}return 0;}\n", NULL, NULL, NULL },
        { "#pragma clang diagnostic error \"-Wunknown-pragmas\"\n#pragma clang diagnostic push\n#pragma GCC diagnostic ignored \"-Wunknown-pragmas\"\n#pragma buster_hidden\n#pragma clang diagnostic pop\n#pragma buster_visible\nint main(void){return 0;}\n", NULL, NULL, NULL },
        { "#pragma buster_before\n#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint main(void){return 0;}\n", "-Wunknown-pragmas", NULL, NULL },
        { "#pragma clang diagnostic ignored \"-Wpragmas\"\n#pragma align 4\n#pragma buster_hidden\nint main(void){return 0;}\n", "-Werror=unknown-pragmas", NULL, NULL },
        { "#pragma clang diagnostic error \"-Wsource-uses-openmp\"\nint main(void){\n#pragma omp parallel\n{}return 0;}\n", NULL, NULL, NULL },
        { "int main(void){return 0;}\n#pragma buster_eof\n", "-Werror=unknown-pragmas", NULL, NULL },
        { "#pragma buster_only\n", "-Wunknown-pragmas", NULL, NULL },
        { "#pragma\nint main(void){return 0;}\n", "-Werror=unknown-pragmas", NULL, NULL },
        { "#define P _Pragma(\"buster_macro\")\nint main(void){return P 0;}\n", "-Wunknown-pragmas", NULL, NULL },
        { "int value=\n#pragma buster_before_error\n;\n", "-Wunknown-pragmas", NULL, NULL },
        { "int value=;\n#pragma buster_after_error\n", "-Wunknown-pragmas", NULL, NULL },
        { "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint value=\n#pragma buster_before_error\n;\n", NULL, NULL, NULL },
    };
    char source[512];
    char object[512];
    snprintf(source, sizeof(source), "%s/ignored-pragma.c", directory);
    snprintf(object, sizeof(object), "%s/ignored-pragma.o", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const bool written = parse_test_write(source, cases[c].source, strlen(cases[c].source));
        check_test_expect(test, written, "ignored pragma fixture is written");
        for (uint32_t mode = 0; written && mode < 2; mode += 1)
        {
            char* arguments[16] = { (char*)clang, "-std=gnu17", "-fno-caret-diagnostics", mode == 0 ? "-fsyntax-only" : "-c", source, "-o", object };
            uint32_t count = 7;
            if (cases[c].first != NULL)
            {
                arguments[count++] = (char*)cases[c].first;
            }
            if (cases[c].second != NULL)
            {
                arguments[count++] = (char*)cases[c].second;
            }
            if (cases[c].third != NULL)
            {
                arguments[count++] = (char*)cases[c].third;
            }
            char* expected = NULL;
            const int oracle = check_test_driver_spawn(arguments, &expected);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                unlink(object);
                arguments[0] = (char*)executable;
                arguments[count] = scalar != 0 ? "--scalar" : NULL;
                char* actual = NULL;
                const int result = check_test_driver_spawn(arguments, &actual);
                bool equal = expected != NULL && actual != NULL && (result == 0) == (oracle == 0);
                static const char* const groups[] = { "-Wunknown-pragmas]", "-Wignored-pragmas]", "-Wsource-uses-openmp]" };
                for (uint32_t group = 0; group < BUSTER_ARRAY_LENGTH(groups); group += 1)
                {
                    equal &= check_test_pragma_diagnostics(actual, groups[group]) == check_test_pragma_diagnostics(expected, groups[group]);
                }
                if (!equal)
                {
                    fprintf(stderr, "pragma case %u mode %u scalar %u: clang=%d buster=%d\nclang: %s\nbuster: %s\n", c, mode, scalar, oracle, result,
                            expected != NULL ? expected : "", actual != NULL ? actual : "");
                }
                check_test_expect(test, equal, "retained pragma acceptance and warning groups match Clang");
                check_test_expect(test, result == 0 || access(object, F_OK) != 0, "a rejected pragma compilation does not emit an object");
                free(actual);
            }
            free(expected);
        }
    }
    static const struct
    {
        const char* directive;
        const char* name;
        const char* first;
        const char* second;
    } refused[] = {
        { "#pragma redefine_extname old new\nint old;\n", "redefine_extname", NULL, NULL },
        { "#pragma scalar_storage_order big-endian\nint object;\n", "scalar_storage_order", NULL, NULL },
        { "#pragma GCC target(\"avx2\")\nint object;\n", "GCC target", NULL, NULL },
        { "#pragma align=packed\nstruct S{char c;int n;};\n", "align", NULL, NULL },
        { "#pragma options align=packed\nstruct S{char c;int n;};\n", "options", NULL, NULL },
        { "#pragma ms_struct on\nint object;\n", "ms_struct", NULL, NULL },
        { "#pragma omp parallel\nint object;\n", "omp", "-fopenmp", NULL },
        { "#pragma omp parallel\nint object;\n", "omp", "-fno-openmp", "-fopenmp" },
        { "#undef _OPENMP\n#pragma omp parallel\nint object;\n", "omp", "-fopenmp", NULL },
        { "#pragma omp parallel\nint object;\n", "omp", "-fopenmp=libiomp5", "-undef" },
        { "#pragma omp parallel\nint object;\n", "omp", "-fopenmp=libgomp", NULL },
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(refused); c += 1)
    {
        char body[1024];
        const int length = snprintf(body, sizeof(body), "#pragma clang diagnostic ignored \"-Weverything\"\n%s", refused[c].directive);
        const bool written = length > 0 && (uint32_t)length < sizeof(body) && parse_test_write(source, body, (uint64_t)length);
        check_test_expect(test, written, "unsupported semantic pragma fixture is written");
        for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
        {
            unlink(object);
            char* arguments[14] = { (char*)executable, "-std=gnu17", "-c", source, "-o", object, "-w" };
            uint32_t count = 7;
            if (refused[c].first != NULL)
            {
                arguments[count++] = (char*)refused[c].first;
            }
            if (refused[c].second != NULL)
            {
                arguments[count++] = (char*)refused[c].second;
            }
            arguments[count] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int result = check_test_driver_spawn(arguments, &actual);
            check_test_expect(test, result != 0 && actual != NULL && strstr(actual, "unsupported semantic '#pragma ") != NULL &&
                              strstr(actual, refused[c].name) != NULL && access(object, F_OK) != 0,
                              "warning controls cannot suppress a named semantic pragma refusal or emit an object");
            free(actual);
        }
    }
    // Malformed supported directives are still owned by their semantic readers, not the unknown fallback.
    static const char* const malformed[] = { "#pragma clang loop unroll_count(\n", "#pragma clang optimize maybe\n", "#pragma clang section text=4\n" };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(malformed); c += 1)
    {
        char body[512];
        const int length = snprintf(body, sizeof(body), "%sint main(void){return 0;}\n", malformed[c]);
        const bool written = length > 0 && (uint32_t)length < sizeof(body) && parse_test_write(source, body, (uint64_t)length);
        for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
        {
            char* text = NULL;
            char* arguments[] = { (char*)executable, "-fsyntax-only", "-w", source, scalar != 0 ? "--scalar" : NULL, NULL };
            check_test_expect(test, check_test_driver_spawn(arguments, &text) != 0, "malformed supported pragma does not become an ignorable unknown");
            free(text);
        }
    }
    check_test_driver_pragma_system(test, clang, executable, directory);
    check_test_driver_pragma_execution(test, executable, directory);
    unlink(source);
    unlink(object);
}
