// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The preprocessor tests (c_preprocessor.c), for the test-preprocessor command:
//
//   build/compiler test-preprocessor [--clang] [--keep] [clang-style options] [file...]
//
// Without files, it runs the built-in cases (pp_test_cases): small sources on the topics of the C standard's examples
// (6.10.4.5), the cases where GCC's and clang's behavior was probed (docs/c-preprocessor.md), and inputs past the
// vectorized kernels' limits, each written to a new directory under $TMPDIR (or /tmp) and preprocessed by the vectorized and the
// scalar kernels, whose output streams must be the same token for token (kind, flags, value and location), whose
// directive tables must be the same (pp_same_directive_tables), whose tokens' spellings must be the expected ones, and
// which must give the expected number of warnings. With --clang, each case is also preprocessed by `clang -E -P` and
// must give the same spellings.
//
// With files, each is preprocessed with the options (-I, -isystem, -D, -std=, -ffreestanding, ... as for -E) by both
// kernels, which must agree, and with --clang compared with `clang -E -P` and the same options: our side then reads
// clang's own predefines (`clang -dM -E` with the options, as --predefines), so that both start from the same macros.
// The first difference is printed with the tokens around it. In both, the vectorized printer's text must be the scalar
// printer's byte for byte, with line markers and without, and must lex back into the stream's tokens.
//
// main.c includes this file after c_preprocessor.c. Layout: the token spellings of a stream and of a text, the
// comparison, the cases, clang's side (pp_test_clang), and run_preprocessor_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define BUSTER_PP_TEST_CONTEXT 6
#define BUSTER_PP_TEST_MAX_OUTPUT (1ull << 32)

// A sequence of token spellings: the bytes, and each token's offset and length.
typedef struct PPSpellings PPSpellings;
struct PPSpellings
{
    Arena arena;
    uint8_t* bytes;
    uint64_t length;
    uint64_t* offsets;
    uint32_t* lengths;
    uint32_t* locations; // The stream's locations (0 for a text's tokens).
    uint64_t count;
    uint64_t capacity;
};

static bool pp_spellings_initialize(PPSpellings* spellings, uint64_t capacity, uint64_t bytes)
{
    spellings->arena = arena_reserve(capacity * 16 + bytes + 1024);
    spellings->offsets = arena_push_array(&spellings->arena, capacity, sizeof(uint64_t), 64);
    spellings->lengths = arena_push_array(&spellings->arena, capacity, sizeof(uint32_t), 64);
    spellings->locations = arena_push_array(&spellings->arena, capacity, sizeof(uint32_t), 64);
    spellings->bytes = arena_push(&spellings->arena, bytes, 64);
    spellings->length = 0;
    spellings->count = 0;
    spellings->capacity = capacity;
    return spellings->offsets != 0 && spellings->lengths != 0 && spellings->locations != 0 && spellings->bytes != 0;
}

static void pp_spellings_add(PPSpellings* spellings, const uint8_t* bytes, uint32_t length, uint32_t location)
{
    if (spellings->count < spellings->capacity)
    {
        memcpy(spellings->bytes + spellings->length, bytes, length);
        spellings->offsets[spellings->count] = spellings->length;
        spellings->lengths[spellings->count] = length;
        spellings->locations[spellings->count] = location;
        spellings->length += length;
        spellings->count += 1;
    }
}

// The spellings of a preprocessor's output stream (without the pragmas' end markers).
static bool pp_stream_spellings(Preprocessor* pp, PPSpellings* spellings)
{
    const PPTokens* out = &pp->output;
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < out->count; i += 1)
    {
        const uint8_t kind = out->kinds[i];
        bytes += kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END) ? intern_length(pp->table, out->values[i])
                 : kind == BUSTER_PP_KIND_PRAGMA_END ? 0 : out->values[i] - out->locations[i];
    }
    bool result = pp_spellings_initialize(spellings, (uint64_t)out->count + 1, bytes + 64);
    Scratch scratch = scratch_begin(0);
    uint8_t* buffer = arena_push(scratch.arena, bytes + 64, 1);
    result = result && buffer != 0;
    for (uint32_t i = 0; result && i < out->count; i += 1)
    {
        if (out->kinds[i] != BUSTER_PP_KIND_PRAGMA_END)
        {
            const uint8_t* spelling;
            const uint32_t length = pp_spelling(pp, out->kinds[i], out->values[i], out->locations[i], buffer, &spelling);
            pp_spellings_add(spellings, spelling, length, out->locations[i]);
        }
    }
    scratch_end(scratch);
    return result;
}

// The spellings of a text's tokens (as c_tokenize lexes it).
static bool pp_text_spellings(const uint8_t* text, uint64_t length, PPSpellings* spellings)
{
    const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY(length);
    Arena arena = arena_reserve(capacity * 9 + 256);
    uint8_t* kinds = arena_push(&arena, capacity, 64);
    uint32_t* starts = arena_push_array(&arena, capacity, sizeof(uint32_t), 64);
    uint32_t* ends = arena_push_array(&arena, capacity, sizeof(uint32_t), 64);
    const int64_t count = kinds != 0 && starts != 0 && ends != 0 ? c_tokenize(text, length, kinds, starts, ends, 0) : -1;
    bool result = count >= 0 && pp_spellings_initialize(spellings, (uint64_t)count + 1, length + 64);
    for (int64_t i = 0; result && i < count; i += 1)
    {
        pp_spellings_add(spellings, text + starts[i], ends[i] - starts[i], 0);
    }
    arena_release(&arena);
    return result;
}

