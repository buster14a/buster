// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included by c_preprocessor_test.c. Original fixtures, checked against the installed Clang as an external oracle.

// Scratch-buffer line numbers reflect each compiler's allocation order, not a source position. Normalize only those
// numbers, keeping their column, the note, and every real/presumed filename, line and column byte-for-byte.
static void pp_test_normalize_scratch(char* text)
{
    char* at = text;
    while (at != 0 && (at = strstr(at, "<scratch space>:")) != 0)
    {
        char* number = at + strlen("<scratch space>:");
        char* end = number;
        while (*end >= '0' && *end <= '9')
        {
            end += 1;
        }
        if (end > number)
        {
            *number = '1';
            memmove(number + 1, end, strlen(end) + 1);
        }
        at = number + 1;
    }
}

// Clang's diagnostics for `path`, preprocessed (or with `parser`, compiled) with `directory` as an -I directory.
static char* pp_test_clang_diagnostics(const char* path, const char* directory, bool parser)
{
    char* result = 0;
    FILE* errors = tmpfile();
    const pid_t child = errors != 0 ? fork() : -1;
    if (child == 0)
    {
        const int null = open("/dev/null", O_WRONLY);
        if (null < 0 || dup2(null, STDOUT_FILENO) < 0 || dup2(fileno(errors), STDERR_FILENO) < 0)
        {
            _exit(127);
        }
        close(null);
        unsetenv("CPATH");
        unsetenv("C_INCLUDE_PATH");
        execlp("clang", "clang", parser ? "-fsyntax-only" : "-E", "-std=gnu17", "-fno-caret-diagnostics", "-fno-diagnostics-show-option",
               "-fmacro-backtrace-limit=0", "-I", directory, path, (char*)0);
        _exit(127);
    }
    int status = 0;
    const bool ran = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 1;
    if (ran && fseek(errors, 0, SEEK_END) == 0)
    {
        const long length = ftell(errors);
        if (length >= 0 && length < (1 << 20) && fseek(errors, 0, SEEK_SET) == 0)
        {
            result = calloc((size_t)length + 1, 1);
            if (result != 0 && fread(result, 1, (size_t)length, errors) != (size_t)length)
            {
                free(result);
                result = 0;
            }
        }
    }
    if (errors != 0)
    {
        fclose(errors);
    }
    return result;
}

typedef struct PPHistoryCase PPHistoryCase;
struct PPHistoryCase
{
    const char* name;
    const char* source;
    const char* header;
    bool parser;
};

// A case's history.c (and history.h), with its directory as an -I directory too, by both kernels and with `with_clang`
// by clang: the diagnostics must be the same text.
static bool pp_test_history_case(const PPHistoryCase* test, const char* directory, bool with_clang)
{
    char path[4096];
    char header[4096];
    snprintf(path, sizeof(path), "%s/history.c", directory);
    snprintf(header, sizeof(header), "%s/history.h", directory);
    bool result = pp_test_write_file(path, test->source) && (test->header == 0 || pp_test_write_file(header, test->header));
    static Preprocessor runs[2];
    static InternTable tables[2];
    char* diagnostics[2] =
    {
        0,
    };
    const char* includes[] = { directory };
    for (uint32_t mode = 0; result && mode < 2; mode += 1)
    {
        size_t length = 0;
        FILE* stream = open_memstream(&diagnostics[mode], &length);
        const PPOptions options = {
            .language =
            {
                .standard = 2017,
                .gnu = true,
            },
            .scalar = mode != 0,
            .include_directories = includes,
            .include_directory_count = 1,
            .diagnostics = stream,
        };
        result = stream != 0 && intern_initialize(&tables[mode]) && pp_initialize(&runs[mode], &options, &tables[mode]);
        if (result)
        {
            const bool ran = pp_run(&runs[mode], path);
            result = ran == test->parser;
            if (test->parser)
            {
                uint32_t diagnosed = 0;
                for (uint32_t i = 0; i < runs[mode].output.count; i += 1)
                {
                    if (runs[mode].output.kinds[i] == C_TOKEN_RAW_IDENTIFIER && intern_length(runs[mode].table, runs[mode].output.values[i]) == 7 &&
                        memcmp(intern_bytes(runs[mode].table, runs[mode].output.values[i]), "missing", 7) == 0)
                    {
                        pp_diagnose_output(&runs[mode], PP_SEVERITY_ERROR, i, "use of undeclared identifier 'missing'");
                        diagnosed += 1;
                    }
                }
                result = result && diagnosed != 0;
            }
        }
        if (stream != 0)
        {
            fclose(stream);
        }
    }
    result = result && pp_same_streams(&runs[0], &runs[1], test->name) && diagnostics[0] != 0 && diagnostics[1] != 0 && strcmp(diagnostics[0], diagnostics[1]) == 0;
    char* oracle = result && with_clang ? pp_test_clang_diagnostics(path, directory, test->parser) : 0;
    if (result && with_clang)
    {
        pp_test_normalize_scratch(diagnostics[0]);
        pp_test_normalize_scratch(oracle);
        result = oracle != 0 && strcmp(diagnostics[0], oracle) == 0;
    }
    if (!result)
    {
        printf("history %s differs:\nours:\n%sclang:\n%s\n", test->name, diagnostics[0] != 0 ? diagnostics[0] : "<failed>", oracle != 0 ? oracle : "<not run>");
    }
    free(oracle);
    for (uint32_t mode = 0; mode < 2; mode += 1)
    {
        free(diagnostics[mode]);
        pp_release(&runs[mode]);
        intern_release(&tables[mode]);
    }
    unlink(path);
    unlink(header);
    return result;
}

