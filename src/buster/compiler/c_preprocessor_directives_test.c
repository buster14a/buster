// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The directive tables' checks (#118), for test-preprocessor; c_preprocessor_test.c includes this file:
//
//   - Random token streams (pp_directive_test_streams): generated sources of directives, conditionals nested to many
//     levels (balanced and not, past the vectorized pairing's depth limit too), '#' at the start of a line and
//     elsewhere, %:, '#' alone, line markers, unknown names, and '#'s that literals, comments and line splices hide.
//     Each is prepared by the vectorized kernels and by their scalar twins (pp_prepare_data), whose tables must be the
//     oracle's: a walk of the tokens one at a time, of its own, that finds each directive, its line's end and its kind
//     (from its name's spelling), pairs the conditionals with a stack, and finds the notes and the include guard.
//   - Files (pp_directive_test_path, --tables PATH...): the same for every .c and .h file under each path.
//   - The structure (pp_directive_test_structure): fixtures of the errors a file's conditionals can have, which clang
//     reports where it reads the directives and where it skips them (#else and #elif after #else, a directive without
//     #if, a group left open, in an included file too), and the warnings beside them (#elifdef before C23, extra
//     tokens). Both kernels must report the same, and with --clang, clang's text exactly.
//
// Layout: the random sources, the oracle, the comparison, files, the structure fixtures, and pp_test_directives.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

// The random sources pp_test_directives checks by default (--count N).
#define BUSTER_PP_DIRECTIVE_TEST_SOURCES 2000
#define BUSTER_PP_DIRECTIVE_TEST_TEXT_BYTES (1u << 22)
#define BUSTER_PP_DIRECTIVE_TEST_MAX_FILE (1ull << 30)
#define BUSTER_PP_DIRECTIVE_TEST_MAX_DEPTH 64
#define BUSTER_PP_DIRECTIVE_TEST_MAX_PATH 4096

// ---------------------------------------------------------------------------------------------------------------------
// Random sources

