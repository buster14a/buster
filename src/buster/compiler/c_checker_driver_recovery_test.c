// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original rejected-source fixtures. Clang is an external ordering/budget oracle, not a fixture source.

// Compare every primary/note's exact location and severity in sequence. Message wording is checked elsewhere;
// these fixtures distinguish collection/order without weakening the include-stack or attached-note boundaries.
static bool check_test_driver_recovery_groups(const char* text, char* groups, uint64_t capacity)
{
    uint64_t used = 0;
    bool valid = text != NULL && capacity != 0;
    for (const char* at = text; valid && at != NULL && *at != 0;)
    {
        const char* end = strchr(at, '\n');
        const uint64_t length = end != NULL ? (uint64_t)(end - at) : strlen(at);
        static const char* const kinds[] = { ": warning: ", ": error: ", ": fatal error: ", ": note: " };
        uint64_t take = 0;
        for (uint32_t kind = 0; kind < BUSTER_ARRAY_LENGTH(kinds); kind += 1)
        {
            const char* marker = memmem(at, length, kinds[kind], strlen(kinds[kind]));
            if (marker != NULL)
            {
                take = (uint64_t)(marker - at) + strlen(kinds[kind]) - 1;
            }
        }
        if (length >= 13 && memcmp(at, "fatal error: ", 13) == 0)
        {
            take = 12;
        }
        if (length >= 22 && memcmp(at, "In file included from ", 22) == 0)
        {
            take = length;
        }
        if (take != 0)
        {
            valid = take + 1 < capacity - used;
            if (valid)
            {
                memcpy(groups + used, at, take);
                groups[used + take] = '\n';
                used += take + 1;
            }
        }
        at = end != NULL ? end + 1 : NULL;
    }
    if (capacity != 0)
    {
        groups[used] = 0;
    }
    return valid;
}