// Runs and lookups are tested independently of macro execution: arbitrary origins, all vector tails, repeated runs,
// appended chunks, queries outside the output, and inputs ending at an unreadable page.
static bool pp_test_history_runs(void)
{
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, 3 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool result = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0;
    PPHistoryRun vector_runs[1025];
    PPHistoryRun scalar_runs[1025];
    static Preprocessor vector;
    static Preprocessor scalar;
    vector = (Preprocessor)
    {
        .vectorized = true,
        .history_runs = vector_runs,
        .history_run_capacity = BUSTER_ARRAY_LENGTH(vector_runs),
    };
    scalar = (Preprocessor)
    {
        .history_runs = scalar_runs,
        .history_run_capacity = BUSTER_ARRAY_LENGTH(scalar_runs),
    };
    for (uint32_t length = 0; result && length <= 200; length += 1)
    {
        uint32_t* origins = (uint32_t*)(mapping + 2 * page) - length;
        for (uint32_t i = 0; i < length; i += 1)
        {
            origins[i] = (i * 71 + length * 23) % 17 < 9 ? i / 7 : 0;
        }
        const PPTokens source =
        {
            .origins = origins,
            .count = length,
        };
        vector.history_run_count = 0;
        scalar.history_run_count = 0;
        vector.output.count = length;
        scalar.output.count = length;
        // Splitting a copy at any lane boundary must still produce maximal runs.
        const uint32_t middle = length / 2;
        pp_history_copy(&vector, &vector.output, 0, &source, 0, middle);
        pp_history_copy(&vector, &vector.output, middle, &source, middle, length);
        pp_history_copy(&scalar, &scalar.output, 0, &source, 0, length);
        result = vector.history_run_count == scalar.history_run_count && memcmp(vector_runs, scalar_runs, vector.history_run_count * sizeof(PPHistoryRun)) == 0;
        uint32_t positions[256];
        uint32_t answers[256];
        uint32_t twin[256];
        for (uint32_t i = 0; i < length + 2; i += 1)
        {
            positions[i] = i == length + 1 ? UINT32_MAX : i;
        }
        pp_history_lookup(&vector, positions, answers, length + 2, false);
        pp_history_lookup(&scalar, positions, twin, length + 2, true);
        for (uint32_t i = 0; i < length + 2; i += 1)
        {
            result = result && answers[i] == twin[i] && answers[i] == (i < length ? origins[i] : 0);
        }
        for (uint32_t i = 1; i < vector.history_run_count; i += 1)
        {
            result = result && vector_runs[i - 1].token < vector_runs[i].token && vector_runs[i - 1].origin != vector_runs[i].origin;
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 3 * page);
    }
    printf("history runs: %s (201 lengths, split copies, scalar/vector queries, guard page)\n", result ? "pass" : "FAIL");
    return result;
}

