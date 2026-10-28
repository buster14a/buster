// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original system-header warning fixtures; Clang is an external diagnostic oracle.

typedef struct PPSystemWarningCase PPSystemWarningCase;
struct PPSystemWarningCase
{
    const char* name;
    const char* source;
    const char* header;
    bool system;
    uint32_t hidden_warnings;
    uint32_t shown_warnings;
    uint32_t errors;
};

static char* pp_test_system_warning_clang(const char* path, const char* directory, bool system, bool show, uint32_t errors)
{
    FILE* diagnostics = tmpfile();
    char* text = NULL;
    const pid_t child = diagnostics != NULL ? fork() : -1;
    if (child == 0)
    {
        const int output = open("/dev/null", O_WRONLY);
        if (output >= 0 && dup2(output, STDOUT_FILENO) >= 0 && dup2(fileno(diagnostics), STDERR_FILENO) >= 0)
        {
            unsetenv("CPATH");
            unsetenv("C_INCLUDE_PATH");
            execlp("clang", "clang", "-E", "-std=gnu17", "-nostdinc", "-fno-caret-diagnostics", "-fno-diagnostics-show-option", "-fmacro-backtrace-limit=0",
                   "-D__INT64_C(c)=c##L", show ? "-Wsystem-headers" : "-Wno-system-headers", system ? "-isystem" : "-I", directory, path, (char*)NULL);
        }
        _exit(127);
    }
    int status = 0;
    size_t length = 0;
    const bool ran = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == (errors != 0);
    if (ran)
    {
        pp_test_read_output(diagnostics, &text, &length);
    }
    if (diagnostics != NULL)
    {
        fclose(diagnostics);
    }
    return text;
}

static bool pp_test_system_warnings(const char* directory, bool with_clang)
{
    static const char messages[] = "#warning kept\n#pragma message(\"message\")\n#pragma GCC warning \"gcc warning\"\n#define VALUE 1\n#define VALUE 2\n#pragma clang diagnostic pop\n";
    const PPSystemWarningCase cases[] =
    {
        { "system directory", "#include <system.h>\n", messages, true, 1, 5, 0 },
        { "pragma system_header", "#include <system.h>\n", "#pragma message(\"before\")\n#pragma GCC system_header\n#warning kept\n#pragma message(\"after\")\n#pragma GCC warning \"after gcc\"\n#define VALUE 1\n#define VALUE 2\n#pragma clang diagnostic pop\n", false, 2, 6, 0 },
        { "predefined macro whitespace", "#include <system.h>\n", "#define __INT64_C(c) c ## L\n", true, 0, 1, 0 },
        { "system macro used in ordinary source", "#include <system.h>\nMESSAGE\n", "#define MESSAGE _Pragma(\"message(\\\"from user\\\")\")\n", true, 1, 1, 0 },
        { "ordinary macro used in a system header", "#define MESSAGE _Pragma(\"message(\\\"from system\\\")\")\n#include <system.h>\n", "MESSAGE\n", true, 0, 1, 0 },
        { "nested system include", "#include <system.h>\n", "#include \"nested.h\"\n", true, 1, 2, 0 },
        { "system errors remain visible", "#include <system.h>\n", "#pragma message(\"hidden\")\n#if 1 abc\n#endif\n", true, 0, 1, 1 },
        { "system invocation closes in user source", "#include <system.h>\nb)\n", "#define F(x) _Pragma(\"message(\\\"cross\\\")\") x\nF(a\n", true, 0, 1, 0 },
        { "user invocation crosses system include", "#define F(x) _Pragma(\"message(\\\"cross\\\")\") x\n#define G() 1\nF(a\n#if G()\n#endif\n#include <system.h>\n)\n", "b\n", true, 1, 1, 0 },
    };
    char path[4096];
    char header[4096];
    char nested[4096];
    snprintf(path, sizeof(path), "%s/system-warning.c", directory);
    snprintf(header, sizeof(header), "%s/system.h", directory);
    snprintf(nested, sizeof(nested), "%s/nested.h", directory);
    const char* directories[] = { directory };
    const char* macros[] = { "D__INT64_C(c)=c##L" };
    uint32_t failures = 0;
    uint32_t runs = 0;
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(cases); fixture += 1)
    {
        const PPSystemWarningCase* test = cases + fixture;
        const bool written = pp_test_write_file(path, test->source) && pp_test_write_file(header, test->header) &&
                             pp_test_write_file(nested, "#pragma message(\"nested\")\n#warning nested warning\n");
        failures += !written;
        for (uint32_t show = 0; written && show < 2; show += 1)
        {
            char* oracle = with_clang ? pp_test_system_warning_clang(path, directory, test->system, show != 0, test->errors) : NULL;
            failures += with_clang && oracle == NULL;
            for (uint32_t mode = 0; mode < 8; mode += 1)
            {
                static InternTable table;
                static Preprocessor pp;
                char* text = NULL;
                size_t length = 0;
                FILE* stream = open_memstream(&text, &length);
                const bool quiet = (mode & 4) != 0;
                const PPOptions options =
                {
                    .language = { .standard = 2017, .gnu = true },
                    .scalar = (mode & 1) != 0,
                    .defer_history = (mode & 2) != 0,
                    .quiet = quiet,
                    .system_header_warnings = show != 0,
                    .no_standard_includes = true,
                    .include_directories = test->system ? NULL : directories,
                    .include_directory_count = test->system ? 0 : 1,
                    .system_directories = test->system ? directories : NULL,
                    .system_directory_count = test->system ? 1 : 0,
                    .macros = macros,
                    .macro_count = 1,
                    .diagnostics = stream,
                };
                const bool ready = stream != NULL && intern_initialize(&table) && pp_initialize(&pp, &options, &table);
                const bool accepted = ready && pp_run(&pp, path);
                bool passed = ready && accepted == (test->errors == 0) && pp.errors == test->errors &&
                              pp.warnings == (show != 0 ? test->shown_warnings : test->hidden_warnings);
                if (stream != NULL)
                {
                    passed = fclose(stream) == 0 && passed;
                }
                if (!quiet && text != NULL && oracle != NULL)
                {
                    pp_test_normalize_scratch(text);
                    pp_test_normalize_scratch(oracle);
                }
                passed = passed && text != NULL && (quiet ? length == 0 : !with_clang || (oracle != NULL && strcmp(text, oracle) == 0));
                if (!passed)
                {
                    printf("system warnings %s show=%u mode=%u: errors=%u warnings=%u\nours:\n%sClang:\n%s", test->name, show, mode, pp.errors, pp.warnings,
                           text != NULL ? text : "<failed>\n", oracle != NULL ? oracle : "<not run>\n");
                }
                failures += !passed;
                runs += 1;
                free(text);
                pp_release(&pp);
                intern_release(&table);
            }
            free(oracle);
        }
    }
    unlink(path);
    unlink(header);
    unlink(nested);
    printf("system warning policy: %u failures in %u runs%s\n", failures, runs, with_clang ? " (Clang oracle)" : "");
    return failures == 0;
}
