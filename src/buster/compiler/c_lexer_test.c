// SPDX-License-Identifier: MIT
// Copyright (c) 2023 Niles Salter. Local modifications Copyright (c) 2026 David Gonzalez Martin.
// Adapted for buster-fast: command integration, boundary/page-edge checks and extended comment tests.
// Retain LICENSES/Accelerated-Zig-Parser.txt and the provenance in THIRD_PARTY_NOTICES.md.
//
// The C lexer tests: they check that c_tokenize (c_lexer.c) agrees with its scalar lexer, token by token, on inputs
// that move tricky C across the vectorized path's 64-byte vector and 512-byte chunk boundaries. The compiler runs them
// as a command:
//
//   build/compiler test-lexer [--cplusplus] [--raw-strings | --raw-strings-c99] [--fuzz N] [--seed N]
//
// main.c includes this file after c_lexer.c, String8, BUSTER_ARRAY_LENGTH and BUSTER_COMPILE_TIME_STRING_LENGTH, and
// calls run_c_lexer_tests for that command. The tests are a port of the snippet and random-input checks of
// c/c_tokenizer_test.c in https://github.com/davidgmbb/Accelerated-Zig-Parser (commit
// f4482e2dd53413522d75d8c254b9a824021238ce, which c_lexer.c also comes from). They make the same inputs and print the
// same lines for them, but also fail if the snippet needs the scalar lexer, which upstream only counts.
//
// The checks, in this order:
//   - The snippet: boundary_snippet, tricky C that the vectorized path tokenizes by itself, after 0 to
//     BUSTER_TEST_SNIPPET_PADDING - 1 bytes of padding at every misalignment from a cache line. A placement fails if
//     the paths differ on it, and also if it needs the scalar lexer, because then that part only checks the scalar
//     lexer against itself.
//   - Random inputs: N of them (BUSTER_TEST_DEFAULT_RANDOM_INPUTS by default) from fuzz_input, which strings
//     fuzz_pieces together with a xorshift sequence. --seed N picks another sequence: the one upstream's --seed N
//     picks. The first inputs that differ are saved in the current directory as fuzz_failure_0.c, fuzz_failure_1.c...
//   - Page edges: inputs of 0 to BUSTER_TEST_PAGE_EDGE_LENGTHS - 1 bytes that end exactly at the end of a readable
//     page, or start exactly at the start of one, in a mapping with a PROT_NONE guard page on either side. Each one is
//     a prefix or a suffix of a template: the snippet repeated, BUSTER_TEST_PAGE_EDGE_RANDOM_TEMPLATES strings of
//     random inputs that continue the random check's sequence, or one of three comment templates. Besides comparing
//     c_tokenize, c_tokenize_vectorized and c_tokenize_scalar as the other checks do, it catches a read outside of
//     the source, which the other checks' buffers can't: that faults, and the fault handler names the input and exits
//     with status 1. Length 0 ends at a page end too, so its source pointer is the first byte of the guard page.
//   - Comment delimiters: comments that end around the leading-comment scanner's 64-byte loads and around 512-byte
//     chunk boundaries (with closers split across chunks), at every misalignment from a cache line, a spliced closer
//     and a closer right after a slash among them (see check_comments).
//   - Raw string literals, and identifiers before a quote that start none, with each byte of the prefix at each place
//     around 64-byte vector and 512-byte chunk boundaries, at every misalignment from a cache line: the vectorized
//     path must leave each literal to the scalar lexer, and none of the others (see check_raw_strings).
//
// --cplusplus tokenizes everything as C++, with boundary_snippet_cpp instead and the fuzz_pieces_cpp pieces too.
// --raw-strings tokenizes C with raw string literals as C11 and later GNU modes have them (C_TOKENIZER_RAW_STRINGS and
// C_TOKENIZER_UTF_RAW_STRINGS), --raw-strings-c99 as gnu99 has them (C_TOKENIZER_RAW_STRINGS alone), both with
// boundary_snippet_raw instead and the fuzz_pieces_raw pieces too. The results go to stdout, with the first
// differences of each check. The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order:
//   - The comparison: TokenArrays, print_token and compare.
//   - The snippet: boundary_snippet, boundary_snippet_cpp, boundary_snippet_raw, test_snippet and check_snippet.
//   - Random inputs: random_next, fuzz_pieces, fuzz_pieces_cpp, fuzz_pieces_raw, has_backslash, fuzz_input,
//     save_failure and check_random.
//   - Page edges: page_edge_input, report_fault, page_edge_template and check_page_edges.
//   - Comment delimiters: check_comments, with delimiters split across the prefix scanner's unaligned loads and
//     across chunks.
//   - Raw string literals: check_raw_strings.
//   - run_c_lexer_tests, with parse_u64.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

// The input buffers are aligned to a cache line (which is also a vector), and inputs start at every misalignment
// from one.
#define BUSTER_TEST_BUFFER_ALIGNMENT 64
// The output arrays have room for inputs of up to this many bytes.
#define BUSTER_TEST_MAX_INPUT_BYTES 8192
// The snippet goes after 0 to BUSTER_TEST_SNIPPET_PADDING - 1 bytes of spaces with a newline every
// BUSTER_TEST_PADDING_LINE_BYTES bytes, in a buffer of BUSTER_TEST_SNIPPET_BUFFER_BYTES.
#define BUSTER_TEST_SNIPPET_PADDING 1100
#define BUSTER_TEST_PADDING_LINE_BYTES 61
#define BUSTER_TEST_SNIPPET_BUFFER_BYTES 4096
// A random input has 1 to BUSTER_TEST_MAX_PIECES pieces, each followed with odds of 1 in BUSTER_TEST_RUN_ODDS by a
// run of 0 to BUSTER_TEST_RUN_LIMIT - 1 bytes of plain code, and is up to BUSTER_TEST_RANDOM_INPUT_BYTES long.
#define BUSTER_TEST_DEFAULT_RANDOM_INPUTS 200000
#define BUSTER_TEST_MAX_PIECES 300
#define BUSTER_TEST_RUN_ODDS 16
#define BUSTER_TEST_RUN_LIMIT 200
#define BUSTER_TEST_RANDOM_INPUT_BYTES 8000
// The page edge check places inputs of 0 to BUSTER_TEST_PAGE_EDGE_LENGTHS - 1 bytes, which cover the 64-byte vector
// and 512-byte chunk boundaries (and the chunk-ahead lane 8 past the second chunk), cut from the snippet template and
// this many random templates.
#define BUSTER_TEST_PAGE_EDGE_LENGTHS 1100
#define BUSTER_TEST_PAGE_EDGE_RANDOM_TEMPLATES 4
// Long leading and interior comments, and splices in a leading line comment.
#define BUSTER_TEST_PAGE_EDGE_COMMENT_TEMPLATES 3
#define BUSTER_TEST_PAGE_EDGE_TEMPLATES (1 + BUSTER_TEST_PAGE_EDGE_RANDOM_TEMPLATES + BUSTER_TEST_PAGE_EDGE_COMMENT_TEMPLATES)
// 2^64 divided by the golden ratio: the first state of the random sequence, and the multiplier that turns --seed N
// into one (N * BUSTER_TEST_GOLDEN_RATIO + 1), as upstream does.
#define BUSTER_TEST_GOLDEN_RATIO 0x9E3779B97F4A7C15ull
// Each check prints the first difference of this many inputs (and saves this many random ones), with this many tokens
// of context on either side and this many bytes of each token.
#define BUSTER_TEST_REPORTED_INPUTS 3
#define BUSTER_TEST_CONTEXT_TOKENS 2
#define BUSTER_TEST_TOKEN_PREVIEW_BYTES 40