static bool pp_test_history_lifetimes(const char* directory)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/history-lifetimes.c", directory);
    const char* source = "#define TWICE(x) x x\n#define S(x) #x\n#define CAT(a,b) a##b\n#line 31 \"before.c\"\n"
                         "TWICE(__LINE__) S(a b c) CAT(he,llo) TWICE(7) __COUNTER__ __COUNTER__\n"
                         "#undef TWICE\n#define TWICE(x) wrong\n";
    bool result = pp_test_write_file(path, source);
    static Preprocessor runs[2];
    static InternTable tables[2];
    for (uint32_t i = 0; result && i < 2; i += 1)
    {
        const PPOptions options = {
            .language =
            {
                .standard = 2017,
                .gnu = true,
            },
            .no_history = i != 0,
        };
        result = pp_test_run(&runs[i], &tables[i], &options, path);
    }
    if (result)
    {
        Preprocessor* pp = &runs[0];
        const PPTokens* a = &pp->output;
        const PPTokens* b = &runs[1].output;
        result = a->origins == 0 && b->origins == 0 && a->count == 8 && a->count == b->count && memcmp(a->kinds, b->kinds, a->count) == 0 &&
                 memcmp(a->flags, b->flags, a->count) == 0 && memcmp(a->values, b->values, a->count * sizeof(uint32_t)) == 0 &&
                 memcmp(a->locations, b->locations, a->count * sizeof(uint32_t)) == 0 && runs[1].history_count == 0 && runs[1].history_run_count == 0;
        const uint32_t first = pp_history_at(pp, 0);
        const uint32_t second = pp_history_at(pp, 1);
        result = result && first != second && (first & ~BUSTER_PP_ORIGIN_INDEX) == BUSTER_PP_ORIGIN_ARGUMENT &&
                 (second & ~BUSTER_PP_ORIGIN_INDEX) == BUSTER_PP_ORIGIN_ARGUMENT;
        if (result)
        {
            const PPSubstitution* x = &pp->substitutions[(first & BUSTER_PP_ORIGIN_INDEX) - 1];
            const PPSubstitution* y = &pp->substitutions[(second & BUSTER_PP_ORIGIN_INDEX) - 1];
            const PPExpansion* expansion = &pp->history[x->expansion - 1];
            result = x->source == y->source && x->parameter != y->parameter && x->expansion == y->expansion &&
                     x->source != 0 && (x->source & ~BUSTER_PP_ORIGIN_INDEX) == 0 && pp->history[x->source - 1].builtin == PP_BUILTIN_LINE &&
                     expansion->definition != pp->macros[pp->macro_of[expansion->name]].location &&
                     memcmp(pp_location_bytes(pp, expansion->begin), "TWICE(", 6) == 0 && *pp_location_bytes(pp, expansion->end) == ')';
        }
        const uint32_t string_id = pp_history_at(pp, 2);
        const uint32_t paste_id = pp_history_at(pp, 3);
        result = result && (string_id & ~BUSTER_PP_ORIGIN_INDEX) == BUSTER_PP_ORIGIN_GENERATED &&
                 (paste_id & ~BUSTER_PP_ORIGIN_INDEX) == BUSTER_PP_ORIGIN_GENERATED;
        if (result)
        {
            const PPGenerated* string = &pp->generated[(string_id & BUSTER_PP_ORIGIN_INDEX) - 1];
            const PPGenerated* paste = &pp->generated[(paste_id & BUSTER_PP_ORIGIN_INDEX) - 1];
            result = string->kind == PP_GENERATED_STRING && string->count == 3 && paste->kind == PP_GENERATED_PASTE && paste->count == 2;
            for (uint32_t i = 0; result && i < string->count; i += 1)
            {
                result = *pp_location_bytes(pp, pp->history_sources[string->first + i].location) == 'a' + i;
            }
            result = result && memcmp(pp_location_bytes(pp, pp->history_sources[paste->first].location), "he", 2) == 0 &&
                     memcmp(pp_location_bytes(pp, pp->history_sources[paste->first + 1].location), "llo", 3) == 0;
        }
        for (uint32_t token = 6; result && token < 8; token += 1)
        {
            const uint32_t id = pp_history_at(pp, token);
            result = id != 0 && (id & ~BUSTER_PP_ORIGIN_INDEX) == 0;
            if (result)
            {
                result = pp->history[id - 1].builtin == PP_BUILTIN_COUNTER && (token == 6 || id != pp_history_at(pp, token - 1));
            }
        }
    }
    for (uint32_t i = 0; i < 2; i += 1)
    {
        pp_release(&runs[i]);
        intern_release(&tables[i]);
    }
    unlink(path);
    printf("history lifetimes: %s (redefinition, repeated parameters, retained generated inputs, builtins, recording control)\n", result ? "pass" : "FAIL");
    return result;
}