// Compares two spelling sequences; prints the first difference with the tokens around it (and where ours came from).
static bool pp_compare_spellings(Preprocessor* pp, const PPSpellings* ours, const PPSpellings* theirs, const char* name, const char* their_name)
{
    uint64_t i = 0;
    while (i < ours->count && i < theirs->count && ours->lengths[i] == theirs->lengths[i] &&
           memcmp(ours->bytes + ours->offsets[i], theirs->bytes + theirs->offsets[i], ours->lengths[i]) == 0)
    {
        i += 1;
    }
    const bool result = i == ours->count && i == theirs->count;
    if (!result)
    {
        printf("%s: token %lu differs (%lu tokens, %s %lu)\n", name, (unsigned long)i, (unsigned long)ours->count, their_name, (unsigned long)theirs->count);
        const uint64_t first = i > BUSTER_PP_TEST_CONTEXT ? i - BUSTER_PP_TEST_CONTEXT : 0;
        for (int side = 0; side < 2; side += 1)
        {
            const PPSpellings* spellings = side == 0 ? ours : theirs;
            printf("  %-6s", side == 0 ? "ours" : their_name);
            for (uint64_t k = first; k < spellings->count && k < i + BUSTER_PP_TEST_CONTEXT; k += 1)
            {
                printf("%s%.*s", k == i ? " >>" : " ", (int)(spellings->lengths[k] < 60 ? spellings->lengths[k] : 60), spellings->bytes + spellings->offsets[k]);
            }
            printf("\n");
        }
        if (pp != 0 && i < ours->count && ours->locations[i] != 0)
        {
            const uint32_t range = pp_find_range(pp, ours->locations[i]);
            if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
            {
                uint32_t column;
                const uint32_t line = pp_line_of(pp, range, ours->locations[i], &column);
                printf("  ours is spelled at %s:%u:%u\n", pp->files[pp->ranges[range].file].path, line, column);
            }
        }
    }
    return result;
}

// Are two runs' output streams the same, token for token?
static bool pp_same_streams(const Preprocessor* a, const Preprocessor* b, const char* name)
{
    const PPTokens* x = &a->output;
    const PPTokens* y = &b->output;
    uint32_t i = 0;
    while (i < x->count && i < y->count && x->kinds[i] == y->kinds[i] && x->flags[i] == y->flags[i] && x->values[i] == y->values[i] &&
           x->locations[i] == y->locations[i])
    {
        i += 1;
    }
    const bool result = i == x->count && i == y->count;
    if (!result)
    {
        printf("%s: the vectorized and scalar kernels differ at token %u of %u/%u", name, i, x->count, y->count);
        if (i < x->count && i < y->count)
        {
            printf(" (kind %u/%u, flags %u/%u, value %u/%u, location %u/%u)", x->kinds[i], y->kinds[i], x->flags[i], y->flags[i], x->values[i], y->values[i],
                   x->locations[i], y->locations[i]);
        }
        printf("\n");
    }
    return result;
}

// Do two runs' files have the same directive tables (kinds, line ends, conditional pairing and guards)? The runs read
// the same files in the same order, so their file data line up.
static bool pp_same_directive_tables(const Preprocessor* a, const Preprocessor* b, const char* name)
{
    bool result = a->data_count == b->data_count;
    for (uint32_t f = 0; result && f < a->data_count; f += 1)
    {
        const PPFileData* x = &a->datas[f];
        const PPFileData* y = &b->datas[f];
        result = x->prepared == y->prepared && x->directive_count == y->directive_count && x->guard == y->guard;
        for (uint32_t d = 0; result && x->prepared && d < x->directive_count; d += 1)
        {
            result = x->directive_hash[d] == y->directive_hash[d] && x->directive_end[d] == y->directive_end[d] &&
                     x->directive_kind[d] == y->directive_kind[d] && x->directive_next[d] == y->directive_next[d];
            if (!result)
            {
                printf("%s: the kernels' directive tables differ in file %u at directive %u (kind %u/%u, next %u/%u)\n", name, f, d,
                       x->directive_kind[d], y->directive_kind[d], x->directive_next[d], y->directive_next[d]);
            }
        }
    }
    if (!result && a->data_count != b->data_count)
    {
        printf("%s: the kernels read %u and %u files\n", name, a->data_count, b->data_count);
    }
    return result;
}

// Preprocesses `path` with `options`, in a fresh table: `pp` must be released by the caller. Returns whether it ran
// without errors.
static bool pp_test_run(Preprocessor* pp, InternTable* table, const PPOptions* options, const char* path)
{
    bool result = intern_initialize(table) && pp_initialize(pp, options, table);
    if (result)
    {
        result = pp_run(pp, path);
    }
    return result;
}

// The text one of the printers gives for a run, in memory (null if it fails); `free` it.
static char* pp_test_print(Preprocessor* pp, bool vector, bool markers, size_t* length)
{
    char* result = 0;
    *length = 0;
    FILE* stream = open_memstream(&result, length);
    const bool printed = stream != 0 && (vector ? pp_print_vector(pp, stream, markers) : pp_print_scalar(pp, stream, markers));
    if (stream != 0)
    {
        fclose(stream);
    }
    if (!printed)
    {
        free(result);
        result = 0;
    }
    return result;
}

