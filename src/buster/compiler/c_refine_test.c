// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The token refinement tests: they check the intern table (c_intern.c) and refine_tokens (c_refine.c), with every
// variant of its pass B (RefineVariant), against refine_tokens_scalar and against an oracle of their own, which removes
// line splices and decodes UCNs in two plain passes and interns with a different table (FNV-1a and linear probing). The
// compiler runs them as a command:
//
//   build/compiler test-refinement [--count N] [--seed N] [path...]
//
// main.c includes this file after c_lexer.c, lane.c, c_intern.c and c_refine.c, and calls run_refinement_tests for
// that command. The checks, in this order:
//   - hash: intern_hash_bytes, and intern_hash_vector up to 64 bytes, agree with intern_hash_bytes_scalar on random
//     spellings of every length from 0 to 200 bytes.
//   - structured hashes: long spellings made of repeated or swapped 64-byte chunks and 16-byte lanes, and 32-byte ones
//     whose two lanes change in bits their rotations line up, all hash differently; and a bit flipped in one lane with
//     the bit its rotation lines up with in another always changes the hash.
//   - names: every predefined name has its C_NAME id and spelling, and is a keyword in exactly the language modes
//     clang makes it one in (every name in every mode from c89 to gnu23).
//   - table: random spellings of 1 to 200 bytes, enough to grow the table seven times, get ids in the order they are
//     first added, and the same id again; absent ones aren't found.
//   - fixtures: the boundary cases of docs/c-refinement.md, each with the leading-space bits and identifier equalities
//     it must have, at each of the 64 misalignments from a cache line: line splices between tokens (with and without
//     whitespace, \r\n, trigraphs), comments, identifiers spliced and written with UCNs, long identifiers and
//     identifiers around 63 and 64 bytes.
//   - spellings: identifiers of every length from 1 to BUSTER_REFINE_TEST_SPELLING_LENGTHS bytes with a line splice
//     (four spellings, one of them a trigraph's) at every position inside, or a UCN at every position, each followed
//     by the same identifier written plainly: both must be one identifier.
//   - random inputs: N of them (BUSTER_REFINE_TEST_DEFAULT_INPUTS by default) made of identifiers (spliced, with UCNs,
//     long), splices, comments and other tokens (digraphs and pp-numbers among them), lexed by c_tokenize, then
//     refined by both paths and the oracle, with the tables kept from input to input so that most identifiers are
//     found rather than added.
//   - page edges: prefixes and suffixes of random inputs that end exactly at the end of a readable page, before a
//     PROT_NONE guard page, or start exactly at the start of one: a read outside of the source faults, and the fault
//     handler names the input and exits with status 1.
//   - files: each path (a file, or the .c and .h files under a directory) lexed and refined like a random input.
// --seed N picks another random sequence. The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the random sequence, the oracle (RefineOracle and its key, gap and intern functions),
// refine_test_compare (one input through both paths and the oracle), the checks, and run_refinement_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define BUSTER_REFINE_TEST_DEFAULT_INPUTS 20000
#define BUSTER_REFINE_TEST_HASH_ROUNDS 200000
#define BUSTER_REFINE_TEST_HASH_LENGTHS 201
// refine_check_long_hashes's spellings: 8,192 + 4,096 + 2 * 2,048 + 3 * 2,048 + 65,536.
#define BUSTER_REFINE_TEST_LONG_HASHES 88064
// refine_check_aligned_flips's spellings.
#define BUSTER_REFINE_TEST_FLIPS 20000
#define BUSTER_REFINE_TEST_TABLE_SPELLINGS 300000
// A random input is up to this long, and is made of up to this many pieces.
#define BUSTER_REFINE_TEST_INPUT_BYTES 8192
#define BUSTER_REFINE_TEST_MAX_PIECES 400
// The page edge check cuts inputs of 0 to this many bytes.
#define BUSTER_REFINE_TEST_PAGE_EDGE_LENGTHS 600
#define BUSTER_REFINE_TEST_SPELLING_LENGTHS 130
#define BUSTER_REFINE_TEST_REPORTED 3
#define BUSTER_REFINE_TEST_GOLDEN_RATIO 0x9E3779B97F4A7C15ull
// The oracle's memory: spellings, ids and slots.
#define BUSTER_REFINE_ORACLE_BYTES (1ull << 30)
#define BUSTER_REFINE_ORACLE_IDS (1u << 24)
#define BUSTER_REFINE_ORACLE_SLOTS (1u << 25)
// The largest file the files check reads, and how deep and long its paths may be.
#define BUSTER_REFINE_TEST_MAX_FILE_BYTES (256ull << 20)
#define BUSTER_REFINE_TEST_MAX_DEPTH 64
#define BUSTER_REFINE_TEST_MAX_PATH 4096

// ---------------------------------------------------------------------------------------------------------------------
// The random sequence: xorshift64, as in the other tests.

static uint64_t refine_test_random(uint64_t* state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

// ---------------------------------------------------------------------------------------------------------------------
// The oracle

typedef struct RefineOracle RefineOracle;
struct RefineOracle
{
    Arena arena;
    uint8_t* bytes;
    uint64_t bytes_length;
    uint64_t* offsets;
    uint32_t* lengths;
    uint32_t* slots; // An id, or 0 for an empty slot.
    uint32_t count;
};

static uint64_t refine_oracle_hash(const uint8_t* bytes, uint32_t length)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < length; i += 1)
    {
        hash = (hash ^ bytes[i]) * 0x100000001b3ull;
    }
    return hash;
}

static uint32_t refine_oracle_intern(RefineOracle* oracle, const uint8_t* bytes, uint32_t length)
{
    uint64_t slot = refine_oracle_hash(bytes, length) & (BUSTER_REFINE_ORACLE_SLOTS - 1);
    uint32_t result = 0;
    while (result == 0)
    {
        const uint32_t id = oracle->slots[slot];
        if (id == 0)
        {
            result = oracle->count;
            oracle->offsets[result] = oracle->bytes_length;
            oracle->lengths[result] = length;
            memcpy(oracle->bytes + oracle->bytes_length, bytes, length);
            oracle->bytes_length += length;
            oracle->count += 1;
            oracle->slots[slot] = result;
        }
        else if (oracle->lengths[id] == length && memcmp(oracle->bytes + oracle->offsets[id], bytes, length) == 0)
        {
            result = id;
        }
        slot = (slot + 1) & (BUSTER_REFINE_ORACLE_SLOTS - 1);
    }
    return result;
}

// Back to the predefined names, which get the ids the intern table gives them.
static void refine_oracle_reset(RefineOracle* oracle)
{
    memset(oracle->slots, 0, BUSTER_REFINE_ORACLE_SLOTS * sizeof(uint32_t));
    oracle->bytes_length = 0;
    oracle->count = 1;
    for (uint32_t name = 1; name < C_NAME_COUNT; name += 1)
    {
        refine_oracle_intern(oracle, (const uint8_t*)c_name_spellings[name], (uint32_t)strlen(c_name_spellings[name]));
    }
}

static bool refine_oracle_initialize(RefineOracle* oracle)
{
    oracle->arena = arena_reserve(BUSTER_REFINE_ORACLE_BYTES + (uint64_t)BUSTER_REFINE_ORACLE_IDS * 12 + (uint64_t)BUSTER_REFINE_ORACLE_SLOTS * 4 + 256);
    oracle->bytes = arena_push(&oracle->arena, BUSTER_REFINE_ORACLE_BYTES, 64);
    oracle->offsets = arena_push_array(&oracle->arena, BUSTER_REFINE_ORACLE_IDS, sizeof(uint64_t), 64);
    oracle->lengths = arena_push_array(&oracle->arena, BUSTER_REFINE_ORACLE_IDS, sizeof(uint32_t), 64);
    oracle->slots = arena_push_array(&oracle->arena, BUSTER_REFINE_ORACLE_SLOTS, sizeof(uint32_t), 64);
    const bool result = oracle->bytes != 0 && oracle->offsets != 0 && oracle->lengths != 0 && oracle->slots != 0;
    if (result)
    {
        refine_oracle_reset(oracle);
    }
    return result;
}

