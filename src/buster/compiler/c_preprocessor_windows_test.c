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
// the windows complete at every place. Each windowed preparation's line pages are checked against a count a byte at a
// time; and sources with __LINE__ around the pages' starts are preprocessed as units (pp_test_window_units), which must
// count from the pages and give the scalar twins' output. Files made by hand check the line ends at a query's end
// (pp_test_line_ends, #441): a "\r\n" counts at its '\n', so a query that ends at the '\n' counts no line end there.
//
// Layout: the random sources, the preparations and their comparison, the line pages, lines in units, the windows' cut
// (pp_window_cut) against a byte-at-a-time one and on windows made by hand, the directives' places (pp_directive_places) against their twin, line
// ends at a query's end, and pp_test_windows.

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
    uint32_t capacity;
};

static void pp_window_test_append(PPWindowTestText* text, const char* piece)
{
    const uint32_t length = (uint32_t)strlen(piece);
    if (text->length + length <= text->capacity)
    {
        memcpy(text->bytes + text->length, piece, length);
        text->length += length;
    }
}

static void pp_window_test_byte(PPWindowTestText* text, uint8_t byte)
{
    if (text->length < text->capacity)
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
    // Named UCNs (#452), whose errors are raw errors too: in an identifier, a pp-number, an unknown token, after a line
    // splice, as a trigraph and in a literal.
    "x\\N{GREEK SMALL LETTER ALPHA}", "1\\N{DIGIT ONE}", "\\N{lower case}", "a\\\\\nN{DIGIT ZERO}", "?" "?/N{DIGIT ZERO}", "\"\\N{DIGIT ZERO}\"",
};

