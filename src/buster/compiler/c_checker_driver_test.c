// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// End-to-end source diagnostics and emission barriers. Every original file is independently checked by Clang;
// the driver runs in a child so its real diagnostic renderer and return status are observed together. Supplying
// --executable also tests the production command's default C route, independently of its explicit check command.

typedef struct CheckDriverFixture CheckDriverFixture;
struct CheckDriverFixture
{
    const char* name;
    const char* source;
    bool rejected;
    uint32_t warnings;
    bool quiet;
};

static bool check_test_driver_run(const char* executable, const char* source, const char* object, bool scalar, bool dump_static, bool quiet,
                                  CheckCorpusOracle* outcome, uint32_t* warnings)
{
    *outcome = (CheckCorpusOracle){ 0 };
    *warnings = 0;
    FILE* diagnostics = tmpfile();
    bool ready = diagnostics != NULL;
    int status = 0;
    pid_t child = -1;
    char* text = NULL;
    size_t length = 0;
    if (ready)
    {
        fflush(NULL);
        child = fork();
        if (child == 0)
        {
            const int output = open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (output < 0 || dup2(output, STDOUT_FILENO) < 0 || dup2(fileno(diagnostics), STDERR_FILENO) < 0)
            {
                _exit(126);
            }
            close(output);
            char* arguments[16] = { 0 };
            uint32_t count = 0;
            arguments[count++] = (char*)(executable != NULL ? executable : "checker");
            if (scalar)
            {
                arguments[count++] = "--scalar";
            }
            if (dump_static)
            {
                arguments[count++] = "--dump-static";
            }
            if (quiet)
            {
                arguments[count++] = "-w";
            }
            arguments[count++] = "-std=gnu17";
            arguments[count++] = "-c";
            arguments[count++] = "-o";
            arguments[count++] = (char*)object;
            arguments[count++] = (char*)source;
            if (executable != NULL)
            {
                execvp(executable, arguments);
                _exit(127);
            }
            const int code = run_check_command((int)count - 1, arguments + 1);
            fflush(NULL);
            _exit(code);
        }
        pid_t waited = -1;
        do
        {
            waited = child > 0 ? waitpid(child, &status, 0) : -1;
        }
        while (waited < 0 && child > 0 && errno == EINTR);
        ready = child > 0 && waited == child && WIFEXITED(status) && pp_test_read_output(diagnostics, &text, &length);
    }
    bool unlocated = false;
    for (char* at = text; ready && at != NULL && *at != 0;)
    {
        char* end = strchr(at, '\n');
        if (end != NULL)
        {
            *end = 0;
        }
        if (strstr(at, "error: ") != NULL)
        {
            uint32_t line = 0;
            uint32_t column = 0;
            const bool located = sscanf(at, "%*[^:]:%u:%u:", &line, &column) == 2 && line != 0 && column != 0;
            unlocated |= !located;
            if (located && !outcome->rejected)
            {
                outcome->rejected = true;
                outcome->line = line;
                outcome->column = column;
            }
        }
        *warnings += strstr(at, "warning: ") != NULL;
        at = end != NULL ? end + 1 : NULL;
    }
    ready = ready && !unlocated && ((WEXITSTATUS(status) == 0 && !outcome->rejected) || (WEXITSTATUS(status) == 1 && outcome->rejected));
    if (diagnostics != NULL)
    {
        ready = fclose(diagnostics) == 0 && ready;
    }
    free(text);
    return ready;
}

static void check_test_driver_diagnostic_capacity(CheckTest* test)
{
    Arena scratch = arena_reserve(64);
    const uint64_t capacity = scratch.capacity;
    uint64_t* sentinel = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint64_t, 1);
    check_test_expect(test, sentinel != NULL, "diagnostic rollback fixture storage is available");
    if (sentinel != NULL)
    {
        *sentinel = UINT64_C(0x73516A93);
        const uint64_t mark = scratch.length;
        Parser parser = { 0 };
        parser.tokens.count = 1;
        CheckInput input = { .report_count = 1 };
        Preprocessor pp = { 0 };
        for (uint32_t phase = 0; phase < 2; phase += 1)
        {
            scratch.capacity = mark + phase * 2 * sizeof(uint64_t);
            const bool ready = check_driver_reports(&parser, &pp, &input, NULL, NULL, NULL, 0, NULL, 0, false, &scratch);
            check_test_expect(test, !ready && scratch.length == mark && *sentinel == UINT64_C(0x73516A93) && pp.errors == 0,
                              "diagnostic allocation failure restores scratch and publishes no partial diagnostics");
        }
    }
    scratch.capacity = capacity;
    arena_release(&scratch);
}