static void check_test_driver_recovery(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const fixtures[] = {
        "int first(void){return 2147483647\n#error before_plus\n+1;}\n",
        "int first(void){return 2147483647+\n#error before_rhs\n1;}\n",
        "int first(void){return 2147483647+1\n#error after_rhs\n;}\n",
        "int first(void){return 2147483647+1;\n#error after_semicolon\n}\n",
        "int first(void){return 2147483647+1;}\n#error after_body\n",
        "int first(void){return 1<<32;}\n#error after_body\n",
        "#pragma GCC visibility pop\n#error later\nint anchor;\n",
        "#pragma GCC visibility pop\nint anchor;\n#error later\n",
        "#pragma GCC visibility push\n#error later\nint anchor;\n",
        "#pragma GCC visibility push(bad)\n#error later\nint anchor;\n",
        "#pragma GCC visibility pop\nint first(void){break;return 0;}\n#error later\n",
        "int first(void){return missing\n#error before_open\n(1); }\n",
        "int first(void){return missing(\n#error after_open\n1); }\n",
        "int first(void){return missing(1\n#error before_close\n); }\n",
        "int first(void){return missing(1)\n#error after_close\n; }\n",
        "int first(void){return 2147483647+1;}\nint incomplete = ;\n#error later\n",
        "int first(void){return 1<<32;}\n#warning lookahead\nint incomplete = ;\n#error later\n",
        "int first(void){return missing;}\n#error later\n",
        "#error earlier\nint later(void){return missing;}\n",
        "int first(void){return missing;}\nint later(void){return 1<<32;}\n",
        "int first(void){int n=1<<32;return missing+n;}\nint later(void){return 1<<32;}\n",
        "int first(void){return missing;}\nint middle(void){return *1;}\nint later(void){return 1<<32;}\n",
        "int first(void){return missing;}\nint later(void){return _Generic(1,int:1<<32,default:0);}\n",
        "#define NAME missing\n#define SHIFT (1<<32)\nint first(void){return NAME;}\n#warning between\nint later(void){return SHIFT;}\n#error end\n",
        "#include \"recovery-header.h\"\n#error outer\nint later(void){return 1<<32;}\n",
        "int first(void){return missing;}\n#include \"absent-698-header.h\"\nint excluded(void){return 1<<32;}\n",
        "int first(void){return 1<<32;}\nint partial(void){int n=1<<32;\n#include \"absent-698-header.h\"\n",
        "#if 1\nint first(void){return missing;}\nint later(void){return 1<<32;}\n",
        "int first(void){return *1;}\nint incomplete = ;\n",
        "int first=missing;\nint later(void){return 1<<32;}\nint excess[1]={1,2};\n",
        "#define ONE(x) x\nint first(void){return missing;}\n#if ONE(1,2)+0\n#endif\nint later(void){return 1<<32;}\n",
        "#line 900 \"late-name.c\"\nint first(void){return missing;}\n#line 2 \"early-name.c\"\n#error afterwards\nint later(void){return 1<<32;}\n",
        "int first(void){return missing\n#error after_name\n;}\nint later(void){return 1<<32;}\n",
        "int first(void){return *\n#error after_operator\n1;}\nint later(void){return 1<<32;}\n",
        "int x=1<<32\n#warning before_semicolon\n;\n#warning after_semicolon\nint first(void){return missing;}\n",
        "enum { A=2147483647+1\n#warning inside_enum\n};\nint first(void){return missing;}\n",
        "int array[1]={1,2}\n#error after_initializer\n;\n",
        "struct P{int x;};\nint f(void){struct P p={.x=(int[1]){1,2}[0]\n#warning after_inner\n};return p.x;}\nint bad(void){return missing;}\n",
    };
    static const char* const limits[] = { NULL, "-ferror-limit=0", "-ferror-limit=1", "-ferror-limit=2", "-ferror-limit=20" };
    static const char* const policies[] = { NULL, "error", "ignored", "-w" };
    char source[PATH_MAX];
    char header[PATH_MAX];
    char object[PATH_MAX];
    char policy_header[PATH_MAX];
    snprintf(source, sizeof(source), "%s/recovery.c", directory);
    snprintf(header, sizeof(header), "%s/recovery-header.h", directory);
    snprintf(object, sizeof(object), "%s/recovery.o", directory);
    snprintf(policy_header, sizeof(policy_header), "%s/recovery-policy.h", directory);
    const char header_text[] = "#define HEADER_NAME missing\nint header_first(void){return HEADER_NAME;}\n#warning in_header\nint header_later(void){return 1<<32;}\n";
    bool made = parse_test_write(header, header_text, sizeof(header_text) - 1);
    check_test_expect(test, made, "diagnostic recovery header can be written");
    for (uint32_t fixture = 0; made && fixture <= BUSTER_ARRAY_LENGTH(fixtures); fixture += 1)
    {
        if (fixture < BUSTER_ARRAY_LENGTH(fixtures))
        {
            made = parse_test_write(source, fixtures[fixture], strlen(fixtures[fixture]));
        }
        else
        {
            FILE* file = fopen(source, "w");
            made = file != NULL;
            for (uint32_t i = 0; made && i < 25; i += 1)
            {
                fprintf(file, "int bad_%u(void){return missing_%u;}\nint warn_%u(void){return 1<<32;}\n", i, i, i);
            }
            if (file != NULL)
            {
                made = fclose(file) == 0 && made;
            }
        }
        check_test_expect(test, made, "original diagnostic recovery source can be written");
        for (uint32_t limit = 0; made && limit < BUSTER_ARRAY_LENGTH(limits); limit += 1)
        {
            for (uint32_t policy = 0; policy < BUSTER_ARRAY_LENGTH(policies); policy += 1)
            {
                if (policy == 1 || policy == 2)
                {
                    char pragma[128];
                    const int length = snprintf(pragma, sizeof(pragma), "#pragma clang diagnostic %s \"-Wshift-count-overflow\"\n", policies[policy]);
                    made = length > 0 && (uint32_t)length < sizeof(pragma) && parse_test_write(policy_header, pragma, (uint64_t)(uint32_t)length);
                    check_test_expect(test, made, "source warning policy is available for recovery");
                }
                char* arguments[12] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics" };
                uint32_t count = 4;
                if (limits[limit] != NULL)
                {
                    arguments[count++] = (char*)limits[limit];
                }
                if (policy == 1 || policy == 2)
                {
                    arguments[count++] = "-include";
                    arguments[count++] = policy_header;
                }
                else if (policies[policy] != NULL)
                {
                    arguments[count++] = (char*)policies[policy];
                }
                arguments[count++] = source;
                char* reference = NULL;
                const int oracle = check_test_driver_spawn(arguments, &reference);
                char expected_groups[32768];
                const bool expected_valid = check_test_driver_recovery_groups(reference, expected_groups, sizeof(expected_groups));
                const CheckDriverDiagnosticCounts expected = check_test_driver_diagnostic_counts(reference);
                check_test_expect(test, expected_valid && oracle == 1, "Clang rejects each diagnostic recovery fixture");
                free(reference);
                for (uint32_t route = 0; route < 4; route += 1)
                {
                    static const char sentinel[] = "keep-rejected-output";
                    const bool prior = parse_test_write(object, sentinel, sizeof(sentinel));
                    char* checked[16] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17" };
                    uint32_t used = 2;
                    if ((route & 1) != 0)
                    {
                        checked[used++] = "--scalar";
                    }
                    checked[used++] = route < 2 ? "-fsyntax-only" : "-c";
                    if (route >= 2)
                    {
                        checked[used++] = "-o";
                        checked[used++] = object;
                    }
                    if (limits[limit] != NULL)
                    {
                        checked[used++] = (char*)limits[limit];
                    }
                    if (policy == 1 || policy == 2)
                    {
                        checked[used++] = "-include";
                        checked[used++] = policy_header;
                    }
                    else if (policies[policy] != NULL)
                    {
                        checked[used++] = (char*)policies[policy];
                    }
                    checked[used++] = source;
                    char* text = NULL;
                    const int status = check_test_driver_spawn(checked, &text);
                    char actual_groups[32768];
                    const bool actual_valid = check_test_driver_recovery_groups(text, actual_groups, sizeof(actual_groups));
                    const CheckDriverDiagnosticCounts actual = check_test_driver_diagnostic_counts(text);
                    char description[160];
                    snprintf(description, sizeof(description), "recovery groups fixture=%u limit=%u policy=%u route=%u", fixture, limit, policy, route);
                    check_test_expect(test, expected_valid && actual_valid && strcmp(expected_groups, actual_groups) == 0, description);
                    snprintf(description, sizeof(description), "recovery budget/counts fixture=%u limit=%u policy=%u route=%u", fixture, limit, policy, route);
                    check_test_expect(test, status == oracle && actual.errors == expected.errors && actual.warnings == expected.warnings &&
                                           actual.notes == expected.notes && actual.fatal == expected.fatal && !actual.after_fatal &&
                                           strcmp(actual.summary, expected.summary) == 0, description);
                    FILE* preserved = fopen(object, "r");
                    char bytes[sizeof(sentinel)] = { 0 };
                    const bool unchanged = preserved != NULL && fread(bytes, 1, sizeof(bytes), preserved) == sizeof(bytes) &&
                                           memcmp(bytes, sentinel, sizeof(bytes)) == 0 && fgetc(preserved) == EOF;
                    check_test_expect(test, prior && unchanged, "rejected diagnostic recovery never replaces an output file");
                    if (preserved != NULL)
                    {
                        fclose(preserved);
                    }
                    free(text);
                }
            }
        }
    }
    // Completed control and TU-final constraints stay independent of preprocessing/body rejection. Existing
    // forward-tag/previous-case notes are separate checker text boundaries: these controls pin primary counts,
    // final error location and cutoff, while the fixtures above compare every existing macro/include note.
    const char* constraints[] = {
        "struct S;\nstruct S object;\nint first(void){return missing;}\nint later(void){return 1<<32;}\n",
        "int f(int n){switch(n){case 0:;case 0:;}\n#error after_switch\nreturn 0;}\n",
        "int f(void){break\n#error before_semicolon\n;return 0;}\n",
        "int f(void){continue;\n#error after_semicolon\nreturn 0;}\n",
        "#pragma unroll 4\n#error after_file_pragma\nint object;\n",
    };
    for (uint32_t constraint = 0; constraint < BUSTER_ARRAY_LENGTH(constraints); constraint += 1)
    {
        made = parse_test_write(source, constraints[constraint], strlen(constraints[constraint]));
        check_test_expect(test, made, "independent completed constraint source can be written");
        for (uint32_t limit = 0; made && limit < BUSTER_ARRAY_LENGTH(limits); limit += 1)
        {
            char* arguments[10] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics" };
            uint32_t used = 4;
            if (limits[limit] != NULL)
            {
                arguments[used++] = (char*)limits[limit];
            }
            arguments[used++] = source;
            char* text = NULL;
            const int oracle = check_test_driver_spawn(arguments, &text);
            const CheckDriverDiagnosticCounts expected = check_test_driver_diagnostic_counts(text);
            free(text);
            for (uint32_t route = 0; route < 4; route += 1)
            {
                char* checked[12] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17" };
                used = 2;
                if ((route & 1) != 0)
                {
                    checked[used++] = "--scalar";
                }
                checked[used++] = route < 2 ? "-fsyntax-only" : "-c";
                if (route >= 2)
                {
                    checked[used++] = "-o";
                    checked[used++] = object;
                }
                if (limits[limit] != NULL)
                {
                    checked[used++] = (char*)limits[limit];
                }
                checked[used++] = source;
                text = NULL;
                const int status = check_test_driver_spawn(checked, &text);
                const CheckDriverDiagnosticCounts actual = check_test_driver_diagnostic_counts(text);
                char description[160];
                snprintf(description, sizeof(description), "independent constraint=%u limit=%u route=%u keeps Clang counts, order and budget", constraint, limit, route);
                check_test_expect(test, oracle == 1 && status == oracle && actual.errors == expected.errors && actual.warnings == expected.warnings &&
                                       actual.fatal == expected.fatal && actual.last_error_line == expected.last_error_line && !actual.after_fatal &&
                                       strcmp(actual.summary, expected.summary) == 0, description);
                free(text);
            }
        }
    }
    // Analysis-based return warnings distinguish genuine errors from warnings promoted to errors. A real
    // error in the immediate post-body lookahead inhibits analysis; a later error after an anchor does not.
    static const struct
    {
        const char* text;
        const char* header;
        const char* standard;
        const char* first;
        const char* second;
    } returns[] = {
        { "#error before\nint f(void){}\n", NULL, "-std=gnu17", NULL, NULL },
        { "int f(int n){\n#error inside\nif(n)return 1;}\nint g(int n){if(n)return 1;}\n", NULL, "-std=gnu17", NULL, NULL },
        { "struct S{long a,b;};struct S f(int n,struct S s){if(n)return s;}\n#error lookahead\n", NULL, "-std=gnu17", NULL, NULL },
        { "struct S{long a,b;};struct S f(int n,struct S s){if(n)return s;}\nint anchor;\n#error later\n", NULL, "-std=gnu17", NULL, NULL },
        { "struct S{long a,b;};struct S f(int n,struct S s){if(n)return s;\n#warning before_close\n}\n#warning after_close\n", NULL, "-std=gnu17", NULL, NULL },
        { "int f(int n){if(n)return 1;}int g(int n){if(n)return 1;}\n", NULL, "-std=gnu17", "-Werror=return-type", NULL },
        { "int f(int n){int x=2147483647+1;if(n)return x;}int g(int n){if(n)return 1;}\n", NULL, "-std=gnu17", "-Werror=integer-overflow", "-Werror=return-type" },
        { "int f(int n){int x=1<<32;if(n)return x;}int g(int n){if(n)return 1;}\n", NULL, "-std=gnu17", "-Werror=shift-count-overflow", "-Werror=return-type" },
        { "int f(int n){if(n)return missing(1);}int g(int n){if(n)return 1;}\n", NULL, "-std=gnu17", NULL, NULL },
        { "int f(int n){if(n)return missing(1);}int g(int n){if(n)return 1;}\n", NULL, "-std=gnu17", "-Wno-error=implicit-function-declaration", "-Werror=return-type" },
        { "int f(int n){if(n)return missing(1);}int g(int n){if(n)return 1;}\n", NULL, "-std=gnu89", "-Werror=implicit-function-declaration", "-Werror=return-type" },
        { "#include \"recovery-header.h\"\n", "#pragma GCC system_header\nstruct S{long a,b;};struct S f(int n,struct S s){if(n)return s;}\n", "-std=gnu17", NULL, NULL },
        { "#include \"recovery-header.h\"\n", "#pragma GCC system_header\nstruct S{long a,b;};struct S f(int n,struct S s){if(n)return s;}\n", "-std=gnu17", "-Wsystem-headers", NULL },
        { "#include \"recovery-header.h\"\n", "#pragma GCC system_header\nstruct S{long a,b;};struct S f(int n,struct S s){if(n)return s;}\n", "-std=gnu17", "-Wsystem-headers", "-Werror=return-type" },
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(returns); fixture += 1)
    {
        made = parse_test_write(source, returns[fixture].text, strlen(returns[fixture].text)) &&
               (returns[fixture].header == NULL || parse_test_write(header, returns[fixture].header, strlen(returns[fixture].header)));
        check_test_expect(test, made, "return publication and system-header source is available");
        for (uint32_t limit = 0; made && limit < 3; limit += 1)
        {
            char* arguments[16] = { (char*)clang, (char*)returns[fixture].standard, "-fsyntax-only", "-fno-color-diagnostics" };
            uint32_t used = 4;
            if (limit != 0)
            {
                arguments[used++] = (char*)limits[limit];
            }
            if (returns[fixture].first != NULL)
            {
                arguments[used++] = (char*)returns[fixture].first;
            }
            if (returns[fixture].second != NULL)
            {
                arguments[used++] = (char*)returns[fixture].second;
            }
            arguments[used++] = source;
            char* reference = NULL;
            const int oracle = check_test_driver_spawn(arguments, &reference);
            char expected_groups[32768];
            const bool expected_valid = check_test_driver_recovery_groups(reference, expected_groups, sizeof(expected_groups));
            const CheckDriverDiagnosticCounts expected = check_test_driver_diagnostic_counts(reference);
            free(reference);
            for (uint32_t route = 0; route < 4; route += 1)
            {
                static const char sentinel[] = "keep-return-rejected-output";
                const bool prior = parse_test_write(object, sentinel, sizeof(sentinel));
                char* checked[20] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), (char*)returns[fixture].standard };
                used = 2;
                if ((route & 1) != 0)
                {
                    checked[used++] = "--scalar";
                }
                checked[used++] = route < 2 ? "-fsyntax-only" : "-c";
                checked[used++] = "-o";
                checked[used++] = object;
                if (limit != 0)
                {
                    checked[used++] = (char*)limits[limit];
                }
                if (returns[fixture].first != NULL)
                {
                    checked[used++] = (char*)returns[fixture].first;
                }
                if (returns[fixture].second != NULL)
                {
                    checked[used++] = (char*)returns[fixture].second;
                }
                checked[used++] = source;
                char* text = NULL;
                const int status = check_test_driver_spawn(checked, &text);
                char actual_groups[32768];
                const bool actual_valid = check_test_driver_recovery_groups(text, actual_groups, sizeof(actual_groups));
                const CheckDriverDiagnosticCounts actual = check_test_driver_diagnostic_counts(text);
                char description[160];
                snprintf(description, sizeof(description), "return publication fixture=%u limit=%u route=%u keeps exact Clang groups and status", fixture, limit, route);
                check_test_expect(test, expected_valid && actual_valid && status == oracle && strcmp(expected_groups, actual_groups) == 0, description);
                check_test_expect(test, actual.errors == expected.errors && actual.warnings == expected.warnings && actual.fatal == expected.fatal &&
                                       !actual.after_fatal && strcmp(actual.summary, expected.summary) == 0,
                                  "return warning class, suppression and budget match the independent oracle");
                FILE* emitted = fopen(object, "rb");
                char bytes[sizeof(sentinel)] = { 0 };
                const uint64_t taken = emitted != NULL ? fread(bytes, 1, sizeof(bytes), emitted) : 0;
                const bool unchanged = taken == sizeof(bytes) && memcmp(bytes, sentinel, sizeof(bytes)) == 0 && emitted != NULL && fgetc(emitted) == EOF;
                const bool elf = taken >= 4 && memcmp(bytes, "\177ELF", 4) == 0;
                check_test_expect(test, prior && (status != 0 || route < 2 ? unchanged : elf), "return diagnostic-only work never publishes a rejected object");
                if (emitted != NULL)
                {
                    fclose(emitted);
                }
                free(text);
            }
        }
    }
    unlink(source);
    unlink(header);
    unlink(object);
    unlink(policy_header);
}