// A splitmix64 step.
static uint64_t pp_directive_test_random(uint64_t* state)
{
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

typedef struct PPDirectiveTestText PPDirectiveTestText;
struct PPDirectiveTestText
{
    char* bytes;
    uint32_t length;
    uint32_t capacity;
};

static void pp_directive_test_append(PPDirectiveTestText* text, const char* piece)
{
    const uint32_t length = (uint32_t)strlen(piece);
    if (text->length + length < text->capacity)
    {
        memcpy(text->bytes + text->length, piece, length);
        text->length += length;
    }
}

#define BUSTER_PP_DIRECTIVE_TEST_PICK(random, array) (array[pp_directive_test_random(random) % BUSTER_ARRAY_LENGTH(array)])

// How a directive's '#' is written: alone, as the digraph, after blanks or a comment, before a comment or a line splice.
static const char* const pp_directive_test_hashes[] = { "#", "#", "#", "#", "%:", "  #", "\t# ", "/* c */ #", "# /* c */ ", "#\\\n", " %:\\\n " };

// The conditionals' lines after the '#'.
static const char* const pp_directive_test_openers[] = { "if 1", "if defined(X) && Y", "if", "ifdef X", "ifndef X", "ifdef", "ifndef X Y", "i\\\nf 0" };
static const char* const pp_directive_test_middles[] = { "elif 0", "elif", "elifdef X", "elifndef Y", "else", "else junk", "els\\\ne" };
static const char* const pp_directive_test_endifs[] = { "endif", "endif", "endif X", "end\\\nif" };

// The other directives' lines after the '#': each name, and names and tokens that aren't directives.
static const char* const pp_directive_test_others[] = {
    "define X 1",
    "define F(a) #a",
    "undef X",
    "include \"a.h\"",
    "include <a.h>",
    "include_next <a.h>",
    "import \"a.h\"",
    "line 10",
    "line 20 \"b.c\"",
    "error x",
    "warning x",
    "pragma once",
    "pragma pack(1)",
    "ident \"x\"",
    "sccs \"x\"",
    "assert x(y)",
    "unassert x",
    "embed \"f\"",
    "",
    "33 \"file.h\" 1 3",
    "1",
    "foo",
    "IF",
    "Define",
    "endiff",
    "if0",
    "\"if\"",
    "(",
    "## x",
    "define",
    "de\\\nfine Y",
};

// Lines that aren't directives: '#'s inside a line, in literals and comments, and after a line splice, which continues
// the line before.
static const char* const pp_directive_test_texts[] = {
    "int x;",
    "a # b",
    "x ## y",
    "##if 1",
    "%:%: define",
    "\"#if 1\"",
    "'#'",
    "a /*\n#if 1\n*/ b",
    "a \\\n#if 1",
    "",
    "// #if\n",
    "/* #endif */",
    "\\",
    "b\\\n\\\n#endif",
    "c /* x */ # if",
    "u8\"#\" L'#'",
};

// One random source into `text`: about `lines` lines of directives, conditionals nested up to `max_depth` deep, and
// other lines. Unless `balanced`, it leaves groups open and adds continuations without one.
static void pp_directive_test_generate(uint64_t* random, PPDirectiveTestText* text, uint32_t lines, uint32_t max_depth, bool balanced)
{
    text->length = 0;
    uint32_t depth = 0;
    for (uint32_t line = 0; line < lines; line += 1)
    {
        const uint64_t roll = pp_directive_test_random(random) % 100;
        const char* hash = BUSTER_PP_DIRECTIVE_TEST_PICK(random, pp_directive_test_hashes);
        if (roll < 20 && depth < max_depth)
        {
            pp_directive_test_append(text, hash);
            pp_directive_test_append(text, BUSTER_PP_DIRECTIVE_TEST_PICK(random, pp_directive_test_openers));
            depth += 1;
        }
        else if (roll < 32 && (depth != 0 || !balanced))
        {
            pp_directive_test_append(text, hash);
            pp_directive_test_append(text, BUSTER_PP_DIRECTIVE_TEST_PICK(random, pp_directive_test_middles));
        }
        else if (roll < 46 && (depth != 0 || !balanced))
        {
            pp_directive_test_append(text, hash);
            pp_directive_test_append(text, BUSTER_PP_DIRECTIVE_TEST_PICK(random, pp_directive_test_endifs));
            depth -= depth != 0;
        }
        else if (roll < 70)
        {
            pp_directive_test_append(text, hash);
            pp_directive_test_append(text, BUSTER_PP_DIRECTIVE_TEST_PICK(random, pp_directive_test_others));
        }
        else
        {
            pp_directive_test_append(text, BUSTER_PP_DIRECTIVE_TEST_PICK(random, pp_directive_test_texts));
        }
        pp_directive_test_append(text, pp_directive_test_random(random) % 16 == 0 ? "\r\n" : "\n");
    }
    // A balanced source closes its groups; another may leave them open, and a few end in a comment that the lexer makes
    // one token of the rest of the file.
    for (; depth != 0 && (balanced || pp_directive_test_random(random) % 2 == 0); depth -= 1)
    {
        pp_directive_test_append(text, "#endif\n");
    }
    if (pp_directive_test_random(random) % 64 == 0)
    {
        pp_directive_test_append(text, "/* unterminated\n#else\n#endif\n");
    }
}

// Conditionals nested `depth` deep, each group with an #else, then closed: past BUSTER_PP_PAIR_MAX_DEPTH, the vectorized
// pairing leaves the file to the stack.
static void pp_directive_test_nest(PPDirectiveTestText* text, uint32_t depth)
{
    text->length = 0;
    for (uint32_t i = 0; i < depth; i += 1)
    {
        pp_directive_test_append(text, i % 3 == 0 ? "#ifdef A\n" : "#if 1\n");
    }
    for (uint32_t i = 0; i < depth; i += 1)
    {
        pp_directive_test_append(text, i % 2 == 0 ? "#else\n#endif\n" : "#endif\n");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The oracle

// A file's directive table, as the oracle finds it.
typedef struct PPDirectiveTestTable PPDirectiveTestTable;
struct PPDirectiveTestTable
{
    uint32_t count;
    uint32_t* hash;
    uint32_t* end;
    uint8_t* kind;
    uint32_t* next;
    uint32_t note_count;
    uint32_t* note_directives;
    uint8_t* note_kinds;
    IdentId guard;
};

// A directive's kind from its name's spelling, as C 6.10 and clang's extensions name them.
static uint8_t pp_directive_test_kind(const uint8_t* name, uint32_t length)
{
    static const struct
    {
        const char* name;
        uint8_t kind;
    } names[] = {
        { "define", PP_DIRECTIVE_DEFINE },
        { "undef", PP_DIRECTIVE_UNDEF },
        { "include", PP_DIRECTIVE_INCLUDE },
        { "include_next", PP_DIRECTIVE_INCLUDE_NEXT },
        { "import", PP_DIRECTIVE_IMPORT },
        { "if", PP_DIRECTIVE_IF },
        { "ifdef", PP_DIRECTIVE_IFDEF },
        { "ifndef", PP_DIRECTIVE_IFNDEF },
        { "elif", PP_DIRECTIVE_ELIF },
        { "elifdef", PP_DIRECTIVE_ELIFDEF },
        { "elifndef", PP_DIRECTIVE_ELIFNDEF },
        { "else", PP_DIRECTIVE_ELSE },
        { "endif", PP_DIRECTIVE_ENDIF },
        { "line", PP_DIRECTIVE_LINE },
        { "error", PP_DIRECTIVE_ERROR },
        { "warning", PP_DIRECTIVE_WARNING },
        { "pragma", PP_DIRECTIVE_PRAGMA },
        { "ident", PP_DIRECTIVE_IDENT },
        { "sccs", PP_DIRECTIVE_IDENT },
        { "assert", PP_DIRECTIVE_ASSERT },
        { "unassert", PP_DIRECTIVE_ASSERT },
        { "embed", PP_DIRECTIVE_EMBED },
    };
    uint8_t result = PP_DIRECTIVE_UNKNOWN;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        result = strlen(names[i].name) == length && memcmp(names[i].name, name, length) == 0 ? names[i].kind : result;
    }
    return result;
}

// Is token t an identifier spelled `spelling`?
static bool pp_directive_test_spelled(const PPFileData* data, const InternTable* table, uint32_t t, const char* spelling)
{
    const bool identifier = (data->kinds[t] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
    return identifier && intern_length(table, data->values[t]) == strlen(spelling) && memcmp(intern_bytes(table, data->values[t]), spelling, strlen(spelling)) == 0;
}

// The oracle's table of a prepared file's tokens, one token at a time: a directive is a '#' (or %:) that starts a line,
// and runs to the next token that starts one; its kind is its name's (the next token on its line). The conditionals
// pair with a stack of the open groups, which also gives the notes; then the include guard. The arrays go in `arena`.
static bool pp_directive_test_oracle(const PPFileData* data, const InternTable* table, Arena* arena, PPDirectiveTestTable* result_table)
{
    const uint32_t tokens = data->token_count;
    uint32_t count = 0;
    for (uint32_t t = 0; t < tokens; t += 1)
    {
        count += (data->kinds[t] & 0x7F) == C_TOKEN_HASH && (data->kinds[t] & BUSTER_C_TOKEN_START_OF_LINE) != 0;
    }
    PPDirectiveTestTable table_out = { .count = count };
    table_out.hash = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1);
    table_out.end = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1);
    table_out.kind = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1);
    table_out.next = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1);
    table_out.note_directives = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1);
    table_out.note_kinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1);
    uint32_t* open = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1);
    uint32_t* last = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1);
    bool* seen_else = BUSTER_ARENA_PUSH_ARRAY(arena, bool, (uint64_t)count + 1);
    uint8_t* notes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1);
    const bool result = table_out.hash != 0 && table_out.end != 0 && table_out.kind != 0 && table_out.next != 0 && table_out.note_directives != 0 &&
                        table_out.note_kinds != 0 && open != 0 && last != 0 && seen_else != 0 && notes != 0;
    uint32_t d = 0;
    for (uint32_t t = 0; result && t < tokens; t += 1)
    {
        if ((data->kinds[t] & 0x7F) == C_TOKEN_HASH && (data->kinds[t] & BUSTER_C_TOKEN_START_OF_LINE) != 0)
        {
            uint32_t end = t + 1;
            while (end < tokens && (data->kinds[end] & BUSTER_C_TOKEN_START_OF_LINE) == 0)
            {
                end += 1;
            }
            uint8_t kind = PP_DIRECTIVE_NULL;
            if (t + 1 < end && (data->kinds[t + 1] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
            {
                kind = pp_directive_test_kind(intern_bytes(table, data->values[t + 1]), intern_length(table, data->values[t + 1]));
            }
            else if (t + 1 < end)
            {
                kind = (data->kinds[t + 1] & 0x7F) == C_TOKEN_NUMERIC_CONSTANT ? PP_DIRECTIVE_LINE_MARKER : PP_DIRECTIVE_UNKNOWN;
            }
            table_out.hash[d] = t;
            table_out.end[d] = end;
            table_out.kind[d] = kind;
            table_out.next[d] = BUSTER_PP_NONE;
            d += 1;
        }
    }
    // The stack: each open group's opener, its last directive so far, and whether it has had its #else.
    uint32_t open_count = 0;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        const uint8_t kind = table_out.kind[i];
        notes[i] = kind == PP_DIRECTIVE_ELIFDEF || kind == PP_DIRECTIVE_ELIFNDEF ? BUSTER_PP_NOTE_ELIFDEF : 0;
        if (kind == PP_DIRECTIVE_IF || kind == PP_DIRECTIVE_IFDEF || kind == PP_DIRECTIVE_IFNDEF)
        {
            open[open_count] = i;
            last[open_count] = i;
            seen_else[open_count] = false;
            open_count += 1;
        }
        else if ((kind == PP_DIRECTIVE_ELIF || kind == PP_DIRECTIVE_ELIFDEF || kind == PP_DIRECTIVE_ELIFNDEF || kind == PP_DIRECTIVE_ELSE) && open_count != 0)
        {
            table_out.next[last[open_count - 1]] = i;
            last[open_count - 1] = i;
            notes[i] |= seen_else[open_count - 1] ? BUSTER_PP_NOTE_AFTER_ELSE : 0;
            seen_else[open_count - 1] = seen_else[open_count - 1] || kind == PP_DIRECTIVE_ELSE;
        }
        else if (kind == PP_DIRECTIVE_ENDIF && open_count != 0)
        {
            table_out.next[last[open_count - 1]] = i;
            open_count -= 1;
        }
    }
    for (uint32_t i = 0; result && i < open_count; i += 1)
    {
        notes[open[i]] |= BUSTER_PP_NOTE_UNTERMINATED;
    }
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        if (notes[i] != 0)
        {
            table_out.note_directives[table_out.note_count] = i;
            table_out.note_kinds[table_out.note_count] = notes[i];
            table_out.note_count += 1;
        }
    }
    // The include guard, as clang's multiple-include optimization finds one: after null directives, `#ifndef X` or a
    // `#if` of `defined X` or `defined(X)` under an odd number of '!' and any parentheses, whose group's next directive
    // is an #endif that only null directives follow.
    uint32_t first = 0;
    while (result && first < count && table_out.kind[first] == PP_DIRECTIVE_NULL && table_out.end[first] == table_out.hash[first] + 1 && table_out.hash[first] == first)
    {
        first += 1;
    }
    const uint32_t endif = result && first < count && table_out.hash[first] == first ? table_out.next[first] : BUSTER_PP_NONE;
    uint32_t after = endif != BUSTER_PP_NONE ? table_out.end[endif] : 0;
    for (uint32_t i = endif != BUSTER_PP_NONE ? endif + 1 : count; i < count; i += 1)
    {
        after = after == table_out.hash[i] && table_out.kind[i] == PP_DIRECTIVE_NULL ? table_out.end[i] : UINT32_MAX;
    }
    const bool closed = endif != BUSTER_PP_NONE && table_out.kind[endif] == PP_DIRECTIVE_ENDIF && after == tokens;
    const uint32_t opener = closed ? table_out.hash[first] : 0;
    const uint32_t line = closed ? table_out.end[first] : 0;
    if (closed && table_out.kind[first] == PP_DIRECTIVE_IFNDEF && opener + 2 < line && (data->kinds[opener + 2] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
    {
        table_out.guard = data->values[opener + 2];
    }
    else if (closed && table_out.kind[first] == PP_DIRECTIVE_IF)
    {
        // The line from both ends: '!'s and '('s, `defined`, the name, maybe in parentheses, and ')'s.
        uint32_t begin = opener + 2;
        uint32_t stop = line;
        uint32_t negations = 0;
        int32_t balance = 0;
        bool shaped = true;
        for (; begin < stop && !pp_directive_test_spelled(data, table, begin, "defined"); begin += 1)
        {
            const uint8_t kind = data->kinds[begin] & 0x7F;
            negations += kind == C_TOKEN_EXCLAIM;
            balance += kind == C_TOKEN_L_PAREN;
            shaped = shaped && (kind == C_TOKEN_EXCLAIM || kind == C_TOKEN_L_PAREN);
        }
        for (; stop > begin && (data->kinds[stop - 1] & 0x7F) == C_TOKEN_R_PAREN; stop -= 1)
        {
            balance -= 1;
        }
        // What's left is `defined X`, or `defined(X` whose ')' the loop took.
        const bool plain = stop - begin == 2 && (data->kinds[begin + 1] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
        const bool enclosed = stop - begin == 3 && (data->kinds[begin + 1] & 0x7F) == C_TOKEN_L_PAREN && (data->kinds[begin + 2] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
        balance += enclosed ? 1 : 0;
        const bool guard = shaped && begin < stop && (plain || enclosed) && balance == 0 && negations % 2 == 1;
        table_out.guard = guard ? data->values[begin + (plain ? 1 : 2)] : 0;
    }
    *result_table = table_out;
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The comparison

// Does a kernel's table (`data`, from `kernel`) equal the oracle's? Prints the first difference.
static bool pp_directive_test_same(const PPFileData* data, const PPDirectiveTestTable* oracle, const char* name, const char* kernel)
{
    bool result = data->prepared && data->directive_count == oracle->count && data->note_count == oracle->note_count && data->guard == oracle->guard;
    uint32_t d = 0;
    for (; result && d < oracle->count; d += 1)
    {
        result = data->directive_hash[d] == oracle->hash[d] && data->directive_end[d] == oracle->end[d] && data->directive_kind[d] == oracle->kind[d] &&
                 data->directive_next[d] == oracle->next[d];
    }
    for (uint32_t n = 0; result && n < oracle->note_count; n += 1)
    {
        result = data->note_directives[n] == oracle->note_directives[n] && data->note_kinds[n] == oracle->note_kinds[n];
    }
    if (!result && data->prepared && data->directive_count == oracle->count && d != 0 && d <= oracle->count)
    {
        d -= 1;
        printf("%s: the %s kernels' directive %u: '#' %u/%u, end %u/%u, kind %u/%u, next %u/%u\n", name, kernel, d, data->directive_hash[d], oracle->hash[d],
               data->directive_end[d], oracle->end[d], data->directive_kind[d], oracle->kind[d], data->directive_next[d], oracle->next[d]);
    }
    else if (!result)
    {
        printf("%s: the %s kernels' table: %u directives (the oracle's %u), %u notes (%u), guard %u (%u)%s\n", name, kernel, data->directive_count, oracle->count,
               data->note_count, oracle->note_count, data->guard, oracle->guard, data->prepared ? "" : ", not prepared");
    }
    return result;
}

// The two kernels' preprocessors, whose tables the comparisons build, and their intern tables.
typedef struct PPDirectiveTest PPDirectiveTest;
struct PPDirectiveTest
{
    InternTable tables[2];
    Preprocessor runs[2];
    Arena arena;
    uint64_t files;
    uint64_t tokens;
    uint64_t directives;
    uint64_t notes;
    uint64_t guards;
};

static bool pp_directive_test_initialize(PPDirectiveTest* test)
{
    bool result = true;
    for (uint32_t k = 0; result && k < 2; k += 1)
    {
        const PPOptions options = {
            .language = { .standard = 2017, .gnu = true },
            .scalar = k == 1,
        };
        result = intern_initialize(&test->tables[k]) && pp_initialize(&test->runs[k], &options, &test->tables[k]);
    }
    test->arena = arena_reserve(1ull << 34);
    return result && test->arena.base != 0;
}

static void pp_directive_test_release(PPDirectiveTest* test)
{
    for (uint32_t k = 0; k < 2; k += 1)
    {
        pp_release(&test->runs[k]);
        intern_release(&test->tables[k]);
    }
    arena_release(&test->arena);
}

// Prepares `length` bytes by both kernels and compares their tables with the oracle's; the memory each kernel's run
// took goes back after.
static bool pp_directive_test_compare(PPDirectiveTest* test, const uint8_t* bytes, uint64_t length, const char* name)
{
    bool result = length <= UINT32_MAX - 64;
    uint8_t* copy = result ? arena_push(&test->arena, length + 64, 64) : 0;
    result = copy != 0;
    if (result)
    {
        memcpy(copy, bytes, length);
        memset(copy + length, 0, 64);
    }
    for (uint32_t k = 0; result && k < 2; k += 1)
    {
        Preprocessor* pp = &test->runs[k];
        const uint64_t arena_length = pp->arena.length;
        uint64_t column_lengths[3];
        for (uint32_t c = 0; c < 3; c += 1)
        {
            column_lengths[c] = pp->columns[c].length;
        }
        // A file of the unit's own, prepared into its arenas (no cache record).
        PPFileData data = { .bytes = copy, .length = (uint32_t)length, .record = BUSTER_PP_NONE };
        result = pp_prepare_data(pp, &data, name);
        PPDirectiveTestTable oracle = { 0 };
        const uint64_t scratch_length = test->arena.length;
        result = result && pp_directive_test_oracle(&data, pp->table, &test->arena, &oracle) &&
                 pp_directive_test_same(&data, &oracle, name, k == 0 ? "vectorized" : "scalar");
        if (k == 0)
        {
            test->tokens += data.token_count;
            test->directives += data.directive_count;
            test->notes += data.note_count;
            test->guards += data.guard != 0;
        }
        arena_pop_to(&test->arena, scratch_length);
        arena_pop_to(&pp->arena, arena_length);
        for (uint32_t c = 0; c < 3; c += 1)
        {
            arena_pop_to(&pp->columns[c], column_lengths[c]);
        }
    }
    test->files += 1;
    arena_pop_to(&test->arena, 0);
    return result;
}

// `count` random sources from `seed`, of up to 120 lines and, one in eight, up to 3,000, with conditionals nested up
// to 2, 8 or 64 deep, half of them unbalanced; then nests past and below the vectorized pairing's depth limit.
static bool pp_directive_test_streams(uint64_t seed, uint64_t count)
{
    static PPDirectiveTest test;
    PPDirectiveTestText text = { .bytes = malloc(BUSTER_PP_DIRECTIVE_TEST_TEXT_BYTES), .capacity = BUSTER_PP_DIRECTIVE_TEST_TEXT_BYTES };
    bool result = text.bytes != 0 && pp_directive_test_initialize(&test);
    uint64_t random = seed;
    uint64_t failures = 0;
    for (uint64_t i = 0; result && i < count && failures < 8; i += 1)
    {
        static const uint32_t depths[] = { 2, 8, BUSTER_PP_DIRECTIVE_TEST_MAX_DEPTH };
        const uint32_t lines = pp_directive_test_random(&random) % 8 == 0 ? (uint32_t)(pp_directive_test_random(&random) % 3000) : (uint32_t)(pp_directive_test_random(&random) % 120);
        const uint32_t depth = BUSTER_PP_DIRECTIVE_TEST_PICK(&random, depths);
        pp_directive_test_generate(&random, &text, lines, depth, pp_directive_test_random(&random) % 2 == 0);
        char name[64];
        snprintf(name, sizeof(name), "random source %lu", (unsigned long)i);
        failures += !pp_directive_test_compare(&test, (const uint8_t*)text.bytes, text.length, name);
    }
    static const uint32_t nests[] = { BUSTER_PP_PAIR_MAX_DEPTH - 1, BUSTER_PP_PAIR_MAX_DEPTH, BUSTER_PP_PAIR_MAX_DEPTH + 1, BUSTER_PP_PAIR_MAX_DEPTH + 100 };
    for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(nests); i += 1)
    {
        pp_directive_test_nest(&text, nests[i]);
        char name[64];
        snprintf(name, sizeof(name), "a nest %u deep", nests[i]);
        failures += !pp_directive_test_compare(&test, (const uint8_t*)text.bytes, text.length, name);
    }
    printf("directive tables: %lu of %lu random sources and nests differ from the oracle (%lu tokens, %lu directives, %lu notes, %lu guards)\n",
           (unsigned long)failures, (unsigned long)test.files, (unsigned long)test.tokens, (unsigned long)test.directives, (unsigned long)test.notes,
           (unsigned long)test.guards);
    pp_directive_test_release(&test);
    free(text.bytes);
    return result && failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Files

static bool pp_directive_test_file(PPDirectiveTest* test, const char* path)
{
    bool result = false;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat file_stats;
    if (fd >= 0 && fstat(fd, &file_stats) == 0 && (uint64_t)file_stats.st_size <= BUSTER_PP_DIRECTIVE_TEST_MAX_FILE)
    {
        const uint64_t length = (uint64_t)file_stats.st_size;
        void* mapped = length != 0 ? mmap(0, length, PROT_READ, MAP_PRIVATE, fd, 0) : 0;
        if (length == 0 || mapped != MAP_FAILED)
        {
            result = pp_directive_test_compare(test, mapped, length, path);
            if (length != 0)
            {
                munmap(mapped, length);
            }
        }
    }
    if (fd >= 0)
    {
        close(fd);
    }
    if (!result)
    {
        printf("directive tables: %s differs or can't be read\n", path);
    }
    return result;
}

// A file, or the .c and .h files under a directory (a stack of open directories, no recursion).
static bool pp_directive_test_path(PPDirectiveTest* test, const char* path)
{
    struct stat path_stats;
    bool result = stat(path, &path_stats) == 0;
    if (result && !S_ISDIR(path_stats.st_mode))
    {
        result = pp_directive_test_file(test, path);
    }
    else if (result)
    {
        DIR* directories[BUSTER_PP_DIRECTIVE_TEST_MAX_DEPTH];
        static char paths[BUSTER_PP_DIRECTIVE_TEST_MAX_DEPTH][BUSTER_PP_DIRECTIVE_TEST_MAX_PATH];
        directories[0] = opendir(path);
        snprintf(paths[0], sizeof(paths[0]), "%s", path);
        result = directories[0] != 0;
        uint32_t depth = result ? 1 : 0;
        while (depth != 0)
        {
            struct dirent* entry = readdir(directories[depth - 1]);
            if (entry == 0)
            {
                closedir(directories[depth - 1]);
                depth -= 1;
            }
            else if (entry->d_name[0] != '.')
            {
                char child[BUSTER_PP_DIRECTIVE_TEST_MAX_PATH];
                snprintf(child, sizeof(child), "%s/%s", paths[depth - 1], entry->d_name);
                struct stat child_stats;
                const bool listed = lstat(child, &child_stats) == 0;
                if (listed && S_ISDIR(child_stats.st_mode) && depth < BUSTER_PP_DIRECTIVE_TEST_MAX_DEPTH)
                {
                    directories[depth] = opendir(child);
                    if (directories[depth] != 0)
                    {
                        snprintf(paths[depth], sizeof(paths[depth]), "%s", child);
                        depth += 1;
                    }
                }
                else if (listed && S_ISREG(child_stats.st_mode))
                {
                    const uint64_t name_length = strlen(entry->d_name);
                    const bool source = name_length > 2 && entry->d_name[name_length - 2] == '.' &&
                                        (entry->d_name[name_length - 1] == 'c' || entry->d_name[name_length - 1] == 'h');
                    if (source)
                    {
                        result &= pp_directive_test_file(test, child);
                    }
                }
            }
        }
    }
    else
    {
        printf("directive tables: can't open %s\n", path);
    }
    return result;
}

// --tables: every .c and .h file under each path.
static bool pp_directive_test_paths(char** paths, int count)
{
    static PPDirectiveTest test;
    bool result = pp_directive_test_initialize(&test);
    for (int i = 0; result && i < count; i += 1)
    {
        result &= pp_directive_test_path(&test, paths[i]);
    }
    printf("directive tables: %s for %lu files (%lu tokens, %lu directives, %lu notes, %lu guards)\n", result ? "the oracle's" : "DIFFER", (unsigned long)test.files,
           (unsigned long)test.tokens, (unsigned long)test.directives, (unsigned long)test.notes, (unsigned long)test.guards);
    pp_directive_test_release(&test);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The structure

// A fixture: its source, a header it includes as "structure.h" (or null), and its language mode.
typedef struct PPDirectiveTestStructure PPDirectiveTestStructure;
struct PPDirectiveTestStructure
{
    const char* name;
    const char* source;
    const char* header;
    const char* standard;
};

// Original raw-string inputs; Clang is an external diagnostic/location oracle, including suppressed contexts.
static const PPDirectiveTestStructure pp_directive_test_raw_errors[] = {
    { "invalid delimiter character", "R\"bad space\"\n", 0, "gnu17" },
    { "delimiter lookahead past the salvaged token", "R\"a\"b c\n", 0, "gnu17" },
    { "delimiter newline", "LR\"bad\n\"\n", 0, "gnu17" },
    { "delimiter too long", "u8R\"abcdefghijklmnopq\"\n", 0, "gnu17" },
    { "unterminated", "R\"tag(body\n", 0, "gnu17" },
    { "spliced prefix", "L\\\nR\"bad space\"\n", 0, "gnu17" },
    { "macro definitions diagnose once", "#define BAD R\"bad space\"\nBAD BAD\n", 0, "gnu17" },
    { "discarded macro argument", "#define DROP(x) kept\nDROP(R\"bad space\")\n", 0, "gnu17" },
    { "condition after expansion", "#define DROP(x) 1\n#if DROP(R\"bad space\")\nkept\n#endif\n", 0, "gnu17" },
    { "skipped body", "#if 0\nR\"bad space\"\n#endif\n", 0, "gnu17" },
    { "untaken elif is still lexed", "#if 1\nkept\n#elif R\"bad space\"\n#endif\n", 0, "gnu17" },
    { "opaque diagnostic", "#warning R\"bad space\"\n", 0, "gnu17" },
    { "opaque query", "#define HI __has_include\n#if HI(<R\"bad\"x>)\n#endif\n", 0, "gnu17" },
    { "included file", "#include \"structure.h\"\n#include \"structure.h\"\n", "R\"bad space\"\n", "gnu17" },
};

static const PPDirectiveTestStructure pp_directive_test_structures[] = {
    { "#else after #else", "#if 1\n#else\n#else\n#endif\n", 0, 0 },
    { "#elif after #else", "#if 0\n#else\n#elif 1\n#endif\n#if 1\n#else\n#elif 1\n#endif\n", 0, 0 },
    { "#elifdef and #elifndef after #else", "#if 0\n#else\n#elifdef X\n#endif\n#if 0\n#else\n#elifndef X\n#endif\n", 0, 0 },
    { "the same in C23", "#if 0\n#else\n#elifdef X\n#endif\n#if 1\n#elifndef Y\n#endif\n", 0, "c23" },
    { "without #if", "a\n#endif\nb\n#else\n#elif 1\n#elifdef X\n#elifndef Y\n", 0, 0 },
    { "unterminated", "#if 1\n#if 0\nx\n", 0, 0 },
    { "inside a skipped group", "#if 0\n#if 1\n#else\n#else\n#elif 1\n#endif\n#endif\n", 0, 0 },
    { "deep inside skipped groups",
      "#if 0\n#ifdef A\n#if 2\n#else\n#elifdef B\n#else\n#endif\n#else\n#else\n#endif\n#elif 1\n#else\n#else\n#endif\n", 0, 0 },
    { "after a taken group", "#if 1\n#elif 1\n#if 0\n#else\n#else\n#endif\n#else\n#elif 0\n#endif\n", 0, 0 },
    { "unterminated inside skipped groups", "#if 1\n#if 0\n#if 1\n#ifdef X\n#else\n#else\n", 0, 0 },
    { "unterminated in an include", "#if 1\n#include \"structure.h\"\n#endif\n#endif\n", "#if 1\n#if 0\n#if 1\n", 0 },
    { "a comment that swallows the group", "#if 0\n/* never closed\n#else\n#endif\nx\n", 0, 0 },
    { "an active comment that swallows the group", "#if 1\n/* never closed\n#else\n#endif\nx\n", 0, 0 },
    { "a comment on the opening directive", "#if 0 /* never closed\n#else\n#endif\nx\n", 0, 0 },
    { "a spliced comment on the opening directive", "#if 0\\\n /* never closed\n#else\n#endif\nx\n", 0, 0 },
    { "a comment after a skipped group", "#if 0\n#else /* never closed\n#endif\nx\n", 0, 0 },
    { "a comment after a taken group", "#if 1\n#else /* never closed\n#endif\nx\n", 0, 0 },
    { "a comment on a selected #elif", "#if 0\n#elif 1 /* never closed\n#endif\nx\n", 0, 0 },
    { "a comment on an excluded #elif", "#if 1\n#elif 1 /* never closed\n#endif\nx\n", 0, 0 },
    { "a comment on a skipped group's end", "#if 0\n#endif /* never closed\nx\n", 0, 0 },
    { "a comment in a repeated include", "#include \"structure.h\"\n#include \"structure.h\"\n", "/* never closed\n", 0 },
    { "extra tokens", "#if 1 // c\n#else junk\n#endif junk\n#if 0\n#else junk\n#endif junk\n#ifdef A B\n#endif\n#if 0\n#if 1\n#else x\n#endif y\n#endif\n",
      0, 0 },
    { "extra tokens around the structure", "#else junk\n#endif junk\n#if 0\n#else\n#else junk\n#endif\n#if 1\n#else\n#else junk\n#endif\n#if 1\n#elif 1\n#else junk\n#endif\n",
      0, 0 },
    { "extra tokens on #elifdef", "#if 0\n#elifdef X Y\n#elifndef Z W\n#endif\n", 0, "c23" },
    { "after #line", "#line 100\n#else\n#if 0\n#else\n#else\n#endif\n#line 7 \"renamed.c\"\n#elif 1\n#if 1\n", 0, 0 },
    { "digraphs and splices", "%:if 0\n%:  if 1\n#el\\\nse\n%:else\n%:endif\n#endif\n#\\\nendif\n", 0, 0 },
};

// Diagnostic messages are original source text, including comments and trailing blanks. A skipped directive still
// belongs to raw excluded input; an active one must not let its comment text swallow the source after its newline.
static const PPDirectiveTestStructure pp_directive_test_messages[] = {
    { "an active warning's comment text", "#warning /* never closed\ntail\n", 0, 0 },
    { "an active error's comment text", "#error /* never closed\ntail\n", 0, 0 },
    { "comments, quotes and trailing blanks", "#warning /* comment */ x   \t\n#error '\n", 0, 0 },
    { "a spliced name and message", "#war\\\nning /* op\\\nen\ntail\n", 0, 0 },
    { "an excluded warning's comment", "#if 0\n#warning /* never closed\n#endif\ntail\n", 0, 0 },
    { "an excluded error's comment", "#if 0\n#error /* never closed\n#endif\ntail\n", 0, 0 },
    { "repeated inclusions", "#include \"structure.h\"\n#include \"structure.h\"\n", "#warning /* never closed\ntail\n", 0 },
};

// Fixtures of #define's and #undef's constraints and of redefinitions (C 6.10.4, #121), reported as clang reports them.
static const PPDirectiveTestStructure pp_directive_test_definitions[] = {
    { "'#' without a parameter", "#define F(x) #y\n#define G() #\n#define H(a) a #\n#define O #y\n#define P(x) #x x ## ##\n", 0, 0 },
    { "'##' at either end", "#define F(x) ## x\n#define G(x) x ##\n#define H ## x\n#define I x ##\n", 0, 0 },
    { "__VA_OPT__",
      "#define F(x) __VA_OPT__(x)\n#define G __VA_OPT__\n#define H(...) __VA_OPT__(__VA_OPT__())\n#define I(...) __VA_OPT__ x\n#define J(...) __VA_OPT__\n"
      "#define K(...) __VA_OPT__(## a)\n#define L(...) __VA_OPT__((a ##))\n#define M(...) __VA_OPT__(a ##)\n#define N(...) #__VA_ARGS__ __VA_OPT__(a ## b)\n",
      0, 0 },
    { "an unterminated __VA_OPT__",
      "#define F(...) __VA_OPT__(\n#define G(...) __VA_OPT__(a\n#define H(...) __VA_OPT__(a (b)\n#define I(...) __VA_OPT__(int\n#define J(...) __VA_OPT__(1\n"
      "#define K(...) __VA_OPT__(<:\n#define L(...) __VA_OPT__(L\"s\"\n#define M(...) __VA_OPT__(@\n",
      0, 0 },
    { "parameter lists",
      "#define A(x, x) x\n#define B(x\n#define C(x,) x\n#define D(\n#define E(1) x\n#define F(...x) x\n#define G(x...y) x\n#define H(x,...,y) x\n"
      "#define I(x y) x\n#define J(a,b,a) a\n#define K(__VA_ARGS__) 1\n#define L(x...) x\n#define M(for, int) for int\n",
      0, 0 },
    { "names", "#define\n#define 123\n#define defined 1\n#undef\n#undef 1\n#undef X Y\n#ifdef\n#endif\n#ifdef 1\n#endif\n#ifndef /* c */ \n#endif\n#if 0\n#elifdef\n#endif\n",
      0, "c23" },
    { "builtins",
      "#define __LINE__ 1\n#undef __FILE__\n#define __STDC__ 2\n#define __has_include 1\n#undef __STDC_VERSION__\n#undef __COUNTER__\n#undef _Pragma\n"
      "#undef __x86_64__\n#define __STDC_HOSTED__ 0\n#undef __LINE__\n",
      0, 0 },
    { "whitespace after the name", "#define A+1\n#define B\"s\"\n#define C\\\n+1\n#define D\\\n 1\n#define E/**/+1\n#define F@\n", 0, 0 },
    { "whitespace after the name in C89", "#define A+1\n#define B@\n#define C`\n#define D\"s\"\n", 0, "c89" },
    { "redefinitions",
      "#define A 1\n#define A 2\n#define B(x) x\n#define B(y) y\n#define C 1\n#define C  1\n#define D a/**/b\n#define D a b\n#define E <:\n#define E [\n"
      "#define G 1 // c\n#define G 1\n#define H /* a */\n#define H\n#define I(x) x\n#undef I\n#define I(y) y\n#define J(...) __VA_OPT__(a)\n#define J(...) __VA_OPT__(a )\n"
      "#define K(x) #x\n#define K(x) # x\n#define L 01\n#define L 1\n",
      0, 0 },
};

// Runs a command (`argv`, ended by a null) with CPATH and C_INCLUDE_PATH unset: what it writes to standard output and
// to standard error go to `*output` and `*errors` (each may be null to drop it), in memory (null if it doesn't run).
// Returns its exit status, or -1.
static int pp_directive_test_command(char* const* argv, char** output, char** errors)
{
    FILE* files[2] = { tmpfile(), tmpfile() };
    const pid_t child = files[0] != 0 && files[1] != 0 ? fork() : -1;
    if (child == 0)
    {
        if (dup2(fileno(files[0]), STDOUT_FILENO) < 0 || dup2(fileno(files[1]), STDERR_FILENO) < 0)
        {
            _exit(127);
        }
        unsetenv("CPATH");
        unsetenv("C_INCLUDE_PATH");
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    const bool ran = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) != 127;
    char** texts[2] = { output, errors };
    for (uint32_t k = 0; k < 2; k += 1)
    {
        const long length = ran && texts[k] != 0 && fseek(files[k], 0, SEEK_END) == 0 ? ftell(files[k]) : -1;
        char* text = length >= 0 && length < (1l << 30) && fseek(files[k], 0, SEEK_SET) == 0 ? calloc((size_t)length + 1, 1) : 0;
        if (text != 0 && fread(text, 1, (size_t)length, files[k]) != (size_t)length)
        {
            free(text);
            text = 0;
        }
        if (texts[k] != 0)
        {
            *texts[k] = text;
        }
        if (files[k] != 0)
        {
            fclose(files[k]);
        }
    }
    return ran ? WEXITSTATUS(status) : -1;
}

// Runs clang -E on `path` in the mode (`standard`, or gnu17), and returns what it writes to stderr (without its "N
// errors generated." lines), or null.
static char* pp_directive_test_clang(const char* path, const char* standard)
{
    char mode[32];
    snprintf(mode, sizeof(mode), "-std=%s", standard != 0 ? standard : "gnu17");
    char* const argv[] = { "clang", "-E", mode, "-fno-caret-diagnostics", "-fno-diagnostics-show-option", "-fno-color-diagnostics", "-o", "/dev/null", (char*)path, 0 };
    char* result = 0;
    const int status = pp_directive_test_command(argv, 0, &result);
    if (status != 0 && status != 1)
    {
        free(result);
        result = 0;
    }
    // Without the summary lines ("1 error generated.", "2 warnings and 1 error generated.").
    for (char* line = result; line != 0 && *line != 0;)
    {
        char* end = strchr(line, '\n');
        const size_t line_length = end != 0 ? (size_t)(end - line) + 1 : strlen(line);
        const bool summary = line_length >= 12 && memcmp(line + line_length - 12, " generated.\n", 12) == 0;
        if (summary)
        {
            memmove(line, line + line_length, strlen(line + line_length) + 1);
        }
        else
        {
            line += line_length;
        }
    }
    return result;
}

// How many inclusions clang skips by their guards (its multiple-include optimization: -print-stats' "#includes
// skipped due to the multi-include optimization"), preprocessing `path` with the arguments; -1 if it can't say.
static int64_t pp_directive_test_clang_guards(char** arguments, int argument_count, const char* path)
{
    char* argv[256];
    int n = 0;
    argv[n++] = "clang";
    for (int i = 0; i < argument_count && n < 248; i += 1)
    {
        argv[n++] = arguments[i];
    }
    argv[n++] = "-E";
    argv[n++] = "-o";
    argv[n++] = "/dev/null";
    argv[n++] = "-Xclang";
    argv[n++] = "-print-stats";
    argv[n++] = (char*)path;
    argv[n] = 0;
    char* errors = 0;
    const int status = pp_directive_test_command(argv, 0, &errors);
    const char* found = status == 0 && errors != 0 ? strstr(errors, " #includes skipped due to the multi-include optimization") : 0;
    int64_t result = -1;
    if (found != 0)
    {
        const char* digits = found;
        while (digits > errors && digits[-1] >= '0' && digits[-1] <= '9')
        {
            digits -= 1;
        }
        result = digits < found ? strtoll(digits, 0, 10) : -1;
    }
    free(errors);
    return result;
}

// Each fixture by both kernels, whose diagnostics must be the same, and with clang, clang's.
static bool pp_directive_test_diagnostics(const PPDirectiveTestStructure* fixtures, uint32_t count, const char* label, const char* directory, bool with_clang)
{
    uint32_t failures = 0;
    for (uint32_t f = 0; f < count; f += 1)
    {
        const PPDirectiveTestStructure* fixture = &fixtures[f];
        char path[4200];
        char header[4200];
        snprintf(path, sizeof(path), "%s/structure.c", directory);
        snprintf(header, sizeof(header), "%s/structure.h", directory);
        bool result = pp_test_write_file(path, fixture->source) && (fixture->header == 0 || pp_test_write_file(header, fixture->header));
        char* diagnostics[2] = { 0, 0 };
        for (uint32_t k = 0; result && k < 2; k += 1)
        {
            size_t length = 0;
            FILE* stream = open_memstream(&diagnostics[k], &length);
            PPOptions options = {
                .language = { .standard = 2017, .gnu = true },
                .scalar = k == 1,
                .diagnostics = stream,
            };
            result = stream != 0 && (fixture->standard == 0 || pp_parse_standard(fixture->standard, &options.language));
            static InternTable table;
            static Preprocessor pp;
            if (result)
            {
                result = intern_initialize(&table) && pp_initialize(&pp, &options, &table);
                if (result)
                {
                    pp_run(&pp, path);
                }
                pp_release(&pp);
                intern_release(&table);
            }
            if (stream != 0)
            {
                fclose(stream);
            }
        }
        result = result && diagnostics[0] != 0 && diagnostics[1] != 0 && strcmp(diagnostics[0], diagnostics[1]) == 0;
        char* oracle = result && with_clang ? pp_directive_test_clang(path, fixture->standard) : 0;
        result = result && (!with_clang || (oracle != 0 && strcmp(diagnostics[0], oracle) == 0));
        if (!result)
        {
            printf("%s %s differs:\nours:\n%s%sclang:\n%s\n", label, fixture->name, diagnostics[0] != 0 ? diagnostics[0] : "<failed>",
                   diagnostics[1] != 0 && diagnostics[0] != 0 && strcmp(diagnostics[0], diagnostics[1]) != 0 ? "(the scalar kernels' differ)\n" : "",
                   oracle != 0 ? oracle : "<not run>");
        }
        failures += !result;
        free(oracle);
        free(diagnostics[0]);
        free(diagnostics[1]);
        unlink(path);
        unlink(header);
    }
    printf("%s: %u of %u fixtures differ%s\n", label, failures, count, with_clang ? " from clang's diagnostics" : " between the kernels");
    return failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Include guards and #pragma once (#120)

// Both ordinary and exceptional messages preserve the whole tail beyond the former 64-repair limit.
static bool pp_directive_test_message_capacity(void)
{
    uint32_t failures = 0;
    for (uint32_t test = 0; test < 3; test += 1)
    {
        char source[4096];
        uint32_t length = 0;
        const uint32_t count = test == 1 ? 64 : 65;
        const char* message = test == 0 ? "#warning plain\n" : "#warning /*\n";
        for (uint32_t i = 0; i < count; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "%s", message);
        }
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, "tail\n");
        for (uint32_t kernel = 0; kernel < 2; kernel += 1)
        {
            static Preprocessor pp;
            static InternTable table;
            const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .quiet = true, .scalar = kernel != 0 };
            const bool initialized = intern_initialize(&table) && pp_initialize(&pp, &options, &table);
            const bool ran = initialized && pp_run_source(&pp, "message-capacity.c", (const uint8_t*)source, length);
            bool same = initialized && ran && !pp.fatal && pp.errors == 0 && pp.warnings == count;
            if (same)
            {
                same = pp.output.count == 1 && pp.output.kinds[0] == C_TOKEN_RAW_IDENTIFIER && intern_length(&table, pp.output.values[0]) == 4 &&
                       memcmp(intern_bytes(&table, pp.output.values[0]), "tail", 4) == 0;
            }
            failures += !same;
            pp_release(&pp);
            intern_release(&table);
        }
    }
    printf("diagnostic recovery capacity: %u of 6 kernel runs differ\n", failures);
    return failures == 0;
}

// A private prepared view doesn't change the origin of language macros read from the supplied predefines file.
static bool pp_directive_test_predefines_origin(const char* directory)
{
    char path[4200];
    snprintf(path, sizeof(path), "%s/message-predefines.h", directory);
    bool result = pp_test_write_file(path, "#warning /* never closed\n#define __STDC__ 1\n");
    for (uint32_t kernel = 0; result && kernel < 2; kernel += 1)
    {
        static Preprocessor pp;
        static InternTable table;
        const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .quiet = true, .scalar = kernel != 0, .predefines_path = path };
        const bool initialized = intern_initialize(&table) && pp_initialize(&pp, &options, &table);
        static const uint8_t source[] = "#undef __STDC__\n";
        const bool ran = initialized && pp_run_source(&pp, "message-origin.c", source, sizeof(source) - 1);
        result = initialized && ran && pp.errors == 0 && pp.warnings == 2;
        pp_release(&pp);
        intern_release(&table);
    }
    unlink(path);
    printf("diagnostic recovery predefines origin: %s\n", result ? "preserved" : "DIFFERS");
    return result;
}

// A guard fixture: the main source, its files (a name and its text, or for a link "->target" (symbolic) or "=>target"
// (hard) to another of them; ended by a null name), and how many inclusions it skips by guards and by #pragma once.
typedef struct PPDirectiveTestGuard PPDirectiveTestGuard;
struct PPDirectiveTestGuard
{
    const char* name;
    const char* source;
    const char* files[4][2];
    uint32_t guards;
    uint32_t once;
};

#define BUSTER_PP_DIRECTIVE_TEST_GUARDED "#ifndef G\n#define G\nint g;\n#endif\n"
#define BUSTER_PP_DIRECTIVE_TEST_TWICE "#include \"g.h\"\n#include \"g.h\"\nx\n"

static const PPDirectiveTestGuard pp_directive_test_guard_fixtures[] = {
    { "a guarded file included twice", BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", BUSTER_PP_DIRECTIVE_TEST_GUARDED } }, 1, 0 },
    { "#undef between the inclusions", "#include \"g.h\"\n#undef G\n#include \"g.h\"\n#include \"g.h\"\n", { { "g.h", BUSTER_PP_DIRECTIVE_TEST_GUARDED } }, 1, 0 },
    { "#if !defined G", BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", "#if !defined G\n#define G\nint g;\n#endif\n" } }, 1, 0 },
    { "#if !defined(G)", BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", "#if !defined(G)\n#define G\nint g;\n#endif\n" } }, 1, 0 },
    { "parentheses and three '!'", BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", "#if ( !!!( defined ( G ) ) )\n#define G\nint g;\n#endif\n" } }, 1, 0 },
    { "two '!'", BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", "#if !!defined G\n#else\n#define G\nint g;\n#endif\n" } }, 0, 0 },
    { "!defined G || 0", BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", "#if !defined G || 0\n#define G\nint g;\n#endif\n" } }, 0, 0 },
    { "a guard with #else", BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", "#ifndef G\n#define G\nfirst\n#else\nsecond\n#endif\n" } }, 0, 0 },
    { "a guard with #elif", BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", "#ifndef G\n#define G\nfirst\n#elif 1\nsecond\n#endif\n" } }, 0, 0 },
    { "a token before the #ifndef", BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", "int before;\n#ifndef G\n#define G\n#endif\n" } }, 0, 0 },
    { "a token after the #endif", BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", "#ifndef G\n#define G\n#endif\nint after;\n" } }, 0, 0 },
    { "a directive before the #ifndef", BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", "#define X\n#ifndef G\n#define G\nint g;\n#endif\n" } }, 0, 0 },
    { "a directive after the #endif", BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", "#ifndef G\n#define G\nint g;\n#endif\n#define Y\n" } }, 0, 0 },
    { "comments and null directives around the group", BUSTER_PP_DIRECTIVE_TEST_TWICE,
      { { "g.h", "/* a */\n#\n%:\n#ifndef G // b\n#define G\nint g;\n#endif /* c */\n#\n// d\n" } }, 1, 0 },
    { "extra tokens", BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", "#ifndef G junk\n#define G\nint g;\n#endif junk\n" } }, 1, 0 },
    { "the macro defined before the first inclusion", "#define G\n" BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", BUSTER_PP_DIRECTIVE_TEST_GUARDED } }, 0, 0 },
    { "an inclusion from inside the file", "#include \"n.h\"\n#include \"n.h\"\n", { { "n.h", "#ifndef N\n#define N\n#include \"n.h\"\nint n;\n#endif\n" } }, 1, 0 },
    { "an inclusion in a skipped group", "#if 0\n#include \"g.h\"\n#endif\n" BUSTER_PP_DIRECTIVE_TEST_TWICE, { { "g.h", BUSTER_PP_DIRECTIVE_TEST_GUARDED } }, 1, 0 },
    { "#pragma once through a symbolic link and a hard link", "#include \"o.h\"\n#include \"link.h\"\n#include \"hard.h\"\n#include \"o.h\"\n",
      { { "o.h", "#pragma once\nint o;\n" }, { "link.h", "->o.h" }, { "hard.h", "=>o.h" } }, 0, 3 },
    { "#import", "#import \"i.h\"\n#include \"i.h\"\n#import \"i.h\"\n", { { "i.h", "int i;\n" } }, 0, 2 },
};

// Writes a fixture's files into `directory` (links as links), or removes them (`write` false).
static bool pp_directive_test_guard_files(const PPDirectiveTestGuard* fixture, const char* directory, bool write)
{
    bool result = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixture->files) && fixture->files[i][0] != 0; i += 1)
    {
        char path[4200];
        char target[4200];
        const char* text = fixture->files[i][1];
        snprintf(path, sizeof(path), "%s/%s", directory, fixture->files[i][0]);
        snprintf(target, sizeof(target), "%s/%s", directory, text + 2);
        if (!write)
        {
            unlink(path);
        }
        else if (text[0] == '-' && text[1] == '>')
        {
            result &= symlink(text + 2, path) == 0;
        }
        else if (text[0] == '=' && text[1] == '>')
        {
            result &= link(target, path) == 0;
        }
        else
        {
            result &= pp_test_write_file(path, text);
        }
    }
    return result;
}

// A fixture by both kernels: the same stream, and the skips it must make; with clang, clang's output and its count of
// the inclusions it skips by guards.
static bool pp_directive_test_guard(const PPDirectiveTestGuard* fixture, const char* directory, bool with_clang)
{
    char path[4200];
    snprintf(path, sizeof(path), "%s/guard.c", directory);
    bool result = pp_test_write_file(path, fixture->source) && pp_directive_test_guard_files(fixture, directory, true);
    static InternTable tables[2];
    static Preprocessor runs[2];
    bool ran[2] = { false, false };
    for (uint32_t k = 0; result && k < 2; k += 1)
    {
        const PPOptions options = {
            .language = { .standard = 2017, .gnu = true },
            .scalar = k == 1,
            .timing = true,
        };
        ran[k] = intern_initialize(&tables[k]) && pp_initialize(&runs[k], &options, &tables[k]) && pp_run(&runs[k], path);
        const uint64_t guards = runs[k].statistics.skipped_guards;
        const uint64_t once = runs[k].statistics.skipped_once;
        if (ran[k] && (guards != fixture->guards || once != fixture->once))
        {
            printf("include guards %s: the %s kernels skip %lu inclusions by guards and %lu by #pragma once, not %u and %u\n", fixture->name,
                   k == 0 ? "vectorized" : "scalar", (unsigned long)guards, (unsigned long)once, fixture->guards, fixture->once);
            result = false;
        }
    }
    result = result && ran[0] && ran[1] && pp_same_streams(&runs[0], &runs[1], fixture->name);
    if (result && with_clang)
    {
        char* const argv[] = { "clang", "-E", "-P", "-std=gnu17", path, 0 };
        char* text = 0;
        PPSpellings ours = { 0 };
        PPSpellings theirs = { 0 };
        result = pp_directive_test_command(argv, &text, 0) == 0 && text != 0 && pp_stream_spellings(&runs[0], &ours) &&
                 pp_text_spellings((const uint8_t*)text, strlen(text), runs[0].split_kinds, pp_test_lex_flags(&runs[0]), &theirs) &&
                 pp_compare_spellings(&runs[0], &ours, &theirs, fixture->name, "clang");
        const int64_t clang_guards = result ? pp_directive_test_clang_guards(0, 0, path) : -1;
        if (result && clang_guards != fixture->guards)
        {
            printf("include guards %s: clang skips %ld inclusions by guards, not %u\n", fixture->name, (long)clang_guards, fixture->guards);
            result = false;
        }
        arena_release(&ours.arena);
        arena_release(&theirs.arena);
        free(text);
    }
    for (uint32_t k = 0; k < 2; k += 1)
    {
        pp_release(&runs[k]);
        intern_release(&tables[k]);
    }
    pp_directive_test_guard_files(fixture, directory, false);
    unlink(path);
    return result;
}

static bool pp_directive_test_guards(const char* directory, bool with_clang)
{
    uint32_t failures = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(pp_directive_test_guard_fixtures); i += 1)
    {
        failures += !pp_directive_test_guard(&pp_directive_test_guard_fixtures[i], directory, with_clang);
    }
    printf("include guards: %u of %lu fixtures differ%s\n", failures, (unsigned long)BUSTER_ARRAY_LENGTH(pp_directive_test_guard_fixtures),
           with_clang ? " (outputs and skips by guards against clang's)" : "");
    return failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The location space (#124)

// One run of `path` in a location space of `limit` bytes (0 for the whole), by the kernels `scalar` picks: its output's
// spellings, one space apart, and its diagnostics, in `*output` and `*diagnostics` (free them); and the bases of the
// inclusion of a file whose name ends with "b.h" and of the first chunk of pasted spellings, or BUSTER_PP_NONE.
static bool pp_directive_test_space_run(const char* path, uint32_t limit, bool scalar, char** output, char** diagnostics, uint32_t* include_base, uint32_t* scratch_base)
{
    size_t diagnostics_length = 0;
    FILE* stream = open_memstream(diagnostics, &diagnostics_length);
    const PPOptions options = {
        .language = { .standard = 2017, .gnu = true },
        .scalar = scalar,
        .diagnostics = stream,
        .location_limit = limit,
    };
    static InternTable table;
    static Preprocessor pp;
    PPSpellings spellings = { 0 };
    bool result = stream != 0 && intern_initialize(&table) && pp_initialize(&pp, &options, &table);
    if (result)
    {
        pp_run(&pp, path);
        result = pp_stream_spellings(&pp, &spellings);
        *include_base = BUSTER_PP_NONE;
        for (uint32_t r = 0; r < pp.range_count; r += 1)
        {
            const uint32_t file = pp.ranges[r].file;
            const char* name = file != BUSTER_PP_NONE ? pp.files[file].path : "";
            const size_t length = strlen(name);
            *include_base = length >= 3 && strcmp(name + length - 3, "b.h") == 0 ? pp.ranges[r].base : *include_base;
        }
        *scratch_base = pp.scratch_range[0] != BUSTER_PP_NONE ? pp.ranges[pp.scratch_range[0]].base : BUSTER_PP_NONE;
    }
    pp_release(&pp);
    intern_release(&table);
    if (stream != 0)
    {
        fclose(stream);
    }
    size_t text_length = 0;
    FILE* text = result ? open_memstream(output, &text_length) : 0;
    for (uint64_t i = 0; text != 0 && i < spellings.count; i += 1)
    {
        fprintf(text, "%s%.*s", i != 0 ? " " : "", (int)spellings.lengths[i], (const char*)spellings.bytes + spellings.offsets[i]);
    }
    if (text != 0)
    {
        fclose(text);
    }
    arena_release(&spellings.arena);
    return result && text != 0;
}

// A location space just too small for an inclusion and for the first pasted spelling (--location-limit's option): the
// run must stop there, after the output before it, reporting the inclusion at its #include and the spelling without a
// place, as clang reports them, by both kernels alike.
static bool pp_directive_test_location_space(const char* directory)
{
    static const char header[] = "in_b\n/* a header longer than a chunk of the main file's lines */\n";
    static const struct
    {
        const char* name;
        const char* source;
        const char* output;
        const char* diagnostic;
    } cases[] = {
        { "an inclusion", "first\n#include \"a.h\"\nsecond\n#include \"b.h\"\nthird\n", "first in_a second",
          "space.c:4:1: fatal error: this include makes the translation unit too large: it passes the location space (" },
        { "a pasted spelling", "before\n#define P(a,b) a##b\nP(x,y)\nafter\n", "before",
          "fatal error: the translation unit is too large: it passes the location space (" },
    };
    char path[4200];
    char a[4200];
    char b[4200];
    snprintf(path, sizeof(path), "%s/space.c", directory);
    snprintf(a, sizeof(a), "%s/a.h", directory);
    snprintf(b, sizeof(b), "%s/b.h", directory);
    bool result = pp_test_write_file(a, "in_a\n") && pp_test_write_file(b, header);
    uint32_t failures = 0;
    for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        result = pp_test_write_file(path, cases[c].source);
        char* output = 0;
        char* diagnostics = 0;
        uint32_t include_base = BUSTER_PP_NONE;
        uint32_t scratch_base = BUSTER_PP_NONE;
        // First the whole space, for where the inclusion or the chunk starts; then a space that ends there.
        result = result && pp_directive_test_space_run(path, 0, false, &output, &diagnostics, &include_base, &scratch_base);
        const uint32_t base = c == 0 ? include_base : scratch_base;
        free(output);
        free(diagnostics);
        bool same = result && base != BUSTER_PP_NONE;
        char* outputs[2] = { 0, 0 };
        char* reports[2] = { 0, 0 };
        for (uint32_t k = 0; same && k < 2; k += 1)
        {
            uint32_t unused[2];
            same = pp_directive_test_space_run(path, base + 1, k == 1, &outputs[k], &reports[k], &unused[0], &unused[1]) && outputs[k] != 0 && reports[k] != 0;
        }
        // The diagnostic, one line: the inclusion's at its #include's place.
        const char* found = same ? strstr(reports[0], cases[c].diagnostic) : 0;
        same = same && strcmp(outputs[0], outputs[1]) == 0 && strcmp(reports[0], reports[1]) == 0 && strcmp(outputs[0], cases[c].output) == 0 && found != 0 &&
               strchr(reports[0], '\n') == reports[0] + strlen(reports[0]) - 1;
        same = same && (c != 0 || (found >= reports[0] + strlen(directory) + 1 && memcmp(found - strlen(directory) - 1, directory, strlen(directory)) == 0));
        if (!same)
        {
            printf("location space %s: output \"%s\" and diagnostics:\n%s(the scalar kernels' \"%s\")\n", cases[c].name, outputs[0] != 0 ? outputs[0] : "<failed>",
                   reports[0] != 0 ? reports[0] : "<failed>\n", outputs[1] != 0 ? outputs[1] : "<failed>");
        }
        failures += !same;
        for (uint32_t k = 0; k < 2; k += 1)
        {
            free(outputs[k]);
            free(reports[k]);
        }
    }
    unlink(path);
    unlink(a);
    unlink(b);
    printf("location space: %u of %lu runs out where expected%s\n", (uint32_t)BUSTER_ARRAY_LENGTH(cases) - failures, (unsigned long)BUSTER_ARRAY_LENGTH(cases),
           failures == 0 ? "" : " (DIFFERS)");
    return result && failures == 0;
}

// --location-limit N through the commands that split -E's options from their files, run on a small file as children of
// this executable: test-preprocessor's files mode and benchmark-preprocessor must take the option and its value as one
// option of ours, and with --clang give clang neither, nor may --compare-clang. In a space the file fits in (it needs
// about 80 KB: a chunk of scratch spellings and the predefines), each must succeed and give its report, with no error
// from clang; in a space of one byte, each must run out of it, reporting that space.
static bool pp_directive_test_location_option(const char* directory, bool with_clang)
{
    // Each run: the command, its mode (with clang alone: --clang, which the run leaves out without it, or
    // --compare-clang, which needs it), the space, the exit status, and what the run must write, to its output if it
    // succeeds and else to its diagnostics.
    static const struct
    {
        const char* command;
        const char* mode;
        const char* limit;
        int status;
        const char* says;
    } runs[] = {
        { "test-preprocessor", "--clang", "1000000", 0, "files: 0 of 1 differ" },
        { "test-preprocessor", "--compare-clang", "1000000", 0, " identical=1 " },
        { "benchmark-preprocessor", "--clang", "1000000", 0, "1 files, " },
        { "test-preprocessor", 0, "1", 1, "it passes the location space (1 bytes)" },
        { "benchmark-preprocessor", 0, "1", 1, "it passes the location space (1 bytes)" },
    };
    char path[4200];
    snprintf(path, sizeof(path), "%s/limit.c", directory);
    const bool written = pp_test_write_file(path, "int x;\n");
    uint32_t count = 0;
    uint32_t failures = 0;
    for (uint32_t r = 0; written && r < BUSTER_ARRAY_LENGTH(runs); r += 1)
    {
        const bool compare = runs[r].mode != 0 && strcmp(runs[r].mode, "--compare-clang") == 0;
        if (with_clang || !compare)
        {
            char* argv[8];
            int n = 0;
            argv[n++] = "/proc/self/exe";
            argv[n++] = (char*)runs[r].command;
            if (with_clang && runs[r].mode != 0)
            {
                argv[n++] = (char*)runs[r].mode;
            }
            argv[n++] = "--location-limit";
            argv[n++] = (char*)runs[r].limit;
            argv[n++] = path;
            argv[n] = 0;
            char* output = 0;
            char* errors = 0;
            const int status = pp_directive_test_command(argv, &output, &errors);
            const bool unavailable = !BUSTER_HOST_VECTOR && strcmp(runs[r].command, "benchmark-preprocessor") == 0;
            const char* text = runs[r].status == 0 ? output : errors;
            // clang's driver reports an option it doesn't know, or a value it takes for an input, as an error.
            const bool same = unavailable ? status == 1 && output != 0 && output[0] == 0 && errors != 0 &&
                                                strcmp(errors, "benchmarks require an AVX-512 host build; run tests with this scalar build\n") == 0
                                          : status == runs[r].status && text != 0 && strstr(text, runs[r].says) != 0 &&
                                                (runs[r].status != 0 || (errors != 0 && strstr(errors, "error:") == 0));
            if (!same)
            {
                printf("location option: %s %s--location-limit %s exits with %d, wanted %d with \"%s\"; its output:\n%s%s", runs[r].command,
                       with_clang && runs[r].mode != 0 ? (compare ? "--compare-clang " : "--clang ") : "", runs[r].limit, status,
                       unavailable ? 1 : runs[r].status, unavailable ? "benchmarks require an AVX-512 host build" : runs[r].says,
                       output != 0 ? output : "", errors != 0 ? errors : "");
            }
            failures += !same;
            count += 1;
            free(output);
            free(errors);
        }
    }
    unlink(path);
    printf("location option: %u of %u command checks pass%s%s\n", count - failures, count,
           with_clang ? ", clang without them" : "", failures == 0 ? "" : " (DIFFERS)");
    return written && failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// pp_test_directives

// The checks without files: random streams (`count` from `seed`), the structure and definition fixtures, the guard
// fixtures, and the location space, shrunk and through the commands' --location-limit.
static bool pp_test_directives(const char* directory, bool with_clang, uint64_t seed, uint64_t count)
{
    bool result = pp_directive_test_streams(seed, count);
    result &= pp_directive_test_diagnostics(pp_directive_test_structures, BUSTER_ARRAY_LENGTH(pp_directive_test_structures), "directive structure", directory, with_clang);
    result &= pp_directive_test_diagnostics(pp_directive_test_messages, BUSTER_ARRAY_LENGTH(pp_directive_test_messages), "diagnostic text", directory, with_clang);
    result &= pp_directive_test_diagnostics(pp_directive_test_raw_errors, BUSTER_ARRAY_LENGTH(pp_directive_test_raw_errors), "raw string diagnostics", directory, with_clang);
    result &= pp_directive_test_message_capacity();
    result &= pp_directive_test_predefines_origin(directory);
    result &= pp_directive_test_diagnostics(pp_directive_test_definitions, BUSTER_ARRAY_LENGTH(pp_directive_test_definitions), "definitions", directory, with_clang);
    result &= pp_directive_test_guards(directory, with_clang);
    result &= pp_directive_test_location_space(directory);
    result &= pp_directive_test_location_option(directory, with_clang);
    return result;
}
