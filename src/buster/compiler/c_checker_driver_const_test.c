// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original GNU constant-folding fixtures. Clang supplies diagnostic policy, wording and event order externally.
// Include after c_checker_driver_recovery_test.c for the shared diagnostic group/count readers.

typedef struct CheckDriverConstFixture CheckDriverConstFixture;
struct CheckDriverConstFixture
{
    const char* name;
    const char* source;
    const char* options[3];
    // A negative value leaves option precedence entirely to the external oracle.
    int oracle_status;
    const char* oracle_message;
    bool quiet;
};

static const char check_driver_const_array[] = "const int k=3;\nint a[k];\n";
static const char check_driver_const_enum[] = "const int k=3;\nenum { E=k };\n";
static const char check_driver_const_bitfield[] = "const unsigned k=3;\nstruct S { unsigned b:k; };\n";
static const char check_driver_const_case[] = "const int k=3;\nint f(int n){switch(n){case k:return 1;default:return 0;}}\n";
static const char check_driver_const_assert[] = "const int k=3;\n_Static_assert(k,\"k\");\n";
static const char check_driver_const_static[] = "const int k=3;\nint g=k;\n";
static const char check_driver_const_return[] = "struct S{int x;};\nstruct S f(void)\n{\nconst int k=3;\nenum{E=k};\n}\n";