static int run_checker_driver_tests(int argc, char** argv)
{
    CheckTest test = { 0 };
    const char* clang = "clang";
    const char* executable = NULL;
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[++i];
        }
        else if (strcmp(argv[i], "--executable") == 0 && i + 1 < argc)
        {
            executable = argv[++i];
        }
        else
        {
            options = false;
        }
    }
    check_test_expect(&test, options, "driver test options are recognized");
    check_test_driver_diagnostic_capacity(&test);
    const CheckDriverFixture fixtures[] =
    {
        {
            "valid functions and relocatable static storage",
            "static int value=7; int *pointer=&value; int f(int x){return x+value;}\n",
            false,
        },
        {
            "static function relocation shares the emitted object symbols",
            "static int helper(void){return 7;} int (*pointer)(void)=helper;\n",
            false,
        },
        {
            "invalid call before later body syntax error",
            "int f(void){\n1();\nint broken=;\n}\n",
            true,
        },
        {
            "invalid original DO body before missing condition",
            "int f(void){do{\n1();\n}\n}\n",
            true,
        },
        {
            "macro error before later body syntax error",
            "#define CALL(x) x()\nint f(void){CALL(1);\nint broken=;\n}\n",
            true,
        },
        {
            "static initializer before later graph error",
            "int *p[1]={\n1\n};\nint f(void){return 1();}\n",
            true,
        },
        {
            "automatic initializer before later missing name",
            "int f(void){\nint *p[1]={1};\nreturn missing;\n}\n",
            true,
        },
        {
            "assembly destination before later missing name",
            "int f(void){\n__asm__(\"\" : \"=r\"(1));\nreturn missing;\n}\n",
            true,
        },
        {
            "duplicate switch case before later missing name",
            "int f(int n){\nswitch(n){case 0:break;\ncase 0:break;}\nreturn missing;\n}\n",
            true,
        },
        {
            "GNU enum folding retains its declaration warning",
            "enum { N=2147483647+1 }; int f(void){return N;}\n",
            false,
            1,
        },
        {
            "GNU assertion folding retains its graph warning",
            "_Static_assert(2147483647+1<0,\"wrap\"); int f(void){return 0;}\n",
            false,
            1,
        },
        {
            "GNU bound warning appears once across reader and graph",
            "int a[(2147483647+1)<0?1:2]; int f(void){return 0;}\n",
            false,
            2,
        },
        {
            "diagnostic pragma suppresses integer overflow",
            "#pragma clang diagnostic ignored \"-Winteger-overflow\"\nenum { N=2147483647+1 }; int f(void){return N;}\n",
            false,
        },
        {
            "diagnostic pragma promotes integer overflow",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nenum { N=2147483647+1 }; int f(void){return N;}\n",
            true,
        },
        {
            "global warning suppression also suppresses promoted warning",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nenum { N=2147483647+1 }; int f(void){return N;}\n",
            false,
            0,
            true,
        },
        {
            "mixed dialect pop restores previous warning state",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\n#pragma GCC diagnostic push\n"
            "#pragma GCC diagnostic ignored \"-Winteger-overflow\"\nenum { A=2147483647+1 };\n"
            "#pragma clang diagnostic pop\nenum { B=2147483647+1 };\n",
            true,
        },
        {
            "explicit warning overrides earlier error state",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\n#pragma clang diagnostic warning \"-Winteger-overflow\"\n"
            "enum { A=2147483647+1 };\n",
            false,
            1,
        },
        {
            "fatal warning promotion blocks emission",
            "#pragma clang diagnostic fatal \"-Winteger-overflow\"\nenum { A=2147483647+1 };\n",
            true,
        },
        {
            "later pragma cannot suppress earlier overflow",
            "enum { A=2147483647+1 };\n#pragma clang diagnostic ignored \"-Winteger-overflow\"\n",
            false,
            1,
        },
        {
            "specific pragma overrides group suppression",
            "#pragma clang diagnostic ignored \"-Wincompatible-pointer-types\"\n"
            "#pragma clang diagnostic error \"-Wincompatible-pointer-types-discards-qualifiers\"\n"
            "const int *p; int *q; void f(void){q=p;}\n",
            true,
        },
        {
            "group pragma overrides earlier specific promotion",
            "#pragma clang diagnostic error \"-Wincompatible-pointer-types-discards-qualifiers\"\n"
            "#pragma clang diagnostic ignored \"-Wincompatible-pointer-types\"\n"
            "const int *p; int *q; void f(void){q=p;}\n",
            false,
        },
        {
            "initializer warning promotion blocks emission",
            "#pragma clang diagnostic error \"-Wexcess-initializers\"\nint a[1]={1,2};\n",
            true,
        },
        {
            "initializer warning suppression preserves object",
            "#pragma clang diagnostic ignored \"-Wexcess-initializers\"\nint a[1]={1,2};\n",
            false,
        },
        {
            "ordinary constraint stays before later promoted warning",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nint f(void){\n1();\nreturn 2147483647+1;\n}\n",
            true,
        },
        {
            "completed control constraint stays before later promoted warning",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nint f(int n){\nswitch(n){\ncase 0:break;\n"
            "case 0:break;\n}\nenum { A=2147483647+1 }; return A;\n}\n",
            true,
        },
        {
            "promoted warning stays before later ordinary constraint",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nenum { A=2147483647+1 };\nint f(void){return 1();}\n",
            true,
        },
        {
            "original syntax rejection cannot emit a repaired object",
            "int f(void){int broken=;}\n",
            true,
        },
        {
            "earlier original enum warning survives later syntax error",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\n"
            "int f(void){enum { A=2147483647+1 };\nint broken=;\n}\n",
            true,
        },
        {
            "later promoted initializer warning survives earlier ignored warning and graph error",
            "#pragma clang diagnostic ignored \"-Wexcess-initializers\"\nint a[1]={1,2};\n"
            "#pragma clang diagnostic error \"-Wexcess-initializers\"\nint b[1]={1,2};\n"
            "int f(void){return missing;}\n",
            true,
        },
        {
            "later promoted initializer warning survives planner semantic failure",
            "#pragma clang diagnostic ignored \"-Wexcess-initializers\"\nint a[1]={1,2};\n"
            "#pragma clang diagnostic error \"-Wexcess-initializers\"\nint b[1]={1,2};\n"
            "int *p[1]={1};\n",
            true,
        },
        {
            "fatal warning retains precedence over later parser error",
            "#pragma clang diagnostic fatal \"-Winteger-overflow\"\nenum { A=2147483647+1 };\n"
            "int f(void){int broken=;}\n",
            true,
        },
    };
    char directory[] = "/tmp/buster-checker-driver-XXXXXX";
    const bool temporary = options && mkdtemp(directory) != NULL;
    check_test_expect(&test, temporary, "driver test directory is available");
    const char* clang_arguments[] = { "-std=gnu17", "-w" };
    for (uint32_t i = 0; temporary && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        char description[384];
        snprintf(source, sizeof(source), "%s/%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/%u.o", directory, i);
        CheckCorpusOracle oracle = { 0 };
        const bool oracle_ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                                  check_corpus_clang_file(clang, clang_arguments, fixtures[i].quiet ? 2 : 1, source, &oracle);
        check_test_expect(&test, oracle_ready && oracle.rejected == fixtures[i].rejected, fixtures[i].name);
        const uint32_t schedules = executable != NULL ? 4 : 2;
        const uint32_t routes = schedules * 2;
        for (uint32_t route = 0; oracle_ready && route < routes; route += 1)
        {
            unlink(object);
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const bool external = route % schedules >= 2;
            const bool dump_static = route >= schedules;
            const bool ready = check_test_driver_run(external ? executable : NULL, source, object, (route & 1) != 0, dump_static,
                                                     fixtures[i].quiet, &checked, &warnings);
            snprintf(description, sizeof(description), "%s, %s %s%s: error line %u, Clang %u", fixtures[i].name,
                     external ? "default CLI" : "direct", (route & 1) != 0 ? "scalar" : "vector", dump_static ? " with static dump" : "", checked.line, oracle.line);
            check_test_expect(&test, ready && checked.rejected == oracle.rejected && checked.line == oracle.line, description);
            check_test_expect(&test, warnings == fixtures[i].warnings, "driver renders each semantic warning once");
            FILE* emitted = fopen(object, "rb");
            unsigned char magic[4] = { 0 };
            const bool elf = emitted != NULL && fread(magic, 1, sizeof(magic), emitted) == sizeof(magic) &&
                             memcmp(magic, "\177ELF", sizeof(magic)) == 0;
            check_test_expect(&test, oracle.rejected ? emitted == NULL : elf, "driver emits an ELF object only for accepted original source");
            if (emitted != NULL)
            {
                fclose(emitted);
            }
            if (fixtures[i].rejected && route == 0)
            {
                static const char preserved[] = "existing output remains intact";
                const bool written = parse_test_write(object, preserved, sizeof(preserved));
                const bool rejected = written && check_test_driver_run(NULL, source, object, false, false, fixtures[i].quiet, &checked, &warnings) && checked.rejected;
                FILE* prior = fopen(object, "rb");
                char bytes[sizeof(preserved) + 1] = { 0 };
                const size_t length = prior != NULL ? fread(bytes, 1, sizeof(bytes), prior) : 0;
                check_test_expect(&test, rejected && length == sizeof(preserved) && memcmp(bytes, preserved, sizeof(preserved)) == 0,
                                  "rejected original source preserves an existing output file");
                if (prior != NULL)
                {
                    fclose(prior);
                }
            }
        }
        unlink(object);
        unlink(source);
    }
    if (temporary)
    {
        rmdir(directory);
    }
    printf("checker-driver: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    return test.failures != 0;
}