// Exceed every initial history slice, including all three temporary columns. Repeated arguments also read earlier
// origins after their owning buffer moves, and stringification must retain its source records after that buffer dies.
static bool pp_test_history_growth(const char* directory, bool with_clang)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/history-growth.c", directory);
    FILE* file = fopen(path, "w");
    bool result = file != 0;
    if (result)
    {
        fputs("#define I(x) x x\n#define A 1\n#define S(x) #x\n#define C(a,b) a##b\nI(", file);
        for (uint32_t i = 0; i < 1100; i += 1)
        {
            fputs(" A", file);
        }
        fputs(") S(", file);
        for (uint32_t i = 0; i < 1100; i += 1)
        {
            fputs(" a", file);
        }
        fputs(")\n", file);
        for (uint32_t i = 0; i < 600; i += 1)
        {
            fputs("C(mis,sing)\n", file);
        }
        result = !ferror(file);
        result = fclose(file) == 0 && result;
    }
    const PPOptions options =
    {
        .language =
        {
            .standard = 2017,
            .gnu = true,
        },
    };
    char* arguments[] =
    {
        "-std=gnu17",
    };
    uint64_t tokens = 0;
    result = result && pp_test_file(1, arguments, 0, &options, path, with_clang, directory, &tokens) && tokens == 2801;
    unlink(path);
    printf("history growth: %s (all eight initial slices, repeated arguments, generated sources)\n", result ? "pass" : "FAIL");
    return result;
}