// ---------------------------------------------------------------------------------------------------------------------
// The comparison

// The tokens of one path, with room for any input here.
typedef struct TokenArrays TokenArrays;
struct TokenArrays
{
    uint8_t kinds[BUSTER_C_TOKENIZER_CAPACITY(BUSTER_TEST_MAX_INPUT_BYTES)];
    uint32_t starts[BUSTER_C_TOKENIZER_CAPACITY(BUSTER_TEST_MAX_INPUT_BYTES)];
    uint32_t ends[BUSTER_C_TOKENIZER_CAPACITY(BUSTER_TEST_MAX_INPUT_BYTES)];
};

typedef enum Comparison
{
    COMPARISON_SAME,
    COMPARISON_DIFFERENT,
    // The same, but the vectorized path left a part of the source to the scalar lexer.
    COMPARISON_FELL_BACK,
} Comparison;

// How many inputs of a check the paths tokenize differently, and how many need the scalar lexer for a part.
typedef struct CheckCounts CheckCounts;
struct CheckCounts
{
    uint64_t different;
    uint64_t fell_back;
};

// Prints token `i` of `count` (or "-" if there's no such token): its kind, position and first bytes.
static void print_token(String8 source, const TokenArrays* tokens, int64_t i, int64_t count)
{
    if (i < 0 || i >= count)
    {
        printf("-");
    }
    else
    {
        uint32_t start = tokens->starts[i];
        uint32_t end = tokens->ends[i];
        uint64_t preview_end = (uint64_t)start + BUSTER_TEST_TOKEN_PREVIEW_BYTES;
        printf("%s%s [%u, %u) \"", c_token_kind_name(tokens->kinds[i]), (tokens->kinds[i] & BUSTER_C_TOKEN_START_OF_LINE) ? " SOL" : "", start, end);
        for (uint64_t p = start; p < end && p < preview_end && p < source.length; p += 1)
        {
            uint8_t c = (uint8_t)source.pointer[p];
            if (c >= 32 && c < 127 && c != '\\')
            {
                putchar(c);
            }
            else
            {
                printf("\\x%02X", c);
            }
        }
        printf("\"");
    }
}