// The size of the line splice at `p` (a backslash, or "??/" with trigraphs, then spaces, tabs, form feeds or vertical
// tabs, then a newline: "\n", "\r", "\r\n" or "\n\r"), or 0 if there is none.
static uint64_t refine_oracle_splice(const uint8_t* source, uint64_t length, uint64_t p, bool trigraphs)
{
    uint64_t backslash = 0;
    if (p < length && source[p] == '\\')
    {
        backslash = 1;
    }
    else if (trigraphs && p + 2 < length && source[p] == '?' && source[p + 1] == '?' && source[p + 2] == '/')
    {
        backslash = 3;
    }
    uint64_t result = 0;
    if (backslash != 0)
    {
        uint64_t q = p + backslash;
        while (q < length && (source[q] == ' ' || source[q] == '\t' || source[q] == '\f' || source[q] == '\v'))
        {
            q += 1;
        }
        if (q < length && (source[q] == '\n' || source[q] == '\r'))
        {
            const uint8_t newline = source[q];
            q += 1;
            if (q < length && (source[q] == '\n' || source[q] == '\r') && source[q] != newline)
            {
                q += 1;
            }
            result = q - p;
        }
    }
    return result;
}

static bool refine_oracle_gap_is_splices(const uint8_t* source, uint64_t length, uint32_t gap_start, uint32_t gap_end, bool trigraphs)
{
    uint64_t p = gap_start;
    uint64_t splice = 1;
    while (p < gap_end && splice != 0)
    {
        splice = refine_oracle_splice(source, length, p, trigraphs);
        p += splice;
    }
    return p == gap_end;
}

static int32_t refine_oracle_hex(uint8_t c)
{
    int32_t result = -1;
    if (c >= '0' && c <= '9')
    {
        result = c - '0';
    }
    else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
    {
        result = (c | 0x20) - 'a' + 10;
    }
    return result;
}

