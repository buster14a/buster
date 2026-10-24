// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The windowed preparation's checks (#116), for test-preprocessor; c_preprocessor_test.c includes this file. A large file
// is lexed and refined a window at a time (pp_prepare_windows), and its records must be the whole file's prepared at once
// (pp_prepare_whole), byte for byte: its tokens, values, leading-space words, raw errors, unterminated comment and
// directive table. The sources are generated from the constructs a window's end can cut: block comments and raw string
// literals over many lines, line splices inside tokens and between them, "\r\n", lines longer than a window, literals
// left open at a line's end, a byte order mark, and a comment or raw string left open at the file's end; with
// identifiers that share long prefixes, so that refinement's candidates and new spellings come in every block. Each is
// prepared whole by the vectorized kernels and by their scalar twins, which must agree too, and in windows of 1 byte to
// 8 KB, in five language modes; sources of up to 96 KB give refinement many blocks of BUSTER_REFINE_BLOCK_TOKENS, which
// the windows complete at every place.
//
// Layout: the random sources, the preparations and their comparison, the line ends' scan (pp_window_has_line_end) against
// a byte-at-a-time one, and pp_test_windows.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// The sources pp_test_windows checks, and the most bytes each may have.
#define BUSTER_PP_WINDOW_TEST_SOURCES 240
#define BUSTER_PP_WINDOW_TEST_MAX_BYTES (96u << 10)
// The room for a source: a piece may pass the target size by a long line's bytes.
#define BUSTER_PP_WINDOW_TEST_CAPACITY (BUSTER_PP_WINDOW_TEST_MAX_BYTES + (64u << 10))
#define BUSTER_PP_WINDOW_TEST_ARENA_BYTES (1ull << 30)

// ---------------------------------------------------------------------------------------------------------------------
// Random sources