// Compares c_tokenize with the scalar lexer on `source`, and prints where they differ if `name` isn't null.
static Comparison compare(String8 source, TokenArrays* vectorized, TokenArrays* scalar, uint32_t flags, const char* name)
{
    const uint8_t* bytes = (const uint8_t*)source.pointer;
    const bool fell_back = c_tokenize_vectorized(bytes, source.length, vectorized->kinds, vectorized->starts, vectorized->ends, flags) ==
                           C_TOKENIZER_ERROR_NEEDS_SCALAR;
    int64_t vectorized_count = c_tokenize(bytes, source.length, vectorized->kinds, vectorized->starts, vectorized->ends, flags);
    int64_t scalar_count = c_tokenize_scalar(bytes, source.length, scalar->kinds, scalar->starts, scalar->ends, flags);
    int64_t i = 0;
    while (i < vectorized_count && i < scalar_count && vectorized->kinds[i] == scalar->kinds[i] && vectorized->starts[i] == scalar->starts[i] &&
           vectorized->ends[i] == scalar->ends[i])
    {
        i += 1;
    }

    Comparison result;
    if (i == vectorized_count && i == scalar_count)
    {
        result = fell_back ? COMPARISON_FELL_BACK : COMPARISON_SAME;
    }
    else
    {
        result = COMPARISON_DIFFERENT;
        if (name != 0)
        {
            printf("%s: token %ld differs (vectorized has %ld tokens, scalar %ld)\n", name, (long)i, (long)vectorized_count, (long)scalar_count);
            for (int64_t j = i - BUSTER_TEST_CONTEXT_TOKENS; j <= i + BUSTER_TEST_CONTEXT_TOKENS; j += 1)
            {
                if (j >= 0)
                {
                    printf("  %s vectorized: ", j == i ? ">" : " ");
                    print_token(source, vectorized, j, vectorized_count);
                    printf("\n      scalar:     ");
                    print_token(source, scalar, j, scalar_count);
                    printf("\n");
                }
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The snippet

// Tricky C that the vectorized path tokenizes by itself (no fallback): literals with escapes, prefixes and digit
// separators, comments of all kinds, line splices, digraphs, punctuator chains, unterminated literals and stray
// characters.
static const char boundary_snippet[] =
    "#include <stdio.h> // a \"comment\" with 'quotes' and /* in it\n"
    "  # define X(a, b) a ## b %: y %:%: z \\\n"
    "    (x) /* block\n"
    " * comment with \"quotes\" and 'apostrophes' don't\n"
    " */ int main(void) { return 0; } /**/ /* not closed by / or * / */ x /***/\n"
    "char *s = \"string with \\\" escaped \\\\ and // not a comment\", c = 'x', d = '\\'', e = '\\\\', f = '\"';\n"
    "wchar_t *w = L\"wide\"; char8_t *u = u8\"utf8\" u\"utf16\" U\"utf32\" L'w' u8'8' u'u' U'U' LR\"(no raw)\" Lx\"y\" uu\"z\";\n"
    "x = a+++b - -c --d ->e <<= f >>= g <= h >= i == j != k && l || m *= n /= o %= p += q -= r &= s ^= t |= u;\n"
    "a<::>b <% %> <: :> ... .. . .... a.b ...5 ..5 .5.5 1.2.3 x.e+5 1e+e+5 0x1p-3 1'000'000 1'a 1'e+5 0b1010 12.5DF 1$x;\n"
    "'' 'unterminated\n\"unterminated string\nok = \"multi \\\nline string\" 'a\\\nb' @ ` $x a$ ::: <=> ->* .* ## #\r\n"
    "s = \"a\\\\\\\\\" \"tab\tin\" /* \"unterminated\n*/ 'x' // line comment \\\ncontinued\nend;\r\n";

// The same for C++ (raw string literals, which need the scalar lexer, are left out).
static const char boundary_snippet_cpp[] =
    "#include <vector> // a \"comment\" with 'quotes' and /* in it\n"
    "template <class T> auto operator<=>(const T& a, const T& b) { return a.*p <=> (b->*q)(); } /* block\n"
    " * comment with \"quotes\" and 'apostrophes' don't\n"
    " */ using namespace std::literals; auto s = \"str\"s + \"x\"sv; auto t = 10ms + 1.5_km + 'c'_ch + u8\"x\"_u8;\n"
    "std::vector<::std::string> v; template<::T> x <::: y <::> z <:: w a<::b \"%\"PRId64 \"a\"sss \"a\"_ \"a\"$x\n"
    "x = a+++b - -c --d ->e <<= f >>= g <= h >= i == j != k && l || m *= n /= o %= p += q -= r &= s ^= t |= u;\n"
    "a<::>b <% %> <: :> ... .. . .... a.b ...5 ...* .** ->** <=>= <==> ==> :>* %>* 1.*2 1'000'000 1'a 0b1010 1$x;\n"
    "'' ''_x 'unterminated\n\"unterminated string\nok = \"multi \\\nline string\"_sfx 'a\\\nb'_c @ ` $x a$ ::: ## #\r\n"
    "s = \"a\\\\\\\\\" \"tab\tin\" /* \"unterminated\n*/ 'x' // line comment \\\ncontinued\nend;\r\n";

// The same for C with raw string literals: identifiers before a quote that start no raw string literal, where the C
// snippet has LR"(no raw)" (an identifier and a pp-number end in R or LR, and "%"PRIxPTR"\n" is how C code writes
// inttypes.h's formats; R'c' is an identifier and a character constant).
static const char boundary_snippet_raw[] =
    "#include <stdio.h> // a \"comment\" with 'quotes' and /* in it\n"
    "  # define X(a, b) a ## b %: y %:%: z \\\n"
    "    (x) /* block\n"
    " * comment with \"quotes\" and 'apostrophes' don't\n"
    " */ int main(void) { return 0; } /**/ /* not closed by / or * / */ x /***/\n"
    "char *s = \"string with \\\" escaped \\\\ and // not a comment\", c = 'x', d = '\\'', e = '\\\\', f = '\"';\n"
    "wchar_t *w = L\"wide\"; char8_t *u = u8\"utf8\" u\"utf16\" U\"utf32\" L'w' u8'8' u'u' U'U' xLR\"(no raw)\" Lx\"y\" uu\"z\";\n"
    "p = \"%\"PRIxPTR\"\\n\" RR\"(a)\" LLR\"(b)\" u88R\"(c)\" 1R\"(d)\" .5LR\"(e)\" $R\"(f)\" R'c' LR'c' R (g) \"R\"(h);\n"
    "x = a+++b - -c --d ->e <<= f >>= g <= h >= i == j != k && l || m *= n /= o %= p += q -= r &= s ^= t |= u;\n"
    "a<::>b <% %> <: :> ... .. . .... a.b ...5 ..5 .5.5 1.2.3 x.e+5 1e+e+5 0x1p-3 1'000'000 1'a 1'e+5 0b1010 12.5DF 1$x;\n"
    "'' 'unterminated\n\"unterminated string\nok = \"multi \\\nline string\" 'a\\\nb' @ ` $x a$ ::: <=> ->* .* ## #\r\n"
    "s = \"a\\\\\\\\\" \"tab\tin\" /* \"unterminated\n*/ 'x' // line comment \\\ncontinued\nend;\r\n";

// The last placement of each snippet fits in the buffer, and so in the output arrays.
_Static_assert(BUSTER_TEST_BUFFER_ALIGNMENT - 1 + BUSTER_TEST_SNIPPET_PADDING - 1 + BUSTER_COMPILE_TIME_STRING_LENGTH(boundary_snippet) <=
                   BUSTER_TEST_SNIPPET_BUFFER_BYTES,
               "the C snippet doesn't fit after the padding");
_Static_assert(BUSTER_TEST_BUFFER_ALIGNMENT - 1 + BUSTER_TEST_SNIPPET_PADDING - 1 + BUSTER_COMPILE_TIME_STRING_LENGTH(boundary_snippet_cpp) <=
                   BUSTER_TEST_SNIPPET_BUFFER_BYTES,
               "the C++ snippet doesn't fit after the padding");
_Static_assert(BUSTER_TEST_BUFFER_ALIGNMENT - 1 + BUSTER_TEST_SNIPPET_PADDING - 1 + BUSTER_COMPILE_TIME_STRING_LENGTH(boundary_snippet_raw) <=
                   BUSTER_TEST_SNIPPET_BUFFER_BYTES,
               "the raw strings' snippet doesn't fit after the padding");
_Static_assert(BUSTER_TEST_SNIPPET_BUFFER_BYTES <= BUSTER_TEST_MAX_INPUT_BYTES, "a snippet placement doesn't fit in the output arrays");

// The snippet for `flags`' language, and its length.
static String8 test_snippet(uint32_t flags)
{
    String8 result = { .pointer = (char*)boundary_snippet, .length = BUSTER_COMPILE_TIME_STRING_LENGTH(boundary_snippet) };
    if (flags & C_TOKENIZER_CPLUSPLUS)
    {
        result = (String8){ .pointer = (char*)boundary_snippet_cpp, .length = BUSTER_COMPILE_TIME_STRING_LENGTH(boundary_snippet_cpp) };
    }
    else if (flags & C_TOKENIZER_RAW_STRINGS)
    {
        result = (String8){ .pointer = (char*)boundary_snippet_raw, .length = BUSTER_COMPILE_TIME_STRING_LENGTH(boundary_snippet_raw) };
    }
    return result;
}

// Compares the paths on the snippet after each padding at each misalignment, and prints the counts. The first
// placements that differ or need the scalar lexer are printed too.
static CheckCounts check_snippet(TokenArrays* vectorized, TokenArrays* scalar, uint32_t flags)
{
    uint8_t buffer[BUSTER_TEST_SNIPPET_BUFFER_BYTES] __attribute__((aligned(BUSTER_TEST_BUFFER_ALIGNMENT)));
    const String8 snippet_text = test_snippet(flags);
    const char* snippet = snippet_text.pointer;
    const uint64_t snippet_length = snippet_text.length;
    CheckCounts counts = {0};
    char name[128];
    for (uint64_t misalignment = 0; misalignment < BUSTER_TEST_BUFFER_ALIGNMENT; misalignment += 1)
    {
        for (uint64_t padding = 0; padding < BUSTER_TEST_SNIPPET_PADDING; padding += 1)
        {
            // Zeros around every placement, so that it only depends on its misalignment and padding.
            memset(buffer, 0, sizeof(buffer));
            for (uint64_t p = 0; p < padding; p += 1)
            {
                buffer[misalignment + p] = p % BUSTER_TEST_PADDING_LINE_BYTES == BUSTER_TEST_PADDING_LINE_BYTES - 1 ? '\n' : ' ';
            }
            memcpy(buffer + misalignment + padding, snippet, snippet_length);
            const String8 placement = { .pointer = (char*)buffer + misalignment, .length = padding + snippet_length };
            snprintf(name, sizeof(name), "snippet (misalignment %lu, padding %lu)", (unsigned long)misalignment, (unsigned long)padding);
            Comparison comparison = compare(placement, vectorized, scalar, flags, counts.different < BUSTER_TEST_REPORTED_INPUTS ? name : 0);
            if (comparison == COMPARISON_FELL_BACK && counts.fell_back < BUSTER_TEST_REPORTED_INPUTS)
            {
                printf("%s needs the scalar lexer for a part\n", name);
            }
            counts.different += comparison == COMPARISON_DIFFERENT;
            counts.fell_back += comparison == COMPARISON_FELL_BACK;
        }
    }
    printf("snippet: %lu of %lu placements differ, %lu need the scalar lexer for a part\n", (unsigned long)counts.different,
           (unsigned long)(BUSTER_TEST_BUFFER_ALIGNMENT * BUSTER_TEST_SNIPPET_PADDING), (unsigned long)counts.fell_back);
    return counts;
}

// ---------------------------------------------------------------------------------------------------------------------
// Random inputs

// A xorshift64 generator (shifts 13, 7 and 17). A state of 0 stays 0; any other goes through all nonzero states.
typedef struct Random Random;
struct Random
{
    uint64_t state;
};

static uint64_t random_next(Random* random)
{
    uint64_t x = random->state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    random->state = x;
    return x;
}

// Pieces of C that random inputs are made of, weighted towards what makes the bitstrings interact.
static const char* const fuzz_pieces[] =
{
    "a", "b", "L", "u", "u8", "U", "e", "E", "p", "x", "_", "$", "0", "1", "9", ".", "..", "...", "'", "'", "\"", "\"", "\"",
    "\\", "\\\\", "\\\"", "\\'", "\\\n", "\\\r\n", "/", "*", "/*", "*/", "//", "\n", "\n", "\r\n", "\r", " ", " ", "  ", "\t",
    "+", "-", "<", ">", "=", "!", "&", "|", "^", "%", ":", "#", "?", "(", ")", "[", "]", "{", "}", ";", ",", "~", "@", "`",
    "e+", "1e+", "0x", "'1'", "'a'", "%:", "<:", ":>", "<%", "%>", "->", "++", "<<=", ">>=", "##", "::", "\v", "\f",
};

// More pieces for C++.
static const char* const fuzz_pieces_cpp[] =
{
    "<=>", "->*", ".*", "<::", "<::>", "\"x\"_y", "\"x\"s", "\"x\"sv", "'x'_y", "s", "sv", "min", "_", "_x", "R", "LR", "u8R",
};

// More pieces for C with raw string literals: their prefixes, openers with and without a valid delimiter (of 16 and 17
// characters: the longest and one too long), and closers that match some openers' delimiters.
static const char* const fuzz_pieces_raw[] =
{
    "R", "LR", "u8R", "uR", "UR", "R\"", "R\"(", "LR\"(", "u8R\"(", "uR\"x(", "UR\"(", "R\"x(", "R\"x y(", "R\"0123456789abcdef(",
    "R\"0123456789abcdefg(", ")\"", ")x\"", ")0123456789abcdef\"", ")", "(", "\"",
};

// The runs of plain code between pieces are made of these bytes.
static const char fuzz_plain_code[] = "abc xyz 123 (); ";
// A careful input ends with this, to close a block comment that may be open: an unterminated one needs the scalar
// lexer.
static const char fuzz_comment_end[] = "\n*/";

// Pieces with a backslash, which often need the scalar lexer outside of literals: they're only used inside of literals
// in half of the random inputs.
static bool has_backslash(const char* piece)
{
    return strchr(piece, '\\') != 0;
}

// Writes a random input of up to `capacity` bytes to `buffer`, and returns it, with pieces for `flags`' language. Half
// of the inputs are careful: they leave out the pieces with a backslash outside of string literals (after a piece with
// a line break, a literal counts as closed), and end with fuzz_comment_end.
static String8 fuzz_input(Random* random, uint8_t* buffer, uint64_t capacity, uint32_t flags)
{
    const uint64_t piece_count = 1 + random_next(random) % BUSTER_TEST_MAX_PIECES;
    const bool careful = random_next(random) % 2 == 0;
    const char* const* extra = fuzz_pieces_cpp;
    uint64_t extra_count = BUSTER_ARRAY_LENGTH(fuzz_pieces_cpp);
    if (!(flags & C_TOKENIZER_CPLUSPLUS))
    {
        extra = fuzz_pieces_raw;
        extra_count = flags & C_TOKENIZER_RAW_STRINGS ? BUSTER_ARRAY_LENGTH(fuzz_pieces_raw) : 0;
    }
    const uint64_t choices = BUSTER_ARRAY_LENGTH(fuzz_pieces) + extra_count;
    uint64_t length = 0;
    bool in_literal = false;
    bool full = false;
    for (uint64_t i = 0; !full && i < piece_count; i += 1)
    {
        const uint64_t which = random_next(random) % choices;
        const char* piece = which < BUSTER_ARRAY_LENGTH(fuzz_pieces) ? fuzz_pieces[which] : extra[which - BUSTER_ARRAY_LENGTH(fuzz_pieces)];
        bool skipped = false;
        if (careful)
        {
            if (strcmp(piece, "\"") == 0)
            {
                in_literal = !in_literal;
            }
            else if (strchr(piece, '\n') != 0 || strchr(piece, '\r') != 0)
            {
                in_literal = false;
            }
            skipped = has_backslash(piece) && !in_literal;
        }

        if (!skipped)
        {
            const uint64_t piece_length = strlen(piece);
            if (length + piece_length > capacity)
            {
                full = true;
            }
            else
            {
                memcpy(buffer + length, piece, piece_length);
                length += piece_length;
                // Long runs of plain code between the interesting bits, to move them across the chunk boundaries.
                if (random_next(random) % BUSTER_TEST_RUN_ODDS == 0)
                {
                    const uint64_t run = random_next(random) % BUSTER_TEST_RUN_LIMIT;
                    for (uint64_t j = 0; j < run && length < capacity; j += 1)
                    {
                        buffer[length] = fuzz_plain_code[random_next(random) % BUSTER_COMPILE_TIME_STRING_LENGTH(fuzz_plain_code)];
                        length += 1;
                    }
                }
            }
        }
    }

    if (careful && length + BUSTER_COMPILE_TIME_STRING_LENGTH(fuzz_comment_end) <= capacity)
    {
        memcpy(buffer + length, fuzz_comment_end, BUSTER_COMPILE_TIME_STRING_LENGTH(fuzz_comment_end));
        length += BUSTER_COMPILE_TIME_STRING_LENGTH(fuzz_comment_end);
    }
    return (String8){ .pointer = (char*)buffer, .length = length };
}

// Saves a random input that the paths tokenize differently as fuzz_failure_<index>.c in the current directory.
static void save_failure(String8 input, uint64_t index)
{
    char path[64];
    snprintf(path, sizeof(path), "fuzz_failure_%lu.c", (unsigned long)index);
    FILE* file = fopen(path, "wb");
    bool saved = false;
    if (file != 0)
    {
        const bool written = fwrite(input.pointer, 1, input.length, file) == input.length;
        const bool closed = fclose(file) == 0;
        saved = written && closed;
    }

    if (saved)
    {
        printf("  (saved as %s)\n", path);
    }
    else
    {
        fprintf(stderr, "can't save %s\n", path);
    }
}

_Static_assert(BUSTER_TEST_RANDOM_INPUT_BYTES <= BUSTER_TEST_MAX_INPUT_BYTES, "a random input doesn't fit in the output arrays");

// Compares the paths on `count` random inputs, and prints the counts. The first inputs that differ are printed and
// saved too.
static CheckCounts check_random(Random* random, uint64_t count, TokenArrays* vectorized, TokenArrays* scalar, uint32_t flags)
{
    uint8_t buffer[BUSTER_TEST_BUFFER_ALIGNMENT + BUSTER_TEST_RANDOM_INPUT_BYTES] __attribute__((aligned(BUSTER_TEST_BUFFER_ALIGNMENT)));
    CheckCounts counts = {0};
    char name[64];
    // The bytes around an input are left from earlier ones (the lexer doesn't read them). Zeroing the buffer first
    // makes them the same on every run.
    memset(buffer, 0, sizeof(buffer));
    for (uint64_t i = 0; i < count; i += 1)
    {
        const uint64_t misalignment = random_next(random) % BUSTER_TEST_BUFFER_ALIGNMENT;
        const String8 input = fuzz_input(random, buffer + misalignment, BUSTER_TEST_RANDOM_INPUT_BYTES, flags);
        snprintf(name, sizeof(name), "random input %lu", (unsigned long)i);
        const bool reported = counts.different < BUSTER_TEST_REPORTED_INPUTS;
        Comparison comparison = compare(input, vectorized, scalar, flags, reported ? name : 0);
        if (comparison == COMPARISON_DIFFERENT && reported)
        {
            save_failure(input, counts.different);
        }
        counts.different += comparison == COMPARISON_DIFFERENT;
        counts.fell_back += comparison == COMPARISON_FELL_BACK;
    }
    printf("random: %lu of %lu inputs differ, %lu need the scalar lexer for a part\n", (unsigned long)counts.different, (unsigned long)count,
           (unsigned long)counts.fell_back);
    return counts;
}

// ---------------------------------------------------------------------------------------------------------------------
// Page edges

// The name of the input being compared, for report_fault.
static char page_edge_input[128];
static int page_edge_input_length;

// The SIGSEGV handler while check_page_edges runs: a lexer read a guard page, outside of the input in page_edge_input.
// It only makes async-signal-safe calls.
static void report_fault(int signal_number)
{
    static const char prefix[] = "page edges: ";
    static const char suffix[] = " faults: a lexer reads outside the source\n";
    ssize_t written = write(STDOUT_FILENO, prefix, BUSTER_COMPILE_TIME_STRING_LENGTH(prefix));
    written += write(STDOUT_FILENO, page_edge_input, (size_t)page_edge_input_length);
    written += write(STDOUT_FILENO, suffix, BUSTER_COMPILE_TIME_STRING_LENGTH(suffix));
    (void)written;
    (void)signal_number;
    _exit(1);
}

// Writes template `index` (BUSTER_TEST_PAGE_EDGE_LENGTHS bytes) to `buffer`: for index 0 the snippet of `flags`'
// language repeated, and then random inputs one after the other, then comment regressions. The extra templates do not
// consume the random sequence. Prefixes and suffixes move their delimiters across every alignment and cut both complete
// and malformed comments.
static void page_edge_template(Random* random, uint64_t index, uint8_t* buffer, uint32_t flags)
{
    if (index > BUSTER_TEST_PAGE_EDGE_RANDOM_TEMPLATES)
    {
        static const char body[] = " words 123 'quotes' \"strings\" // ignored **\n";
        const uint64_t variant = index - BUSTER_TEST_PAGE_EDGE_RANDOM_TEMPLATES - 1;
        const char* prefix = variant == 0 ? "/*" : variant == 1 ? "#define X a /*" : "// lead\\\ncontinued\n/*";
        const char* suffix = "*/ + b;\n// last comment";
        const uint64_t suffix_start = BUSTER_TEST_PAGE_EDGE_LENGTHS - strlen(suffix);
        for (uint64_t i = 0; i < BUSTER_TEST_PAGE_EDGE_LENGTHS; i += 1)
        {
            buffer[i] = (uint8_t)body[i % BUSTER_COMPILE_TIME_STRING_LENGTH(body)];
        }
        memcpy(buffer, prefix, strlen(prefix));
        memcpy(buffer + suffix_start, suffix, strlen(suffix));
    }
    else
    {
        const String8 snippet_text = test_snippet(flags);
        const char* snippet = snippet_text.pointer;
        const uint64_t snippet_length = snippet_text.length;
        uint64_t length = 0;
        while (length < BUSTER_TEST_PAGE_EDGE_LENGTHS)
        {
            if (index == 0)
            {
                const uint64_t copied = snippet_length < BUSTER_TEST_PAGE_EDGE_LENGTHS - length ? snippet_length : BUSTER_TEST_PAGE_EDGE_LENGTHS - length;
                memcpy(buffer + length, snippet, copied);
                length += copied;
            }
            else
            {
                length += fuzz_input(random, buffer + length, BUSTER_TEST_RANDOM_INPUT_BYTES, flags).length;
            }
        }
    }
}

_Static_assert(BUSTER_TEST_PAGE_EDGE_LENGTHS <= BUSTER_TEST_MAX_INPUT_BYTES, "a page edge input doesn't fit in the output arrays");

// Compares the paths on every prefix and suffix of each template, ending at the end of a readable page and starting at
// the start of one, and prints the counts. The first inputs that differ are printed too. A read outside of an input
// faults, and report_fault ends the program.
static CheckCounts check_page_edges(Random* random, TokenArrays* vectorized, TokenArrays* scalar, uint32_t flags)
{
    // A template, with room for the last random input to run past BUSTER_TEST_PAGE_EDGE_LENGTHS.
    uint8_t template[BUSTER_TEST_PAGE_EDGE_LENGTHS + BUSTER_TEST_RANDOM_INPUT_BYTES];
    const uint64_t page_bytes = (uint64_t)sysconf(_SC_PAGESIZE);
    CheckCounts counts = {0};
    // A guard page, the readable page and another guard page.
    uint8_t* mapping = mmap(0, 3 * page_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* page = mapping + page_bytes;
    struct sigaction action = { .sa_handler = report_fault };
    // Lines printed so far must not be lost if report_fault ends the program.
    fflush(stdout);
    if (mapping == MAP_FAILED || page_bytes < BUSTER_TEST_PAGE_EDGE_LENGTHS || mprotect(page, page_bytes, PROT_READ | PROT_WRITE) != 0 ||
        sigaction(SIGSEGV, &action, 0) != 0)
    {
        fprintf(stderr, "page edges: can't set up the guard pages\n");
        counts.different = 1;
    }
    else
    {
        for (uint64_t index = 0; index < BUSTER_TEST_PAGE_EDGE_TEMPLATES; index += 1)
        {
            page_edge_template(random, index, template, flags);
            for (uint64_t length = 0; length < BUSTER_TEST_PAGE_EDGE_LENGTHS; length += 1)
            {
                for (uint64_t suffix = 0; suffix < 2; suffix += 1)
                {
                    const uint8_t* content = suffix ? template + BUSTER_TEST_PAGE_EDGE_LENGTHS - length : template;
                    for (uint64_t at_start = 0; at_start < 2; at_start += 1)
                    {
                        // Zeros around every input, so that it only depends on its content and position.
                        const uint64_t offset = at_start ? 0 : page_bytes - length;
                        memset(page, 0, page_bytes);
                        memcpy(page + offset, content, length);
                        const String8 input = { .pointer = (char*)page + offset, .length = length };
                        page_edge_input_length = snprintf(page_edge_input, sizeof(page_edge_input), "%s of %lu bytes of template %lu %s",
                                                          suffix ? "suffix" : "prefix", (unsigned long)length, (unsigned long)index,
                                                          at_start ? "at a page start" : "at a page end");
                        const bool reported = counts.different < BUSTER_TEST_REPORTED_INPUTS;
                        Comparison comparison = compare(input, vectorized, scalar, flags, reported ? page_edge_input : 0);
                        if (comparison == COMPARISON_DIFFERENT && reported)
                        {
                            fflush(stdout);
                        }
                        counts.different += comparison == COMPARISON_DIFFERENT;
                        counts.fell_back += comparison == COMPARISON_FELL_BACK;
                    }
                }
            }
        }
        action.sa_handler = SIG_DFL;
        sigaction(SIGSEGV, &action, 0);
        printf("page edges: %lu of %lu inputs differ, %lu need the scalar lexer for a part\n", (unsigned long)counts.different,
               (unsigned long)(BUSTER_TEST_PAGE_EDGE_TEMPLATES * BUSTER_TEST_PAGE_EDGE_LENGTHS * 4), (unsigned long)counts.fell_back);
    }

    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 3 * page_bytes);
    }
    return counts;
}

// ---------------------------------------------------------------------------------------------------------------------
// Comment delimiters

// The leading-comment scanner starts its loads after the opener, independently of the source's alignment: the
// lengths sweep delimiter positions around those load boundaries. The comments after "#define X a" are in the chunk
// loop, whose 512-byte chunks start at the 64-byte boundary at or before the source. There, the closer's '*' is the
// last byte of the first chunk at lengths 495 to 497 (alignments 2 to 0), and of the second, which is wholly inside
// the comment, at lengths 1008 and 1009 (alignments 1 and 0; 1010 puts it just after): the next chunk finds the closer
// by the '*' it carries. The last variant puts a slash right before the closer: the vectorized path leaves a slash,
// a star and a slash to the scalar lexer. (The source of these tests doesn't have one, which would send a part of it
// to the scalar lexer in `benchmark-lexer src`.)
static bool check_comments(TokenArrays* vectorized, TokenArrays* scalar, uint32_t flags)
{
    static const uint32_t lengths[] = {0, 1, 31, 62, 63, 64, 65, 126, 127, 128, 495, 496, 497, 511, 512, 513,
                                       1008, 1009, 1010, 1023, 1024, 1025};
    static const char* const prefixes[] = {"/*", "//", "#define X a /*", "/*", "#define X a /*"};
    static const char* const suffixes[] = {"*/ int x;\n", "\n int x;\n", "*/ + b\n", "*\\\n/ int x;\n", "/" "*/ + b\n"};
    // The first three must stay vectorized; the fourth splices its closer, and the fifth has a slash before it.
    static const bool may_fall_back[] = {false, false, false, true, true};
    uint8_t buffer[BUSTER_TEST_SNIPPET_BUFFER_BYTES] __attribute__((aligned(BUSTER_TEST_BUFFER_ALIGNMENT)));
    uint64_t checked = 0;
    uint64_t failed = 0;
    for (uint64_t alignment = 0; alignment < BUSTER_TEST_BUFFER_ALIGNMENT; alignment += 1)
    {
        for (uint64_t variant = 0; variant < BUSTER_ARRAY_LENGTH(prefixes); variant += 1)
        {
            for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(lengths); i += 1)
            {
                char* source = (char*)buffer + alignment;
                const uint64_t prefix_bytes = strlen(prefixes[variant]);
                const uint64_t suffix_bytes = strlen(suffixes[variant]);
                const uint64_t length = prefix_bytes + lengths[i] + suffix_bytes;
                memset(buffer, 0, sizeof(buffer));
                memcpy(source, prefixes[variant], prefix_bytes);
                memset(source + prefix_bytes, 'a', lengths[i]);
                memcpy(source + prefix_bytes + lengths[i], suffixes[variant], suffix_bytes);
                char name[128];
                snprintf(name, sizeof(name), "comment (alignment %lu, variant %lu, length %u)",
                         (unsigned long)alignment, (unsigned long)variant, lengths[i]);
                const Comparison comparison = compare((String8){ .pointer = source, .length = length }, vectorized, scalar, flags,
                                                      failed < BUSTER_TEST_REPORTED_INPUTS ? name : 0);
                const bool unexpected_fallback = !may_fall_back[variant] && comparison == COMPARISON_FELL_BACK;
                if (unexpected_fallback && failed < BUSTER_TEST_REPORTED_INPUTS)
                {
                    printf("%s unexpectedly needs the scalar lexer\n", name);
                }
                failed += comparison == COMPARISON_DIFFERENT || unexpected_fallback;
                checked += 1;
            }
        }
    }
    printf("comments: %lu of %lu cases fail\n", (unsigned long)failed, (unsigned long)checked);
    return failed == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Raw string literals

// Which of check_raw_strings' cases start a raw string literal: none of them in any mode, the R and LR ones wherever C
// or C++ has raw string literals, and the u8R, uR and UR ones where those prefixes do too (C++ and C11's GNU modes).
typedef enum RawCase
{
    RAW_CASE_NEVER,
    RAW_CASE_RAW,
    RAW_CASE_UTF,
} RawCase;

// Each case of check_raw_strings starts at the last few bytes before 64-byte and 512-byte boundaries and the first ones
// after (from the 64-byte-aligned buffer the source starts in, where the chunks start too), and at the source's start.
static const uint32_t raw_string_starts[] = {56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 500, 504, 505, 506, 507, 508, 509, 510, 511, 512, 513};

// Raw string literals of every prefix and of valid and invalid delimiters, unterminated and over lines, and identifiers
// and pp-numbers before a quote that start none, with each byte of the prefix at each place around the vector and
// chunk boundaries (raw_string_starts) at every misalignment from a cache line, after padding of spaces and newlines.
// The paths must agree, the vectorized path must leave each raw string literal to the scalar lexer (it lexes none), and
// the others must stay on it: before C11, u8R, uR and UR may take the scalar lexer, which lexes them as identifiers.
static bool check_raw_strings(TokenArrays* vectorized, TokenArrays* scalar, uint32_t flags)
{
    static const struct
    {
        const char* text;
        RawCase raw;
    } cases[] = {
        { "R\"(a)\"", RAW_CASE_RAW },
        { "LR\"(b)\"", RAW_CASE_RAW },
        { "u8 R\"(c)\"", RAW_CASE_RAW },
        { "R\"x(d\")x\"", RAW_CASE_RAW },
        { "R\"(e\n f)\"", RAW_CASE_RAW },
        { "R\"0123456789abcdef(g)0123456789abcdef\"", RAW_CASE_RAW },
        { "R\"0123456789abcdefg(h)\"", RAW_CASE_RAW },
        { "R\"i j(k)\"", RAW_CASE_RAW },
        { "R\"(l\\\n)\"", RAW_CASE_RAW },
        { "u8R\"(m)\"", RAW_CASE_UTF },
        { "uR\"(n)\"", RAW_CASE_UTF },
        { "UR\"x(o)x\"", RAW_CASE_UTF },
        { "xR\"(p)\"", RAW_CASE_NEVER },
        { "RR\"(q)\"", RAW_CASE_NEVER },
        { "xLR\"(r)\"", RAW_CASE_NEVER },
        { "LLR\"(s)\"", RAW_CASE_NEVER },
        { "xu8R\"(t)\"", RAW_CASE_NEVER },
        { "u88R\"(u)\"", RAW_CASE_NEVER },
        { "_R\"(v)\"", RAW_CASE_NEVER },
        { "$R\"(w)\"", RAW_CASE_NEVER },
        { "9R\"(x)\"", RAW_CASE_NEVER },
        { ".5LR\"(y)\"", RAW_CASE_NEVER },
        { "1e+R\"(z)\"", RAW_CASE_NEVER },
        { "R'c'", RAW_CASE_NEVER },
        { "LR'c'", RAW_CASE_NEVER },
        { "R (a)", RAW_CASE_NEVER },
        { "\"R\"(b)", RAW_CASE_NEVER },
        { "PRIxPTR\"\\n\"", RAW_CASE_NEVER },
    };
    static const char tail[] = " t; /* tail */ y = 1;\n";
    const bool raw_strings = (flags & (C_TOKENIZER_CPLUSPLUS | C_TOKENIZER_RAW_STRINGS)) != 0;
    const bool utf_raw_strings = (flags & C_TOKENIZER_CPLUSPLUS) || (flags & C_TOKENIZER_UTF_RAW_STRINGS);
    uint8_t buffer[BUSTER_TEST_SNIPPET_BUFFER_BYTES] __attribute__((aligned(BUSTER_TEST_BUFFER_ALIGNMENT)));
    uint64_t checked = 0;
    uint64_t failed = 0;
    for (uint64_t misalignment = 0; misalignment < BUSTER_TEST_BUFFER_ALIGNMENT; misalignment += 1)
    {
        for (uint64_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
        {
            const bool raw = cases[c].raw == RAW_CASE_RAW ? raw_strings : cases[c].raw == RAW_CASE_UTF && utf_raw_strings;
            // Before C11, the vectorized path leaves u8R, uR and UR before a quote to the scalar lexer too.
            const bool may_fall_back = cases[c].raw == RAW_CASE_UTF && raw_strings;
            for (uint64_t i = 0; i <= BUSTER_ARRAY_LENGTH(raw_string_starts); i += 1)
            {
                const uint64_t start = i < BUSTER_ARRAY_LENGTH(raw_string_starts) ? raw_string_starts[i] : misalignment;
                if (start >= misalignment)
                {
                    const uint64_t text_bytes = strlen(cases[c].text);
                    memset(buffer, 0, sizeof(buffer));
                    for (uint64_t p = misalignment; p < start; p += 1)
                    {
                        buffer[p] = p % BUSTER_TEST_PADDING_LINE_BYTES == BUSTER_TEST_PADDING_LINE_BYTES - 1 ? '\n' : ' ';
                    }
                    memcpy(buffer + start, cases[c].text, text_bytes);
                    memcpy(buffer + start + text_bytes, tail, BUSTER_COMPILE_TIME_STRING_LENGTH(tail));
                    const String8 source = { .pointer = (char*)buffer + misalignment,
                                             .length = start - misalignment + text_bytes + BUSTER_COMPILE_TIME_STRING_LENGTH(tail) };
                    char name[160];
                    snprintf(name, sizeof(name), "raw strings (misalignment %lu, case %lu, start %lu)", (unsigned long)misalignment, (unsigned long)c,
                             (unsigned long)start);
                    const Comparison comparison = compare(source, vectorized, scalar, flags, failed < BUSTER_TEST_REPORTED_INPUTS ? name : 0);
                    const bool unexpected = comparison == COMPARISON_DIFFERENT || (raw && comparison != COMPARISON_FELL_BACK) ||
                                            (!raw && !may_fall_back && comparison != COMPARISON_SAME);
                    if (unexpected && comparison != COMPARISON_DIFFERENT && failed < BUSTER_TEST_REPORTED_INPUTS)
                    {
                        printf("%s %s the scalar lexer\n", name, raw ? "doesn't take" : "takes");
                    }
                    failed += unexpected;
                    checked += 1;
                }
            }
        }
    }
    printf("raw strings: %lu of %lu cases fail\n", (unsigned long)failed, (unsigned long)checked);
    return failed == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// run_c_lexer_tests

// Parses a decimal number of up to 64 bits into `value`. Returns false if `text` isn't one.
static bool parse_u64(const char* text, uint64_t* value)
{
    uint64_t number = 0;
    bool valid = text[0] != 0;
    for (uint64_t i = 0; valid && text[i] != 0; i += 1)
    {
        const uint64_t digit = (uint64_t)text[i] - '0';
        valid = digit <= 9 && number <= (UINT64_MAX - digit) / 10;
        number = valid ? number * 10 + digit : number;
    }
    *value = number;
    return valid;
}

// Runs the tests with the `arguments` that follow "test-lexer" on the command line of `program`, and returns the exit
// status for it.
static int run_c_lexer_tests(const char* program, int argument_count, char** arguments)
{
    // The output arrays for every input: static rather than 150 KB of stack.
    static TokenArrays vectorized;
    static TokenArrays scalar;
    int result = 1;
    uint32_t flags = 0;
    uint64_t random_count = BUSTER_TEST_DEFAULT_RANDOM_INPUTS;
    Random random = { .state = BUSTER_TEST_GOLDEN_RATIO };
    bool valid_arguments = true;
    for (int i = 0; valid_arguments && i < argument_count; i += 1)
    {
        if (strcmp(arguments[i], "--cplusplus") == 0)
        {
            flags |= C_TOKENIZER_CPLUSPLUS;
        }
        else if (strcmp(arguments[i], "--raw-strings") == 0 || strcmp(arguments[i], "--raw-strings-c99") == 0)
        {
            flags |= C_TOKENIZER_RAW_STRINGS | (arguments[i][13] == 0 ? C_TOKENIZER_UTF_RAW_STRINGS : 0);
        }
        else if (strcmp(arguments[i], "--fuzz") == 0 && i + 1 < argument_count)
        {
            i += 1;
            valid_arguments = parse_u64(arguments[i], &random_count);
        }
        else if (strcmp(arguments[i], "--seed") == 0 && i + 1 < argument_count)
        {
            uint64_t seed;
            i += 1;
            valid_arguments = parse_u64(arguments[i], &seed);
            random.state = seed * BUSTER_TEST_GOLDEN_RATIO + 1;
        }
        else
        {
            valid_arguments = false;
        }
    }

    if (!valid_arguments)
    {
        fprintf(stderr, "usage: %s test-lexer [--cplusplus] [--raw-strings | --raw-strings-c99] [--fuzz N] [--seed N]\n", program);
    }
    else
    {
        const CheckCounts snippet = check_snippet(&vectorized, &scalar, flags);
        const CheckCounts random_inputs = check_random(&random, random_count, &vectorized, &scalar, flags);
        // The page edge inputs continue the random sequence, so that the random check's inputs stay upstream's.
        const CheckCounts page_edges = check_page_edges(&random, &vectorized, &scalar, flags);
        const bool comments = check_comments(&vectorized, &scalar, flags);
        const bool raw_strings = check_raw_strings(&vectorized, &scalar, flags);
        // Random inputs and cut templates are meant to need the scalar lexer at times, but the snippet is meant to stay
        // on the vectorized path.
        result = snippet.different == 0 && snippet.fell_back == 0 && random_inputs.different == 0 && page_edges.different == 0 && comments && raw_strings
                     ? 0
                     : 1;
    }
    return result;
}