// The key of an identifier token in two passes: translation phase 2 (splices removed, and "??/" read as a backslash
// with trigraphs), then each UCN (\uXXXX, \UXXXXXXXX or \u{...}) as UTF-8; a named UCN stays as written.
static uint32_t refine_oracle_key(const uint8_t* source, uint64_t length, uint32_t start, uint32_t end, bool trigraphs, uint8_t* cleaned, uint8_t* out)
{
    uint32_t cleaned_length = 0;
    uint64_t p = start;
    while (p < end)
    {
        const uint64_t splice = refine_oracle_splice(source, length, p, trigraphs);
        if (splice != 0)
        {
            p += splice;
        }
        else if (trigraphs && p + 2 < end && source[p] == '?' && source[p + 1] == '?' && source[p + 2] == '/')
        {
            cleaned[cleaned_length] = '\\';
            cleaned_length += 1;
            p += 3;
        }
        else
        {
            cleaned[cleaned_length] = source[p];
            cleaned_length += 1;
            p += 1;
        }
    }
    uint32_t result = 0;
    uint32_t i = 0;
    while (i < cleaned_length)
    {
        if (cleaned[i] == '\\' && i + 1 < cleaned_length && (cleaned[i + 1] == 'u' || cleaned[i + 1] == 'U'))
        {
            uint32_t code_point = 0;
            uint32_t j = i + 2;
            if (cleaned[j] == '{')
            {
                j += 1;
                while (cleaned[j] != '}')
                {
                    code_point = code_point * 16 + (uint32_t)refine_oracle_hex(cleaned[j]);
                    j += 1;
                }
                j += 1;
            }
            else
            {
                const uint32_t digits = cleaned[i + 1] == 'u' ? 4 : 8;
                for (uint32_t k = 0; k < digits; k += 1)
                {
                    code_point = code_point * 16 + (uint32_t)refine_oracle_hex(cleaned[j]);
                    j += 1;
                }
            }
            result += refine_encode_utf8(code_point, out + result);
            i = j;
        }
        else
        {
            out[result] = cleaned[i];
            result += 1;
            i += 1;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// One input through both paths and the oracle

typedef struct RefineTestState RefineTestState;
struct RefineTestState
{
    InternTable scalar_table;
    InternTable variant_tables[REFINE_VARIANT_COUNT];
    RefineOracle oracle;
    // The lexer's output, kept for the oracle, and each path's values and leading-space words.
    uint8_t* kinds;
    uint32_t* starts;
    uint32_t* ends;
    uint32_t* scalar_values;
    uint32_t* variant_values[REFINE_VARIANT_COUNT];
    uint64_t* scalar_space;
    uint64_t* variant_space[REFINE_VARIANT_COUNT];
    uint8_t* key_buffers;
    uint64_t capacity;
    Arena arena;
    // How many inputs were compared: the vectorized path refines every other one in place.
    uint64_t comparisons;
};

// Room for refining sources of up to `length` bytes, which it allocates again when a source is longer.
static bool refine_test_reserve(RefineTestState* state, uint64_t length)
{
    bool result = true;
    if (length > state->capacity)
    {
        arena_release(&state->arena);
        const uint64_t tokens = BUSTER_C_TOKENIZER_CAPACITY(length);
        state->arena = arena_reserve(tokens * (13 + 4 * (REFINE_VARIANT_COUNT + 1)) + (REFINE_VARIANT_COUNT + 1) * (tokens / 64 + 2) * 8 + 2 * length + 4096);
        state->kinds = arena_push(&state->arena, tokens, 64);
        state->starts = arena_push_array(&state->arena, tokens, sizeof(uint32_t), 64);
        state->ends = arena_push_array(&state->arena, tokens, sizeof(uint32_t), 64);
        state->scalar_values = arena_push_array(&state->arena, tokens, sizeof(uint32_t), 64);
        state->scalar_space = arena_push_array(&state->arena, tokens / 64 + 1, sizeof(uint64_t), 64);
        state->key_buffers = arena_push(&state->arena, 2 * length + 2, 64);
        result = state->kinds != 0 && state->starts != 0 && state->ends != 0 && state->scalar_values != 0 && state->scalar_space != 0 && state->key_buffers != 0;
        for (uint32_t variant = 0; variant < REFINE_VARIANT_COUNT; variant += 1)
        {
            state->variant_values[variant] = arena_push_array(&state->arena, tokens, sizeof(uint32_t), 64);
            state->variant_space[variant] = arena_push_array(&state->arena, tokens / 64 + 1, sizeof(uint64_t), 64);
            result &= state->variant_values[variant] != 0 && state->variant_space[variant] != 0;
        }
        state->capacity = result ? length : 0;
    }
    return result;
}

// Prints a token with its first bytes.
static void refine_test_print_token(const uint8_t* source, const RefineTestState* state, uint64_t i)
{
    printf("token %lu (%s, [%u, %u)) \"", (unsigned long)i, c_token_kind_name(state->kinds[i]), state->starts[i], state->ends[i]);
    for (uint32_t p = state->starts[i]; p < state->ends[i] && p < state->starts[i] + 40; p += 1)
    {
        const uint8_t c = source[p];
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

// Lexes `source` and refines it with the scalar path into separate values and with every variant of the vectorized
// path, each with a table of its own, by turns in place (in a copy of the ends, as the preprocessor does) and into
// separate values, and compares them all with the oracle. Every token goes through the oracle even after a difference,
// so that its table stays in step with the others for the next inputs. Returns false (and prints the first difference,
// if `name` isn't null) if they differ.
static bool refine_test_compare(RefineTestState* state, const uint8_t* source, uint64_t length, uint32_t flags, const char* name)
{
    bool result = refine_test_reserve(state, length);
    const int64_t count = result ? c_tokenize(source, length, state->kinds, state->starts, state->ends, flags) : -1;
    result = count >= 0;
    if (result)
    {
        const uint64_t token_count = (uint64_t)count;
        const bool trigraphs = (flags & C_TOKENIZER_TRIGRAPHS) != 0;
        state->comparisons += 1;
        result = refine_tokens_scalar(&state->scalar_table, source, length, state->kinds, state->starts, state->ends, token_count, state->scalar_values,
                                      state->scalar_space, flags);
        for (uint32_t variant = 0; variant < REFINE_VARIANT_COUNT; variant += 1)
        {
            const uint32_t* ends = state->ends;
            if (((state->comparisons + variant) & 1) != 0)
            {
                memcpy(state->variant_values[variant], state->ends, token_count * sizeof(uint32_t));
                ends = state->variant_values[variant];
            }
            result &= refine_tokens_variant((RefineVariant)variant, &state->variant_tables[variant], source, length, state->kinds, state->starts, ends,
                                            token_count, state->variant_values[variant], state->variant_space[variant], flags);
        }
        const bool fits = result;
        bool reported = false;
        uint32_t previous_end = 0;
        for (uint64_t i = 0; i < token_count; i += 1)
        {
            const uint32_t start = state->starts[i];
            const uint32_t end = state->ends[i];
            const bool start_of_line = (state->kinds[i] & BUSTER_C_TOKEN_START_OF_LINE) != 0;
            const bool gap = start != previous_end;
            const bool expected_space = gap && (start_of_line || !refine_oracle_gap_is_splices(source, length, previous_end, start, trigraphs));
            previous_end = end;
            uint32_t expected_value = end;
            if ((state->kinds[i] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
            {
                const uint32_t key_length = refine_oracle_key(source, length, start, end, trigraphs, state->key_buffers, state->key_buffers + length + 1);
                expected_value = refine_oracle_intern(&state->oracle, state->key_buffers + length + 1, key_length);
            }
            bool same = state->scalar_values[i] == expected_value && ((state->scalar_space[i / 64] >> (i % 64)) & 1) == expected_space;
            for (uint32_t variant = 0; variant < REFINE_VARIANT_COUNT; variant += 1)
            {
                same &= state->variant_values[variant][i] == expected_value && ((state->variant_space[variant][i / 64] >> (i % 64)) & 1) == expected_space;
            }
            if (!same && !reported && name != 0)
            {
                printf("%s: ", name);
                refine_test_print_token(source, state, i);
                printf(": value %u, leading space %d; scalar %u %d", expected_value, expected_space, state->scalar_values[i],
                       (int)((state->scalar_space[i / 64] >> (i % 64)) & 1));
                for (uint32_t variant = 0; variant < REFINE_VARIANT_COUNT; variant += 1)
                {
                    printf("; %s %u %d", refine_variant_names[variant], state->variant_values[variant][i],
                           (int)((state->variant_space[variant][i / 64] >> (i % 64)) & 1));
                }
                printf("\n");
                reported = true;
            }
            result &= same;
        }
        // The words' bits past the last token are zero on every path.
        bool same_counts = state->scalar_table.count == state->oracle.count;
        for (uint32_t variant = 0; variant < REFINE_VARIANT_COUNT; variant += 1)
        {
            if (token_count % 64 != 0)
            {
                result &= state->scalar_space[token_count / 64] == state->variant_space[variant][token_count / 64];
            }
            same_counts &= state->variant_tables[variant].count == state->oracle.count;
        }
        if (fits && !same_counts && name != 0)
        {
            printf("%s: %lu tokens; ids in use: scalar %u, oracle %u\n", name, (unsigned long)token_count, state->scalar_table.count, state->oracle.count);
        }
        result &= same_counts;
    }
    else if (name != 0)
    {
        printf("%s: can't lex (%lu bytes)\n", name, (unsigned long)length);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The checks

static bool refine_check_hash(uint64_t* random)
{
    uint8_t bytes[BUSTER_REFINE_TEST_HASH_LENGTHS] __attribute__((aligned(64)));
    uint64_t failures = 0;
    // The last four spellings of up to 64 bytes, for intern_hash_vectors4: their vectors, lengths and hashes.
    __m512i group[4];
    uint32_t group_lengths[16] __attribute__((aligned(64))) = { 0 };
    uint64_t group_hashes[4];
    uint32_t grouped = 0;
    for (uint64_t round = 0; round < BUSTER_REFINE_TEST_HASH_ROUNDS; round += 1)
    {
        const uint32_t length = (uint32_t)(round % BUSTER_REFINE_TEST_HASH_LENGTHS);
        for (uint32_t i = 0; i < length; i += 1)
        {
            bytes[i] = (uint8_t)(1 + refine_test_random(random) % 255);
        }
        const uint64_t expected = intern_hash_bytes_scalar(bytes, length);
        failures += intern_hash_bytes(bytes, length) != expected;
        if (length <= 64)
        {
            const __m512i vector = _mm512_maskz_loadu_epi8(length >= 64 ? ~0ull : _bzhi_u64(~0ull, length), bytes);
            failures += intern_hash_vector(vector, length) != expected;
            group[grouped] = vector;
            group_lengths[4 * grouped] = length;
            group_hashes[grouped] = expected;
            grouped += 1;
            if (grouped == 4)
            {
                uint64_t hashes[8];
                __m128i firsts[4];
                __m512i first_lanes;
                _mm512_storeu_si512(hashes, intern_hash_vectors4(group[0], group[1], group[2], group[3], _mm512_load_si512(group_lengths), &first_lanes));
                _mm512_storeu_si512(firsts, first_lanes);
                for (uint32_t i = 0; i < 4; i += 1)
                {
                    failures += hashes[2 * i] != group_hashes[i];
                    failures += _mm_movemask_epi8(_mm_cmpeq_epi8(firsts[i], _mm512_castsi512_si128(group[i]))) != 0xFFFF;
                }
                grouped = 0;
            }
        }
    }
    printf("hash: %lu differences in %u spellings of 0 to %u bytes\n", (unsigned long)failures, BUSTER_REFINE_TEST_HASH_ROUNDS, BUSTER_REFINE_TEST_HASH_LENGTHS - 1);
    return failures == 0;
}

// Writes `index` in base 26 (lowercase letters, the least significant first) into `count` bytes at `out`.
static void refine_test_base26(uint64_t index, uint8_t* out, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        out[i] = (uint8_t)('a' + index % 26);
        index /= 26;
    }
}

// `count` random lowercase letters at `out`.
static void refine_test_letters(uint64_t* random, uint8_t* out, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        out[i] = (uint8_t)('a' + refine_test_random(random) % 26);
    }
}

static int refine_test_compare_hashes(const void* a, const void* b)
{
    const uint64_t x = *(const uint64_t*)a;
    const uint64_t y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

// Records a spelling's hash for refine_check_long_hashes, and counts a difference between intern_hash_bytes and
// intern_hash_bytes_scalar.
static void refine_test_long_hash(const uint8_t* bytes, uint32_t length, uint64_t* hashes, uint32_t* count, uint64_t* differences)
{
    const uint64_t hash = intern_hash_bytes_scalar(bytes, length);
    *differences += intern_hash_bytes(bytes, length) != hash;
    hashes[*count] = hash;
    *count += 1;
}

// Spellings made of repeated, swapped or paired parts, which a fold that XORed every 64-byte chunk alike, or a chunk's
// rotated lanes without a round, would hash alike:
//   - `X X` for 8,192 64-byte X (an index in base 26, then 'a's), as such a fold would hash every one of them by its
//     length alone;
//   - a 16-byte lane eight times, for 4,096 lanes;
//   - `X Y` and `Y X` for 2,048 random pairs;
//   - `X` two, three and four times for 2,048 random X;
//   - 65,536 spellings of 32 bytes (#192's review): 'a's, where each set bit b of a 16-bit index makes byte b an 'e' and
//     the byte that lane 1's rotation by 17 lines its changed bit up with a 'c'.
// All of them must hash differently: the chance that two of 88,064 random 64-bit hashes are equal is about 2^-32.
static bool refine_check_long_hashes(uint64_t* random)
{
    static uint64_t hashes[BUSTER_REFINE_TEST_LONG_HASHES];
    uint8_t bytes[256];
    uint8_t swapped[128];
    uint32_t count = 0;
    uint64_t differences = 0;
    for (uint32_t i = 0; i < 8192; i += 1)
    {
        memset(bytes, 'a', 64);
        refine_test_base26(i, bytes, 8);
        memcpy(bytes + 64, bytes, 64);
        refine_test_long_hash(bytes, 128, hashes, &count, &differences);
    }
    for (uint32_t i = 0; i < 4096; i += 1)
    {
        memset(bytes, 'b', 16);
        refine_test_base26(i, bytes, 4);
        for (uint32_t lane = 1; lane < 8; lane += 1)
        {
            memcpy(bytes + lane * 16, bytes, 16);
        }
        refine_test_long_hash(bytes, 128, hashes, &count, &differences);
    }
    for (uint32_t i = 0; i < 2048; i += 1)
    {
        refine_test_letters(random, bytes, 128);
        memcpy(swapped, bytes + 64, 64);
        memcpy(swapped + 64, bytes, 64);
        refine_test_long_hash(bytes, 128, hashes, &count, &differences);
        refine_test_long_hash(swapped, 128, hashes, &count, &differences);
    }
    for (uint32_t i = 0; i < 2048; i += 1)
    {
        refine_test_letters(random, bytes, 64);
        for (uint32_t times = 2; times <= 4; times += 1)
        {
            memcpy(bytes + (times - 1) * 64, bytes, 64);
            refine_test_long_hash(bytes, times * 64, hashes, &count, &differences);
        }
    }
    for (uint32_t index = 0; index < 65536; index += 1)
    {
        memset(bytes, 'a', 32);
        for (uint32_t b = 0; b < 16; b += 1)
        {
            if (index & (1u << b))
            {
                bytes[b] = 'e';
                bytes[16 + (b / 8) * 8 + ((b + 6) & 7)] = 'c';
            }
        }
        refine_test_long_hash(bytes, 32, hashes, &count, &differences);
    }
    qsort(hashes, count, sizeof(hashes[0]), refine_test_compare_hashes);
    uint64_t equal = 0;
    for (uint32_t i = 1; i < count; i += 1)
    {
        equal += hashes[i] == hashes[i - 1];
    }
    printf("structured hashes: %lu equal hashes and %lu differences in %u spellings of repeated, swapped and paired parts\n", (unsigned long)equal,
           (unsigned long)differences, count);
    return equal == 0 && differences == 0;
}

// The same bit flipped in two lanes of one chunk, both inside a random spelling of 17 to 200 bytes: the hash must change,
// which a fold of the lanes as they are can't do (the two flips would cancel), nor one AES round per lane where the two
// lanes' bytes there are equal once whitened (a third of the spellings make them so).
static bool refine_check_aligned_flips(uint64_t* random)
{
    uint8_t bytes[BUSTER_REFINE_TEST_HASH_LENGTHS];
    uint64_t unchanged = 0;
    uint64_t differences = 0;
    uint32_t tried = 0;
    for (uint32_t i = 0; i < BUSTER_REFINE_TEST_FLIPS; i += 1)
    {
        const uint32_t length = 17 + (uint32_t)(refine_test_random(random) % (BUSTER_REFINE_TEST_HASH_LENGTHS - 17));
        refine_test_letters(random, bytes, length);
        // Half of the spellings repeat one letter, so that the two lanes' bytes are equal.
        if (refine_test_random(random) % 2 == 0)
        {
            memset(bytes, bytes[0], length);
        }
        // Two lanes j < k of one chunk, and a byte offset and bit in a lane, inside the spelling in both.
        const uint32_t lanes = (length + 15) / 16;
        const uint32_t k_lane = 1 + (uint32_t)(refine_test_random(random) % (lanes - 1));
        const uint32_t chunk_first = k_lane & ~3u;
        const uint32_t j_lane = k_lane == chunk_first ? k_lane : chunk_first + (uint32_t)(refine_test_random(random) % (k_lane - chunk_first));
        const uint32_t offset = (uint32_t)(refine_test_random(random) % 16);
        const uint8_t bit = (uint8_t)(1u << (refine_test_random(random) % 8));
        const uint64_t equal_whitened = refine_test_random(random) % 3;
        if (j_lane != k_lane && k_lane * 16 + offset < length && equal_whitened == 0)
        {
            // Lane k's byte such that both bytes are equal after the whitening: hashed again, before the flips.
            const uint8_t j_white = (uint8_t)(intern_lane_whitening[2 * (j_lane & 3) + offset / 8] >> (8 * (offset % 8)));
            const uint8_t k_white = (uint8_t)(intern_lane_whitening[2 * (k_lane & 3) + offset / 8] >> (8 * (offset % 8)));
            bytes[k_lane * 16 + offset] = (uint8_t)(bytes[j_lane * 16 + offset] ^ j_white ^ k_white);
        }
        const uint64_t base = intern_hash_bytes_scalar(bytes, length);
        if (j_lane != k_lane && k_lane * 16 + offset < length)
        {
            bytes[j_lane * 16 + offset] ^= bit;
            bytes[k_lane * 16 + offset] ^= bit;
            const uint64_t after = intern_hash_bytes_scalar(bytes, length);
            unchanged += after == base;
            differences += intern_hash_bytes(bytes, length) != after;
            tried += 1;
        }
    }
    printf("aligned flips: %lu of %u hashes unchanged and %lu differences\n", (unsigned long)unchanged, tried, (unsigned long)differences);
    return unchanged == 0 && differences == 0;
}


// The language modes, by their -std= names.
typedef struct RefineTestMode RefineTestMode;
struct RefineTestMode
{
    const char* name;
    CLanguage language;
};

static const RefineTestMode refine_test_modes[] = {
    { "c89", { .standard = 1989, .gnu = false } },
    { "gnu89", { .standard = 1989, .gnu = true } },
    { "c99", { .standard = 1999, .gnu = false } },
    { "gnu99", { .standard = 1999, .gnu = true } },
    { "c11", { .standard = 2011, .gnu = false } },
    { "gnu11", { .standard = 2011, .gnu = true } },
    { "c17", { .standard = 2017, .gnu = false } },
    { "gnu17", { .standard = 2017, .gnu = true } },
    { "c23", { .standard = 2023, .gnu = false } },
    { "gnu23", { .standard = 2023, .gnu = true } },
};

// The names that are keywords in some modes only, with the modes clang 23 makes them keywords in (where its
// __is_identifier gives 0), written out mode by mode rather than computed from the years as c_name_is_keyword computes
// them. Every other keyword name is a keyword in every mode, and the other names in none.
typedef struct RefineTestKeyword RefineTestKeyword;
struct RefineTestKeyword
{
    IdentId name;
    const char* modes;
};

static const RefineTestKeyword refine_test_keywords[] = {
    { C_NAME_INLINE, " gnu89 c99 gnu99 c11 gnu11 c17 gnu17 c23 gnu23 " },
    { C_NAME_RESTRICT, " c99 gnu99 c11 gnu11 c17 gnu17 c23 gnu23 " },
    { C_NAME_ALIGNAS, " c23 gnu23 " },
    { C_NAME_ALIGNOF, " c23 gnu23 " },
    { C_NAME_BOOL, " c23 gnu23 " },
    { C_NAME_CONSTEXPR, " c23 gnu23 " },
    { C_NAME_FALSE, " c23 gnu23 " },
    { C_NAME_NULLPTR, " c23 gnu23 " },
    { C_NAME_STATIC_ASSERT, " c23 gnu23 " },
    { C_NAME_THREAD_LOCAL, " c23 gnu23 " },
    { C_NAME_TRUE, " c23 gnu23 " },
    { C_NAME_TYPEOF_UNQUAL, " c23 gnu23 " },
    { C_NAME_TYPEOF, " gnu89 gnu99 gnu11 gnu17 c23 gnu23 " },
    { C_NAME_ASM, " gnu89 gnu99 gnu11 gnu17 gnu23 " },
};

// Spellings chosen apart from BUSTER_C_NAMES, with clang 23's answer in every C mode (its __is_identifier), so that a
// keyword missing from the predefined names fails: keywords from each part of clang's TokenKinds.def that C has
// (#192's review named the first eight), and names that are no keywords in C (C++'s, Microsoft's and builtins').
typedef struct RefineTestSpelling RefineTestSpelling;
struct RefineTestSpelling
{
    const char* spelling;
    bool keyword;
};

static const RefineTestSpelling refine_test_spellings[] = {
    { "_ExtInt", true },
    { "__builtin_bit_cast", true },
    { "__builtin_FILE", true },
    { "__builtin_LINE", true },
    { "__cdecl", true },
    { "__fp16", true },
    { "__nullptr", true },
    { "__builtin_convertvector", true },
    { "_Countof", true },
    { "__builtin_offsetof", true },
    { "__arm_streaming", true },
    { "__objc_yes", true },
    { "__vectorcall", true },
    { "__module_private__", true },
    { "__ptrauth", true },
    { "__auto_type", true },
    { "_Nullable_result", true },
    { "__builtin_expect", false },
    { "__has_include", false },
    { "__int64", false },
    { "__declspec", false },
    { "__is_pod", false },
    { "__w64", false },
    { "main", false },
};

// Is `name` a keyword in the mode `mode` names, by refine_test_keywords?
static bool refine_test_expected_keyword(IdentId name, const char* mode)
{
    bool result = name != 0 && name <= C_NAME_KEYWORD_COUNT;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refine_test_keywords); i += 1)
    {
        if (refine_test_keywords[i].name == name)
        {
            char key[16];
            snprintf(key, sizeof(key), " %s ", mode);
            result = strstr(refine_test_keywords[i].modes, key) != 0;
        }
    }
    return result;
}

static bool refine_check_names(InternTable* table)
{
    uint64_t failures = 0;
    for (uint32_t name = 1; name < C_NAME_COUNT; name += 1)
    {
        const char* spelling = c_name_spellings[name];
        const uint32_t length = (uint32_t)strlen(spelling);
        const bool same = intern_find(table, (const uint8_t*)spelling, length) == name && intern_length(table, name) == length &&
                          memcmp(intern_bytes(table, name), spelling, length) == 0;
        if (!same)
        {
            printf("names: %s doesn't have id %u\n", spelling, name);
        }
        failures += !same;
    }
    uint64_t wrong = 0;
    for (uint32_t name = 0; name < C_NAME_COUNT; name += 1)
    {
        for (uint32_t m = 0; m < BUSTER_ARRAY_LENGTH(refine_test_modes); m += 1)
        {
            const bool keyword = c_name_is_keyword(name, refine_test_modes[m].language);
            const bool expected = refine_test_expected_keyword(name, refine_test_modes[m].name);
            if (keyword != expected && wrong < BUSTER_REFINE_TEST_REPORTED)
            {
                printf("names: %s is%s a keyword in %s\n", name == 0 ? "id 0" : c_name_spellings[name], keyword ? "" : " not", refine_test_modes[m].name);
            }
            wrong += keyword != expected;
        }
    }
    // The spellings chosen apart: a keyword must be a predefined keyword in every mode, and anything else no keyword.
    uint64_t misnamed = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refine_test_spellings); i += 1)
    {
        const char* spelling = refine_test_spellings[i].spelling;
        const IdentId id = intern_find(table, (const uint8_t*)spelling, (uint32_t)strlen(spelling));
        bool keyword = true;
        bool identifier = true;
        for (uint32_t m = 0; m < BUSTER_ARRAY_LENGTH(refine_test_modes); m += 1)
        {
            const bool is_keyword = c_name_is_keyword(id, refine_test_modes[m].language);
            keyword &= is_keyword;
            identifier &= !is_keyword;
        }
        const bool right = refine_test_spellings[i].keyword ? keyword : identifier;
        if (!right)
        {
            printf("names: %s should%s be a keyword in every mode\n", spelling, refine_test_spellings[i].keyword ? "" : " not");
        }
        misnamed += !right;
    }
    printf("names: %lu of %u predefined names are wrong, %lu of %u keyword answers (every name in %u modes), and %lu of %u spellings chosen apart\n",
           (unsigned long)failures, C_NAME_COUNT - 1, (unsigned long)wrong, C_NAME_COUNT * (uint32_t)BUSTER_ARRAY_LENGTH(refine_test_modes),
           (uint32_t)BUSTER_ARRAY_LENGTH(refine_test_modes), (unsigned long)misnamed, (uint32_t)BUSTER_ARRAY_LENGTH(refine_test_spellings));
    return failures == 0 && wrong == 0 && misnamed == 0;
}

// A random spelling of 1 to 200 bytes (mostly short) from the identifier characters, written to `out`.
static uint32_t refine_test_random_spelling(uint64_t* random, uint8_t* out)
{
    static const char characters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$";
    const uint64_t shape = refine_test_random(random) % 16;
    const uint32_t length = shape < 12 ? 1 + (uint32_t)(refine_test_random(random) % 16) : 1 + (uint32_t)(refine_test_random(random) % 200);
    for (uint32_t i = 0; i < length; i += 1)
    {
        out[i] = (uint8_t)characters[refine_test_random(random) % (sizeof(characters) - 1)];
    }
    return length;
}

static bool refine_check_table(uint64_t* random)
{
    InternTable table;
    static IdentId ids[BUSTER_REFINE_TEST_TABLE_SPELLINGS];
    bool result = intern_initialize(&table);
    uint64_t failures = 0;
    if (result)
    {
        uint64_t sequence = *random;
        uint8_t spelling[256];
        for (uint32_t i = 0; i < BUSTER_REFINE_TEST_TABLE_SPELLINGS; i += 1)
        {
            const uint32_t length = refine_test_random_spelling(random, spelling);
            const IdentId found = intern_find(&table, spelling, length);
            const IdentId id = intern_find_or_add(&table, spelling, length);
            // A spelling seen before (short ones repeat, and some are predefined names) keeps its id; a new one gets the
            // next.
            failures += id == 0 || (found != 0 && found != id) || (found == 0 && id != table.count - 1);
            ids[i] = id;
        }
        // The same sequence again: every spelling is found, with its bytes.
        for (uint32_t i = 0; i < BUSTER_REFINE_TEST_TABLE_SPELLINGS; i += 1)
        {
            const uint32_t length = refine_test_random_spelling(&sequence, spelling);
            const IdentId id = intern_find(&table, spelling, length);
            failures += id != ids[i] || intern_find_or_add(&table, spelling, length) != id || intern_length(&table, id) != length ||
                        memcmp(intern_bytes(&table, id), spelling, length) != 0;
        }
        // Spellings with characters the others don't have are absent.
        for (uint32_t i = 0; i < 10000; i += 1)
        {
            const uint32_t length = refine_test_random_spelling(random, spelling);
            spelling[0] = '#';
            failures += intern_find(&table, spelling, length) != 0;
        }
        printf("table: %lu failures in %u spellings (%u ids, %lu buckets)\n", (unsigned long)failures, BUSTER_REFINE_TEST_TABLE_SPELLINGS, table.count,
               (unsigned long)(table.bucket_mask + 1));
        intern_release(&table);
    }
    else
    {
        printf("table: can't reserve the table\n");
    }
    return result && failures == 0;
}

// A fixture: a source, the leading-space bit of the token at `token` that it must have, and two tokens that must have
// the same value (the same identifier), if `same_as` isn't negative.
typedef struct RefineFixture RefineFixture;
struct RefineFixture
{
    const char* source;
    uint32_t flags;
    int32_t token;
    bool leading_space;
    int32_t same_a;
    int32_t same_b;
};

static const RefineFixture refine_fixtures[] = {
    // A splice is no whitespace: function-like; a comment is: object-like.
    { "#define F\\\n(x) x + 10\nF(2)\n", 0, 3, false, -1, -1 },
    { "#define F/**/(x) x + 10\nF(2)\n", 0, 3, true, -1, -1 },
    { "#define F \\\n(x) x\n", 0, 3, true, -1, -1 },
    { "#define F\\  \t\n(x) x\n", 0, 3, false, -1, -1 },
    { "#define F\\\r\n(x) x\n", 0, 3, false, -1, -1 },
    { "#define F\\\n\\\n\\\r\n(x) x\n", 0, 3, false, -1, -1 },
    { "#define F\\\n/**/(x) x\n", 0, 3, true, -1, -1 },
    { "#define F/**/\\\n(x) x\n", 0, 3, true, -1, -1 },
    { "#define F?" "?/\n(x) x\n", C_TOKENIZER_TRIGRAPHS, 3, false, -1, -1 },
    // Stringizing a\<newline>+b gives "a+b", and a/**/+b gives "a +b".
    { "S(a\\\n+b)", 0, 3, false, -1, -1 },
    { "S(a/**/+b)", 0, 3, true, -1, -1 },
    { "x)\\\n(y", 0, 2, false, -1, -1 },
    // Identifiers spelled with splices and UCNs are the same identifier.
    { "foo fo\\\no f\\\r\no\\\no", 0, -1, false, 0, 1 },
    { "foo fo\\\no f\\\r\no\\\no", 0, -1, false, 1, 2 },
    { "\xC3\xA9t\xC3\xA9 \\u00e9t\\u00E9 \\U000000e9t\\u{e9}", 0, -1, false, 0, 1 },
    { "\xC3\xA9t\xC3\xA9 \\u00e9t\\u00E9 \\U000000e9t\\u{e9}", 0, -1, false, 1, 2 },
    { "caf\\u00\\\ne9 caf\xC3\xA9", 0, -1, false, 0, 1 },
    { "x?" "?/u00e9 x\xC3\xA9", C_TOKENIZER_TRIGRAPHS, -1, false, 0, 1 },
    // Around the vectorized path's 63-byte limit: 62, 63, 64 and 65 bytes, and a spliced long one.
    { "a12345678901234567890123456789012345678901234567890123456789012 a12345678901234567890123456789012345678901234567890123456789012", 0, -1, false, 0, 1 },
    { "a123456789012345678901234567890123456789012345678901234567890123 a123456789012345678901234567890123456789012345678901234567890123", 0, -1, false, 0, 1 },
    { "a1234567890123456789012345678901234567890123456789012345678901234 a1234567890123456789012345678901234567890123456789012345678901234", 0, -1, false, 0, 1 },
    { "a12345678901234567890123456789012345678901234567890123456789012345 a123456789012345678901234567890123456789012345678901234567890\\\n12345", 0, -1, false, 0, 1 },
};

// Each fixture at each of the 64 misalignments from a cache line.
static bool refine_check_fixtures(RefineTestState* state)
{
    static uint8_t buffer[64 + 256] __attribute__((aligned(64)));
    uint64_t failures = 0;
    for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(refine_fixtures); i += 1)
    {
        const RefineFixture* fixture = &refine_fixtures[i];
        const uint64_t length = strlen(fixture->source);
        bool passed = length <= 256;
        for (uint32_t misalignment = 0; passed && misalignment < 64; misalignment += 1)
        {
            char name[64];
            snprintf(name, sizeof(name), "fixture %lu at misalignment %u", (unsigned long)i, misalignment);
            memcpy(buffer + misalignment, fixture->source, length);
            passed = refine_test_compare(state, buffer + misalignment, length, fixture->flags, name);
            if (passed && fixture->token >= 0)
            {
                passed = ((state->scalar_space[fixture->token / 64] >> (fixture->token % 64)) & 1) == fixture->leading_space;
            }
            if (passed && fixture->same_a >= 0)
            {
                passed = state->scalar_values[fixture->same_a] == state->scalar_values[fixture->same_b] &&
                         (state->kinds[fixture->same_a] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
            }
            if (!passed)
            {
                printf("fixture %lu fails at misalignment %u: %s\n", (unsigned long)i, misalignment, fixture->source);
            }
        }
        failures += !passed;
    }
    printf("fixtures: %lu of %lu fail (each at 64 misalignments)\n", (unsigned long)failures, (unsigned long)BUSTER_ARRAY_LENGTH(refine_fixtures));
    return failures == 0;
}

// What refine_check_spellings writes into an identifier: a line splice (three spellings, and the trigraph one, with
// trigraphs on) or a UCN, in place of an é (which the plain identifier has there).
static const char* const refine_spelling_inserts[] = { "\\\n", "\\\r\n", "\\ \t\n", "?" "?/\n", "\\u00e9", "\\U000000E9" };

// Identifiers of every length from 1 to BUSTER_REFINE_TEST_SPELLING_LENGTHS bytes, with each insert of
// refine_spelling_inserts at every position (a splice strictly inside, a UCN anywhere), each followed by the same
// identifier written plainly: the two must be one identifier, and each input is compared with the oracle as a random one
// is. The lengths around 11, 16, 32 and 64 bytes cross the table's prefix, the vector hash's lanes and the masked load.
static bool refine_check_spellings(RefineTestState* state, uint64_t misalignment_seed)
{
    static uint8_t buffer[64 + 4 * BUSTER_REFINE_TEST_SPELLING_LENGTHS] __attribute__((aligned(64)));
    uint64_t failures = 0;
    uint64_t inputs = 0;
    for (uint32_t length = 1; length <= BUSTER_REFINE_TEST_SPELLING_LENGTHS; length += 1)
    {
        for (uint32_t insert = 0; insert < BUSTER_ARRAY_LENGTH(refine_spelling_inserts); insert += 1)
        {
            const bool ucn = insert >= 4;
            const uint32_t flags = insert == 3 ? C_TOKENIZER_TRIGRAPHS : 0;
            for (uint32_t position = ucn ? 0 : 1; position < length; position += 1)
            {
                // The plain identifier: letters and digits after a letter, with an é (two bytes) at `position` for a UCN.
                uint8_t plain[2 * BUSTER_REFINE_TEST_SPELLING_LENGTHS];
                uint32_t plain_length = 0;
                for (uint32_t i = 0; i < length; i += 1)
                {
                    if (ucn && i == position)
                    {
                        plain[plain_length] = 0xC3;
                        plain[plain_length + 1] = 0xA9;
                        plain_length += 2;
                    }
                    else
                    {
                        plain[plain_length] = i == 0 ? (uint8_t)('a' + length % 26) : (uint8_t)"abcdefghijklmnopqrstuvwxyz0123456789_"[(i * 7 + length) % 37];
                        plain_length += 1;
                    }
                }
                // The spelled one: the same, with the insert before character `position` (in place of the é for a UCN).
                uint32_t offset = 0;
                uint32_t character = 0;
                uint8_t* out = buffer + (inputs * 13 + misalignment_seed) % 64;
                for (uint32_t i = 0; i < plain_length; character += 1)
                {
                    const uint32_t size = plain[i] == 0xC3 ? 2 : 1;
                    if (character == position)
                    {
                        const char* text = refine_spelling_inserts[insert];
                        const uint32_t text_length = (uint32_t)strlen(text);
                        memcpy(out + offset, text, text_length);
                        offset += text_length;
                        if (!ucn)
                        {
                            memcpy(out + offset, plain + i, size);
                            offset += size;
                        }
                    }
                    else
                    {
                        memcpy(out + offset, plain + i, size);
                        offset += size;
                    }
                    i += size;
                }
                out[offset] = ' ';
                memcpy(out + offset + 1, plain, plain_length);
                const uint64_t input_length = offset + 1 + plain_length;
                char name[96];
                snprintf(name, sizeof(name), "spelling of %u bytes with insert %u at %u", length, insert, position);
                bool passed = refine_test_compare(state, out, input_length, flags, failures < BUSTER_REFINE_TEST_REPORTED ? name : 0);
                const uint64_t token_count = (uint64_t)c_tokenize(out, input_length, state->kinds, state->starts, state->ends, flags);
                passed = passed && token_count == 2 && (state->kinds[0] & 0x7F) == C_TOKEN_RAW_IDENTIFIER && state->scalar_values[0] == state->scalar_values[1];
                if (!passed && failures < BUSTER_REFINE_TEST_REPORTED)
                {
                    printf("%s: not one identifier\n", name);
                }
                failures += !passed;
                inputs += 1;
            }
        }
    }
    printf("spellings: %lu of %lu inputs fail (identifiers of 1 to %u bytes, a splice or a UCN at every position)\n", (unsigned long)failures,
           (unsigned long)inputs, BUSTER_REFINE_TEST_SPELLING_LENGTHS);
    return failures == 0;
}

// The pieces random inputs are made of: identifiers from a vocabulary (so that they repeat), spliced and written with
// UCNs, splices and comments between tokens, and the other kinds of tokens.
static const char* const refine_test_pieces[] =
{
    "a", "b", "foo", "bar", "int", "return", "define", "x1", "_y", "$z", "very_long_identifier_name_that_goes_on",
    "fo\\\no", "f\\\r\noo", "ba\\  \nr", "\\u00e9", "caf\\u00e9", "caf\xC3\xA9", "\\U000003b1lpha", "\\u{3b1}lpha", "\xCE\xB1lpha",
    "x\\N{GREEK SMALL LETTER ALPHA}", "\\\n", "\\\r\n", " \\\n", "\\\n ", "/**/", "/* c */", "// line\n", " ", " ", "\t", "\n", "\r\n",
    "(", ")", "+", "+=", ";", ",", "#", "##", "123", "0x1F", "1e+5", "\"str\"", "\"s\\\\t\"", "'c'", "'\\n'",
    "<:", ":>", "<%", "%>", "%:", "%:%:", "1.2e+3", "0x1.p-3", ".5", "1..2", "0b101", "1'000", "08",
    "\n#define F\\\n(x) x\n", "\n#define G/**/(x) x\n",
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijk",
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl",
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklm",
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz",
};

// Writes a random input of up to `capacity` bytes: pieces, or now and then a new random identifier.
static uint64_t refine_test_input(uint64_t* random, uint8_t* buffer, uint64_t capacity)
{
    const uint64_t piece_count = 1 + refine_test_random(random) % BUSTER_REFINE_TEST_MAX_PIECES;
    uint64_t length = 0;
    bool full = false;
    for (uint64_t i = 0; !full && i < piece_count; i += 1)
    {
        uint8_t fresh[256];
        const uint8_t* piece;
        uint64_t piece_length;
        if (refine_test_random(random) % 8 == 0)
        {
            piece_length = refine_test_random_spelling(random, fresh);
            // Not a digit first, so that it lexes as an identifier.
            fresh[0] = 'i';
            piece = fresh;
        }
        else
        {
            const char* text = refine_test_pieces[refine_test_random(random) % BUSTER_ARRAY_LENGTH(refine_test_pieces)];
            piece = (const uint8_t*)text;
            piece_length = strlen(text);
        }
        if (length + piece_length + 1 > capacity)
        {
            full = true;
        }
        else
        {
            memcpy(buffer + length, piece, piece_length);
            length += piece_length;
            // Mostly a separator, so that pieces stay tokens of their own.
            if (refine_test_random(random) % 4 != 0)
            {
                buffer[length] = ' ';
                length += 1;
            }
        }
    }
    return length;
}

static bool refine_check_random(RefineTestState* state, uint64_t* random, uint64_t count)
{
    static uint8_t buffer[64 + BUSTER_REFINE_TEST_INPUT_BYTES] __attribute__((aligned(64)));
    uint64_t failures = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        const uint64_t misalignment = refine_test_random(random) % 64;
        const uint64_t length = refine_test_input(random, buffer + misalignment, BUSTER_REFINE_TEST_INPUT_BYTES);
        const uint32_t flags = refine_test_random(random) % 16 == 0 ? C_TOKENIZER_TRIGRAPHS : 0;
        char name[64];
        snprintf(name, sizeof(name), "random input %lu", (unsigned long)i);
        const bool passed = refine_test_compare(state, buffer + misalignment, length, flags, failures < BUSTER_REFINE_TEST_REPORTED ? name : 0);
        failures += !passed;
    }
    printf("random: %lu of %lu inputs differ (%u ids in use)\n", (unsigned long)failures, (unsigned long)count, state->oracle.count);
    return failures == 0;
}

// The input being refined, for refine_test_report_fault.
static char refine_page_edge_input[128];
static int refine_page_edge_input_length;

static void refine_test_report_fault(int signal_number)
{
    static const char prefix[] = "page edges: ";
    static const char suffix[] = " faults: refinement reads outside the source\n";
    ssize_t written = write(STDOUT_FILENO, prefix, sizeof(prefix) - 1);
    written += write(STDOUT_FILENO, refine_page_edge_input, (size_t)refine_page_edge_input_length);
    written += write(STDOUT_FILENO, suffix, sizeof(suffix) - 1);
    (void)written;
    (void)signal_number;
    _exit(1);
}

static bool refine_check_page_edges(RefineTestState* state, uint64_t* random)
{
    static uint8_t template[BUSTER_REFINE_TEST_PAGE_EDGE_LENGTHS + BUSTER_REFINE_TEST_INPUT_BYTES];
    const uint64_t page_bytes = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, 3 * page_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* page = mapping + page_bytes;
    struct sigaction action = { .sa_handler = refine_test_report_fault };
    uint64_t failures = 0;
    uint64_t checked = 0;
    fflush(stdout);
    bool result = mapping != MAP_FAILED && page_bytes >= BUSTER_REFINE_TEST_PAGE_EDGE_LENGTHS && mprotect(page, page_bytes, PROT_READ | PROT_WRITE) == 0 &&
                  sigaction(SIGSEGV, &action, 0) == 0;
    if (!result)
    {
        printf("page edges: can't set up the guard pages\n");
    }
    for (uint64_t index = 0; result && index < 4; index += 1)
    {
        uint64_t filled = 0;
        while (filled < BUSTER_REFINE_TEST_PAGE_EDGE_LENGTHS)
        {
            filled += refine_test_input(random, template + filled, BUSTER_REFINE_TEST_INPUT_BYTES);
        }
        for (uint64_t length = 0; length < BUSTER_REFINE_TEST_PAGE_EDGE_LENGTHS; length += 1)
        {
            for (uint64_t variant = 0; variant < 4; variant += 1)
            {
                const bool suffix = (variant & 1) != 0;
                const bool at_start = (variant & 2) != 0;
                const uint8_t* content = suffix ? template + BUSTER_REFINE_TEST_PAGE_EDGE_LENGTHS - length : template;
                const uint64_t offset = at_start ? 0 : page_bytes - length;
                memset(page, 0, page_bytes);
                memcpy(page + offset, content, length);
                refine_page_edge_input_length = snprintf(refine_page_edge_input, sizeof(refine_page_edge_input), "%s of %lu bytes of template %lu %s",
                                                         suffix ? "suffix" : "prefix", (unsigned long)length, (unsigned long)index,
                                                         at_start ? "at a page start" : "at a page end");
                const bool passed = refine_test_compare(state, page + offset, length, 0, failures < BUSTER_REFINE_TEST_REPORTED ? refine_page_edge_input : 0);
                failures += !passed;
                checked += 1;
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        action.sa_handler = SIG_DFL;
        sigaction(SIGSEGV, &action, 0);
        munmap(mapping, 3 * page_bytes);
    }
    printf("page edges: %lu of %lu inputs differ\n", (unsigned long)failures, (unsigned long)checked);
    return result && failures == 0;
}

// Refines one file like a random input. Returns false if it differs or can't be read.
static bool refine_check_file(RefineTestState* state, const char* path, uint64_t* files, uint64_t* bytes)
{
    bool result = false;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat file_stats;
    if (fd >= 0 && fstat(fd, &file_stats) == 0 && (uint64_t)file_stats.st_size <= BUSTER_REFINE_TEST_MAX_FILE_BYTES)
    {
        const uint64_t length = (uint64_t)file_stats.st_size;
        void* mapped = length != 0 ? mmap(0, length, PROT_READ, MAP_PRIVATE, fd, 0) : 0;
        if (length == 0 || mapped != MAP_FAILED)
        {
            result = refine_test_compare(state, mapped, length, 0, path);
            *files += 1;
            *bytes += length;
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
        printf("files: %s differs or can't be read\n", path);
    }
    return result;
}

// Refines a file, or the .c and .h files under a directory (a stack of open directories, no recursion).
static bool refine_check_path(RefineTestState* state, const char* path, uint64_t* files, uint64_t* bytes)
{
    struct stat path_stats;
    bool result = stat(path, &path_stats) == 0;
    if (result && !S_ISDIR(path_stats.st_mode))
    {
        result = refine_check_file(state, path, files, bytes);
    }
    else if (result)
    {
        DIR* directories[BUSTER_REFINE_TEST_MAX_DEPTH];
        static char paths[BUSTER_REFINE_TEST_MAX_DEPTH][BUSTER_REFINE_TEST_MAX_PATH];
        uint32_t depth = 1;
        directories[0] = opendir(path);
        snprintf(paths[0], sizeof(paths[0]), "%s", path);
        result = directories[0] != 0;
        depth = result ? 1 : 0;
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
                char child[BUSTER_REFINE_TEST_MAX_PATH];
                snprintf(child, sizeof(child), "%s/%s", paths[depth - 1], entry->d_name);
                struct stat child_stats;
                if (lstat(child, &child_stats) == 0 && S_ISDIR(child_stats.st_mode) && depth < BUSTER_REFINE_TEST_MAX_DEPTH)
                {
                    directories[depth] = opendir(child);
                    if (directories[depth] != 0)
                    {
                        snprintf(paths[depth], sizeof(paths[depth]), "%s", child);
                        depth += 1;
                    }
                }
                else if (S_ISREG(child_stats.st_mode))
                {
                    const uint64_t name_length = strlen(entry->d_name);
                    const bool source = name_length > 2 && entry->d_name[name_length - 2] == '.' &&
                                        (entry->d_name[name_length - 1] == 'c' || entry->d_name[name_length - 1] == 'h');
                    if (source)
                    {
                        result &= refine_check_file(state, child, files, bytes);
                    }
                }
            }
        }
    }
    else
    {
        printf("files: can't open %s\n", path);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// run_refinement_tests

static bool refine_test_initialize_variants(RefineTestState* state)
{
    bool result = true;
    for (uint32_t variant = 0; result && variant < REFINE_VARIANT_COUNT; variant += 1)
    {
        result = intern_initialize(&state->variant_tables[variant]);
    }
    return result;
}

static bool refine_test_parse_u64(const char* text, uint64_t* value)
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

static int run_refinement_tests(int argument_count, char** arguments)
{
    static RefineTestState state;
    uint64_t count = BUSTER_REFINE_TEST_DEFAULT_INPUTS;
    uint64_t random = BUSTER_REFINE_TEST_GOLDEN_RATIO;
    bool valid = true;
    int first_path = argument_count;
    for (int i = 0; valid && i < argument_count && first_path == argument_count; i += 1)
    {
        if (strcmp(arguments[i], "--count") == 0 && i + 1 < argument_count)
        {
            i += 1;
            valid = refine_test_parse_u64(arguments[i], &count);
        }
        else if (strcmp(arguments[i], "--seed") == 0 && i + 1 < argument_count)
        {
            uint64_t seed;
            i += 1;
            valid = refine_test_parse_u64(arguments[i], &seed);
            random = seed * BUSTER_REFINE_TEST_GOLDEN_RATIO + 1;
        }
        else if (arguments[i][0] == '-')
        {
            valid = false;
        }
        else
        {
            first_path = i;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-refinement [--count N] [--seed N] [path...]\n");
    }
    else if (!intern_initialize(&state.scalar_table) || !refine_oracle_initialize(&state.oracle))
    {
        fprintf(stderr, "test-refinement: can't reserve memory\n");
    }
    else if (!refine_test_initialize_variants(&state))
    {
        fprintf(stderr, "test-refinement: can't reserve memory\n");
    }
    else
    {
        bool passed = refine_check_hash(&random);
        passed &= refine_check_long_hashes(&random);
        passed &= refine_check_aligned_flips(&random);
        passed &= refine_check_names(&state.scalar_table);
        passed &= refine_check_table(&random);
        passed &= refine_check_fixtures(&state);
        passed &= refine_check_spellings(&state, random % 64);
        passed &= refine_check_random(&state, &random, count);
        passed &= refine_check_page_edges(&state, &random);
        uint64_t files = 0;
        uint64_t bytes = 0;
        for (int i = first_path; i < argument_count; i += 1)
        {
            passed &= refine_check_path(&state, arguments[i], &files, &bytes);
        }
        if (first_path < argument_count)
        {
            printf("files: %lu files, %lu bytes (%u ids in use)\n", (unsigned long)files, (unsigned long)bytes, state.oracle.count);
        }
        result = passed ? 0 : 1;
    }
    return result;
}