// The text -E prints for a run, which must lex into the stream's spellings; the vectorized printer's must be the scalar
// printer's byte for byte, with line markers and without.
static bool pp_test_printed(Preprocessor* pp, const PPSpellings* ours, const char* name)
{
    size_t length = 0;
    char* text = pp_test_print(pp, true, false, &length);
    bool result = text != 0;
    for (uint32_t markers = 0; result && markers < 2; markers += 1)
    {
        size_t vector_length = 0;
        size_t scalar_length = 0;
        char* vector_text = pp_test_print(pp, true, markers != 0, &vector_length);
        char* scalar_text = pp_test_print(pp, false, markers != 0, &scalar_length);
        result = vector_text != 0 && scalar_text != 0 && vector_length == scalar_length && memcmp(vector_text, scalar_text, vector_length) == 0;
        if (!result)
        {
            size_t at = 0;
            while (vector_text != 0 && scalar_text != 0 && at < vector_length && at < scalar_length && vector_text[at] == scalar_text[at])
            {
                at += 1;
            }
            printf("%s: the printers differ%s at byte %zu of %zu/%zu\n", name, markers ? " with line markers" : "", at, vector_length, scalar_length);
        }
        free(vector_text);
        free(scalar_text);
    }
    PPSpellings printed = { 0 };
    result = result && pp_text_spellings((const uint8_t*)text, length, &printed);
    if (result)
    {
        char label[256];
        snprintf(label, sizeof(label), "%s (printed)", name);
        result = pp_compare_spellings(pp, ours, &printed, label, "-E");
    }
    arena_release(&printed.arena);
    free(text);
    return result;
}