// A splitmix64 step.
static uint64_t pp_window_test_random(uint64_t* state)
{
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

typedef struct PPWindowTestText PPWindowTestText;
struct PPWindowTestText
{
    uint8_t* bytes;
    uint32_t length;
};

static void pp_window_test_append(PPWindowTestText* text, const char* piece)
{
    const uint32_t length = (uint32_t)strlen(piece);
    if (text->length + length <= BUSTER_PP_WINDOW_TEST_CAPACITY)
    {
        memcpy(text->bytes + text->length, piece, length);
        text->length += length;
    }
}

static void pp_window_test_byte(PPWindowTestText* text, uint8_t byte)
{
    if (text->length < BUSTER_PP_WINDOW_TEST_CAPACITY)
    {
        text->bytes[text->length] = byte;
        text->length += 1;
    }
}

// Names that share long prefixes (refinement's candidates are spellings past 11 bytes whose first 12 match), keywords,
// directive names and the preprocessor's names.
static const char* const pp_window_test_names[] = {
    "x", "i", "rc", "db", "pPager", "int", "return", "static", "struct", "unsigned", "const", "define", "ifdef", "endif",
    "__VA_ARGS__", "defined", "sqlite3VdbeMemRelease", "sqlite3VdbeMemSetStr", "sqlite3VdbeMemSetInt64", "sqlite3VdbeMem",
    "a_name_of_exactly_32_bytes_long_", "a_name_of_exactly_32_bytes_long_x", "an_identifier_that_is_much_longer_than_sixty_four_bytes_in_all_of_it",
    "_Static_assert", "__attribute__", "typedef", "R", "u8", "LR", "L",
};

static const char* const pp_window_test_punctuators[] = {
    "+", "-", "*", "/", "%", "++", "--", "->", "<<=", ">>=", "...", ".", "::", "<:", ":>", "<%", "%>", "%:", "%:%:", "#", "##", "(",
    ")", "{", "}", "[", "]", ";", ",", "=", "==", "!=", "<", ">", "&&", "||", "?", ":", "~", "^", "|=",
};

static const char* const pp_window_test_literals[] = {
    "0", "42", "0x1fUL", "1e+5", "1.5f", ".5", "1'000", "0b1010", "'a'", "'\\n'", "u8'a'", "L'x'", "\"text\"", "\"a\\\"b\"", "u8\"s\"",
    "u\"s\"", "U\"s\"", "L\"wide\"", "\"open", "'open", "R\"(raw)\"", "LR\"d(raw)d\"", "u8R\"x(raw)x\"", "R\"(two\nlines)\"",
    "R\"delim(a ) b )delim\"", "R\"bad delimiter(x)bad delimiter\"",
};

static const char* const pp_window_test_gaps[] = {
    " ", " ", " ", "\t", "\n", "\n", "\r\n", "\\\n", "\\  \n", "\\\r\n", "/* c */", "// line\n", "/* a\nb\r\nc */", "/* *\\\n/", "\n\n\n",
};

static const char* const pp_window_test_lines[] = {
    "\n#define F(a, ...) a ## __VA_ARGS__\n", "\n# if defined(X) && X > 1\n", "\n#endif\n", "\n%:include <x.h>\n", "\n#\n",
    "\n#ifndef GUARD\n#define GUARD\n", "\n#pragma once\n", "\n#line 7 \"f.c\"\n",
};

// An identifier: a name of the list, one of letters and digits, or one with a universal character name or UTF-8, or a line
// splice inside it.
static void pp_window_test_identifier(PPWindowTestText* text, uint64_t* random)
{
    const uint64_t r = pp_window_test_random(random);
    if (r % 4 != 0)
    {
        pp_window_test_append(text, pp_window_test_names[(r >> 8) % BUSTER_ARRAY_LENGTH(pp_window_test_names)]);
    }
    else
    {
        static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_$0123456789";
        const uint32_t length = 1 + (uint32_t)((r >> 8) % 40);
        for (uint32_t i = 0; i < length; i += 1)
        {
            const uint64_t c = pp_window_test_random(random);
            pp_window_test_byte(text, (uint8_t)letters[i == 0 ? c % 53 : c % 63]);
        }
        const uint32_t extra = (uint32_t)((r >> 16) % 16);
        if (extra == 0)
        {
            pp_window_test_append(text, "\\u00e9z");
        }
        else if (extra == 1)
        {
            pp_window_test_append(text, "\xc3\xa9z");
        }
        else if (extra == 2)
        {
            pp_window_test_append(text, "\\\nsplit");
        }
    }
}

// A line of `bytes` or more without a newline: tokens, or one long comment or literal.
static void pp_window_test_long_line(PPWindowTestText* text, uint64_t* random, uint32_t bytes)
{
    const uint64_t r = pp_window_test_random(random);
    const uint32_t end = text->length + bytes;
    if (r % 3 == 0)
    {
        pp_window_test_append(text, "/*");
        while (text->length < end && text->length < BUSTER_PP_WINDOW_TEST_CAPACITY)
        {
            pp_window_test_append(text, " long comment");
        }
        pp_window_test_append(text, " */");
    }
    else if (r % 3 == 1)
    {
        pp_window_test_append(text, "\"");
        while (text->length < end && text->length < BUSTER_PP_WINDOW_TEST_CAPACITY)
        {
            pp_window_test_append(text, "long string ");
        }
        pp_window_test_append(text, "\"");
    }
    else
    {
        while (text->length < end && text->length < BUSTER_PP_WINDOW_TEST_CAPACITY)
        {
            pp_window_test_identifier(text, random);
            pp_window_test_append(text, (pp_window_test_random(random) & 7) == 0 ? "\\\n" : " ");
        }
    }
}

// A source of about `target` bytes.
static void pp_window_test_source(PPWindowTestText* text, uint64_t* random, uint32_t target)
{
    text->length = 0;
    if ((pp_window_test_random(random) & 7) == 0)
    {
        pp_window_test_append(text, "\xef\xbb\xbf");
    }
    while (text->length < target)
    {
        const uint64_t r = pp_window_test_random(random);
        const uint32_t kind = (uint32_t)(r % 1000);
        const uint64_t pick = r >> 10;
        if (kind < 520)
        {
            pp_window_test_identifier(text, random);
        }
        else if (kind < 720)
        {
            pp_window_test_append(text, pp_window_test_punctuators[pick % BUSTER_ARRAY_LENGTH(pp_window_test_punctuators)]);
        }
        else if (kind < 800)
        {
            pp_window_test_append(text, pp_window_test_literals[pick % BUSTER_ARRAY_LENGTH(pp_window_test_literals)]);
        }
        else if (kind < 960)
        {
            pp_window_test_append(text, pp_window_test_gaps[pick % BUSTER_ARRAY_LENGTH(pp_window_test_gaps)]);
        }
        else if (kind < 990)
        {
            pp_window_test_append(text, pp_window_test_lines[pick % BUSTER_ARRAY_LENGTH(pp_window_test_lines)]);
        }
        else if (kind < 992)
        {
            pp_window_test_long_line(text, random, 256 + (uint32_t)(pick % 6000));
        }
        else if (kind < 994)
        {
            // A block comment or a raw string literal over many lines.
            const bool comment = (pick & 1) != 0;
            pp_window_test_append(text, comment ? "/*" : "R\"d(");
            const uint32_t lines = 1 + (uint32_t)(pick % 120);
            for (uint32_t line = 0; line < lines; line += 1)
            {
                pp_window_test_append(text, (pp_window_test_random(random) & 3) == 0 ? "#define not_a_directive\r\n" : " int x;\n");
            }
            pp_window_test_append(text, comment ? "*/" : ")d\"");
        }
        else if (kind < 996)
        {
            // A macro defined over many lines continued by line splices: one logical line, which may outgrow a window,
            // with splices of every form, a comment and a literal among them.
            static const char* const continuations[] = { " \\\n", " \\\n", " \\\r\n", " \\\r", " \\  \n", " /* c */ \\\n", " ?\?/\n" };
            pp_window_test_append(text, "\n#define TABLE(X)");
            const uint32_t lines = 1 + (uint32_t)(pick % 400);
            for (uint32_t line = 0; line < lines; line += 1)
            {
                pp_window_test_append(text, continuations[pp_window_test_random(random) % BUSTER_ARRAY_LENGTH(continuations)]);
                pp_window_test_append(text, "    X(");
                pp_window_test_identifier(text, random);
                pp_window_test_append(text, (line & 7) == 0 ? ", \"s\")" : ", 1)");
            }
            pp_window_test_append(text, "\n");
        }
        else
        {
            static const uint8_t strays[] = { '@', '`', '\\', 0x80, 0xff, 0x7f, '\f', '\v' };
            pp_window_test_byte(text, strays[pick % sizeof(strays)]);
        }
        pp_window_test_append(text, (r >> 40) % 3 == 0 ? "\n" : " ");
    }
    const uint64_t ending = pp_window_test_random(random) % 16;
    if (ending == 0)
    {
        pp_window_test_append(text, "/* never closed\nint y;\n");
    }
    else if (ending == 1)
    {
        pp_window_test_append(text, "R\"d(never closed\n");
    }
    else if (ending == 2)
    {
        pp_window_test_append(text, "x\\");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The preparations and their comparison

// A preparation's memory: an intern table, the lexer's columns and the directive tables' arena.
typedef struct PPWindowTestRun PPWindowTestRun;
struct PPWindowTestRun
{
    InternTable table;
    Arena columns[3];
    Arena arena;
};

static bool pp_window_test_initialize(PPWindowTestRun* run)
{
    *run = (PPWindowTestRun){ 0 };
    bool result = intern_initialize(&run->table);
    for (uint32_t c = 0; c < 3; c += 1)
    {
        run->columns[c] = arena_reserve(BUSTER_PP_WINDOW_TEST_ARENA_BYTES);
        result &= run->columns[c].base != 0;
    }
    run->arena = arena_reserve(BUSTER_PP_WINDOW_TEST_ARENA_BYTES);
    return result && run->arena.base != 0;
}

static void pp_window_test_release(PPWindowTestRun* run)
{
    intern_release(&run->table);
    for (uint32_t c = 0; c < 3; c += 1)
    {
        arena_release(&run->columns[c]);
    }
    arena_release(&run->arena);
}

// Prepares `text` from a table of the predefined names and empty arenas: whole (`window` 0) or a window at a time.
static PPPrepareStatus pp_window_test_prepare(PPWindowTestRun* run, const PPPrepare* prepare, const PPWindowTestText* text, uint32_t window, PPFileData* data)
{
    intern_reset(&run->table);
    for (uint32_t c = 0; c < 3; c += 1)
    {
        arena_pop_to(&run->columns[c], 0);
    }
    arena_pop_to(&run->arena, 0);
    *data = (PPFileData){ .bytes = text->bytes, .length = text->length, .record = BUSTER_PP_NONE };
    return window == 0 ? pp_prepare_whole(prepare, &run->table, run->columns, &run->arena, data, 0)
                       : pp_prepare_windows(prepare, &run->table, run->columns, &run->arena, data, window, 0);
}

// The first token at which two preparations' columns differ, or the count if none does.
static uint32_t pp_window_test_first_difference(const PPFileData* a, const PPFileData* b)
{
    uint32_t i = 0;
    while (i < a->token_count && a->kinds[i] == b->kinds[i] && a->starts[i] == b->starts[i] && a->values[i] == b->values[i] &&
           ((a->space[i / 64] ^ b->space[i / 64]) >> (i % 64) & 1) == 0)
    {
        i += 1;
    }
    return i;
}

// Are two preparations' records the same: their outcome, tokens, values, leading-space words, raw errors, unterminated
// comment and directive tables? `what` names the second for a difference's report.
static bool pp_window_test_same(PPPrepareStatus a_status, const PPFileData* a, PPPrepareStatus b_status, const PPFileData* b, const char* what, uint64_t source,
                                uint32_t window)
{
    bool result = a_status == b_status;
    uint32_t difference = 0;
    if (result && a_status == PP_PREPARE_DONE)
    {
        const uint32_t count = a->token_count;
        result = b->token_count == count && a->unterminated_comment == b->unterminated_comment &&
                 a->unterminated_comment_starts_line == b->unterminated_comment_starts_line;
        difference = result ? pp_window_test_first_difference(a, b) : 0;
        result = result && difference == count && memcmp(a->space, b->space, (count + 63) / 64 * sizeof(uint64_t)) == 0;
        result = result && a->raw_error_count == b->raw_error_count && a->directive_count == b->directive_count && a->note_count == b->note_count &&
                 a->guard == b->guard && a->guard_opener == b->guard_opener;
        for (uint32_t e = 0; result && e < a->raw_error_count; e += 1)
        {
            result = a->raw_errors[e].token == b->raw_errors[e].token && a->raw_errors[e].offset == b->raw_errors[e].offset && a->raw_errors[e].kind == b->raw_errors[e].kind;
        }
        for (uint32_t d = 0; result && d < a->directive_count; d += 1)
        {
            result = a->directive_hash[d] == b->directive_hash[d] && a->directive_end[d] == b->directive_end[d] && a->directive_kind[d] == b->directive_kind[d] &&
                     a->directive_next[d] == b->directive_next[d];
        }
        for (uint32_t n = 0; result && n < a->note_count; n += 1)
        {
            result = a->note_directives[n] == b->note_directives[n] && a->note_kinds[n] == b->note_kinds[n];
        }
    }
    if (!result && !pp_test_quiet)
    {
        printf("windows: source %lu, %s (window %u): differs from the whole file's vectorized preparation (status %d/%d, tokens %u/%u, first difference at "
               "token %u)\n",
               (unsigned long)source, what, window, (int)a_status, (int)b_status, a->token_count, b->token_count, difference);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The line ends' scan

// pp_window_has_line_end a byte at a time.
static bool pp_window_test_has_line_end(const uint8_t* bytes, uint64_t at, uint64_t end)
{
    bool result = false;
    for (uint64_t p = at; p < end; p += 1)
    {
        const bool after_backslash = p > at && bytes[p - 1] == '\\';
        const bool after_spliced_return = p > at + 1 && bytes[p - 1] == '\r' && bytes[p - 2] == '\\';
        result |= (bytes[p] == '\r' && !after_backslash) || (bytes[p] == '\n' && !after_backslash && !after_spliced_return);
    }
    return result;
}

// pp_window_has_line_end against the byte-at-a-time scan: random bytes of backslashes, line ends, blanks and letters, of
// 0 to 300 bytes, each scanned from four starts to its end at a PROT_NONE page (a read past the end faults). Returns the
// differences and adds the checks to `checks`.
static uint64_t pp_window_test_line_ends(uint64_t* random, uint64_t* checks)
{
    static const uint8_t alphabet[] = { '\\', '\\', '\r', '\n', '\n', ' ', 'a', '\t' };
    const uint64_t page_bytes = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, 2 * page_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint64_t failures = mapping == MAP_FAILED || mprotect(mapping + page_bytes, page_bytes, PROT_NONE) != 0;
    for (uint32_t length = 0; failures == 0 && length <= 300; length += 1)
    {
        for (uint32_t trial = 0; trial < 24; trial += 1)
        {
            uint8_t* bytes = mapping + page_bytes - length;
            // Mostly bytes that aren't line ends, so that a scan goes through several chunks before it finds one.
            const uint64_t density = 1 + pp_window_test_random(random) % 64;
            for (uint32_t i = 0; i < length; i += 1)
            {
                const uint64_t r = pp_window_test_random(random);
                bytes[i] = r % density == 0 ? alphabet[(r >> 8) % sizeof(alphabet)] : (r >> 16) % 3 == 0 ? (uint8_t)'\\' : (uint8_t)'x';
                // A splice's newline after most backslashes.
                if (bytes[i] == '\\' && i + 1 < length && (r >> 24) % 2 == 0)
                {
                    i += 1;
                    bytes[i] = (r >> 25) % 4 == 0 ? (uint8_t)'\r' : (uint8_t)'\n';
                }
            }
            for (uint32_t at = 0; at < 4 && at <= length; at += 1)
            {
                const bool vector = pp_window_has_line_end(mapping, page_bytes - length + at, page_bytes);
                const bool scalar = pp_window_test_has_line_end(mapping, page_bytes - length + at, page_bytes);
                *checks += 1;
                if (vector != scalar && failures < 4 && !pp_test_quiet)
                {
                    printf("windows: the line ends' scan of %u bytes from %u gives %d, a byte at a time %d\n", length, at, vector, scalar);
                }
                failures += vector != scalar;
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 2 * page_bytes);
    }
    return failures;
}

// ---------------------------------------------------------------------------------------------------------------------
// pp_test_windows

// BUSTER_PP_WINDOW_TEST_SOURCES sources from `seed`, each in one of five language modes (with C's raw strings and
// without, digit separators or not, without "//" comments and digraphs, and with literal prefixes to split), prepared
// whole by both kernels and in windows of several sizes.
static bool pp_test_windows(uint64_t seed)
{
    static const uint32_t windows[] = { 1, 7, 64, 100, 333, 1024, 4096, 8192 };
    static const CLanguage languages[] = {
        { .standard = 2017, .gnu = true },
        { .standard = 2023, .gnu = false },
        { .standard = 2023, .gnu = true },
        { .standard = 1989, .gnu = false },
        { .standard = 1999, .gnu = true },
    };
    static PPWindowTestRun runs[3];
    PPWindowTestText text = { .bytes = malloc(BUSTER_PP_WINDOW_TEST_CAPACITY) };
    bool initialized = text.bytes != 0;
    for (uint32_t k = 0; k < 3; k += 1)
    {
        initialized &= pp_window_test_initialize(&runs[k]);
    }
    uint64_t random = seed ^ 0x116116116ull;
    uint64_t line_end_checks = 0;
    const uint64_t line_end_failures = pp_window_test_line_ends(&random, &line_end_checks);
    uint64_t failures = 0;
    uint64_t windowed = 0;
    uint64_t tokens = 0;
    for (uint64_t s = 0; initialized && s < BUSTER_PP_WINDOW_TEST_SOURCES; s += 1)
    {
        const uint32_t target = 1 + (uint32_t)(pp_window_test_random(&random) % BUSTER_PP_WINDOW_TEST_MAX_BYTES);
        pp_window_test_source(&text, &random, target);
        const PPOptions options = { .language = languages[s % BUSTER_ARRAY_LENGTH(languages)] };
        PPPrepare prepare = pp_prepare_options(&options);
        PPPrepare scalar = prepare;
        scalar.vectorized = false;
        PPFileData whole;
        PPFileData twin;
        const PPPrepareStatus whole_status = pp_window_test_prepare(&runs[0], &prepare, &text, 0, &whole);
        const PPPrepareStatus twin_status = pp_window_test_prepare(&runs[1], &scalar, &text, 0, &twin);
        bool same = pp_window_test_same(whole_status, &whole, twin_status, &twin, "the scalar twins", s, 0);
        tokens += whole.token_count;
        // c_tokenize_lines is not for a source that the scalar lexer takes whole (pp_prepare_into never gives it one).
        const bool windows_apply = !c_tokenize_scalar_only(text.bytes, text.length, prepare.refine_flags);
        for (uint32_t w = 0; windows_apply && w < BUSTER_ARRAY_LENGTH(windows); w += 1)
        {
            // The small windows on every source; the larger ones on sources several times their size.
            if (windows[w] <= 100 || (uint64_t)windows[w] * 4 <= text.length)
            {
                PPFileData data;
                const PPPrepareStatus status = pp_window_test_prepare(&runs[2], &prepare, &text, windows[w], &data);
                same &= pp_window_test_same(whole_status, &whole, status, &data, "windows", s, windows[w]);
                windowed += 1;
            }
        }
        failures += !same;
    }
    for (uint32_t k = 0; k < 3; k += 1)
    {
        pp_window_test_release(&runs[k]);
    }
    free(text.bytes);
    printf("windows: %lu of %u sources differ (%lu windowed preparations, %lu tokens); the line ends' scan: %lu of %lu checks fail\n", (unsigned long)failures,
           BUSTER_PP_WINDOW_TEST_SOURCES, (unsigned long)windowed, (unsigned long)tokens, (unsigned long)line_end_failures, (unsigned long)line_end_checks);
    return initialized && failures == 0 && line_end_failures == 0;
}