static const CheckDriverConstFixture check_driver_const_fixtures[] =
{
    {
        .name = "a promoted GNU object fold preserves same-body aggregate return analysis",
        .source = check_driver_const_return,
        .options = { "-Wreturn-type", "-Werror=gnu-folding-constant" },
        .oracle_status = 1,
        .oracle_message = "[-Wreturn-type]",
    },
    {
        .name = "one promoted GNU fold keeps its aggregate return warning at error limit one",
        .source = check_driver_const_return,
        .options = { "-Wreturn-type", "-Werror=gnu-folding-constant", "-ferror-limit=1" },
        .oracle_status = 1,
        .oracle_message = "[-Wreturn-type]",
    },
    {
        .name = "default file array fold",
        .source = check_driver_const_array,
        .oracle_status = 0,
        .oracle_message = ":2:5: warning: variable length array folded to constant array as an extension [-Wgnu-folding-constant]",
    },
    {
        .name = "named array fold warning",
        .source = check_driver_const_array,
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "disabled array fold warning",
        .source = check_driver_const_array,
        .options = { "-Wno-gnu-folding-constant" },
        .oracle_status = 0,
        .quiet = true,
    },
    {
        .name = "named array fold error",
        .source = check_driver_const_array,
        .options = { "-Werror=gnu-folding-constant" },
        .oracle_status = 1,
        .oracle_message = "[-Werror,-Wgnu-folding-constant]",
    },
    {
        .name = "named array fold no-error",
        .source = check_driver_const_array,
        .options = { "-Wno-error=gnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "global error with named no-error",
        .source = check_driver_const_array,
        .options = { "-Werror", "-Wno-error=gnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "named no-error after named error",
        .source = check_driver_const_array,
        .options = { "-Werror=gnu-folding-constant", "-Wno-error=gnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "named error after disabled warning",
        .source = check_driver_const_array,
        .options = { "-Wno-gnu-folding-constant", "-Werror=gnu-folding-constant" },
        .oracle_status = 1,
        .oracle_message = "[-Werror,-Wgnu-folding-constant]",
    },
    {
        .name = "pedantic array fold error",
        .source = check_driver_const_array,
        .options = { "-pedantic-errors" },
        .oracle_status = 1,
        .oracle_message = "[-Werror,-Wgnu-folding-constant]",
    },
    {
        .name = "pedantic array fold named no-error",
        .source = check_driver_const_array,
        .options = { "-pedantic-errors", "-Wno-error=gnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "quiet array fold",
        .source = check_driver_const_array,
        .options = { "-w" },
        .oracle_status = 0,
        .quiet = true,
    },
    {
        .name = "default enum fold is quiet",
        .source = check_driver_const_enum,
        .oracle_status = 0,
        .quiet = true,
    },
    {
        .name = "named enum fold warning",
        .source = check_driver_const_enum,
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "expression is not an integer constant expression; folding it to a constant is a GNU extension [-Wgnu-folding-constant]",
    },
    {
        .name = "named no-error does not enable enum fold warning",
        .source = check_driver_const_enum,
        .options = { "-Wno-error=gnu-folding-constant" },
        .oracle_status = 0,
        .quiet = true,
    },
    {
        .name = "named enum fold error",
        .source = check_driver_const_enum,
        .options = { "-Werror=gnu-folding-constant" },
        .oracle_status = 1,
        .oracle_message = "[-Werror,-Wgnu-folding-constant]",
    },
    {
        .name = "pedantic enum fold error",
        .source = check_driver_const_enum,
        .options = { "-pedantic-errors" },
        .oracle_status = 1,
        .oracle_message = "[-Werror,-Wgnu-folding-constant]",
    },
    {
        .name = "default bitfield fold is quiet",
        .source = check_driver_const_bitfield,
        .oracle_status = 0,
        .quiet = true,
    },
    {
        .name = "named bitfield fold warning",
        .source = check_driver_const_bitfield,
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "named bitfield fold error",
        .source = check_driver_const_bitfield,
        .options = { "-Werror=gnu-folding-constant" },
        .oracle_status = 1,
        .oracle_message = "[-Werror,-Wgnu-folding-constant]",
    },
    {
        .name = "pedantic bitfield fold error",
        .source = check_driver_const_bitfield,
        .options = { "-pedantic-errors" },
        .oracle_status = 1,
        .oracle_message = "[-Werror,-Wgnu-folding-constant]",
    },
    {
        .name = "default case fold is quiet",
        .source = check_driver_const_case,
        .oracle_status = 0,
        .quiet = true,
    },
    {
        .name = "named case fold warning",
        .source = check_driver_const_case,
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "named case fold error",
        .source = check_driver_const_case,
        .options = { "-Werror=gnu-folding-constant" },
        .oracle_status = 1,
        .oracle_message = "[-Werror,-Wgnu-folding-constant]",
    },
    {
        .name = "pedantic case fold error",
        .source = check_driver_const_case,
        .options = { "-pedantic-errors" },
        .oracle_status = 1,
        .oracle_message = "[-Werror,-Wgnu-folding-constant]",
    },
    {
        .name = "default static assertion fold is quiet",
        .source = check_driver_const_assert,
        .oracle_status = 0,
        .quiet = true,
    },
    {
        .name = "named static assertion fold warning",
        .source = check_driver_const_assert,
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "named static assertion fold error",
        .source = check_driver_const_assert,
        .options = { "-Werror=gnu-folding-constant" },
        .oracle_status = 1,
        .oracle_message = "[-Werror,-Wgnu-folding-constant]",
    },
    {
        .name = "pedantic static assertion fold error",
        .source = check_driver_const_assert,
        .options = { "-pedantic-errors" },
        .oracle_status = 1,
        .oracle_message = "[-Werror,-Wgnu-folding-constant]",
    },
    {
        .name = "ordinary static initializer is quiet",
        .source = check_driver_const_static,
        .oracle_status = 0,
        .quiet = true,
    },
    {
        .name = "enabled static initializer is quiet",
        .source = check_driver_const_static,
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .quiet = true,
    },
    {
        .name = "pedantic static initializer is quiet",
        .source = check_driver_const_static,
        .options = { "-pedantic-errors" },
        .oracle_status = 0,
        .quiet = true,
    },
    {
        .name = "named error static initializer is quiet",
        .source = check_driver_const_static,
        .options = { "-Werror=gnu-folding-constant" },
        .oracle_status = 0,
        .quiet = true,
    },
    {
        .name = "source error enables enum fold diagnostic",
        .source = "const int k=3;\n#pragma clang diagnostic error \"-Wgnu-folding-constant\"\nenum{E=k};\n",
        .oracle_status = 1,
        .oracle_message = "[-Werror,-Wgnu-folding-constant]",
    },
    {
        .name = "source push ignored then pop restores error",
        .source = "const int k=3;\n#pragma clang diagnostic error \"-Wgnu-folding-constant\"\n"
                  "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wgnu-folding-constant\"\n"
                  "enum{A=k};\n#pragma clang diagnostic pop\nenum{B=k};\n",
        .oracle_status = 1,
        .oracle_message = ":7:8: error:",
    },
    {
        .name = "source push error then pop restores ignored",
        .source = "const int k=3;\n#pragma clang diagnostic ignored \"-Wgnu-folding-constant\"\n"
                  "#pragma clang diagnostic push\n#pragma clang diagnostic error \"-Wgnu-folding-constant\"\n"
                  "int a[k];\n#pragma clang diagnostic pop\nint b[k];\n",
        .oracle_status = 1,
        .oracle_message = ":5:5: error:",
    },
    {
        .name = "source ignored suppresses enabled enum diagnostic",
        .source = "#pragma clang diagnostic ignored \"-Wgnu-folding-constant\"\nconst int k=3;enum{E=k};\n",
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .quiet = true,
    },
    {
        .name = "quiet option and source error precedence",
        .source = "#pragma clang diagnostic error \"-Wgnu-folding-constant\"\nconst int k=3;enum{E=k};\n",
        .options = { "-w" },
        .oracle_status = -1,
    },
    {
        .name = "array completion after bracket lookahead",
        .source = "const int k=3;\nint a[k\n#warning before-bracket\n]\n#warning after-bracket\n;\n#warning after-semicolon\n",
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "enum completion at comma",
        .source = "const int k=3;\nenum{E=k\n#warning before-comma\n,F=4\n#warning before-brace\n}\n#warning after-brace\n;\n",
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "case completion at colon",
        .source = "int f(int n){const int k=3;switch(n){case k\n#warning before-colon\n:\n#warning after-colon\nreturn 1;default:return 0;}}\n",
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "bitfield completion at semicolon lookahead",
        .source = "const unsigned k=3;struct S{unsigned b:k\n#warning before-semicolon\n;\n#warning after-semicolon\n};\n",
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "static assertion completion after semicolon lookahead",
        .source = "const int k=3;_Static_assert(k\n#warning before-assert-comma\n,\"k\"\n#warning before-assert-close\n)\n#warning after-assert-close\n;\n",
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "enum completion at closing brace",
        .source = "const int k=3;enum{E=k\n#warning before-enum-close\n}\n#warning after-enum-close\n;\n#warning after-enum-semicolon\n",
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
    {
        .name = "static assertion completion lexes after semicolon",
        .source = "const int k=3;_Static_assert(k,\"k\")\n#warning after-assert-close\n;\n#warning after-assert-semicolon\n",
        .options = { "-Wgnu-folding-constant" },
        .oracle_status = 0,
        .oracle_message = "[-Wgnu-folding-constant]",
    },
};

// PP's legacy #warning renderer omits its option suffix. Normalize only messages explicitly written by
// these phase-control directives; GNU semantic text/severity/options and every event's relative order stay exact.
static void check_test_driver_const_pp_controls(char* text, const char* source)
{
    const char suffix[] = " [-W#warnings]";
    const uint64_t suffix_size = sizeof(suffix) - 1;
    char* line = text;
    while (line != NULL && *line != 0)
    {
        char* end = strchr(line, '\n');
        end = end != NULL ? end : line + strlen(line);
        char* option = (uint64_t)(end - line) >= suffix_size ? end - suffix_size : NULL;
        if (option != NULL && memcmp(option, suffix, suffix_size) == 0)
        {
            const char* header = strstr(line, ": warning: ");
            const char* message = header != NULL && header < option ? header + 11 : NULL;
            bool control = false;
            for (const char* directive = strstr(source, "#warning "); message != NULL && !control && directive != NULL;)
            {
                const char* first = directive + 9;
                const char* last = strchr(first, '\n');
                last = last != NULL ? last : first + strlen(first);
                const uint64_t size = (uint64_t)(last - first);
                control = size == (uint64_t)(option - message) && memcmp(message, first, size) == 0;
                directive = *last != 0 ? strstr(last + 1, "#warning ") : NULL;
            }
            if (control)
            {
                memmove(option, end, strlen(end) + 1);
                end = option;
            }
        }
        line = *end == '\n' ? end + 1 : NULL;
    }
}

static void check_test_driver_const(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char objects[3][512];
    snprintf(source, sizeof(source), "%s/const-fold.c", directory);
    for (uint32_t route = 0; route < BUSTER_ARRAY_LENGTH(objects); route += 1)
    {
        snprintf(objects[route], sizeof(objects[route]), "%s/const-fold-%u.o", directory, route);
    }
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(check_driver_const_fixtures); f += 1)
    {
        const CheckDriverConstFixture* fixture = check_driver_const_fixtures + f;
        const bool written = parse_test_write(source, fixture->source, strlen(fixture->source));
        check_test_expect(test, written, "original constant-fold diagnostic fixture is written");
        for (uint32_t mode = 0; written && mode < 2; mode += 1)
        {
            char* arguments[16] = { (char*)clang, "-std=gnu17", "-fno-caret-diagnostics", "-fno-color-diagnostics",
                                    mode == 0 ? "-fsyntax-only" : "-c", source, "-o", objects[2] };
            uint32_t count = 8;
            for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(fixture->options); o += 1)
            {
                if (fixture->options[o] != NULL)
                {
                    arguments[count++] = (char*)fixture->options[o];
                }
            }
            char* expected = NULL;
            const int oracle = check_test_driver_spawn(arguments, &expected);
            check_test_driver_const_pp_controls(expected, fixture->source);
            const CheckDriverDiagnosticCounts counts = check_test_driver_diagnostic_counts(expected);
            char groups[4096];
            const bool grouped = check_test_driver_recovery_groups(expected, groups, sizeof(groups));
            const bool oracle_ready = (oracle == 0 || oracle == 1) && expected != NULL && grouped;
            char description[256];
            snprintf(description, sizeof(description), "%s, %s: external Clang oracle", fixture->name, mode == 0 ? "syntax" : "object");
            check_test_expect(test, oracle_ready && (fixture->oracle_status < 0 || oracle == fixture->oracle_status) &&
                                   (!fixture->quiet || expected[0] == 0) &&
                                   (fixture->oracle_message == NULL || strstr(expected, fixture->oracle_message) != NULL), description);
            for (uint32_t scalar = 0; oracle_ready && scalar < 2; scalar += 1)
            {
                arguments[0] = (char*)(executable != NULL ? executable : "/proc/self/exe");
                arguments[7] = objects[scalar];
                arguments[count] = scalar != 0 ? "--scalar" : NULL;
                arguments[count + 1] = NULL;
                unlink(objects[scalar]);
                char* actual = NULL;
                const int status = check_test_driver_spawn(arguments, &actual);
                check_test_driver_const_pp_controls(actual, fixture->source);
                const CheckDriverDiagnosticCounts checked = check_test_driver_diagnostic_counts(actual);
                char checked_groups[4096];
                const bool actual_grouped = check_test_driver_recovery_groups(actual, checked_groups, sizeof(checked_groups));
                snprintf(description, sizeof(description), "%s, %s %s: exact Clang diagnostic locations, severity, option, text and order", fixture->name,
                         scalar != 0 ? "scalar" : "vector", mode == 0 ? "syntax" : "object");
                check_test_expect(test, status == oracle && actual != NULL && actual_grouped && strcmp(groups, checked_groups) == 0 &&
                                       strcmp(actual, expected) == 0 && checked.warnings == counts.warnings && checked.errors == counts.errors &&
                                       checked.notes == counts.notes && checked.fatal == counts.fatal, description);
                check_test_expect(test, mode == 0 || (access(objects[scalar], F_OK) == 0) == (status == 0),
                                  "constant-fold diagnostics control object publication");
                free(actual);
            }
            if (oracle_ready && mode != 0 && oracle == 0)
            {
                check_test_expect(test, check_test_driver_same_files(objects[0], objects[1]),
                                  "constant-fold warning policy preserves identical scalar/vector objects");
            }
            free(expected);
            for (uint32_t route = 0; route < BUSTER_ARRAY_LENGTH(objects); route += 1)
            {
                unlink(objects[route]);
            }
        }
    }
    unlink(source);
}