static const char* const pp_window_test_gaps[] = {
    " ", " ", " ", "\t", "\n", "\n", "\r\n", "\\\n", "\\  \n", "\\\r\n", "/* c */", "// line\n", "/* a\nb\r\nc */", "/* *\\\n/", "\n\n\n", "/* \\N{DIGIT ZERO} */",
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
        while (text->length < end && text->length < text->capacity)
        {
            pp_window_test_append(text, " long comment");
        }
        pp_window_test_append(text, " */");
    }
    else if (r % 3 == 1)
    {
        pp_window_test_append(text, "\"");
        while (text->length < end && text->length < text->capacity)
        {
            pp_window_test_append(text, "long string ");
        }
        pp_window_test_append(text, "\"");
    }
    else
    {
        while (text->length < end && text->length < text->capacity)
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
// The line pages

// A window-prepared file's line pages (PPFileData.line_pages) against the line ends counted a byte at a time: each
// page's entry, and the line pp_page_line gives every token's start and the end, against the count from the file's
// start. A "\r\n" ends its line at the '\n', and a '\r' alone at itself.
static bool pp_window_test_line_pages(const PPFileData* data, uint64_t source, uint32_t window)
{
    bool result = data->line_pages != 0;
    uint32_t lines = 0;
    uint32_t page = 0;
    uint32_t token = 0;
    uint32_t failed = UINT32_MAX;
    for (uint32_t at = 0; result && at <= data->length; at += 1)
    {
        if (at % BUSTER_PP_LINE_PAGE_BYTES == 0)
        {
            result = data->line_pages[page] == lines;
            page += 1;
        }
        while (result && token < data->token_count && data->starts[token] == at)
        {
            result = pp_page_line(data, at) == lines + 1;
            token += 1;
        }
        result = result && (at != data->length || pp_page_line(data, at) == lines + 1);
        failed = result ? failed : at;
        const uint8_t byte = at < data->length ? data->bytes[at] : 0;
        lines += byte == '\n' || (byte == '\r' && (at + 1 == data->length || data->bytes[at + 1] != '\n'));
    }
    if (!result && !pp_test_quiet)
    {
        printf("windows: source %lu, window %u: the line pages differ from a count a byte at a time at byte %u%s\n", (unsigned long)source, window, failed,
               data->line_pages == 0 ? " (no pages)" : "");
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Lines in units

// The sources pp_test_window_units runs as units, the fewest and the most bytes each may have (past two of each of its
// windows), and the bytes of the last one, which is past two of the default windows (BUSTER_PP_WINDOW_BYTES).
#define BUSTER_PP_WINDOW_UNIT_SOURCES 24
#define BUSTER_PP_WINDOW_UNIT_MIN_BYTES (32u << 10)
#define BUSTER_PP_WINDOW_UNIT_MAX_BYTES (96u << 10)
#define BUSTER_PP_WINDOW_UNIT_LARGE_BYTES (2u * BUSTER_PP_WINDOW_BYTES + (40u << 10))

// A source of about `target` bytes for a unit, whose questions about lines fall around each line page's start
// (BUSTER_PP_LINE_PAGE_BYTES). Its lines of tokens end in "\n", "\r\n" or a '\r' alone, and now and then a line end's
// '\r' ends a page. Its __LINE__s come just before and at a page's start and at random places; in a macro's arguments
// over several lines (the line of its ')'); through an object-like macro; spelled with a line splice; and in #if. It
// has #line and line markers among its lines, from which the printer's line markers count.
static void pp_window_test_unit_source(PPWindowTestText* text, uint64_t* random, uint32_t target)
{
    static const char* const pieces[] = {
        "int", "x", "=", "1;", "f(a, b)", "/* c */", "\"s\"", "'c'", "L", "__LINE__", "F(__LINE__,\n\n L)", "__LI\\\nNE__", "F(L, \r\n__LINE__\r\n)",
    };
    static const char* const ends[] = { "\n", "\n", "\n", "\r\n", "\r" };
    text->length = 0;
    pp_window_test_append(text, "#define F(x, y) x y\n#define L __LINE__\n");
    while (text->length < target)
    {
        const uint64_t r = pp_window_test_random(random);
        const uint32_t to_page = BUSTER_PP_LINE_PAGE_BYTES - text->length % BUSTER_PP_LINE_PAGE_BYTES;
        char directive[64];
        if (to_page == 1 && (r & 3) != 0)
        {
            // A line end whose '\r' ends the page: a "\r\n" counts at its '\n', on the next page, and a '\r' alone at
            // itself. Then a question at the next page's first byte.
            pp_window_test_append(text, (r & 4) != 0 ? "\r\n__LINE__" : "\r__LINE__");
        }
        else if (to_page <= 12 && (r & 3) == 0)
        {
            // A question whose token starts a few bytes before the page's start, or at it.
            pp_window_test_append(text, to_page <= 9 ? " __LINE__" : "__LINE__ ");
        }
        else if (r % 97 == 0)
        {
            const uint32_t number = 1 + (uint32_t)((r >> 8) % 9999);
            const uint32_t form = (uint32_t)((r >> 24) % 3);
            snprintf(directive, sizeof(directive), form == 0 ? "\n#line %u\n" : form == 1 ? "\n# %u \"lines.c\"\n" : "\n#if __LINE__ %% %u\nL\n#endif\n", number);
            pp_window_test_append(text, directive);
        }
        else
        {
            pp_window_test_append(text, pieces[(r >> 8) % BUSTER_ARRAY_LENGTH(pieces)]);
            pp_window_test_append(text, (r >> 20) % 4 == 0 ? ends[(r >> 24) % BUSTER_ARRAY_LENGTH(ends)] : " ");
        }
    }
    pp_window_test_append(text, "\n");
}

// One run of pp_test_window_units: `path` preprocessed as a unit by the scalar twins, whole, or by the vectorized kernels
// in windows of `window` bytes (0 for the default), printed with line markers by its kernels' printer into `*printed`
// (`free` it). Leaves the run to release in `pp` (which `*ready` says) and its options' memory in `arena`.
static bool pp_window_test_unit(const char* path, bool scalar, uint32_t window, InternTable* table, Preprocessor* pp, bool* ready, Arena* arena, char** printed,
                                size_t* printed_length)
{
    char* arguments[] = { (char*)"-std=c17", (char*)path };
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    *arena = arena_reserve(1ull << 20);
    bool result = arena->base != 0 && pp_parse_options(2, arguments, &options, arena, &input, &output, &markers);
    options.scalar = scalar;
    options.window_bytes = window;
    *ready = result && intern_initialize(table) && pp_initialize(pp, &options, table);
    result = *ready && pp_run(pp, path) && pp->errors == 0;
    *printed = result ? pp_test_print(pp, !scalar, true, printed_length) : 0;
    return *printed != 0;
}

// BUSTER_PP_WINDOW_UNIT_SOURCES sources from `seed` (pp_window_test_unit_source), each preprocessed as a unit in
// `directory`. The file cache prepares its file and the unit's data copies the record (pp_copy_prepared). The vectorized
// kernels prepare it in windows of several sizes: smaller than a line page, a page, a byte past one, and larger, and the
// last source in the default windows. __LINE__, #line and -E's line markers then count from its line pages, which each
// run's data of the file must have. The scalar twins prepare it whole and count from a cursor. Each run's output and the
// text -E prints with line markers must be the scalar twins', byte for byte. Returns whether every run's are.
static bool pp_test_window_units(const char* directory, uint64_t seed)
{
    static const uint32_t windows[] = { 1000, BUSTER_PP_LINE_PAGE_BYTES, BUSTER_PP_LINE_PAGE_BYTES + 1, 10000 };
    static InternTable tables[2];
    static Preprocessor runs[2];
    char path[4096];
    snprintf(path, sizeof(path), "%s/window-lines.c", directory);
    const uint32_t capacity = BUSTER_PP_WINDOW_UNIT_LARGE_BYTES + (64u << 10);
    PPWindowTestText text = { .bytes = malloc(capacity + 1), .capacity = capacity };
    const bool allocated = text.bytes != 0;
    uint64_t random = seed ^ 0x117117117ull;
    uint64_t failures = 0;
    uint64_t units = 0;
    uint64_t questions = 0;
    for (uint32_t s = 0; allocated && s < BUSTER_PP_WINDOW_UNIT_SOURCES; s += 1)
    {
        const bool large = s + 1 == BUSTER_PP_WINDOW_UNIT_SOURCES;
        const uint32_t target = large ? BUSTER_PP_WINDOW_UNIT_LARGE_BYTES
                                      : BUSTER_PP_WINDOW_UNIT_MIN_BYTES + (uint32_t)(pp_window_test_random(&random) % (BUSTER_PP_WINDOW_UNIT_MAX_BYTES - BUSTER_PP_WINDOW_UNIT_MIN_BYTES));
        pp_window_test_unit_source(&text, &random, target);
        text.bytes[text.length] = 0;
        bool ready[2] = { false, false };
        Arena arenas[2] = { 0 };
        char* printed[2] = { 0, 0 };
        size_t lengths[2] = { 0, 0 };
        const bool written = pp_test_write_file(path, (const char*)text.bytes);
        const bool twin = written && pp_window_test_unit(path, true, 0, &tables[0], &runs[0], &ready[0], &arenas[0], &printed[0], &lengths[0]);
        bool same = twin;
        for (uint32_t w = 0; twin && w < (large ? 1u : BUSTER_ARRAY_LENGTH(windows)); w += 1)
        {
            const uint32_t window = large ? 0 : windows[w];
            const uint32_t bytes = window != 0 ? window : BUSTER_PP_WINDOW_BYTES;
            bool ran = pp_window_test_unit(path, false, window, &tables[1], &runs[1], &ready[1], &arenas[1], &printed[1], &lengths[1]);
            // The file's data in the run: its bytes are the source's, and windows prepared it.
            bool paged = false;
            for (uint32_t d = 0; ran && d < runs[1].data_count; d += 1)
            {
                const PPFileData* data = &runs[1].datas[d];
                paged |= data->length == text.length && data->length >= 2ull * bytes && data->line_pages != 0;
            }
            char label[64];
            snprintf(label, sizeof(label), "windows: unit %u, window %u", s, window);
            const bool agrees = ran && paged && pp_same_streams(&runs[1], &runs[0], label) && lengths[0] == lengths[1] && memcmp(printed[0], printed[1], lengths[0]) == 0;
            if (!agrees && !pp_test_quiet)
            {
                printf("%s: %s\n", label, !ran ? "the run fails" : !paged ? "the file's data has no line pages" : "the output or its text with line markers isn't the scalar twins'");
            }
            same &= agrees;
            units += 1;
            if (ready[1])
            {
                pp_release(&runs[1]);
            }
            intern_release(&tables[1]);
            arena_release(&arenas[1]);
            free(printed[1]);
            printed[1] = 0;
        }
        // The numbers that __LINE__ gave (pp_number_token's, which a macro made).
        for (uint32_t t = 0; twin && t < runs[0].output.count; t += 1)
        {
            questions += runs[0].output.kinds[t] == C_TOKEN_NUMERIC_CONSTANT && (runs[0].output.flags[t] & BUSTER_PP_FROM_MACRO) != 0;
        }
        if (!twin && !pp_test_quiet)
        {
            printf("windows: unit %u: the scalar twins' run fails\n", s);
        }
        failures += !same;
        if (ready[0])
        {
            pp_release(&runs[0]);
        }
        intern_release(&tables[0]);
        arena_release(&arenas[0]);
        free(printed[0]);
    }
    unlink(path);
    free(text.bytes);
    printf("windows: %lu of %u sources differ as units (%lu windowed units; %lu __LINE__s in the scalar twins' outputs)\n", (unsigned long)failures,
           BUSTER_PP_WINDOW_UNIT_SOURCES, (unsigned long)units, (unsigned long)questions);
    return allocated && failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The windows' cut

// pp_window_cut a byte at a time: its line ends, and where the window is cut from the last two.
static uint64_t pp_window_test_cut(const uint8_t* bytes, uint64_t at, uint64_t end, uint64_t longest_tail)
{
    uint64_t last = UINT64_MAX;
    uint64_t before = UINT64_MAX;
    for (uint64_t p = at; p < end; p += 1)
    {
        const bool after_backslash = p > at && bytes[p - 1] == '\\';
        const bool after_spliced_return = p > at + 1 && bytes[p - 1] == '\r' && bytes[p - 2] == '\\';
        const bool crlf = p + 1 < end && bytes[p + 1] == '\n';
        const bool line_end = (bytes[p] == '\r' && !after_backslash && !crlf) || (bytes[p] == '\n' && !after_backslash && !after_spliced_return);
        before = line_end ? last : before;
        last = line_end ? p : last;
    }
    const uint64_t tail = last != UINT64_MAX ? end - 1 - last : 0;
    return last == UINT64_MAX || (tail > longest_tail && before == UINT64_MAX) ? at : tail > longest_tail && last - before < tail ? last + 1 : end;
}

// pp_window_cut against the byte-at-a-time cut: random bytes of backslashes, line ends ("\r\n" among them), blanks and
// letters, of 0 to 300 bytes and some of up to 12 KB, each cut from four starts to its end with the longest tail of 0,
// 3, 40 and BUSTER_PP_WINDOW_TAIL_BYTES bytes, ending at a PROT_NONE page (a read past the end faults) and starting at
// the end of one (a read before the first start faults). Returns the differences and adds the checks to `checks`.
static uint64_t pp_window_test_line_ends(uint64_t* random, uint64_t* checks)
{
    static const uint64_t tails[] = { 0, 3, 40, BUSTER_PP_WINDOW_TAIL_BYTES };
    static const uint8_t alphabet[] = { '\\', '\\', '\r', '\r', '\n', '\n', ' ', 'a', '\t' };
    const uint64_t page_bytes = (uint64_t)sysconf(_SC_PAGESIZE);
    // Room for the longest inputs, between two guard pages.
    const uint64_t room = (12288 + page_bytes - 1) / page_bytes * page_bytes;
    uint8_t* mapping = mmap(0, room + 2 * page_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint64_t failures = mapping == MAP_FAILED || mprotect(mapping, page_bytes, PROT_NONE) != 0 || mprotect(mapping + page_bytes + room, page_bytes, PROT_NONE) != 0;
    for (uint32_t length = 0; failures == 0 && length <= 12288; length += length < 300 ? 1 : 997)
    {
        for (uint32_t trial = 0; trial < 24; trial += 1)
        {
            // Every other trial ends at the guard page after the bytes, and the others start at the end of the one before.
            uint8_t* bytes = trial % 2 == 0 ? mapping + page_bytes + room - length : mapping + page_bytes;
            // Mostly bytes that aren't line ends, so that a scan goes through several chunks before it finds one; for the
            // longer inputs, fewer still, so that the last line or the tail after it can pass the longest tail.
            const uint64_t density = 1 + pp_window_test_random(random) % (length < 300 ? 64 : 4096);
            for (uint32_t i = 0; i < length; i += 1)
            {
                const uint64_t r = pp_window_test_random(random);
                bytes[i] = r % density == 0 ? alphabet[(r >> 8) % sizeof(alphabet)] : (r >> 16) % 3 == 0 ? (uint8_t)'\\' : (uint8_t)'x';
                // A splice's newline after most backslashes, and a '\n' after most '\r's.
                if ((bytes[i] == '\\' || bytes[i] == '\r') && i + 1 < length && (r >> 24) % 2 == 0)
                {
                    i += 1;
                    bytes[i] = bytes[i - 1] == '\\' && (r >> 25) % 4 == 0 ? (uint8_t)'\r' : (uint8_t)'\n';
                }
            }
            for (uint32_t at = 0; at < 4 && at <= length; at += 1)
            {
                for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(tails); t += 1)
                {
                    const uint64_t vector = pp_window_cut(bytes, at, length, tails[t]);
                    const uint64_t scalar = pp_window_test_cut(bytes, at, length, tails[t]);
                    *checks += 1;
                    if (vector != scalar && failures < 4 && !pp_test_quiet)
                    {
                        printf("windows: the cut of %u bytes from %u, the longest tail %lu, is at %lu, a byte at a time at %lu\n", length, at, (unsigned long)tails[t],
                               (unsigned long)vector, (unsigned long)scalar);
                    }
                    failures += vector != scalar;
                }
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, room + 2 * page_bytes);
    }
    return failures;
}

// A window for pp_window_test_cut_fixtures: `length` bytes of 'x' with a line end at each of `places` (a '\n', or a
// "\r\n" ending there when `crlf`), and splices ("\\\n") at `splices`.
typedef struct PPWindowCutFixture PPWindowCutFixture;
struct PPWindowCutFixture
{
    const char* name;
    uint32_t length;
    uint32_t places[2];
    uint32_t place_count;
    bool crlf;
    // The cut by hand: the window's length (`end`), 0 (`at`, doubled unlexed), or a place.
    uint32_t expected;
};

// pp_window_cut on windows made by hand, against the cut worked out by hand and the byte-at-a-time one, with the
// longest tail BUSTER_PP_WINDOW_TAIL_BYTES: a short tail after a line far longer, whose bytes before the line end's chunk
// are in a PROT_NONE page, so that a scan past the line end faults (a short tail needs no line end before it); one line
// end and a long tail (doubled unlexed), also as "\r\n"; a short line and a long tail (cut after it); a line longer than
// the long tail (lexed to the end); no line end, only splices (doubled); and tails at the bound and one past it. Returns
// the differences and adds the checks to `checks`.
static uint64_t pp_window_test_cut_fixtures(uint64_t* checks)
{
    const uint32_t tail = BUSTER_PP_WINDOW_TAIL_BYTES;
    static const PPWindowCutFixture fixtures[] = {
        { "one line end, then a long tail", 3 * BUSTER_PP_WINDOW_TAIL_BYTES, { 50 }, 1, false, 0 },
        { "one \"\\r\\n\", then a long tail", 3 * BUSTER_PP_WINDOW_TAIL_BYTES, { 50 }, 1, true, 0 },
        { "a short line, then a long tail", 3 * BUSTER_PP_WINDOW_TAIL_BYTES, { 50, 100 }, 2, false, 101 },
        { "a short line ending \"\\r\\n\", then a long tail", 3 * BUSTER_PP_WINDOW_TAIL_BYTES, { 50, 100 }, 2, true, 101 },
        { "a line longer than the long tail after it", 3 * BUSTER_PP_WINDOW_TAIL_BYTES, { 10, 2 * BUSTER_PP_WINDOW_TAIL_BYTES - 500 }, 2, false,
          3 * BUSTER_PP_WINDOW_TAIL_BYTES },
        { "no line end, only splices", 3 * BUSTER_PP_WINDOW_TAIL_BYTES, { 0 }, 0, false, 0 },
        { "a short line, then a tail at the bound", 3 * BUSTER_PP_WINDOW_TAIL_BYTES, { 2 * BUSTER_PP_WINDOW_TAIL_BYTES - 92, 2 * BUSTER_PP_WINDOW_TAIL_BYTES - 1 }, 2,
          false, 3 * BUSTER_PP_WINDOW_TAIL_BYTES },
        { "a short line, then a tail one past the bound", 3 * BUSTER_PP_WINDOW_TAIL_BYTES,
          { 2 * BUSTER_PP_WINDOW_TAIL_BYTES - 92, 2 * BUSTER_PP_WINDOW_TAIL_BYTES - 2 }, 2, false, 2 * BUSTER_PP_WINDOW_TAIL_BYTES - 1 },
    };
    uint64_t failures = 0;
    uint8_t* bytes = malloc(3 * BUSTER_PP_WINDOW_TAIL_BYTES);
    for (uint32_t f = 0; bytes != 0 && f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        const PPWindowCutFixture* fixture = fixtures + f;
        memset(bytes, 'x', fixture->length);
        for (uint32_t s = 300; s + 2 < fixture->length; s += 997)
        {
            bytes[s] = '\\';
            bytes[s + 1] = '\n';
        }
        for (uint32_t i = 0; i < fixture->place_count; i += 1)
        {
            bytes[fixture->places[i]] = '\n';
            bytes[fixture->places[i] - 1] = fixture->crlf ? '\r' : 'x';
        }
        const uint64_t expected = fixture->expected;
        const uint64_t vector = pp_window_cut(bytes, 0, fixture->length, tail);
        const uint64_t scalar = pp_window_test_cut(bytes, 0, fixture->length, tail);
        *checks += 1;
        if ((vector != expected || scalar != expected) && !pp_test_quiet)
        {
            printf("windows: the cut of \"%s\" is at %lu, a byte at a time at %lu, by hand at %lu\n", fixture->name, (unsigned long)vector, (unsigned long)scalar,
                   (unsigned long)expected);
        }
        failures += vector != expected || scalar != expected;
    }
    failures += bytes == 0;
    free(bytes);
    // The short tail after a long line: the window starts in a PROT_NONE page, and its line end and tail are in the
    // readable bytes after it, past their first chunk (whose look-behind would read the page before).
    const uint64_t page_bytes = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t readable = (tail + 200 + page_bytes - 1) / page_bytes * page_bytes;
    uint8_t* mapping = mmap(0, readable + 2 * page_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = mapping != MAP_FAILED && mprotect(mapping, page_bytes, PROT_NONE) == 0 &&
                         mprotect(mapping + page_bytes + readable, page_bytes, PROT_NONE) == 0;
    if (guarded)
    {
        uint8_t* after = mapping + page_bytes;
        memset(after, 'x', readable);
        after[readable - 1 - (tail - 101)] = '\n';
        const uint64_t end = page_bytes + readable;
        const uint64_t cut = pp_window_cut(mapping, 0, end, tail);
        *checks += 1;
        if (cut != end && !pp_test_quiet)
        {
            printf("windows: the cut of a short tail after a long line is at %lu, not at the window's end\n", (unsigned long)cut);
        }
        failures += cut != end;
    }
    failures += !guarded;
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, readable + 2 * page_bytes);
    }
    return failures;
}

// pp_directive_places against its twin: 0 to 300 words of every density (none, sparse, runs, more than 16 bits, all
// ones), each output ending at a PROT_NONE page exactly where its places end, so that a store past them faults. Returns
// the differences and adds the checks to `checks`.
static uint64_t pp_window_test_places(uint64_t* random, uint64_t* checks)
{
    const uint64_t page_bytes = (uint64_t)sysconf(_SC_PAGESIZE);
    // Room for 300 words of 64 places each, and a guard page.
    const uint64_t room = (300 * 64 * sizeof(uint32_t) + page_bytes - 1) / page_bytes * page_bytes;
    uint8_t* mapping = mmap(0, room + page_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint64_t failures = mapping == MAP_FAILED || mprotect(mapping + room, page_bytes, PROT_NONE) != 0;
    static uint64_t words[300];
    static uint32_t expected[300 * 64];
    for (uint32_t word_count = 0; failures == 0 && word_count <= 300; word_count += word_count < 40 ? 1 : 13)
    {
        for (uint32_t trial = 0; trial < 16; trial += 1)
        {
            const uint32_t density = (uint32_t)(pp_window_test_random(random) % 6);
            for (uint32_t w = 0; w < word_count; w += 1)
            {
                const uint64_t r = pp_window_test_random(random);
                const uint64_t s = pp_window_test_random(random);
                words[w] = density == 0 ? 0 : density == 1 ? (r % 8 == 0 ? 1ull << (s % 64) : 0) : density == 2 ? r & s & pp_window_test_random(random)
                         : density == 3 ? r : density == 4 ? (r % 4 == 0 ? ~0ull : r % 4 == 1 ? 0 : r) : ~0ull >> (s % 64);
            }
            const uint32_t count = pp_directive_places_scalar(words, word_count, expected);
            uint32_t* places = (uint32_t*)(mapping + room) - count;
            const uint32_t vector = pp_directive_places(words, word_count, places);
            const bool same = vector == count && memcmp(places, expected, (uint64_t)count * sizeof(uint32_t)) == 0;
            *checks += 1;
            if (!same && failures < 4 && !pp_test_quiet)
            {
                printf("windows: the places of %u words of density %u: %u places, %u a bit at a time, or they differ\n", word_count, density, vector, count);
            }
            failures += !same;
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, room + page_bytes);
    }
    return failures;
}

// ---------------------------------------------------------------------------------------------------------------------
// Line ends at a query's end (#441)

// A file of 'x's with line ends placed in it, for pp_test_line_ends.
typedef struct PPLineEndCase PPLineEndCase;
struct PPLineEndCase
{
    const char* name;
    uint32_t length;
    // Each place's byte, and as many places as are used.
    uint32_t places[6];
    uint8_t bytes[6];
    uint32_t count;
};

// One of pp_test_line_ends' files, at the end of a readable mapping before a PROT_NONE page (a read past the file's
// length faults), against the line ends counted a byte at a time: "\n", "\r\n" at its '\n', and a '\r' alone. A query
// that ends at a "\r\n"'s '\n' has the '\r' and not its line end. Checks pp_count_lines from many starts to every end;
// the line pages counted whole and in two windows cut around each line end (pp_count_page_lines); pp_page_line at every
// byte; and pp_line_of's lines and columns at every byte, from the cursor (without pages, and on a page) and from the
// pages. Returns the failures and adds the checks to `checks`.
static uint64_t pp_line_end_test_case(const PPLineEndCase* line_case, uint8_t* mapping, uint64_t room, uint64_t* checks)
{
    uint64_t failures = 0;
    uint8_t* bytes = mapping + room - line_case->length;
    memset(bytes, 'x', line_case->length);
    for (uint32_t p = 0; p < line_case->count; p += 1)
    {
        bytes[line_case->places[p]] = line_case->bytes[p];
    }
    PPFileData data = { .bytes = bytes, .length = line_case->length };
    // The line ends before each byte, and each byte's line's start.
    uint32_t* before = malloc((line_case->length + 1) * sizeof(uint32_t));
    uint32_t* starts = malloc((line_case->length + 1) * sizeof(uint32_t));
    const uint32_t page_count = line_case->length / BUSTER_PP_LINE_PAGE_BYTES + 1;
    uint32_t* pages = malloc(page_count * sizeof(uint32_t));
    static Preprocessor pp;
    if (before == 0 || starts == 0 || pages == 0)
    {
        failures += 1;
    }
    for (uint32_t at = 0, lines = 0, start = 0; failures == 0 && at <= line_case->length; at += 1)
    {
        before[at] = lines;
        starts[at] = start;
        const bool end = at < line_case->length && (bytes[at] == '\n' || (bytes[at] == '\r' && (at + 1 == line_case->length || bytes[at + 1] != '\n')));
        lines += end;
        start = end ? at + 1 : start;
    }
    // pp_count_lines from the file's start, from a byte to 130 bytes before the end, and from each place around a line
    // end; from every start for a short file.
    for (uint32_t end = 0; failures == 0 && end <= line_case->length; end += 1)
    {
        const uint32_t lowest = line_case->length <= 200 || end <= 130 ? 0 : end - 130;
        for (uint32_t back = 0; back <= end - lowest; back += 1)
        {
            const uint32_t begin = end - back;
            const uint32_t count = pp_count_lines(&data, begin, end);
            *checks += 1;
            if (count != before[end] - before[begin] && failures < 4 && !pp_test_quiet)
            {
                printf("line ends: %s: pp_count_lines(%u, %u) gives %u, a byte at a time %u\n", line_case->name, begin, end, count, before[end] - before[begin]);
            }
            failures += count != before[end] - before[begin];
        }
        const uint32_t from_start = pp_count_lines(&data, 0, end);
        *checks += 1;
        failures += from_start != before[end];
        for (uint32_t p = 0; p < line_case->count; p += 1)
        {
            for (uint32_t begin = line_case->places[p] > 0 ? line_case->places[p] - 1 : 0; begin <= line_case->places[p] + 2 && begin <= end; begin += 1)
            {
                const uint32_t count = pp_count_lines(&data, begin, end);
                *checks += 1;
                failures += count != before[end] - before[begin];
            }
        }
    }
    // The line pages: whole, and in two windows cut at each byte from before a placed line end to past it.
    for (uint32_t p = 0; failures == 0 && p <= line_case->count; p += 1)
    {
        const uint32_t first = p < line_case->count ? (line_case->places[p] > 0 ? line_case->places[p] - 1 : 0) : line_case->length;
        const uint32_t last = p < line_case->count ? line_case->places[p] + 2 : line_case->length;
        for (uint32_t cut = first; cut <= last && cut <= line_case->length; cut += 1)
        {
            uint32_t lines = 0;
            memset(pages, 0xff, page_count * sizeof(uint32_t));
            pages[0] = 0;
            if (cut > 0)
            {
                pp_count_page_lines(&data, 0, cut, pages, &lines);
            }
            if (line_case->length > cut)
            {
                pp_count_page_lines(&data, cut, line_case->length, pages, &lines);
            }
            bool same = lines == before[line_case->length];
            for (uint32_t page = 0; page < page_count; page += 1)
            {
                same &= pages[page] == before[page * BUSTER_PP_LINE_PAGE_BYTES];
            }
            *checks += 1;
            if (!same && failures < 4 && !pp_test_quiet)
            {
                printf("line ends: %s: the line pages counted in windows cut at %u differ from a count a byte at a time\n", line_case->name, cut);
            }
            failures += !same;
        }
    }
    data.line_pages = pages;
    for (uint32_t at = 0; failures == 0 && at <= line_case->length; at += 1)
    {
        const uint32_t line = pp_page_line(&data, at);
        *checks += 1;
        if (line != before[at] + 1 && failures < 4 && !pp_test_quiet)
        {
            printf("line ends: %s: pp_page_line(%u) gives line %u, a byte at a time %u\n", line_case->name, at, line, before[at] + 1);
        }
        failures += line != before[at] + 1;
    }
    // pp_line_of in a range at location 1000, at the bytes within 72 of a placed line end and the file's first and last:
    // each after a byte 1 to 70 before it (from the cursor, or from the pages when that is on another page), after
    // itself, and after a byte past it (the cursor reset to the file's start, or the pages); the first query of each
    // pair from no cursor. (A column walks back over its line a byte at a time, and the lines are long.)
    PPRange range = { .base = 1000, .length = line_case->length, .file = 0, .bytes = bytes };
    PPFile file = { .data = 0 };
    pp.ranges = &range;
    pp.files = &file;
    pp.datas = &data;
    for (uint32_t paged = 0; failures == 0 && paged < 2; paged += 1)
    {
        data.line_pages = paged != 0 ? pages : 0;
        static const int32_t distances[] = { -65, -1, 0, 1, 2, 3, 63, 64, 65, 70 };
        for (uint32_t at = 0; at <= line_case->length; at += 1)
        {
            bool near = at < 2 || at + 2 > line_case->length;
            for (uint32_t p = 0; p < line_case->count; p += 1)
            {
                near |= at + 72 >= line_case->places[p] && at <= line_case->places[p] + 72;
            }
            for (uint32_t d = 0; near && d < BUSTER_ARRAY_LENGTH(distances); d += 1)
            {
                const int64_t earlier = (int64_t)at - distances[d];
                const uint32_t from = earlier < 0 ? 0 : earlier > line_case->length ? line_case->length : (uint32_t)earlier;
                uint32_t column = 0;
                data.line_cursor = 0;
                data.line_cursor_line = 0;
                pp_line_of(&pp, 0, 1000 + from, 0);
                const uint32_t line = pp_line_of(&pp, 0, 1000 + at, &column);
                *checks += 1;
                if ((line != before[at] + 1 || column != at - starts[at] + 1) && failures < 4 && !pp_test_quiet)
                {
                    printf("line ends: %s: pp_line_of(%u) after %u%s gives %u:%u, a byte at a time %u:%u\n", line_case->name, at, from, paged != 0 ? " with pages" : "",
                           line, column, before[at] + 1, at - starts[at] + 1);
                }
                failures += line != before[at] + 1 || column != at - starts[at] + 1;
            }
        }
    }
    free(before);
    free(starts);
    free(pages);
    return failures;
}

// #441's cases: a "\r\n" in a block, across 64-byte blocks, inside a line page and across pages, a '\r' and a '\n' alone,
// a '\r' at the file's end, and a "\r\n" across the second page's start.
static bool pp_test_line_ends(void)
{
    static const PPLineEndCase cases[] = {
        { "CRLF first", 3, { 0, 1 }, { '\r', '\n' }, 2 },
        { "CRLF in a block", 40, { 10, 11 }, { '\r', '\n' }, 2 },
        { "CRLF across blocks", 130, { 63, 64 }, { '\r', '\n' }, 2 },
        { "CRLF across 128", 300, { 127, 128, 190, 191 }, { '\r', '\n', '\r', '\n' }, 4 },
        { "CRLF in a page", 4200, { 4096, 4097 }, { '\r', '\n' }, 2 },
        { "CRLF across pages", 4200, { 4095, 4096 }, { '\r', '\n' }, 2 },
        { "CR and LF alone, CR last", 100, { 20, 30, 99 }, { '\r', '\n', '\r' }, 3 },
        { "CR last at a block's end", 64, { 10, 11, 63 }, { '\r', '\n', '\r' }, 3 },
        { "CR last at a page's end", 4096, { 63, 64, 4095 }, { '\r', '\n', '\r' }, 3 },
        { "LF and CRLF at the second page", 8300, { 4095, 8191, 8192, 8200, 8299 }, { '\n', '\r', '\n', '\r', '\r' }, 5 },
        { "CR before CR, then LF", 200, { 62, 63, 64, 100, 101 }, { '\r', '\r', '\n', '\n', '\r' }, 5 },
    };
    const uint64_t page_bytes = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t room = (8300 + page_bytes - 1) / page_bytes * page_bytes;
    uint8_t* mapping = mmap(0, room + page_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint64_t failures = mapping == MAP_FAILED || mprotect(mapping + room, page_bytes, PROT_NONE) != 0;
    uint64_t checks = 0;
    for (uint32_t c = 0; failures == 0 && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        failures += pp_line_end_test_case(&cases[c], mapping, room, &checks);
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, room + page_bytes);
    }
    printf("line ends: %lu of %lu checks fail in %u files\n", (unsigned long)failures, (unsigned long)checks, (uint32_t)BUSTER_ARRAY_LENGTH(cases));
    return failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// pp_test_windows

// BUSTER_PP_WINDOW_TEST_SOURCES sources from `seed`, each in one of five language modes (with C's raw strings and
// without, digit separators or not, without "//" comments and digraphs, and with literal prefixes to split), prepared
// whole by both kernels and in windows of several sizes, each window's line pages against a count a byte at a time.
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
    PPWindowTestText text = { .bytes = malloc(BUSTER_PP_WINDOW_TEST_CAPACITY), .capacity = BUSTER_PP_WINDOW_TEST_CAPACITY };
    bool initialized = text.bytes != 0;
    for (uint32_t k = 0; k < 3; k += 1)
    {
        initialized &= pp_window_test_initialize(&runs[k]);
    }
    uint64_t random = seed ^ 0x116116116ull;
    uint64_t line_end_checks = 0;
    const uint64_t line_end_failures = pp_window_test_line_ends(&random, &line_end_checks) + pp_window_test_cut_fixtures(&line_end_checks);
    uint64_t place_checks = 0;
    const uint64_t place_failures = pp_window_test_places(&random, &place_checks);
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
                same &= status != PP_PREPARE_DONE || pp_window_test_line_pages(&data, s, windows[w]);
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
    printf("windows: %lu of %u sources differ (%lu windowed preparations, %lu tokens); the windows' cut: %lu of %lu checks fail; the directives' "
           "places: %lu of %lu checks fail\n", (unsigned long)failures, BUSTER_PP_WINDOW_TEST_SOURCES, (unsigned long)windowed, (unsigned long)tokens,
           (unsigned long)line_end_failures, (unsigned long)line_end_checks, (unsigned long)place_failures, (unsigned long)place_checks);
    return initialized && failures == 0 && line_end_failures == 0 && place_failures == 0;
}