static bool pp_test_history(const char* directory, bool with_clang)
{
    static const PPHistoryCase cases[] = {
        {
            .name = "object",
            .source = "#define OBJ \"bad\"\n#if OBJ\n#endif\n",
        },
        {
            .name = "function",
            .source = "#define FUN(x) \"bad\"\n#if FUN(1)\n#endif\n",
        },
        {
            .name = "argument",
            .source = "#define ID(x) x\n#if ID(\"bad\")\n#endif\n",
        },
        {
            .name = "nested",
            .source = "#define INNER \"bad\"\n#define OUT INNER\n#if OUT\n#endif\n",
        },
        {
            .name = "argument prescan",
            .source = "#define OBJ \"bad\"\n#define ID(x) x\n#define OUT(x) ID(x)\n#if OUT(OBJ)\n#endif\n",
        },
        {
            .name = "paste",
            .source = "#define CAT(a,b) a##b\n#if CAT(1,.2)\n#endif\n",
        },
        {
            .name = "stringification",
            .source = "#define S(x) #x\n#if S(a b)\n#endif\n",
        },
        {
            .name = "invalid paste",
            .source = "#define P(a,b) a##b\n#define OUT P(+,*)\nOUT\n",
        },
        {
            .name = "arity",
            .source = "#define BAD(x,y) x\n#define OUT BAD(1)\nOUT\n",
        },
        {
            .name = "arity across contexts",
            .source = "#define BAD(x,y) x\n#define NAME BAD\nNAME(1)\n",
        },
        {
            .name = "extra arguments",
            .source = "#define BAD(x) x\n#define OUT BAD(1,2)\nOUT\n",
        },
        {
            .name = "line/include",
            .source = "#line 30 \"caller.c\"\n#include \"history.h\"\n",
            .header = "#define OBJ \"bad\"\n#line 70 \"renamed.h\"\n#if OBJ\n#endif\n",
        },
        {
            .name = "output/redefinition",
            .source = "#define OBJ missing\nint a=OBJ;\n#undef OBJ\n#define OBJ missing\nint b=OBJ;\n",
            .parser = true,
        },
        {
            .name = "output/nested",
            .source = "#define OBJ missing\n#define OUT OBJ\nint a=OUT;\n",
            .parser = true,
        },
        {
            .name = "output/argument",
            .source = "#define ID(x) x\n#define OBJ missing\nint a=ID(OBJ);\n",
            .parser = true,
        },
        {
            .name = "output/paste",
            .source = "#define CAT(a,b) a##b\nint a=CAT(mis,sing);\n",
            .parser = true,
        },
        {
            .name = "empty paste left",
            .source = "#define CAT(a,b) a##b\nint a=CAT(,missing);\n",
            .parser = true,
        },
        {
            .name = "empty paste right",
            .source = "#define CAT(a,b) a##b\nint a=CAT(missing,);\n",
            .parser = true,
        },
        {
            .name = "cross-context invocation",
            .source = "#define ALIAS F\n#define F(x) missing\nint a=ALIAS(1);\n",
            .parser = true,
        },
        {
            .name = "argument macro name",
            .source = "#define CALL(f) f()\n#define F() missing\nint a=CALL(F);\n",
            .parser = true,
        },
        {
            .name = "forwarded function",
            .source = "#define ID(x) x\n#define F(x) missing\nint a=ID(F)(1);\n",
            .parser = true,
        },
        {
            .name = "builtin",
            .source = "#define FILE_NAME __FILE__\n#if FILE_NAME\n#endif\n",
        },
        {
            .name = "empty stringification",
            .source = "#define S(x) #x\n#if S()\n#endif\n",
        },
        {
            .name = "header definition",
            .source = "#include \"history.h\"\nint a=OBJ;\n",
            .header = "#define OBJ missing\n",
            .parser = true,
        },
        {
            .name = "header errors",
            .source = "#include \"history.h\"\n",
            .header = "#define OBJ \"bad\"\n#if OBJ\n#endif\n#if OBJ\n#endif\n",
        },
        {
            .name = "nested include stack",
            .source = "#define STEP\n#include \"history.h\"\n",
            .header = "#ifdef STEP\n#undef STEP\n#include \"history.h\"\n#else\n#define OBJ \"bad\"\n#if OBJ\n#endif\n#endif\n",
        },
        {
            .name = "nested argument name",
            .source = "#define CALL(f) f()\n#define OUT(x) CALL(x)\n#define F() missing\nint a=OUT(F);\n",
            .parser = true,
        },
        {
            .name = "CRLF",
            .source = "#define OBJ \"bad\"\r\n#line 22 \"logical.c\"\r\n#if OBJ\r\n#endif\r\n",
        },
        // #254's review: header names written out that are too long for a path (clang's fatal error about the first
        // path the search tries, and #embed's file not found), the header refused at the depth limit (one error, the
        // includes of it after that left out: the main file includes itself to the limit), and the limit at a name a
        // macro made (at its expansion, with the macro's note).
        {
            .name = "include: an angled name too long for a path",
            .source = "#define " BUSTER_PP_TEST_A4096 " history.h\n#include <" BUSTER_PP_TEST_A4096 ">\n",
            .header = "int found;\n",
        },
        {
            .name = "include: a quoted name too long for a path",
            .source = "#include \"" BUSTER_PP_TEST_A4096 "\"\n",
        },
        // A name with a part longer than a file name may be (NAME_MAX), in a path shorter than PATH_MAX: the open fails
        // (ENAMETOOLONG), and the file cache's probe keeps the error for the search to report. And an absolute name too
        // long for a path, which is the one place searched, not a name in the includer's directory.
        {
            .name = "include: a name with a part longer than NAME_MAX",
            .source = "#include \"" BUSTER_PP_TEST_A512 ".h\"\n",
        },
        {
            .name = "include: an absolute name too long for a path",
            .source = "#include \"/" BUSTER_PP_TEST_A4096 "\"\n",
        },
        {
            .name = "embed: an angled name too long for a path",
            .source = "#define " BUSTER_PP_TEST_A4096 " history.h\n#embed <" BUSTER_PP_TEST_A4096 ">\n",
            .header = "int found;\n",
        },
        // Extra tokens after a name written out are a warning at the first of them.
        {
            .name = "include: extra tokens after a name written out",
            .source = "#include \"history.h\" junk\n#include <history.h> more junk\nint a = missing;\n",
            .header = "int found;\n",
            .parser = true,
        },
        {
            .name = "include: the header refused at the depth limit",
            .source = "#if __INCLUDE_LEVEL__ < 199\n#include \"history.c\"\n#else\n#include \"history.h\"\n#include \"history.h\"\n#endif\n#include \"history.h\"\n",
            .header = "int leaf;\n",
        },
        {
            .name = "include: the depth limit at a name a macro made",
            .source = "#define H \"history.h\"\n#include H\n",
            .header = "#include H\n",
        },
    };
    uint32_t failures = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        failures += !pp_test_history_case(&cases[i], directory, with_clang);
    }
    printf("history diagnostics: %u of %lu fail%s\n", failures, (unsigned long)BUSTER_ARRAY_LENGTH(cases), with_clang ? " (Clang oracle)" : "");
    const bool runs = pp_test_history_runs();
    const bool lifetimes = pp_test_history_lifetimes(directory);
    const bool growth = pp_test_history_growth(directory, with_clang);
    return runs && lifetimes && growth && failures == 0;
}