// Runs `clang -E -P` with the options' arguments on `path`: its output, or null. Also writes clang's predefines for the
// same arguments to `predefines_path` (when it isn't null), as `clang -dM -E` gives them.
static char* pp_test_clang(char** arguments, int argument_count, const char* path, const char* predefines_path, size_t* length)
{
    char* result = 0;
    *length = 0;
    // The command: env -u CPATH clang [arguments] -E -P path. The arguments are passed through a temporary argv.
    for (int pass = predefines_path != 0 ? 0 : 1; pass < 2; pass += 1)
    {
        int pipe_fds[2];
        if (pipe(pipe_fds) != 0)
        {
            break;
        }
        const pid_t child = fork();
        if (child == 0)
        {
            char* argv[256];
            int n = 0;
            argv[n++] = "clang";
            for (int i = 0; i < argument_count && n < 240; i += 1)
            {
                argv[n++] = arguments[i];
            }
            argv[n++] = "-E";
            if (pass == 0)
            {
                argv[n++] = "-dM";
                argv[n++] = "-x";
                argv[n++] = "c";
                argv[n++] = "/dev/null";
            }
            else
            {
                argv[n++] = "-P";
                argv[n++] = (char*)path;
            }
            argv[n] = 0;
            unsetenv("CPATH");
            unsetenv("C_INCLUDE_PATH");
            dup2(pipe_fds[1], STDOUT_FILENO);
            close(pipe_fds[0]);
            close(pipe_fds[1]);
            execvp("clang", argv);
            _exit(127);
        }
        close(pipe_fds[1]);
        FILE* stream = fdopen(pipe_fds[0], "r");
        char* text = 0;
        size_t text_length = 0;
        FILE* collected = open_memstream(&text, &text_length);
        char chunk[1 << 16];
        size_t n;
        while (stream != 0 && collected != 0 && (n = fread(chunk, 1, sizeof(chunk), stream)) > 0)
        {
            fwrite(chunk, 1, n, collected);
        }
        if (stream != 0)
        {
            fclose(stream);
        }
        if (collected != 0)
        {
            fclose(collected);
        }
        int status = 0;
        waitpid(child, &status, 0);
        if (pass == 0)
        {
            FILE* out = fopen(predefines_path, "w");
            if (out != 0 && text != 0)
            {
                fwrite(text, 1, text_length, out);
                fclose(out);
            }
            free(text);
        }
        else
        {
            result = text;
            *length = text_length;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The cases

typedef struct PPTestCase PPTestCase;
struct PPTestCase
{
    const char* name;
    const char* source;
    // Another file the source can include as "other.h", or null.
    const char* other;
    const char* standard;
    // The expected output's text (compared token by token).
    const char* expected;
    // Whether the case is meant to fail (an error), so that the expected output isn't compared.
    bool fails;
    // The warnings each run must give (redefinitions).
    uint32_t warnings;
};

static const PPTestCase pp_test_cases[] = {
    // The topics of C 6.10.4.5's examples, in cases of our own: rescanning and redefinitions, # and ## with indirection,
    // placemarkers, and variadic macros (the expected text is clang's output).
    { "rescanning, redefinitions and painting", "#define n 4\n#define twice(v) twice(n + (v))\n#undef n\n#define n 5\n#define alias twice\n#define box box{1}\n#define open alias(-\n#define call(f) f(k)\n#define k 7,8\n#define id(v) v\n#define kw() long\n#define one(v) v\n#define join(p,q) p ## q\n#define quote(v) # v\ntwice(a-1) * twice(twice(box)) + id(id(alias)(2) - id)(3);\nalias(n*(9,10)-k) & open 6) | call\n(twice)^call(call);\nkw() arr[one()] = { one(4), join(5,6), join(7,), join(,8), join(,) };\nchar s[2][4] = { quote(abc), quote() };\n",
      0, "c17", "twice(5 + (a-1)) * twice(5 + (twice(5 + (box{1})))) + twice(5 + (2)) - id(3);\ntwice(5 + (5*(9,10)-7,8)) & twice(5 + (- 6)) | twice(5 + (7,8))^call(7,8);\nlong arr[] = { 4, 56, 7, 8, };\nchar s[2][4] = { \"abc\", \"\" };\n", false },
    { "# and ## with indirection", "#define quote(v) # v\n#define xquote(v) quote(v)\n#define trace(a, b) log(\"v\" # a \" is %d, v\" # b \" is %s\", \\\n v ## a, v ## b)\n#define PART_OF(n) part ## n\n#define cat(a, b) a ## b\n#define xcat(a, b) cat(a, b)\n#define UPDOWN \"top\"\n#define DOWN DOWN \" bottom\"\ntrace(3, 4);\nputs(quote(strcmp(\"a\\0b\", \"a\", '\\5') // dropped\n != 0) quote(! #\\n), out);\nxquote(PART_OF(7).inc)\ncat(UP, DOWN);\nxcat(UP, DOWN)\n",
      0, "c17", "log(\"v\" \"3\" \" is %d, v\" \"4\" \" is %s\", v3, v4);\nputs(\"strcmp(\\\"a\\\\0b\\\", \\\"a\\\", '\\\\5') != 0\" \"! #\\n\", out);\n\"part7.inc\"\n\"top\";\n\"top\" \" bottom\"\n", false },
    { "placemarkers", "#define tri(a,b,c) a ## b ## c\nint v[] = { tri(9,8,7), tri(,6,5), tri(4,,3), tri(2,1,),\n tri(10,,), tri(,20,), tri(,,30), tri(,,) };\n",
      0, "c17", "\nint v[] = { 987, 65, 43, 21,\n 10, 20, 30, };\n", false },
    { "variadic macros", "#define log_all(...) logf(level, __VA_ARGS__)\n#define names(...) show(#__VA_ARGS__)\n#define check(c, ...) ((c) ? show(#c) : \\\n emit(__VA_ARGS__))\nlog_all(\"start\");\nlog_all(\"n = %d\\n\", n);\nnames(red, green and blue);\ncheck(a < b, \"a is %d, b is %d\", a, b);\n",
      0, "c17", "logf(level, \"start\");\nlogf(level, \"n = %d\\n\", n);\nshow(\"red, green and blue\");\n((a < b) ? show(\"a < b\") : emit(\"a is %d, b is %d\", a, b));\n", false },
    // The cases the notes probed with GCC and clang (preprocessor-logical-spacing, preprocessor-expansion-boundaries).
    { "splice makes a function-like macro", "#define F\\\n(x) x + 10\nF(2)\n", 0, "c17", "2 + 10\n", false },
    { "comment makes an object-like macro", "#define F/**/(x) x + 10\nF(2)\n", 0, "c17", "(x) x + 10(2)\n", false },
    { "stringize across a splice and a comment", "#define S(x) #x\nS(a\\\n+b)\nS(a/**/+b)\n", 0, "c17", "\"a+b\"\n\"a +b\"\n", false },
    { "redefinition with other spacing", "#define X(x) x+1\n#define X(x) x + 1\nX(2)\n", 0, "c17", "2 + 1\n", false, 1 },
    { "a disabled macro's last token", "#define foo(x) bar x\nfoo(foo) (2)\n", 0, "c17", "bar foo (2)\n", false },
    { "an alias's invocation", "#define F(x) x\n#define ALIAS F\nALIAS (7)\n", 0, "c17", "7\n", false },
    { "adjacent tokens don't paste", "#define PLUS +\n+PLUS\n", 0, "c17", "+ +\n", false },
    { "an unused argument isn't expanded", "#define BAD(x,y) x\n#define DROP(x) 0\nDROP(BAD(1))\n", 0, "c17", "0\n", false },
    { "a stringized argument isn't expanded", "#define BAD(x,y) x\n#define S(x) #x\nS(BAD(1))\n", 0, "c17", "\"BAD(1)\"\n", false },
    { "a dead arm's macros still expand", "#define BAD(x,y) x\n#if 0 && BAD(1)\n#endif\n", 0, "c17", "", true },
    { "a dead arm's division isn't evaluated", "#if 0 && (1/0)\nx\n#else\ny\n#endif\n", 0, "c17", "y\n", false },
    { "a dead arm's division skips an #error", "#if 0 && (1/0)\n#error unexpected\n#endif\nint x;\n", 0, "c17", "int x;\n", false },
    { "a pp-number without exponent digits in a skipped group", "#if 0\nint discarded=1e+;\n#endif\nint retained;\n", 0, "c17", "int retained;\n", false },
    // 1e+ stays one pp-number: the error ("exponent has no digits") is the parser's.
    { "a pp-number without exponent digits", "int x=1e+;\n", 0, "c17", "int x=1e+;\n", false },
    // Redefinitions are compared by spelling (1 and 01, [ and <:, parameter names) and whitespace, after splices: GCC
    // and clang warn about A, B and C only.
    { "redefinitions with other spellings", "#define A 1\n#define A 01\n#define B [\n#define B <:\n#define C(x) x\n#define C(y) y\n#define D (1 + 2)\n"
                                            "#define D (1 + 2)\n#define E(x) x\n#define E(x)  x\n#define F(x) x  +  1\n#define F(x) x + 1\n#define G 1\\\n0\n"
                                            "#define G 10\n#define H \"a\\\nb\"\n#define H \"ab\"\nA B C(3) D E(4) F(5) G H\n",
      0, "c17", "01 <: 3 (1 + 2) 4 5 + 1 10 \"ab\"\n", false, 3 },
    { "true in C17", "#if true\nyes\n#else\nno\n#endif\n", 0, "c17", "no\n", false },
    { "true in C23", "#if true\nyes\n#else\nno\n#endif\n", 0, "c23", "yes\n", false },
    { "__VA_OPT__ of an argument that expands to nothing", "#define EMPTY\n#define V(...) 1 __VA_OPT__(+ 2)\nV()\nV(EMPTY)\nV(x)\n", 0, "c23", "1\n1\n1 + 2\n", false },
    // Probes of clang (docs/c-preprocessor.md).
    { "GNU comma and __VA_OPT__", "#define F(x, ...) f(x __VA_OPT__(,) __VA_ARGS__)\nF(1) F(1,2) F(1,)\n#define C(a,...) g(a, ## __VA_ARGS__)\nC(1) C(1,) C(1,2)\n",
      0, "c23", "f(1) f(1, 2) f(1)\ng(1) g(1,) g(1,2)\n", false },
    { "pastes", "#define CAT(a,b) a##b\nCAT(x,y) CAT(,y) CAT(x,) CAT(,) CAT(1,e) CAT(<,<=) CAT(+,) CAT(L, \"x\") CAT(1,.e+)\n", 0, "c17",
      "xy y x 1e <<= + L\"x\" 1. e+\n", false },
    { "stringize spacing and escapes", "#define H(x) # x\nH(  a   \"\\n\"  ) H() H( + ) H('\\'' @ L\"x\")\n", 0, "c17",
      "\"a \\\"\\\\n\\\"\" \"\" \"+\" \"'\\\\'' @ L\\\"x\\\"\"\n", false },
    { "a function-like name without arguments", "#define EMPTY\n#define LPAREN (\n#define f(x) [x]\nf EMPTY (1)\nf LPAREN 2)\n#define g f\ng(3)\n", 0, "c17",
      "f (1)\nf ( 2)\n[3]\n", false },
    { "builtins", "a __COUNTER__ __COUNTER__ __INCLUDE_LEVEL__ __has_attribute(noreturn) __has_builtin(__builtin_expect) __has_feature(c_static_assert)\n"
                  "#define X noreturn\n#if __has_attribute(X) && __has_include(\"other.h\") && !__has_include(\"missing.h\") && defined X && !defined(Y)\nb\n#endif\n"
                  "#define G(x) __LINE__ x\nint a = G(\n1\n);\n",
      "", "c17", "a 0 1 0 1 1 1\nb\nint a = 9 1;\n", false },
    { "include guard and #pragma once", "#include \"other.h\"\n#include \"other.h\"\nother_t x;\n",
      "#ifndef OTHER_H\n#define OTHER_H\ntypedef int other_t;\n#endif\n", "c17", "typedef int other_t;\nother_t x;\n", false },
    { "pragmas", "#pragma once\n#pragma GCC system_header\n#pragma STDC FP_CONTRACT ON\n#define P(x) _Pragma(#x) after\nP(omp parallel for)\n#pragma push_macro(\"X\")\n"
                 "#define X 1\n#pragma pop_macro(\"X\")\nX\n",
      0, "c17", "#pragma STDC FP_CONTRACT ON\n#pragma omp parallel for\nafter\nX\n", false },
    { "#if arithmetic", "#if -1 < 0u\nunsigned\n#endif\n#if (2 || 1/0) && (0 ? 1/0 : 3) == 3 && 0x10 == 16 && 010 == 8 && 0b101 == 5 && 1'000 == 1000 && 'A' == 65 && '\\377' < 0\nok\n#endif\n"
                     "#if (-9223372036854775807 - 1) / -1 == (-9223372036854775807 - 1) && 18446744073709551615u == -1\nwraps\n#endif\n#if 1 ? 2 : 3 ? 4 : 5\nternary\n#endif\n",
      0, "c23", "ok\nwraps\nternary\n", false },
    { "#elif chains and nesting", "#define A 2\n#if A == 1\none\n#elif A == 2\n#ifdef A\ntwo\n#else\nnot\n#endif\n#elif 1/0\nthree\n#else\nfour\n#endif\n#ifndef B\nb\n#elifdef A\nno\n#endif\n",
      0, "c23", "two\nb\n", false },
    // Argument scanning past its fast path's limits: an argument across two 64-token blocks, parentheses nested 70 deep,
    // 70 arguments, and a variadic argument of 69 commas.
    { "an argument across blocks", "#define F(a, b) [a | b]\nF(( 1 , 2 , 3 , 4 , 5 , 6 , 7 , 8 , 9 , 10 , 11 , 12 , 13 , 14 , 15 , 16 , 17 , 18 , 19 , 20 , 21 , 22 , 23 , 24 , 25 , 26 , 27 , 28 , 29 , 30 , 31 , 32 , 33 , 34 , 35 ), x)\nF(x, ( 1 , 2 , 3 , 4 , 5 , 6 , 7 , 8 , 9 , 10 , 11 , 12 , 13 , 14 , 15 , 16 , 17 , 18 , 19 , 20 , 21 , 22 , 23 , 24 , 25 , 26 , 27 , 28 , 29 , 30 , 31 , 32 , 33 , 34 , 35 ))\n", 0, "c17",
      "[( 1 , 2 , 3 , 4 , 5 , 6 , 7 , 8 , 9 , 10 , 11 , 12 , 13 , 14 , 15 , 16 , 17 , 18 , 19 , 20 , 21 , 22 , 23 , 24 , 25 , 26 , 27 , 28 , 29 , 30 , 31 , 32 , 33 , 34 , 35 ) | x]\n[x | ( 1 , 2 , 3 , 4 , 5 , 6 , 7 , 8 , 9 , 10 , 11 , 12 , 13 , 14 , 15 , 16 , 17 , 18 , 19 , 20 , 21 , 22 , 23 , 24 , 25 , 26 , 27 , 28 , 29 , 30 , 31 , 32 , 33 , 34 , 35 )]\n", false },
    { "parentheses nested 70 deep", "#define F(a, b) [a | b]\nF(((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((x)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))), y)\n", 0, "c17", "[((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((x)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))) | y]\n", false },
    { "70 arguments", "#define M(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69) a0 a69\nM(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69)\n#define V(first, ...) <first> __VA_ARGS__\nV(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69)\n", 0, "c17",
      "0 69\n<0> 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69\n", false },
    // Conditional pairing across 32-directive blocks: 40 nested groups, and a group with 100 directives inside.
    { "conditionals nested 40 deep", "#if 1\na0\n#if 1\na1\n#if 1\na2\n#if 1\na3\n#if 1\na4\n#if 1\na5\n#if 1\na6\n#if 1\na7\n#if 1\na8\n#if 1\na9\n#if 1\na10\n#if 1\na11\n#if 1\na12\n#if 1\na13\n#if 1\na14\n#if 1\na15\n#if 1\na16\n#if 1\na17\n#if 1\na18\n#if 1\na19\n#if 1\na20\n#if 1\na21\n#if 1\na22\n#if 1\na23\n#if 1\na24\n#if 1\na25\n#if 1\na26\n#if 1\na27\n#if 1\na28\n#if 1\na29\n#if 1\na30\n#if 1\na31\n#if 1\na32\n#if 1\na33\n#if 1\na34\n#if 1\na35\n#if 1\na36\n#if 1\na37\n#if 1\na38\n#if 1\na39\ncore\n#else\nb39\n#endif\n#else\nb38\n#endif\n#else\nb37\n#endif\n#else\nb36\n#endif\n#else\nb35\n#endif\n#else\nb34\n#endif\n#else\nb33\n#endif\n#else\nb32\n#endif\n#else\nb31\n#endif\n#else\nb30\n#endif\n#else\nb29\n#endif\n#else\nb28\n#endif\n#else\nb27\n#endif\n#else\nb26\n#endif\n#else\nb25\n#endif\n#else\nb24\n#endif\n#else\nb23\n#endif\n#else\nb22\n#endif\n#else\nb21\n#endif\n#else\nb20\n#endif\n#else\nb19\n#endif\n#else\nb18\n#endif\n#else\nb17\n#endif\n#else\nb16\n#endif\n#else\nb15\n#endif\n#else\nb14\n#endif\n#else\nb13\n#endif\n#else\nb12\n#endif\n#else\nb11\n#endif\n#else\nb10\n#endif\n#else\nb9\n#endif\n#else\nb8\n#endif\n#else\nb7\n#endif\n#else\nb6\n#endif\n#else\nb5\n#endif\n#else\nb4\n#endif\n#else\nb3\n#endif\n#else\nb2\n#endif\n#else\nb1\n#endif\n#else\nb0\n#endif\n", 0, "c17", "a0\na1\na2\na3\na4\na5\na6\na7\na8\na9\na10\na11\na12\na13\na14\na15\na16\na17\na18\na19\na20\na21\na22\na23\na24\na25\na26\na27\na28\na29\na30\na31\na32\na33\na34\na35\na36\na37\na38\na39\ncore\n", false },
    { "a group around 50 others", "#if 0\n#ifdef X0\nx0\n#endif\n#ifdef X1\nx1\n#endif\n#ifdef X2\nx2\n#endif\n#ifdef X3\nx3\n#endif\n#ifdef X4\nx4\n#endif\n#ifdef X5\nx5\n#endif\n#ifdef X6\nx6\n#endif\n#ifdef X7\nx7\n#endif\n#ifdef X8\nx8\n#endif\n#ifdef X9\nx9\n#endif\n#ifdef X10\nx10\n#endif\n#ifdef X11\nx11\n#endif\n#ifdef X12\nx12\n#endif\n#ifdef X13\nx13\n#endif\n#ifdef X14\nx14\n#endif\n#ifdef X15\nx15\n#endif\n#ifdef X16\nx16\n#endif\n#ifdef X17\nx17\n#endif\n#ifdef X18\nx18\n#endif\n#ifdef X19\nx19\n#endif\n#ifdef X20\nx20\n#endif\n#ifdef X21\nx21\n#endif\n#ifdef X22\nx22\n#endif\n#ifdef X23\nx23\n#endif\n#ifdef X24\nx24\n#endif\n#ifdef X25\nx25\n#endif\n#ifdef X26\nx26\n#endif\n#ifdef X27\nx27\n#endif\n#ifdef X28\nx28\n#endif\n#ifdef X29\nx29\n#endif\n#ifdef X30\nx30\n#endif\n#ifdef X31\nx31\n#endif\n#ifdef X32\nx32\n#endif\n#ifdef X33\nx33\n#endif\n#ifdef X34\nx34\n#endif\n#ifdef X35\nx35\n#endif\n#ifdef X36\nx36\n#endif\n#ifdef X37\nx37\n#endif\n#ifdef X38\nx38\n#endif\n#ifdef X39\nx39\n#endif\n#ifdef X40\nx40\n#endif\n#ifdef X41\nx41\n#endif\n#ifdef X42\nx42\n#endif\n#ifdef X43\nx43\n#endif\n#ifdef X44\nx44\n#endif\n#ifdef X45\nx45\n#endif\n#ifdef X46\nx46\n#endif\n#ifdef X47\nx47\n#endif\n#ifdef X48\nx48\n#endif\n#ifdef X49\nx49\n#endif\n#else\nok\n#endif\n", 0, "c17", "ok\n", false },
    // Unbalanced groups, which the vectorized pairing leaves to the stack.
    { "an #endif too many", "#if 1\na\n#endif\n#endif\nb\n", 0, "c17", "", true },
    { "an #elif after #else", "#if 0\na\n#else\nb\n#elif 1\nc\n#endif\n", 0, "c17", "", true },
    { "an unterminated group", "#if 1\na\n#if 0\nb\n#endif\n", 0, "c17", "", true },
    // The main loop's frames: directives among a macro's arguments (a #if line expanded there, a #define), which the main
    // loop collects a token at a time; builtins' operands from other contexts, and a #define in one; _Pragma from a
    // macro; a computed #include; #line with a macro, and a line marker.
    { "directives among a macro's arguments", "#define F(a, b) [a | b]\n#define ONE 1\nF(x,\n#if ONE + 1 == 2\nyes\n#else\nno\n#endif\n)\nF(\n#ifdef ONE\np\n#endif\n, q)\nF(r,\n#define LATE 7\nLATE)\nLATE\n#define G F\nG(\n#if defined(ONE) && __has_include(\"other.h\")\ns\n#endif\n, t)\n", "", "c17", "[x | yes]\n[p | q]\n[r | 7]\n7\n[s | t]\n", false },
    { "builtins' operands, a computed #include and #line", "#define H __has_attribute\nint a = H(noreturn) + H(\nnot_an_attribute\n);\nint b = __has_attribute(\n#define Z 1\nnoreturn) + Z;\n#define PR _Pragma\nPR(\"message(\\\"hi\\\")\") after\n#define HDR \"other.h\"\n#include HDR\n#define N 100\n#line N\nint line = __LINE__;\n# 50 \"renamed.c\"\nint again = __LINE__;\n#if __has_include(HDR) && !__has_include(\"missing.h\")\nboth\n#endif\n", "typedef int other_t;\n", "c17",
      "int a = 1 + 0;\nint b = 1 + 1;\n#pragma message(\"hi\")\nafter\ntypedef int other_t;\nint line = 100;\nint again = 50;\nboth\n", false },
    { "unterminated invocation", "#define f(x) x\nf(1\n", 0, "c17", "", true },
    // An identifier is printed as it is interned (a UCN as UTF-8), but stringized as written, without its splices.
    { "identifiers written with UCNs and splices", "#define S(x) #x\ncaf\\u00e9 S(caf\\u00e9) S(caf\xC3\xA9) S(fo\\\no) S(\\U000000E9t\\\n\\u00e9)\n", 0,
      "c17", "caf\xC3\xA9 \"caf\\u00e9\" \"caf\xC3\xA9\" \"foo\" \"\\U000000E9t\\u00e9\"\n", false },
};

// Writes a case's files into a new directory; returns the main file's path (in `path`).
static bool pp_test_write_case(const PPTestCase* test, const char* directory, char* path, uint32_t capacity)
{
    bool result = true;
    snprintf(path, capacity, "%s/case.c", directory);
    FILE* file = fopen(path, "w");
    result = file != 0 && fputs(test->source, file) >= 0;
    if (file != 0)
    {
        fclose(file);
    }
    if (result && test->other != 0)
    {
        char other[4096];
        snprintf(other, sizeof(other), "%s/other.h", directory);
        file = fopen(other, "w");
        result = file != 0 && fputs(test->other, file) >= 0;
        if (file != 0)
        {
            fclose(file);
        }
    }
    return result;
}

static bool pp_test_case(const PPTestCase* test, const char* directory, bool with_clang)
{
    char path[4096];
    bool result = pp_test_write_case(test, directory, path, sizeof(path));
    PPOptions options = { .language = { .standard = 17, .gnu = true } };
    result = result && pp_parse_standard(test->standard, &options.language);
    static InternTable tables[2];
    static Preprocessor runs[2];
    bool ran[2] = { false, false };
    for (uint32_t k = 0; result && k < 2; k += 1)
    {
        options.scalar = k == 1;
        // A case that fails prints its error; the expected output is then not compared.
        ran[k] = pp_test_run(&runs[k], &tables[k], &options, path);
    }
    if (result)
    {
        result = pp_same_streams(&runs[0], &runs[1], test->name) && pp_same_directive_tables(&runs[0], &runs[1], test->name) && ran[0] == ran[1] &&
                 ran[0] != test->fails;
        if (ran[0] == test->fails)
        {
            printf("%s: %s\n", test->name, test->fails ? "should fail but doesn't" : "fails");
        }
        for (uint32_t k = 0; k < 2; k += 1)
        {
            if (runs[k].warnings != test->warnings)
            {
                printf("%s: %u warnings (%s kernels), not %u\n", test->name, runs[k].warnings, k == 0 ? "vectorized" : "scalar", test->warnings);
                result = false;
            }
        }
    }
    if (result && !test->fails)
    {
        PPSpellings ours = { 0 };
        PPSpellings expected = { 0 };
        result = pp_stream_spellings(&runs[0], &ours) && pp_text_spellings((const uint8_t*)test->expected, strlen(test->expected), &expected) &&
                 pp_compare_spellings(&runs[0], &ours, &expected, test->name, "wanted") && pp_test_printed(&runs[0], &ours, test->name);
        if (result && with_clang)
        {
            char standard[32];
            snprintf(standard, sizeof(standard), "-std=%s", test->standard);
            char* arguments[] = { standard };
            size_t length;
            char* text = pp_test_clang(arguments, 1, path, 0, &length);
            PPSpellings theirs = { 0 };
            result = text != 0 && pp_text_spellings((const uint8_t*)text, length, &theirs) && pp_compare_spellings(&runs[0], &ours, &theirs, test->name, "clang");
            arena_release(&theirs.arena);
            free(text);
        }
        arena_release(&ours.arena);
        arena_release(&expected.arena);
    }
    for (uint32_t k = 0; k < 2; k += 1)
    {
        pp_release(&runs[k]);
        intern_release(&tables[k]);
    }
    return result;
}

// One file, by both kernels, and with --clang against clang with the same options.
static bool pp_test_file(int argument_count, char** arguments, const PPOptions* base, const char* path, bool with_clang, const char* directory, uint64_t* tokens)
{
    PPOptions options = *base;
    char predefines[4096];
    snprintf(predefines, sizeof(predefines), "%s/predefines.h", directory);
    size_t length = 0;
    char* text = 0;
    if (with_clang)
    {
        text = pp_test_clang(arguments, argument_count, path, predefines, &length);
        options.predefines_path = predefines;
    }
    static InternTable tables[2];
    static Preprocessor runs[2];
    bool ran[2];
    for (uint32_t k = 0; k < 2; k += 1)
    {
        options.scalar = k == 1;
        ran[k] = pp_test_run(&runs[k], &tables[k], &options, path);
    }
    bool result = ran[0] && ran[1] && pp_same_streams(&runs[0], &runs[1], path) && pp_same_directive_tables(&runs[0], &runs[1], path);
    *tokens += runs[0].output.count;
    PPSpellings ours = { 0 };
    result = result && pp_stream_spellings(&runs[0], &ours) && pp_test_printed(&runs[0], &ours, path);
    if (result && with_clang)
    {
        PPSpellings theirs = { 0 };
        result = text != 0 && pp_text_spellings((const uint8_t*)text, length, &theirs) && pp_compare_spellings(&runs[0], &ours, &theirs, path, "clang");
        arena_release(&theirs.arena);
    }
    arena_release(&ours.arena);
    free(text);
    for (uint32_t k = 0; k < 2; k += 1)
    {
        pp_release(&runs[k]);
        intern_release(&tables[k]);
    }
    return result;
}

static int run_preprocessor_tests(int argument_count, char** arguments)
{
    bool with_clang = false;
    bool keep = false;
    // The arguments without ours, for clang and pp_parse_options; the files are those that don't start with '-' and
    // aren't an option's value.
    char** passed = calloc((size_t)argument_count + 1, sizeof(char*));
    char** files = calloc((size_t)argument_count + 1, sizeof(char*));
    int passed_count = 0;
    int file_count = 0;
    for (int i = 0; passed != 0 && files != 0 && i < argument_count; i += 1)
    {
        const char* argument = arguments[i];
        const bool takes_value = strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 || strcmp(argument, "-isystem") == 0 ||
                                 strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0 || strcmp(argument, "-include") == 0;
        if (strcmp(argument, "--clang") == 0)
        {
            with_clang = true;
        }
        else if (strcmp(argument, "--keep") == 0)
        {
            keep = true;
        }
        else if (argument[0] == '-')
        {
            passed[passed_count++] = arguments[i];
            if (takes_value && i + 1 < argument_count)
            {
                i += 1;
                passed[passed_count++] = arguments[i];
            }
        }
        else
        {
            files[file_count++] = arguments[i];
        }
    }
    char directory[4096];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-pp-test-XXXXXX", temporary != 0 ? temporary : "/tmp");
    bool passed_all = passed != 0 && files != 0 && mkdtemp(directory) != 0;
    if (passed_all && file_count == 0)
    {
        uint32_t failures = 0;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(pp_test_cases); i += 1)
        {
            failures += !pp_test_case(&pp_test_cases[i], directory, with_clang);
        }
        printf("cases: %u of %lu fail\n", failures, (unsigned long)BUSTER_ARRAY_LENGTH(pp_test_cases));
        passed_all = failures == 0;
    }
    else if (passed_all)
    {
        Arena arena = arena_reserve(1ull << 30);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        // pp_parse_options wants one input: give it the first file, and use its options for every file.
        char** with_input = calloc((size_t)passed_count + 2, sizeof(char*));
        memcpy(with_input, passed, (size_t)passed_count * sizeof(char*));
        with_input[passed_count] = files[0];
        passed_all = pp_parse_options(passed_count + 1, with_input, &options, &arena, &input, &output, &markers);
        uint32_t failures = 0;
        uint64_t tokens = 0;
        for (int i = 0; passed_all && i < file_count; i += 1)
        {
            failures += !pp_test_file(passed_count, passed, &options, files[i], with_clang, directory, &tokens);
        }
        printf("files: %u of %d differ (%lu tokens)\n", failures, file_count, (unsigned long)tokens);
        passed_all = passed_all && failures == 0;
        free(with_input);
        arena_release(&arena);
    }
    if (!keep)
    {
        // The files the cases and the clang runs write, then the directory.
        static const char* const names[] = { "case.c", "other.h", "predefines.h" };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
        {
            char path[4200];
            snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
            unlink(path);
        }
        rmdir(directory);
    }
    free(passed);
    free(files);
    return passed_all ? 0 : 1;
}
