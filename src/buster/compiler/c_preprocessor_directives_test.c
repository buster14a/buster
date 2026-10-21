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
    // The include guard: the file starts with `#ifndef X`, `#if !defined X` or `#if !defined(X)`, whose group's next
    // directive is an #endif that ends the file.
    const bool closed = result && count >= 2 && table_out.hash[0] == 0 && table_out.next[0] != BUSTER_PP_NONE &&
                        table_out.kind[table_out.next[0]] == PP_DIRECTIVE_ENDIF && table_out.end[table_out.next[0]] == tokens;
    const uint32_t line = closed ? table_out.end[0] : 0;
    if (closed && table_out.kind[0] == PP_DIRECTIVE_IFNDEF && line == 3 && (data->kinds[2] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
    {
        table_out.guard = data->values[2];
    }
    else if (closed && table_out.kind[0] == PP_DIRECTIVE_IF && line >= 5 && (data->kinds[2] & 0x7F) == C_TOKEN_EXCLAIM && pp_directive_test_spelled(data, table, 3, "defined"))
    {
        const bool plain = line == 5 && (data->kinds[4] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
        const bool parenthesized = line == 7 && (data->kinds[4] & 0x7F) == C_TOKEN_L_PAREN && (data->kinds[5] & 0x7F) == C_TOKEN_RAW_IDENTIFIER &&
                                   (data->kinds[6] & 0x7F) == C_TOKEN_R_PAREN;
        table_out.guard = plain ? data->values[4] : parenthesized ? data->values[5] : 0;
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
        PPFileData data = { .bytes = copy, .length = (uint32_t)length };
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
    { "extra tokens", "#if 1 // c\n#else junk\n#endif junk\n#if 0\n#else junk\n#endif junk\n#ifdef A B\n#endif\n#if 0\n#if 1\n#else x\n#endif y\n#endif\n",
      0, 0 },
    { "extra tokens around the structure", "#else junk\n#endif junk\n#if 0\n#else\n#else junk\n#endif\n#if 1\n#else\n#else junk\n#endif\n#if 1\n#elif 1\n#else junk\n#endif\n",
      0, 0 },
    { "extra tokens on #elifdef", "#if 0\n#elifdef X Y\n#elifndef Z W\n#endif\n", 0, "c23" },
    { "after #line", "#line 100\n#else\n#if 0\n#else\n#else\n#endif\n#line 7 \"renamed.c\"\n#elif 1\n#if 1\n", 0, 0 },
    { "digraphs and splices", "%:if 0\n%:  if 1\n#el\\\nse\n%:else\n%:endif\n#endif\n#\\\nendif\n", 0, 0 },
};

// Runs clang -E on `path` in the mode (`standard`, or gnu17), and returns what it writes to stderr (without its "N
// errors generated." lines), or null.
static char* pp_directive_test_clang(const char* path, const char* standard)
{
    char* result = 0;
    FILE* errors = tmpfile();
    const pid_t child = errors != 0 ? fork() : -1;
    if (child == 0)
    {
        char mode[32];
        snprintf(mode, sizeof(mode), "-std=%s", standard != 0 ? standard : "gnu17");
        const int null = open("/dev/null", O_WRONLY);
        if (null < 0 || dup2(null, STDOUT_FILENO) < 0 || dup2(fileno(errors), STDERR_FILENO) < 0)
        {
            _exit(127);
        }
        close(null);
        unsetenv("CPATH");
        unsetenv("C_INCLUDE_PATH");
        execlp("clang", "clang", "-E", mode, "-fno-caret-diagnostics", "-fno-diagnostics-show-option", "-fno-color-diagnostics", "-o", "/dev/null", path, (char*)0);
        _exit(127);
    }
    int status = 0;
    const bool ran = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) <= 1;
    const long length = ran && fseek(errors, 0, SEEK_END) == 0 ? ftell(errors) : -1;
    if (length >= 0 && length < (1 << 20) && fseek(errors, 0, SEEK_SET) == 0)
    {
        result = calloc((size_t)length + 1, 1);
        if (result != 0 && fread(result, 1, (size_t)length, errors) != (size_t)length)
        {
            free(result);
            result = 0;
        }
    }
    if (errors != 0)
    {
        fclose(errors);
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

// Each fixture by both kernels, whose diagnostics must be the same, and with clang, clang's.
static bool pp_directive_test_structure(const char* directory, bool with_clang)
{
    uint32_t failures = 0;
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(pp_directive_test_structures); f += 1)
    {
        const PPDirectiveTestStructure* fixture = &pp_directive_test_structures[f];
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
            printf("structure %s differs:\nours:\n%s%sclang:\n%s\n", fixture->name, diagnostics[0] != 0 ? diagnostics[0] : "<failed>",
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
    printf("directive structure: %u of %lu fixtures differ%s\n", failures, (unsigned long)BUSTER_ARRAY_LENGTH(pp_directive_test_structures),
           with_clang ? " from clang's diagnostics" : " between the kernels");
    return failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// pp_test_directives

// The checks without files: random streams (`count` from `seed`) and the structure fixtures.
static bool pp_test_directives(const char* directory, bool with_clang, uint64_t seed, uint64_t count)
{
    bool result = pp_directive_test_streams(seed, count);
    result &= pp_directive_test_structure(directory, with_clang);
    return result;
}
