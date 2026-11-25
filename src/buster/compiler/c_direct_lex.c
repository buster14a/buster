// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Fused AVX-512 streaming lexer and C17 preprocessor for the direct-to-ELF path (#2850).
//
// Design:
//   - Zero whole-file raw-token or refined-token arrays: active source bytes are lexed and interned
//     directly into compact 8-byte FastTok records (or streamed per declaration/body).
//   - Comments (/* ... */, // ...) and inactive #if/#ifdef branches are skipped with 64-byte AVX-512
//     scans without lexing or interning dead tokens.
//   - Identifiers are interned via a 64-way AVX-512 SwissTable (vpcmpeqb over 64 control bytes per probe).
//   - All C17 / GNU keywords are pre-interned at fixed low IdentIds so keyword recognition is a single
//     integer compare, and macro activity is a 1-bit-per-IdentId L1-resident bitset.
//   - Include guards (#ifndef G ... #define G ... #endif) and #pragma once are cached by path so repeated
//     system header includes perform zero syscalls.
//   - Strictly iterative (no recursion) to satisfy buster's call-graph rule.
//   - On any syntax/preprocessing error, warning-producing directive (#warning, #error, diagnostic pragmas),
//     or unsupported exotic construct, sets bailout=true so the driver falls back cleanly to the full pipeline.

#ifndef BUSTER_C_DIRECT_LEX_INCLUDED
#define BUSTER_C_DIRECT_LEX_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <immintrin.h>

#if defined(BUSTER_HOST_STATIC) && BUSTER_HOST_STATIC
#define BUSTER_DIRECT_HOT __attribute__((section(".text.buster_checker")))
#else
#define BUSTER_DIRECT_HOT
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Token kinds (fits in uint8_t, 0..127 so AVX-512 vpermi2b 128-byte LUTs index directly by kind)

typedef enum FastTokKind
{
    FTOK_EOF = 0,
    FTOK_IDENT = 1,
    FTOK_INT_LIT = 2,       // integer literal (small value inline in data, or index into num_pool)
    FTOK_FLOAT_LIT = 3,     // floating-point literal (index into num_pool / str_pool)
    FTOK_CHAR_LIT = 4,      // character literal (code point in data)
    FTOK_STR_LIT = 5,       // string literal (index into lit_pool: offset + length)

    // Single & multi-char punctuators (6..63)
    FTOK_LPAREN = 6,        // (
    FTOK_RPAREN = 7,        // )
    FTOK_LBRACE = 8,        // {
    FTOK_RBRACE = 9,        // }
    FTOK_LBRACKET = 10,     // [
    FTOK_RBRACKET = 11,     // ]
    FTOK_SEMI = 12,         // ;
    FTOK_COMMA = 13,        // ,
    FTOK_DOT = 14,          // .
    FTOK_ARROW = 15,        // ->
    FTOK_ELLIPSIS = 16,     // ...
    FTOK_QUESTION = 17,     // ?
    FTOK_COLON = 18,        // :
    FTOK_TILDE = 19,        // ~
    FTOK_BANG = 20,         // !
    FTOK_INC = 21,          // ++
    FTOK_DEC = 22,          // --
    FTOK_PLUS = 23,         // +
    FTOK_MINUS = 24,        // -
    FTOK_STAR = 25,         // *
    FTOK_SLASH = 26,        // /
    FTOK_PERCENT = 27,      // %
    FTOK_AMP = 28,          // &
    FTOK_PIPE = 29,         // |
    FTOK_CARET = 30,        // ^
    FTOK_LSHIFT = 31,       // <<
    FTOK_RSHIFT = 32,       // >>
    FTOK_LT = 33,           // <
    FTOK_LE = 34,           // <=
    FTOK_GT = 35,           // >
    FTOK_GE = 36,           // >=
    FTOK_EQ = 37,           // ==
    FTOK_NE = 38,           // !=
    FTOK_LAND = 39,         // &&
    FTOK_LOR = 40,          // ||
    FTOK_ASSIGN = 41,       // =
    FTOK_ADD_ASSIGN = 42,   // +=
    FTOK_SUB_ASSIGN = 43,   // -=
    FTOK_MUL_ASSIGN = 44,   // *=
    FTOK_DIV_ASSIGN = 45,   // /=
    FTOK_MOD_ASSIGN = 46,   // %=
    FTOK_AND_ASSIGN = 47,   // &=
    FTOK_OR_ASSIGN = 48,    // |=
    FTOK_XOR_ASSIGN = 49,   // ^=
    FTOK_SHL_ASSIGN = 50,   // <<=
    FTOK_SHR_ASSIGN = 51,   // >>=
    FTOK_HASH = 52,         // #
    FTOK_HASHHASH = 53,     // ##

    // C17 / GNU keywords (64..125)
    FTOK_KW_AUTO = 64,
    FTOK_KW_BREAK = 65,
    FTOK_KW_CASE = 66,
    FTOK_KW_CHAR = 67,
    FTOK_KW_CONST = 68,
    FTOK_KW_CONTINUE = 69,
    FTOK_KW_DEFAULT = 70,
    FTOK_KW_DO = 71,
    FTOK_KW_DOUBLE = 72,
    FTOK_KW_ELSE = 73,
    FTOK_KW_ENUM = 74,
    FTOK_KW_EXTERN = 75,
    FTOK_KW_FLOAT = 76,
    FTOK_KW_FOR = 77,
    FTOK_KW_GOTO = 78,
    FTOK_KW_IF = 79,
    FTOK_KW_INLINE = 80,
    FTOK_KW_INT = 81,
    FTOK_KW_LONG = 82,
    FTOK_KW_REGISTER = 83,
    FTOK_KW_RESTRICT = 84,
    FTOK_KW_RETURN = 85,
    FTOK_KW_SHORT = 86,
    FTOK_KW_SIGNED = 87,
    FTOK_KW_SIZEOF = 88,
    FTOK_KW_STATIC = 89,
    FTOK_KW_STRUCT = 90,
    FTOK_KW_SWITCH = 91,
    FTOK_KW_TYPEDEF = 92,
    FTOK_KW_UNION = 93,
    FTOK_KW_UNSIGNED = 94,
    FTOK_KW_VOID = 95,
    FTOK_KW_VOLATILE = 96,
    FTOK_KW_WHILE = 97,
    FTOK_KW_ALIGNAS = 98,
    FTOK_KW_ALIGNOF = 99,
    FTOK_KW_ATOMIC = 100,
    FTOK_KW_BOOL = 101,
    FTOK_KW_COMPLEX = 102,
    FTOK_KW_GENERIC = 103,
    FTOK_KW_IMAGINARY = 104,
    FTOK_KW_NORETURN = 105,
    FTOK_KW_STATIC_ASSERT = 106,
    FTOK_KW_THREAD_LOCAL = 107,
    FTOK_KW_ATTRIBUTE = 108,
    FTOK_KW_ASM = 109,
    FTOK_KW_EXTENSION = 110,
    FTOK_KW_TYPEOF = 111,
    FTOK_KW_INT128 = 112,
    FTOK_KW_FLOAT128 = 113,
    FTOK_KW_FLOAT16 = 114,
    FTOK_KW_BUILTIN_VA_LIST = 115,
    FTOK_KW_BUILTIN_VA_START = 116,
    FTOK_KW_BUILTIN_VA_ARG = 117,
    FTOK_KW_BUILTIN_VA_END = 118,
    FTOK_KW_BUILTIN_VA_COPY = 119,
    FTOK_KW_BUILTIN_OFFSETOF = 120,
    FTOK_KW_FUNC = 121,      // __func__ / __FUNCTION__ / __PRETTY_FUNCTION__
    FTOK_INT_U32 = 122,      // inline u32 unsigned integer literal
    FTOK_INT_I64 = 123,      // num_pool[data] signed 64-bit integer literal
    FTOK_INT_U64 = 124,      // num_pool[data] unsigned 64-bit integer literal
    FTOK_FLOAT32 = 125,      // num_pool[data] float (32-bit IEEE bits)
    FTOK_FLOAT80 = 126,      // long double literal
    FTOK_PP_OTHER = 127,     // raw PP token
    FTOK_KIND_COUNT = 128
} FastTokKind;

// Token flags (uint8_t)
#define BUSTER_FTOK_BOL         0x01u   // first token on line
#define BUSTER_FTOK_SPACE       0x02u   // preceded by whitespace or comment
#define BUSTER_FTOK_UNSIGNED    0x04u   // integer literal has U suffix
#define BUSTER_FTOK_LONG        0x08u   // integer literal has L suffix (or float has f suffix)
#define BUSTER_FTOK_LONGLONG    0x10u   // integer literal has LL suffix (or float has L suffix)
#define BUSTER_FTOK_HEX         0x20u   // hex/octal integer literal (unsigned promotion allowed)
#define BUSTER_FTOK_IN_POOL     0x40u   // INT_LIT payload is index into num_pool (not inline u32)
#define BUSTER_FTOK_BLUE        0x80u   // macro-expansion blue-painted (cannot expand again)

// Compact 8-byte token
typedef struct FastTok FastTok;
struct FastTok
{
    uint8_t kind;
    uint8_t flags;
    uint16_t extra;   // string/literal flags or wide/prefix kind or param index in macro body
    uint32_t data;    // ident_id (IDENT/KW), inline u32 or num_pool index (INT/FLOAT), lit_pool index (STR)
};

// A token made in a register and given back whole. A token written a field at a time and then loaded whole waits for
// its stores to leave the store buffer: Zen 5 forwards a store to a load only when the load lies inside that one store
// (#2850's profile on the 9700X: 956 K failed forwards a compile of sqlite3.c, a fifth of them fast_lex_one's token).
static inline FastTok fast_tok_make(uint32_t kind, uint32_t flags, uint32_t extra, uint32_t data)
{
    const uint64_t bits = (uint64_t)(kind & 0xffu) | ((uint64_t)(flags & 0xffu) << 8) | ((uint64_t)(extra & 0xffffu) << 16) | ((uint64_t)data << 32);
    FastTok tok;
    memcpy(&tok, &bits, sizeof(tok));
    return tok;
}

// A lexed literal and where its text ends, in two registers.
typedef struct FastLexed FastLexed;
struct FastLexed
{
    const uint8_t* p;
    FastTok tok;
};

// Literal pool slice (for string literals and float/large-int source text)
typedef struct FastLitSlice FastLitSlice;
struct FastLitSlice
{
    uint32_t offset;
    uint32_t length;
};

// ---------------------------------------------------------------------------------------------------------------------
// Lightweight bump allocator for c_direct (uses mmap with MADV_HUGEPAGE; zero libc malloc overhead)

typedef struct FastArena FastArena;
struct FastArena
{
    uint8_t* base;
    uint64_t used;
    uint64_t committed;
    uint64_t reserved;
};

static bool fast_arena_init(FastArena* a, uint64_t reserve_bytes, bool avoid_huge_pages)
{
    bool ok = false;
    a->base = NULL;
    a->used = 0;
    a->committed = 0;
    a->reserved = 0;
    // On a busy host (8 concurrent workers, #672/#1561), avoid Linux 2 MB THP buddy/compaction contention via
    // MAP_STACK + MADV_NOHUGEPAGE; on an idle single-core run, inherit system THP for low minor-fault counts (#2850).
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_STACK
    if (avoid_huge_pages)
    {
        flags |= MAP_STACK;
    }
#endif
    void* mem = mmap(NULL, reserve_bytes, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (mem != MAP_FAILED)
    {
        if (avoid_huge_pages)
        {
            (void)madvise(mem, reserve_bytes, MADV_NOHUGEPAGE);
        }
        a->base = (uint8_t*)mem;
        a->committed = reserve_bytes;
        a->reserved = reserve_bytes;
        ok = true;
    }
    return ok;
}

static void fast_arena_destroy(FastArena* a)
{
    if (a->base != NULL)
    {
        (void)munmap(a->base, a->reserved);
        a->base = NULL;
        a->used = 0;
        a->committed = 0;
        a->reserved = 0;
    }
}

static void* fast_arena_alloc(FastArena* a, uint64_t size, uint64_t align)
{
    void* result = NULL;
    const uint64_t mask = align - 1u;
    const uint64_t aligned = (a->used + mask) & ~mask;
    if (aligned + size <= a->reserved)
    {
        result = a->base + aligned;
        a->used = aligned + size;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// 64-Way AVX-512 SwissTable Identifier Interner

#define BUSTER_FIDENT_INIT_GROUPS 512u   // 32,768 slots -> 32 KB ctrl + 128 KB ids = 160 KB (fits in L2!)
#define BUSTER_FIDENT_MAX_IDS     49152u
#define BUSTER_FIDENT_CACHE_BITS  14u    // 16384 entries * 24 B = 384 KB (fits Zen 5 1 MB L2!)
// A small unit's tables (#3592): the hash table and the cache are written at hashed places, so at their full sizes the
// 400 predefined names alone touch nearly every page of both, 135 page faults of a one-function unit's 359 on the 9700X.
// They start at 64 groups (20 KB) and 2,048 slots (48 KB) and grow with the names: the table as it always did
// (fast_ident_grow), the cache by fast_ident_cache_grow once the names are four times its slots. A page of the cache
// costs a fault, about 2,700 cycles there, and a name that misses it a probe of the table: a unit of six standard
// headers, whose 4,000 names are each looked up a few times, took 4.14 M user and 3.27 M kernel cycles with the cache
// grown at as many names as slots and 4.04 M and 3.03 M at four times. From 16 groups the table grew three times
// under such a unit's names, 2% of musl's units' cycles: the same unit took 3.89 M and 3.03 M from 16 groups and
// 3.79 M and 2.96 M from 64.
#define BUSTER_FIDENT_SMALL_GROUPS     64u
#define BUSTER_FIDENT_SMALL_CACHE_BITS 11u
#define BUSTER_FIDENT_CACHE_GROW_BITS  2u
#define BUSTER_FIDENT_CACHE_GROW_LOAD  4u

typedef struct FastIdentCacheSlot FastIdentCacheSlot;
struct FastIdentCacheSlot
{
    uint64_t w0;
    uint64_t w1;
    uint32_t id;
    uint8_t len;
    uint8_t tok_kind;   // FTOK_IDENT or FTOK_KW_*
    uint16_t pad;
};

static const uint64_t fast_len_mask16[17][2] = {
    { UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x00000000000000ff), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x000000000000ffff), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x0000000000ffffff), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x00000000ffffffff), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x000000ffffffffff), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x0000ffffffffffff), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x00ffffffffffffff), UINT64_C(0x0000000000000000) },
    { UINT64_C(0xffffffffffffffff), UINT64_C(0x0000000000000000) },
    { UINT64_C(0xffffffffffffffff), UINT64_C(0x00000000000000ff) },
    { UINT64_C(0xffffffffffffffff), UINT64_C(0x000000000000ffff) },
    { UINT64_C(0xffffffffffffffff), UINT64_C(0x0000000000ffffff) },
    { UINT64_C(0xffffffffffffffff), UINT64_C(0x00000000ffffffff) },
    { UINT64_C(0xffffffffffffffff), UINT64_C(0x000000ffffffffff) },
    { UINT64_C(0xffffffffffffffff), UINT64_C(0x0000ffffffffffff) },
    { UINT64_C(0xffffffffffffffff), UINT64_C(0x00ffffffffffffff) },
    { UINT64_C(0xffffffffffffffff), UINT64_C(0xffffffffffffffff) },
};

typedef struct FastIdentEntry FastIdentEntry;
struct FastIdentEntry
{
    uint32_t str_offset;
    uint16_t length;
    uint8_t kw_kind;      // 0 if ordinary identifier, else FastTokKind (64..125)
    uint8_t pp_kw;        // preprocessor directive / special keyword code
    uint32_t macro_idx;   // 0 if not a macro, else 1-based index into macro table
    uint32_t hash;        // cached 32-bit hash
};

typedef enum FastPPKw
{
    FPP_KW_NONE = 0,
    FPP_KW_IF,
    FPP_KW_IFDEF,
    FPP_KW_IFNDEF,
    FPP_KW_ELIF,
    FPP_KW_ELSE,
    FPP_KW_ENDIF,
    FPP_KW_DEFINE,
    FPP_KW_UNDEF,
    FPP_KW_INCLUDE,
    FPP_KW_INCLUDE_NEXT,
    FPP_KW_LINE,
    FPP_KW_PRAGMA,
    FPP_KW_ERROR,
    FPP_KW_WARNING,
    FPP_KW_EMBED,
    FPP_KW_DEFINED,
    FPP_KW_VA_ARGS,
    FPP_KW_VA_OPT,
    FPP_KW_LINE_MACRO,
    FPP_KW_FILE_MACRO,
    FPP_KW_COUNTER_MACRO,
    FPP_KW_DATE_MACRO,
    FPP_KW_TIME_MACRO,
    FPP_KW_BASE_FILE_MACRO,
    FPP_KW_INCLUDE_LEVEL_MACRO,
    FPP_KW_PRAGMA_OP,
    FPP_KW_HAS_BUILTIN,
    FPP_KW_HAS_ATTRIBUTE,
    FPP_KW_HAS_FEATURE,
    FPP_KW_HAS_EXTENSION,
    FPP_KW_HAS_INCLUDE,
    FPP_KW_HAS_INCLUDE_NEXT,
    FPP_KW_HAS_C_ATTRIBUTE,
    FPP_KW_BUILDING_MODULE,
    FPP_KW_HAS_WARNING,
    FPP_KW_IS_IDENTIFIER,
    FPP_KW_ONCE,
    FPP_KW_ELIFDEF,
    FPP_KW_ELIFNDEF
} FastPPKw;

typedef struct FastIdentTable FastIdentTable;
struct FastIdentTable
{
    uint8_t* ctrl;             // 64-byte aligned control bytes (group_count * 64)
    uint32_t* slots;           // slot -> 1-based IdentId (group_count * 64)
    uint32_t group_count;      // power of 2
    uint32_t group_mask;
    uint32_t used_slots;
    uint32_t grow_threshold;

    FastIdentEntry* entries;   // 1-based [1 .. count]
    uint32_t count;
    uint32_t capacity;

    uint64_t* macro_bits;      // 1 bit per IdentId (L1-resident fast check!)

    char* strings;             // contiguous NUL-terminated identifier text buffer
    uint64_t str_used;
    uint64_t str_cap;

    FastIdentCacheSlot* cache; // direct-mapped cache of 1 << cache_bits slots, indexed by a key's hash >> cache_shift
    uint32_t cache_bits;
    uint32_t cache_shift;      // 64 - cache_bits
    uint32_t cache_grow_at;    // the count of names at which the cache grows (UINT32_MAX at its full size)

    FastArena* arena;
};

static inline uint64_t fast_hash_ident(const uint8_t* p, uint32_t len)
{
    uint64_t h = (uint64_t)len * UINT64_C(0x9e3779b97f4a7c15);
    if (len >= 8u)
    {
        uint64_t w0 = 0;
        uint64_t w1 = 0;
        memcpy(&w0, p, 8);
        memcpy(&w1, p + len - 8u, 8);
        h ^= w0 * UINT64_C(0xbf58476d1ce4e5b9);
        h = (h << 31) | (h >> 33);
        h ^= w1 * UINT64_C(0x94d049bb133111eb);
        if (__builtin_expect(len > 16u, 0))
        {
            for (uint32_t i = 8u; i + 8u < len; i += 8u)
            {
                uint64_t wi = 0;
                memcpy(&wi, p + i, 8);
                h ^= wi * UINT64_C(0x517cc1b727220a95);
                h = (h << 27) | (h >> 37);
            }
        }
    }
    else if (len >= 4u)
    {
        uint32_t a = 0;
        uint32_t b = 0;
        memcpy(&a, p, 4);
        memcpy(&b, p + len - 4u, 4);
        h ^= ((uint64_t)a | ((uint64_t)b << 32)) * UINT64_C(0xbf58476d1ce4e5b9);
    }
    else if (len != 0u)
    {
        uint64_t w = (uint64_t)p[0] | ((uint64_t)p[len >> 1] << 8) | ((uint64_t)p[len - 1u] << 16);
        h ^= w * UINT64_C(0xbf58476d1ce4e5b9);
    }
    h ^= h >> 30;
    h *= UINT64_C(0xbf58476d1ce4e5b9);
    h ^= h >> 27;
    return h;
}

static inline bool fast_ident_eq(const uint8_t* a, const uint8_t* b, uint32_t len)
{
    bool eq = true;
    if (len <= 8u)
    {
        for (uint32_t i = 0; i < len; i += 1)
        {
            if (a[i] != b[i])
            {
                eq = false;
                break;
            }
        }
    }
    else
    {
        eq = memcmp(a, b, len) == 0;
    }
    return eq;
}

static __attribute__((noinline)) bool fast_ident_grow(FastIdentTable* t)
{
    const uint32_t new_groups = t->group_count * 2u;
    const uint64_t new_slots_count = (uint64_t)new_groups * 64u;
    uint8_t* new_ctrl = (uint8_t*)fast_arena_alloc(t->arena, new_slots_count, 64);
    uint32_t* new_slots = (uint32_t*)fast_arena_alloc(t->arena, new_slots_count * sizeof(uint32_t), 64);
    bool ok = new_ctrl != NULL && new_slots != NULL;
    if (ok)
    {
        memset(new_ctrl, 0, new_slots_count);
        const uint32_t new_mask = new_groups - 1u;
        const __m512i v_zero = _mm512_setzero_si512();
        for (uint32_t id = 1u; id <= t->count; id += 1)
        {
            const uint32_t h32 = t->entries[id].hash;
            const uint8_t h2 = (uint8_t)((h32 >> 25) | 0x80u);
            uint32_t g = h32 & new_mask;
            for (;;)
            {
                const __m512i c = _mm512_load_si512((const void*)(new_ctrl + ((uint64_t)g << 6)));
                const __mmask64 empty = _mm512_cmpeq_epi8_mask(c, v_zero);
                if (empty != 0)
                {
                    const uint32_t idx = (uint32_t)__builtin_ctzll(empty);
                    const uint64_t slot = ((uint64_t)g << 6) | idx;
                    new_ctrl[slot] = h2;
                    new_slots[slot] = id;
                    break;
                }
                g = (g + 1u) & new_mask;
            }
        }
        t->ctrl = new_ctrl;
        t->slots = new_slots;
        t->group_count = new_groups;
        t->group_mask = new_mask;
        t->grow_threshold = (uint32_t)((new_slots_count * 3u) / 4u);
    }
    return ok;
}

static BUSTER_DIRECT_HOT uint32_t fast_ident_intern(FastIdentTable* t, const uint8_t* text, uint32_t len)
{
    uint32_t result = 0;
    if (__builtin_expect(len <= 65535u, 1))
    {
        if (__builtin_expect(t->used_slots >= t->grow_threshold, 0))
        {
            if (!fast_ident_grow(t))
            {
                return 0;
            }
        }
        const uint64_t h64 = fast_hash_ident(text, len);
        const uint32_t h32 = (uint32_t)(h64 ^ (h64 >> 32));
        const uint8_t h2 = (uint8_t)((h32 >> 25) | 0x80u);
        const __m512i v_h2 = _mm512_set1_epi8((char)h2);
        const __m512i v_zero = _mm512_setzero_si512();
        const uint8_t* const ctrl = t->ctrl;
        const uint32_t* const slots = t->slots;
        const FastIdentEntry* const entries = t->entries;
        const char* const strings = t->strings;
        const uint32_t gmask = t->group_mask;
        uint32_t g = h32 & gmask;
        for (;;)
        {
            const uint64_t base = (uint64_t)g << 6;
            const __m512i c = _mm512_load_si512((const void*)(ctrl + base));
            __mmask64 match = _mm512_cmpeq_epi8_mask(c, v_h2);
            while (match != 0)
            {
                const uint32_t bit = (uint32_t)__builtin_ctzll(match);
                const uint32_t id = slots[base | bit];
                const FastIdentEntry* e = entries + id;
                if (e->length == len && fast_ident_eq((const uint8_t*)(strings + e->str_offset), text, len))
                {
                    result = id;
                    break;
                }
                match &= match - 1u;
            }
            if (result != 0)
            {
                break;
            }
            const __mmask64 empty = _mm512_cmpeq_epi8_mask(c, v_zero);
            if (empty != 0)
            {
                const uint32_t bit = (uint32_t)__builtin_ctzll(empty);
                const uint64_t slot = base | bit;
                if (t->count + 1u < t->capacity && t->str_used + len + 1u <= t->str_cap)
                {
                    const uint32_t id = ++t->count;
                    const uint32_t off = (uint32_t)t->str_used;
                    memcpy(t->strings + off, text, len);
                    t->strings[off + len] = '\0';
                    t->str_used += len + 1u;
                    t->entries[id] = (FastIdentEntry){
                        .str_offset = off,
                        .length = (uint16_t)len,
                        .kw_kind = 0,
                        .pp_kw = 0,
                        .macro_idx = 0,
                        .hash = h32,
                    };
                    t->ctrl[slot] = h2;
                    t->slots[slot] = id;
                    t->used_slots += 1u;
                    result = id;
                }
                break;
            }
            g = (g + 1u) & gmask;
        }
    }
    return result;
}

static uint32_t fast_ident_intern_cstr(FastIdentTable* t, const char* s)
{
    return fast_ident_intern(t, (const uint8_t*)s, (uint32_t)strlen(s));
}

// The cache's key hash: fast_ident_lookup_16's and fast_ident_lookup_long's, and fast_ident_cache_grow's for a slot's
// stored key. Its top cache_bits bits are the slot.
static inline uint64_t fast_ident_cache_hash(uint64_t w0, uint64_t w1, uint32_t len)
{
    return (w0 ^ (w1 * UINT64_C(0x9e3779b97f4a7c15)) ^ ((uint64_t)len << 56)) * UINT64_C(0xbf58476d1ce4e5b9);
}

static void fast_ident_cache_set(FastIdentTable* t, FastIdentCacheSlot* cache, uint32_t bits)
{
    t->cache = cache;
    t->cache_bits = bits;
    t->cache_shift = 64u - bits;
    t->cache_grow_at = bits < BUSTER_FIDENT_CACHE_BITS ? BUSTER_FIDENT_CACHE_GROW_LOAD << bits : UINT32_MAX;
}

// The cache four times as large, once the names are four times its slots: a new zero-filled block of the arena, and
// every slot's key at its place in it (a slot holds its whole key, so its hash is computed again; two slots of the
// old cache differ in the bits they shared, so they cannot meet in the new one). The old block stays in the arena.
// Returns whether the cache grew: a caller's slot pointer is then stale. Without room in the arena the cache stays.
static __attribute__((noinline)) bool fast_ident_cache_grow(FastIdentTable* t)
{
    const uint32_t old_bits = t->cache_bits;
    const uint32_t new_bits = old_bits + BUSTER_FIDENT_CACHE_GROW_BITS < BUSTER_FIDENT_CACHE_BITS ? old_bits + BUSTER_FIDENT_CACHE_GROW_BITS : BUSTER_FIDENT_CACHE_BITS;
    FastIdentCacheSlot* const old_cache = t->cache;
    FastIdentCacheSlot* const new_cache = new_bits > old_bits ? (FastIdentCacheSlot*)fast_arena_alloc(t->arena, ((uint64_t)1 << new_bits) * sizeof(FastIdentCacheSlot), 64) : NULL;
    const bool grown = new_cache != NULL;
    if (grown)
    {
        const uint32_t old_count = 1u << old_bits;
        const uint32_t new_shift = 64u - new_bits;
        for (uint32_t i = 0; i < old_count; i += 1)
        {
            if (old_cache[i].id != 0)
            {
                new_cache[fast_ident_cache_hash(old_cache[i].w0, old_cache[i].w1, old_cache[i].len) >> new_shift] = old_cache[i];
            }
        }
        fast_ident_cache_set(t, new_cache, new_bits);
    }
    else
    {
        t->cache_grow_at = UINT32_MAX;
    }
    return grown;
}

// `small`: the tables at a small unit's sizes (BUSTER_FIDENT_SMALL_GROUPS), growing with its names.
static bool fast_ident_init(FastIdentTable* t, FastArena* arena, bool small)
{
    memset(t, 0, sizeof(*t));
    t->arena = arena;
    t->group_count = small ? BUSTER_FIDENT_SMALL_GROUPS : BUSTER_FIDENT_INIT_GROUPS;
    t->group_mask = t->group_count - 1u;
    const uint64_t slot_count = (uint64_t)t->group_count * 64u;
    t->grow_threshold = (uint32_t)((slot_count * 3u) / 4u);
    t->capacity = BUSTER_FIDENT_MAX_IDS;
    t->str_cap = UINT64_C(1) * 1024u * 1024u;

    t->ctrl = (uint8_t*)fast_arena_alloc(arena, slot_count, 64);
    t->slots = (uint32_t*)fast_arena_alloc(arena, slot_count * sizeof(uint32_t), 64);
    t->entries = (FastIdentEntry*)fast_arena_alloc(arena, (uint64_t)t->capacity * sizeof(FastIdentEntry), 64);
    t->macro_bits = (uint64_t*)fast_arena_alloc(arena, ((uint64_t)t->capacity / 64u + 1u) * sizeof(uint64_t), 64);
    t->strings = (char*)fast_arena_alloc(arena, t->str_cap, 64);
    const uint32_t cache_bits = small ? BUSTER_FIDENT_SMALL_CACHE_BITS : BUSTER_FIDENT_CACHE_BITS;
    fast_ident_cache_set(t, (FastIdentCacheSlot*)fast_arena_alloc(arena, ((uint64_t)1 << cache_bits) * sizeof(FastIdentCacheSlot), 64), cache_bits);

    bool ok = t->ctrl != NULL && t->slots != NULL && t->entries != NULL && t->macro_bits != NULL && t->strings != NULL && t->cache != NULL;
    if (ok)
    {
        // Pre-seed C17 and GNU keywords
        typedef struct KwSeed KwSeed;
        struct KwSeed
        {
            const char* name;
            uint8_t kw;
            uint8_t pp;
        };
        static const KwSeed seeds[] = {
            { "auto", FTOK_KW_AUTO, 0 },
            { "break", FTOK_KW_BREAK, 0 },
            { "case", FTOK_KW_CASE, 0 },
            { "char", FTOK_KW_CHAR, 0 },
            { "const", FTOK_KW_CONST, 0 },
            { "__const", FTOK_KW_CONST, 0 },
            { "__const__", FTOK_KW_CONST, 0 },
            { "continue", FTOK_KW_CONTINUE, 0 },
            { "default", FTOK_KW_DEFAULT, 0 },
            { "do", FTOK_KW_DO, 0 },
            { "double", FTOK_KW_DOUBLE, 0 },
            { "else", FTOK_KW_ELSE, FPP_KW_ELSE },
            { "enum", FTOK_KW_ENUM, 0 },
            { "extern", FTOK_KW_EXTERN, 0 },
            { "float", FTOK_KW_FLOAT, 0 },
            { "for", FTOK_KW_FOR, 0 },
            { "goto", FTOK_KW_GOTO, 0 },
            { "if", FTOK_KW_IF, FPP_KW_IF },
            { "inline", FTOK_KW_INLINE, 0 },
            { "__inline", FTOK_KW_INLINE, 0 },
            { "__inline__", FTOK_KW_INLINE, 0 },
            { "int", FTOK_KW_INT, 0 },
            { "long", FTOK_KW_LONG, 0 },
            { "register", FTOK_KW_REGISTER, 0 },
            { "restrict", FTOK_KW_RESTRICT, 0 },
            { "__restrict", FTOK_KW_RESTRICT, 0 },
            { "__restrict__", FTOK_KW_RESTRICT, 0 },
            { "return", FTOK_KW_RETURN, 0 },
            { "short", FTOK_KW_SHORT, 0 },
            { "signed", FTOK_KW_SIGNED, 0 },
            { "__signed", FTOK_KW_SIGNED, 0 },
            { "__signed__", FTOK_KW_SIGNED, 0 },
            { "sizeof", FTOK_KW_SIZEOF, 0 },
            { "static", FTOK_KW_STATIC, 0 },
            { "struct", FTOK_KW_STRUCT, 0 },
            { "switch", FTOK_KW_SWITCH, 0 },
            { "typedef", FTOK_KW_TYPEDEF, 0 },
            { "union", FTOK_KW_UNION, 0 },
            { "unsigned", FTOK_KW_UNSIGNED, 0 },
            { "void", FTOK_KW_VOID, 0 },
            { "volatile", FTOK_KW_VOLATILE, 0 },
            { "__volatile", FTOK_KW_VOLATILE, 0 },
            { "__volatile__", FTOK_KW_VOLATILE, 0 },
            { "while", FTOK_KW_WHILE, 0 },
            { "_Alignas", FTOK_KW_ALIGNAS, 0 },
            { "_Alignof", FTOK_KW_ALIGNOF, 0 },
            { "__alignof", FTOK_KW_ALIGNOF, 0 },
            { "__alignof__", FTOK_KW_ALIGNOF, 0 },
            { "_Atomic", FTOK_KW_ATOMIC, 0 },
            { "_Bool", FTOK_KW_BOOL, 0 },
            { "_Complex", FTOK_KW_COMPLEX, 0 },
            { "__complex__", FTOK_KW_COMPLEX, 0 },
            { "_Generic", FTOK_KW_GENERIC, 0 },
            { "_Imaginary", FTOK_KW_IMAGINARY, 0 },
            { "_Noreturn", FTOK_KW_NORETURN, 0 },
            { "_Static_assert", FTOK_KW_STATIC_ASSERT, 0 },
            { "_Thread_local", FTOK_KW_THREAD_LOCAL, 0 },
            { "__thread", FTOK_KW_THREAD_LOCAL, 0 },
            { "__attribute__", FTOK_KW_ATTRIBUTE, 0 },
            { "__attribute", FTOK_KW_ATTRIBUTE, 0 },
            { "asm", FTOK_KW_ASM, 0 },
            { "__asm", FTOK_KW_ASM, 0 },
            { "__asm__", FTOK_KW_ASM, 0 },
            { "__extension__", FTOK_KW_EXTENSION, 0 },
            { "typeof", FTOK_KW_TYPEOF, 0 },
            { "__typeof", FTOK_KW_TYPEOF, 0 },
            { "__typeof__", FTOK_KW_TYPEOF, 0 },
            { "__int128", FTOK_KW_INT128, 0 },
            { "__int128_t", FTOK_KW_INT128, 0 },
            { "__uint128_t", FTOK_KW_INT128, 0 },
            { "_Float128", FTOK_KW_FLOAT128, 0 },
            { "__float128", FTOK_KW_FLOAT128, 0 },
            { "_Float16", FTOK_KW_FLOAT16, 0 },
            { "_Float32", FTOK_KW_FLOAT, 0 },
            { "_Float64", FTOK_KW_DOUBLE, 0 },
            { "_Float32x", FTOK_KW_DOUBLE, 0 },
            { "_Float64x", FTOK_KW_FLOAT128, 0 },
            { "__builtin_va_list", FTOK_KW_BUILTIN_VA_LIST, 0 },
            { "__builtin_va_start", FTOK_KW_BUILTIN_VA_START, 0 },
            { "__builtin_va_arg", FTOK_KW_BUILTIN_VA_ARG, 0 },
            { "__builtin_va_end", FTOK_KW_BUILTIN_VA_END, 0 },
            { "__builtin_va_copy", FTOK_KW_BUILTIN_VA_COPY, 0 },
            { "__builtin_offsetof", FTOK_KW_BUILTIN_OFFSETOF, 0 },
            { "__func__", FTOK_KW_FUNC, 0 },
            { "__FUNCTION__", FTOK_KW_FUNC, 0 },
            { "__PRETTY_FUNCTION__", FTOK_KW_FUNC, 0 },
            // Preprocessor directive & special identifiers
            { "ifdef", 0, FPP_KW_IFDEF },
            { "ifndef", 0, FPP_KW_IFNDEF },
            { "elif", 0, FPP_KW_ELIF },
            { "elifdef", 0, FPP_KW_ELIFDEF },
            { "elifndef", 0, FPP_KW_ELIFNDEF },
            { "endif", 0, FPP_KW_ENDIF },
            { "define", 0, FPP_KW_DEFINE },
            { "undef", 0, FPP_KW_UNDEF },
            { "include", 0, FPP_KW_INCLUDE },
            { "include_next", 0, FPP_KW_INCLUDE_NEXT },
            { "line", 0, FPP_KW_LINE },
            { "pragma", 0, FPP_KW_PRAGMA },
            { "error", 0, FPP_KW_ERROR },
            { "warning", 0, FPP_KW_WARNING },
            { "embed", 0, FPP_KW_EMBED },
            { "defined", 0, FPP_KW_DEFINED },
            { "__VA_ARGS__", 0, FPP_KW_VA_ARGS },
            { "__VA_OPT__", 0, FPP_KW_VA_OPT },
            { "__LINE__", 0, FPP_KW_LINE_MACRO },
            { "__FILE__", 0, FPP_KW_FILE_MACRO },
            { "__COUNTER__", 0, FPP_KW_COUNTER_MACRO },
            { "__DATE__", 0, FPP_KW_DATE_MACRO },
            { "__TIME__", 0, FPP_KW_TIME_MACRO },
            { "__BASE_FILE__", 0, FPP_KW_BASE_FILE_MACRO },
            { "__INCLUDE_LEVEL__", 0, FPP_KW_INCLUDE_LEVEL_MACRO },
            { "_Pragma", 0, FPP_KW_PRAGMA_OP },
            { "__has_builtin", 0, FPP_KW_HAS_BUILTIN },
            { "__has_attribute", 0, FPP_KW_HAS_ATTRIBUTE },
            { "__has_feature", 0, FPP_KW_HAS_FEATURE },
            { "__has_extension", 0, FPP_KW_HAS_EXTENSION },
            { "__has_include", 0, FPP_KW_HAS_INCLUDE },
            { "__has_include_next", 0, FPP_KW_HAS_INCLUDE_NEXT },
            { "__has_c_attribute", 0, FPP_KW_HAS_C_ATTRIBUTE },
            { "__building_module", 0, FPP_KW_BUILDING_MODULE },
            { "__has_warning", 0, FPP_KW_HAS_WARNING },
            { "__is_identifier", 0, FPP_KW_IS_IDENTIFIER },
            { "once", 0, FPP_KW_ONCE },
        };
        for (uint32_t i = 0; i < (uint32_t)(sizeof(seeds) / sizeof(seeds[0])); i += 1)
        {
            const uint32_t id = fast_ident_intern_cstr(t, seeds[i].name);
            if (id == 0)
            {
                ok = false;
                break;
            }
            t->entries[id].kw_kind = seeds[i].kw;
            t->entries[id].pp_kw = seeds[i].pp;
            if (seeds[i].pp >= FPP_KW_LINE_MACRO && seeds[i].pp <= FPP_KW_PRAGMA_OP)
            {
                t->macro_bits[id >> 6] |= (UINT64_C(1) << (id & 63u));
            }
        }
        // t->cache comes from zero-filled anonymous mmap and fast_ident_intern_cstr does not populate it.
    }
    return ok;
}

// Branchless inline lookup for identifiers of length 1..16 where 16 bytes are readable at `p`.
static inline uint32_t fast_ident_lookup_16(FastIdentTable* t, const uint8_t* p, uint32_t len, uint8_t* out_kind)
{
    uint64_t w0 = 0;
    uint64_t w1 = 0;
    memcpy(&w0, p, 8);
    memcpy(&w1, p + 8, 8);
    w0 &= fast_len_mask16[len][0];
    w1 &= fast_len_mask16[len][1];
    const uint64_t h = fast_ident_cache_hash(w0, w1, len);
    FastIdentCacheSlot* slot = t->cache + (h >> t->cache_shift);
    uint32_t id;
    if (__builtin_expect(((slot->w0 ^ w0) | (slot->w1 ^ w1) | ((uint64_t)slot->len ^ len)) == 0 && slot->id != 0, 1))
    {
        id = slot->id;
        *out_kind = slot->tok_kind;
    }
    else
    {
        id = fast_ident_intern(t, p, len);
        uint8_t kind = FTOK_IDENT;
        if (id != 0)
        {
            const uint8_t kw = t->entries[id].kw_kind;
            kind = kw != 0 ? kw : FTOK_IDENT;
            if (__builtin_expect(t->count >= t->cache_grow_at, 0) && fast_ident_cache_grow(t))
            {
                slot = t->cache + (h >> t->cache_shift);
            }
            slot->w0 = w0;
            slot->w1 = w1;
            slot->id = id;
            slot->len = (uint8_t)len;
            slot->tok_kind = kind;
        }
        *out_kind = kind;
    }
    return id;
}

// An identifier of more than 16 bytes, through the same cache: its key is its first and its last eight bytes and its
// length, and where a slot has them the interned text is compared (the bytes between may differ). Without it every one
// went to fast_ident_intern's table, four dependent loads: 21,500 of sqlite3.c's lookups, a quarter of them new names.
static inline uint32_t fast_ident_lookup_long(FastIdentTable* t, const uint8_t* p, uint32_t len, uint8_t* out_kind)
{
    uint32_t id = 0;
    uint8_t kind = FTOK_IDENT;
    FastIdentCacheSlot* slot = NULL;
    uint64_t w0 = 0;
    uint64_t w1 = 0;
    uint64_t h = 0;
    if (len <= 64u)
    {
        memcpy(&w0, p, 8);
        memcpy(&w1, p + len - 8u, 8);
        h = fast_ident_cache_hash(w0, w1, len);
        slot = t->cache + (h >> t->cache_shift);
        if (((slot->w0 ^ w0) | (slot->w1 ^ w1) | ((uint64_t)slot->len ^ len)) == 0 && slot->id != 0)
        {
            // Masked loads: neither the text nor the interned copy has 64 bytes to read for certain.
            const __mmask64 lanes = _bzhi_u64(~UINT64_C(0), len);
            const __m512i text = _mm512_maskz_loadu_epi8(lanes, (const void*)p);
            const __m512i interned = _mm512_maskz_loadu_epi8(lanes, (const void*)(t->strings + t->entries[slot->id].str_offset));
            if (_mm512_cmpneq_epi8_mask(text, interned) == 0)
            {
                id = slot->id;
                kind = slot->tok_kind;
            }
        }
    }
    if (id == 0)
    {
        id = fast_ident_intern(t, p, len);
        if (id != 0)
        {
            const uint8_t kw = t->entries[id].kw_kind;
            kind = kw != 0 ? kw : FTOK_IDENT;
            if (slot != NULL)
            {
                if (__builtin_expect(t->count >= t->cache_grow_at, 0) && fast_ident_cache_grow(t))
                {
                    slot = t->cache + (h >> t->cache_shift);
                }
                slot->w0 = w0;
                slot->w1 = w1;
                slot->id = id;
                slot->len = (uint8_t)len;
                slot->tok_kind = kind;
            }
        }
    }
    *out_kind = kind;
    return id;
}

static inline bool fast_ident_is_macro(const FastIdentTable* t, uint32_t id)
{
    return (t->macro_bits[id >> 6] & (UINT64_C(1) << (id & 63u))) != 0;
}

static inline void fast_ident_set_macro_bit(FastIdentTable* t, uint32_t id, bool active)
{
    const uint64_t mask = UINT64_C(1) << (id & 63u);
    if (active)
    {
        t->macro_bits[id >> 6] |= mask;
    }
    else
    {
        t->macro_bits[id >> 6] &= ~mask;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// AVX-512 character classification & fast skippers (comments, whitespace, dead #if regions)

// Safe bounded 64-byte load: never reads past `end`
static inline __m512i fast_load64_bounded(const uint8_t* p, const uint8_t* end, uint32_t* out_avail)
{
    const uint64_t rem = (uint64_t)(end - p);
    __m512i v;
    if (__builtin_expect(rem >= 64u, 1))
    {
        *out_avail = 64u;
        v = _mm512_loadu_si512((const void*)p);
    }
    else
    {
        *out_avail = (uint32_t)rem;
        const __mmask64 m = rem == 0 ? 0u : (UINT64_C(1) << rem) - 1u;
        v = _mm512_maskz_loadu_epi8(m, (const void*)p);
    }
    return v;
}

// Skip /* ... */ starting IMMEDIATELY AFTER the opening "/*".
// Updates *line_count with number of '\n' crossed. Returns pointer immediately after "*/", or NULL if unterminated.
static const uint8_t* fast_skip_block_comment(const uint8_t* p, const uint8_t* end, uint32_t* line_count)
{
    const __m512i v_star = _mm512_set1_epi8('*');
    const __m512i v_slash = _mm512_set1_epi8('/');
    const __m512i v_nl = _mm512_set1_epi8('\n');
    uint64_t prev_star = 0;
    uint32_t lines = *line_count;
    const uint8_t* result = NULL;

    while (p < end)
    {
        uint32_t avail = 0;
        const __m512i v = fast_load64_bounded(p, end, &avail);
        const __mmask64 valid_mask = avail == 64u ? ~UINT64_C(0) : ((UINT64_C(1) << avail) - 1u);
        const __mmask64 stars = _mm512_cmpeq_epi8_mask(v, v_star) & valid_mask;
        const __mmask64 slashes = _mm512_cmpeq_epi8_mask(v, v_slash) & valid_mask;
        const __mmask64 nls = _mm512_cmpeq_epi8_mask(v, v_nl) & valid_mask;
        const __mmask64 close = ((stars << 1) | prev_star) & slashes;
        if (close != 0)
        {
            const uint32_t pos = (uint32_t)__builtin_ctzll(close);
            const __mmask64 before = pos == 0 ? 0u : ((UINT64_C(1) << pos) - 1u);
            lines += (uint32_t)__builtin_popcountll(nls & before);
            result = p + pos + 1u;
            break;
        }
        lines += (uint32_t)__builtin_popcountll(nls);
        prev_star = stars >> 63;
        p += avail;
    }
    *line_count = lines;
    return result;
}

// Skip // ... up to (but NOT consuming) the terminating '\n' (or end). Respects backslash-newline line splices.
static const uint8_t* fast_skip_line_comment(const uint8_t* p, const uint8_t* end, uint32_t* line_count)
{
    const __m512i v_nl = _mm512_set1_epi8('\n');
    uint32_t lines = *line_count;
    while (p < end)
    {
        uint32_t avail = 0;
        const __m512i v = fast_load64_bounded(p, end, &avail);
        const __mmask64 valid_mask = avail == 64u ? ~UINT64_C(0) : ((UINT64_C(1) << avail) - 1u);
        __mmask64 nls = _mm512_cmpeq_epi8_mask(v, v_nl) & valid_mask;
        bool done = false;
        while (nls != 0)
        {
            const uint32_t pos = (uint32_t)__builtin_ctzll(nls);
            const uint8_t* nl = p + pos;
            // Check for line splice (\ followed by optional \r before \n)
            const uint8_t* prev = nl;
            if (prev > p && prev[-1] == '\r')
            {
                prev -= 1;
            }
            if (prev > p && prev[-1] == '\\')
            {
                lines += 1u;
                nls &= nls - 1u;
                continue;
            }
            // Also check boundary splice at pos == 0 or pos == 1
            if (pos <= 1u && nl > p - 2)
            {
                const uint8_t* check = nl - 1;
                if (*check == '\r')
                {
                    check -= 1;
                }
                if (*check == '\\')
                {
                    lines += 1u;
                    nls &= nls - 1u;
                    continue;
                }
            }
            p = nl;
            done = true;
            break;
        }
        if (done)
        {
            break;
        }
        p += avail;
    }
    *line_count = lines;
    return p;
}

// Identifier character lookup table (1 for [A-Za-z0-9_$], 2 for [A-Za-z_$], 4 for [0-9], 8 for horizontal ws)
static const uint8_t fast_char_class[256] = {
    ['\t'] = 8, ['\v'] = 8, ['\f'] = 8, ['\r'] = 8, [' '] = 8,
    ['0'] = 5, ['1'] = 5, ['2'] = 5, ['3'] = 5, ['4'] = 5, ['5'] = 5, ['6'] = 5, ['7'] = 5, ['8'] = 5, ['9'] = 5,
    ['A'] = 3, ['B'] = 3, ['C'] = 3, ['D'] = 3, ['E'] = 3, ['F'] = 3, ['G'] = 3, ['H'] = 3, ['I'] = 3, ['J'] = 3,
    ['K'] = 3, ['L'] = 3, ['M'] = 3, ['N'] = 3, ['O'] = 3, ['P'] = 3, ['Q'] = 3, ['R'] = 3, ['S'] = 3, ['T'] = 3,
    ['U'] = 3, ['V'] = 3, ['W'] = 3, ['X'] = 3, ['Y'] = 3, ['Z'] = 3, ['_'] = 3, ['$'] = 3,
    ['a'] = 3, ['b'] = 3, ['c'] = 3, ['d'] = 3, ['e'] = 3, ['f'] = 3, ['g'] = 3, ['h'] = 3, ['i'] = 3, ['j'] = 3,
    ['k'] = 3, ['l'] = 3, ['m'] = 3, ['n'] = 3, ['o'] = 3, ['p'] = 3, ['q'] = 3, ['r'] = 3, ['s'] = 3, ['t'] = 3,
    ['u'] = 3, ['v'] = 3, ['w'] = 3, ['x'] = 3, ['y'] = 3, ['z'] = 3,
};

// AVX-512 identifier continuation length using vpshufb range test across 64 bytes
static inline uint32_t fast_scan_ident_len(const uint8_t* p, const uint8_t* end)
{
    uint32_t total = 0;
    const __m512i v_a = _mm512_set1_epi8('a');
    const __m512i v_z = _mm512_set1_epi8('z');
    const __m512i v_0 = _mm512_set1_epi8('0');
    const __m512i v_9 = _mm512_set1_epi8('9');
    const __m512i v_us = _mm512_set1_epi8('_');
    const __m512i v_dl = _mm512_set1_epi8('$');
    const __m512i v_lc = _mm512_set1_epi8(0x20);
    while (p < end)
    {
        uint32_t avail = 0;
        const __m512i v = fast_load64_bounded(p, end, &avail);
        const __m512i lower = _mm512_or_si512(v, v_lc);
        const __mmask64 is_alpha = _mm512_cmpge_epu8_mask(lower, v_a) & _mm512_cmple_epu8_mask(lower, v_z);
        const __mmask64 is_digit = _mm512_cmpge_epu8_mask(v, v_0) & _mm512_cmple_epu8_mask(v, v_9);
        const __mmask64 is_extra = _mm512_cmpeq_epi8_mask(v, v_us) | _mm512_cmpeq_epi8_mask(v, v_dl);
        const __mmask64 valid_mask = avail == 64u ? ~UINT64_C(0) : ((UINT64_C(1) << avail) - 1u);
        const __mmask64 cont = (is_alpha | is_digit | is_extra) & valid_mask;
        const __mmask64 stop = ~cont;
        if (stop != 0)
        {
            total += (uint32_t)__builtin_ctzll(stop);
            break;
        }
        total += avail;
        p += avail;
    }
    return total;
}

// Fast horizontal whitespace skip (64 bytes at a time when > 1 space)
static inline const uint8_t* fast_skip_hspace(const uint8_t* p, const uint8_t* end)
{
    while (p < end && (fast_char_class[*p] & 8u) != 0)
    {
        p += 1;
        if (p + 8 <= end && (fast_char_class[*p] & 8u) != 0 && (fast_char_class[p[1]] & 8u) != 0)
        {
            const __m512i v_sp = _mm512_set1_epi8(' ');
            const __m512i v_tb = _mm512_set1_epi8('\t');
            const __m512i v_cr = _mm512_set1_epi8('\r');
            while (p < end)
            {
                uint32_t avail = 0;
                const __m512i v = fast_load64_bounded(p, end, &avail);
                const __mmask64 valid_mask = avail == 64u ? ~UINT64_C(0) : ((UINT64_C(1) << avail) - 1u);
                const __mmask64 ws = (_mm512_cmpeq_epi8_mask(v, v_sp) | _mm512_cmpeq_epi8_mask(v, v_tb) |
                                      _mm512_cmpeq_epi8_mask(v, v_cr)) & valid_mask;
                const __mmask64 non_ws = ~ws;
                if (non_ws != 0)
                {
                    p += (uint32_t)__builtin_ctzll(non_ws);
                    break;
                }
                p += avail;
            }
        }
    }
    return p;
}

// ---------------------------------------------------------------------------------------------------------------------
// Preprocessor state: macros, include stack, include-guard cache, conditional stack, expansion stack

#define BUSTER_FPP_MAX_INCLUDES    64u
#define BUSTER_FPP_MAX_CONDS       256u
#define BUSTER_FPP_MAX_EXPAND      128u
#define BUSTER_FPP_MAX_PARAMS      64u
#define BUSTER_FPP_MAX_MACROS      16384u
#define BUSTER_FPP_MAX_DIRS        64u
#define BUSTER_FPP_MAX_GUARDS      2048u
#define BUSTER_FPP_MAX_HIDE        64u
#define BUSTER_FPP_MAX_CALLS       16u

typedef struct FastMacro FastMacro;
struct FastMacro
{
    uint32_t body_start;     // index in macro_toks
    uint32_t body_count;
    uint32_t fast_data;      // pre-encoded SoA data when fast_mode == 1
    uint16_t param_count;
    uint8_t is_func;
    uint8_t is_variadic;
    uint8_t has_ops;         // contains # or ##
    uint8_t active;
    uint8_t fast_mode;       // 0=general, 1=inline 1-tok obj macro, 2=0-param-use func macro (assert/testcase/VVA_ONLY)
    uint8_t fast_kind;       // pre-encoded SoA kind when fast_mode == 1
};

typedef struct FastIncludeFile FastIncludeFile;
struct FastIncludeFile
{
    const uint8_t* base;
    const uint8_t* ptr;
    const uint8_t* end;
    uint64_t map_size;
    const char* path;
    uint32_t path_len;
    uint32_t line;
    uint32_t dir_idx;        // directory index where found (for #include_next)
    uint32_t cond_base;      // conditional stack depth on entry
    uint32_t guard_ident;    // candidate include guard macro ID (0 if none)
    uint32_t serial;         // this push's number, which a window of its text carries (never 0)
    uint8_t guard_state;     // 0=start, 1=saw_ifndef, 2=saw_define, 3=invalidated, 4=closed
    bool at_bol;
    bool owned_mmap;
    bool is_system;
};

// A window of a file's raw tokens (#2850). The lexer's 64-byte loop writes it and does nothing else: no identifier is
// looked up there and no macro asked about, so the loop has no per-token branch, and the readers below it (the bulk
// emitter, the directives' and the macro arguments' reader) take their tokens from it and never lex a byte twice.
// A token is its kind as the classifier gives it (FTOK_IDENT, FTOK_INT_LIT for a number's run, FTOK_STR_LIT and
// FTOK_CHAR_LIT for a plain literal's opening quote, or a punctuator's), its first byte's offset in the file, and an
// identifier's, a number's or a punctuator's length. Whatever the loop can't decide that way ends the window, and
// fast_lex_one lexes the token there, as it lexes every token of a file's last 80 bytes.
#define BUSTER_FWIN_CAPACITY       2048u
#define BUSTER_FWIN_STOP_CAPACITY  0u   // full: the lexing goes on at lex_ptr
#define BUSTER_FWIN_STOP_TAIL      1u   // fewer than 80 bytes are left at lex_ptr
#define BUSTER_FWIN_STOP_DIRECTIVE 2u   // the '#' at lex_ptr starts a line
#define BUSTER_FWIN_STOP_SCALAR    3u   // the token at lex_ptr is fast_lex_one's
#define BUSTER_FWIN_STOP_EOL       4u   // a line's window: the newline that ends the line is at lex_ptr
#define BUSTER_FWIN_STOP_BAIL      5u   // the compile bailed out
// A window token's kind where the token is fast_lex_one's: its readers lex it there.
#define BUSTER_FWIN_SCALAR         FTOK_EOF

typedef struct FastWindow FastWindow;
struct FastWindow
{
    uint8_t* kinds;          // capacity + 64 each: the loop stores whole vectors
    uint32_t* starts;
    uint8_t* lens;
    uint32_t count;
    uint32_t pos;            // the next token to take: every one before it starts before the file's ptr
    uint32_t serial;         // the file's, or 0 for no window
    uint32_t generation;     // counts the windows lexed, for a reader that takes a token back
    const uint8_t* lex_ptr;  // the first byte past what the window covers
    uint8_t stop;            // BUSTER_FWIN_STOP_*: what is at lex_ptr
    bool lex_bol;            // whether lex_ptr starts a line
    bool line_mode;          // lexed to its line's end only (a directive's line)
};

typedef struct FastCondFrame FastCondFrame;
struct FastCondFrame
{
    uint8_t active;          // currently taking tokens
    uint8_t taken;           // some branch already taken
    uint8_t saw_else;        // already saw #else
    uint8_t is_guard;        // matches file's outer #ifndef guard
};

typedef struct FastGuardEntry FastGuardEntry;
struct FastGuardEntry
{
    uint32_t hash;
    uint32_t path_off;
    uint16_t path_len;
    uint8_t once;
    uint8_t used;
    uint32_t guard_ident;
};

typedef struct FastExpandFrame FastExpandFrame;
struct FastExpandFrame
{
    const FastTok* toks;
    uint32_t pos;
    uint32_t count;
    uint32_t macro_ident;    // IdentId being expanded (for hide set)
    uint32_t saved_scratch;  // scratch offset to restore when frame finishes (UINT32_MAX = no rewind)
};

typedef struct FastMacroCall FastMacroCall;
struct FastMacroCall
{
    uint32_t macro_id;
    uint32_t saved_exp_used;
    uint32_t saved_arg_used;
    uint32_t min_expand_depth;
    uint32_t base_expand_depth;
    uint32_t scan_body_idx;
    uint32_t active_pidx;
    uint32_t arg_out_start;
    uint32_t arg_count;
    uint32_t arg_starts[BUSTER_FPP_MAX_PARAMS + 1u];
    uint32_t arg_counts[BUSTER_FPP_MAX_PARAMS + 1u];
    uint32_t exp_arg_starts[BUSTER_FPP_MAX_PARAMS + 1u];
    uint32_t exp_arg_counts[BUSTER_FPP_MAX_PARAMS + 1u];
    uint8_t exp_arg_done[BUSTER_FPP_MAX_PARAMS + 1u];
    uint8_t exp_arg_in_argbuf[BUSTER_FPP_MAX_PARAMS + 1u];
    uint8_t tok_flags;
    uint8_t state;
    bool allow_file;
};

typedef struct FastPP FastPP;
struct FastPP
{
    FastArena* arena;        // persistent arena
    FastArena* scratch;      // reusable scratch arena
    FastIdentTable idents;

    // Preprocessed output token stream in SoA layout (5 bytes/token: 1B kind + 4B data!)
    // 64 kinds fit in one ZMM register for AVX-512 parsing & body pre-scanning!
    uint8_t* out_kind;
    uint32_t* out_data;
    uint32_t out_count;
    uint32_t out_cap;

    // Number literal pool (64-bit integer values & IEEE-754 double/float values)
    uint64_t* num_pool;
    uint32_t num_count;
    uint32_t num_cap;

    // String / float-text literal byte pool
    char* lit_bytes;
    uint64_t lit_bytes_used;
    uint64_t lit_bytes_cap;
    FastLitSlice* lit_slices;
    uint32_t lit_count;
    uint32_t lit_cap;

    // Macro definitions
    FastMacro* macros;
    uint32_t macro_count;
    FastTok* macro_toks;
    uint32_t macro_tok_count;
    uint32_t macro_tok_cap;
    uint32_t* macro_params;
    uint32_t macro_param_count;
    uint32_t macro_param_cap;

    // Include search directories
    const char* dirs[BUSTER_FPP_MAX_DIRS];
    uint32_t dir_lens[BUSTER_FPP_MAX_DIRS];
    uint8_t dir_is_system[BUSTER_FPP_MAX_DIRS];
    uint32_t dir_count;
    uint32_t angled_first;

    // Include guard cache (open-addressed)
    FastGuardEntry guards[BUSTER_FPP_MAX_GUARDS];

    // Mapped file cache (keep small number of system headers? Or unmap immediately on pop!)
    FastIncludeFile files[BUSTER_FPP_MAX_INCLUDES];
    uint32_t file_depth;

    // Conditional stack
    FastCondFrame conds[BUSTER_FPP_MAX_CONDS];
    uint32_t cond_depth;

    // Macro expansion stack
    FastExpandFrame expand[BUSTER_FPP_MAX_EXPAND];
    uint32_t expand_depth;

    // Scratch buffer for macro expansion arguments & pasted tokens
    FastTok* exp_buf;
    uint32_t exp_used;
    uint32_t exp_cap;
    FastTok* arg_buf;
    uint32_t arg_used;
    uint32_t arg_cap;
    FastMacroCall* call_stack;

    // Hide set stack (active macros currently being expanded)
    uint32_t hide_ids[BUSTER_FPP_MAX_HIDE];
    uint32_t hide_count;

    // The top file's raw tokens (FastWindow)
    FastWindow win;
    uint32_t file_serial;

    // Current source state
    uint32_t counter_val;
    const char* base_file;
    bool bailout;
    const char* bailout_reason;
    bool unsigned_char;
    bool c23;
};

static void fast_pp_read_tok(FastPP* pp, FastIncludeFile* f, bool stop_at_eol, FastTok* out_tok);

static __attribute__((cold, noinline)) void fast_pp_bail(FastPP* pp, const char* reason)
{
    if (!pp->bailout)
    {
        pp->bailout = true;
        pp->bailout_reason = reason;
    }
}

static inline uint32_t fast_pp_add_num(FastPP* pp, uint64_t val)
{
    uint32_t idx = 0;
    if (__builtin_expect(pp->num_count < pp->num_cap, 1))
    {
        idx = pp->num_count++;
        pp->num_pool[idx] = val;
    }
    else
    {
        fast_pp_bail(pp, "num_pool capacity");
    }
    return idx;
}

static inline uint32_t fast_pp_add_lit(FastPP* pp, const char* bytes, uint32_t len)
{
    uint32_t idx = 0;
    if (__builtin_expect(pp->lit_count < pp->lit_cap && pp->lit_bytes_used + len + 1u <= pp->lit_bytes_cap, 1))
    {
        idx = pp->lit_count++;
        const uint32_t off = (uint32_t)pp->lit_bytes_used;
        memcpy(pp->lit_bytes + off, bytes, len);
        pp->lit_bytes[off + len] = '\0';
        pp->lit_bytes_used += len + 1u;
        pp->lit_slices[idx] = (FastLitSlice){ .offset = off, .length = len };
    }
    else
    {
        fast_pp_bail(pp, "lit_pool capacity");
    }
    return idx;
}

// ---------------------------------------------------------------------------------------------------------------------
// Fast single-token lexer from a byte buffer (handles splices, numbers, strings, punctuators, identifiers)

// Parse integer or floating-point preprocessing number at [p, end). `flags_in` are the token's flags so far.
static FastLexed fast_lex_number(FastPP* pp, const uint8_t* p, const uint8_t* end, uint32_t flags_in)
{
    const uint8_t* const start = p;
    // Fast path for single ASCII digit followed by non-ident/non-dot char (super common: 0, 1, 2, ...)
    if (__builtin_expect(p + 1 < end && (fast_char_class[p[1]] & 1u) == 0 && p[1] != '.' && p[1] != '\\' && p[1] != '\'', 1))
    {
        return (FastLexed){ .p = p + 1, .tok = fast_tok_make(FTOK_INT_LIT, flags_in, 0, (uint32_t)(p[0] - '0')) };
    }

    bool is_float = false;
    bool is_hex = false;
    bool is_bin = false;
    bool has_splice = false;

    if (p + 1 < end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
        is_hex = true;
        p += 2;
    }
    else if (p + 1 < end && p[0] == '0' && (p[1] == 'b' || p[1] == 'B'))
    {
        is_bin = true;
        p += 2;
    }

    while (p < end)
    {
        const uint8_t c = *p;
        if ((fast_char_class[c] & 1u) != 0)
        {
            if ((!is_hex && (c == 'e' || c == 'E')) || (is_hex && (c == 'p' || c == 'P')))
            {
                is_float = true;
                p += 1;
                if (p < end && (*p == '+' || *p == '-'))
                {
                    p += 1;
                }
                continue;
            }
            p += 1;
        }
        else if (c == '.')
        {
            is_float = true;
            p += 1;
        }
        else if (c == '\'')
        {
            // C23 digit separator -> bail or skip
            fast_pp_bail(pp, "digit separator");
            p += 1;
        }
        else if (c == '\\' && p + 1 < end && (p[1] == '\n' || (p[1] == '\r' && p + 2 < end && p[2] == '\n')))
        {
            has_splice = true;
            p += p[1] == '\r' ? 3 : 2;
        }
        else
        {
            break;
        }
    }

    if (has_splice)
    {
        fast_pp_bail(pp, "spliced number");
        return (FastLexed){ .p = p, .tok = fast_tok_make(FTOK_EOF, flags_in, 0, 0) };
    }

    const uint32_t len = (uint32_t)(p - start);
    if (is_float)
    {
        uint32_t float_flags = flags_in;
        uint32_t float_data = 0;
        uint32_t float_extra = 0;
        // Check suffix f/F/l/L
        const uint8_t last = p[-1];
        if (last == 'f' || last == 'F')
        {
            float_flags |= BUSTER_FTOK_LONG; // f32
        }
        else if (last == 'l' || last == 'L')
        {
            float_flags |= BUSTER_FTOK_LONGLONG; // f80 (long double)
        }
        // Parse double immediately via strtod on small stack copy
        char buf[128];
        if (len < sizeof(buf))
        {
            memcpy(buf, start, len);
            buf[len] = '\0';
            char* ep = NULL;
            const double d = strtod(buf, &ep);
            uint64_t bits = 0;
            memcpy(&bits, &d, 8);
            float_data = fast_pp_add_num(pp, bits);
            float_extra = (uint16_t)fast_pp_add_lit(pp, buf, len);
        }
        else
        {
            fast_pp_bail(pp, "long float literal");
        }
        return (FastLexed){ .p = p, .tok = fast_tok_make(FTOK_FLOAT_LIT, float_flags, float_extra, float_data) };
    }

    // Integer literal
    const uint8_t* dig = start;
    const uint8_t* dig_end = p;
    uint8_t flags = 0;
    // Strip suffixes (u/U, l/L, ll/LL)
    uint32_t l_count = 0;
    while (dig_end > dig)
    {
        const uint8_t s = dig_end[-1];
        if (s == 'u' || s == 'U')
        {
            flags |= BUSTER_FTOK_UNSIGNED;
            dig_end -= 1;
        }
        else if (s == 'l' || s == 'L')
        {
            l_count += 1u;
            dig_end -= 1;
        }
        else
        {
            break;
        }
    }
    if (l_count == 1u)
    {
        flags |= BUSTER_FTOK_LONG;
    }
    else if (l_count >= 2u)
    {
        flags |= BUSTER_FTOK_LONGLONG;
    }

    uint64_t val = 0;
    if (is_hex)
    {
        flags |= BUSTER_FTOK_HEX;
        dig += 2;
        while (dig < dig_end)
        {
            const uint8_t c = *dig++;
            uint32_t d = 0;
            if (c >= '0' && c <= '9')
            {
                d = (uint32_t)(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                d = (uint32_t)(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                d = (uint32_t)(c - 'A' + 10);
            }
            else
            {
                fast_pp_bail(pp, "invalid hex literal");
                break;
            }
            val = (val << 4) | d;
        }
    }
    else if (is_bin)
    {
        flags |= BUSTER_FTOK_HEX;
        dig += 2;
        while (dig < dig_end)
        {
            const uint8_t c = *dig++;
            if (c != '0' && c != '1')
            {
                fast_pp_bail(pp, "invalid bin literal");
                break;
            }
            val = (val << 1) | (uint64_t)(c - '0');
        }
    }
    else if (dig < dig_end && *dig == '0' && dig + 1 < dig_end)
    {
        flags |= BUSTER_FTOK_HEX;
        dig += 1;
        while (dig < dig_end)
        {
            const uint8_t c = *dig++;
            if (c < '0' || c > '7')
            {
                fast_pp_bail(pp, "invalid oct literal");
                break;
            }
            val = (val << 3) | (uint64_t)(c - '0');
        }
    }
    else
    {
        while (dig < dig_end)
        {
            const uint8_t c = *dig++;
            if (c < '0' || c > '9')
            {
                fast_pp_bail(pp, "invalid dec literal");
                break;
            }
            val = val * 10u + (uint64_t)(c - '0');
        }
    }

    uint32_t int_flags = flags_in | flags;
    uint32_t int_data = (uint32_t)val;
    if (val > UINT32_MAX)
    {
        int_flags |= BUSTER_FTOK_IN_POOL;
        int_data = fast_pp_add_num(pp, val);
    }
    return (FastLexed){ .p = p, .tok = fast_tok_make(FTOK_INT_LIT, int_flags, 0, int_data) };
}

static inline uint64_t fast_tok_int_val(const FastPP* pp, FastTok tok)
{
    return (tok.flags & BUSTER_FTOK_IN_POOL) != 0 ? pp->num_pool[tok.data] : (uint64_t)tok.data;
}

// Decode a C escape sequence at *pp_ptr (pointing past '\'). Returns decoded value.
static uint32_t fast_decode_escape(const uint8_t** pp_ptr, const uint8_t* end, bool* ok)
{
    const uint8_t* p = *pp_ptr;
    uint32_t val = 0;
    if (p >= end)
    {
        *ok = false;
    }
    else
    {
        const uint8_t c = *p++;
        switch (c)
        {
            case 'n': val = '\n'; break;
            case 't': val = '\t'; break;
            case 'r': val = '\r'; break;
            case 'a': val = '\a'; break;
            case 'b': val = '\b'; break;
            case 'f': val = '\f'; break;
            case 'v': val = '\v'; break;
            case '\\': val = '\\'; break;
            case '\'': val = '\''; break;
            case '"': val = '"'; break;
            case '?': val = '?'; break;
            case 'e': val = 0x1bu; break;
            case 'x':
            {
                bool any = false;
                while (p < end)
                {
                    const uint8_t h = *p;
                    if (h >= '0' && h <= '9') { val = (val << 4) | (uint32_t)(h - '0'); p += 1; any = true; }
                    else if (h >= 'a' && h <= 'f') { val = (val << 4) | (uint32_t)(h - 'a' + 10); p += 1; any = true; }
                    else if (h >= 'A' && h <= 'F') { val = (val << 4) | (uint32_t)(h - 'A' + 10); p += 1; any = true; }
                    else { break; }
                }
                *ok = *ok && any;
            } break;
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
            {
                val = (uint32_t)(c - '0');
                for (uint32_t i = 0; i < 2u && p < end && *p >= '0' && *p <= '7'; i += 1)
                {
                    val = (val << 3) | (uint32_t)(*p++ - '0');
                }
            } break;
            default:
                *ok = false;
                break;
        }
    }
    *pp_ptr = p;
    return val;
}

// Where the string literal whose text starts at `p` (past its opening quote) stops, by 64-byte scans for '"', '\\'
// and '\n': at its closing quote, at a newline no backslash precedes, or at `end`. `*out_stopped` says which.
#define BUSTER_FSTRING_CLOSED       0u
#define BUSTER_FSTRING_NEWLINE      1u
#define BUSTER_FSTRING_UNTERMINATED 2u

static const uint8_t* fast_scan_string_close(const uint8_t* p, const uint8_t* end, bool* out_has_escape, uint32_t* out_stopped)
{
    const __m512i v_q = _mm512_set1_epi8('"');
    const __m512i v_bs = _mm512_set1_epi8('\\');
    const __m512i v_nl = _mm512_set1_epi8('\n');
    bool has_escape = false;
    uint32_t stopped = BUSTER_FSTRING_UNTERMINATED;

    while (p < end)
    {
        uint32_t avail = 0;
        const __m512i v = fast_load64_bounded(p, end, &avail);
        const __mmask64 valid_mask = avail == 64u ? ~UINT64_C(0) : ((UINT64_C(1) << avail) - 1u);
        const __mmask64 special = (_mm512_cmpeq_epi8_mask(v, v_q) | _mm512_cmpeq_epi8_mask(v, v_bs) |
                                   _mm512_cmpeq_epi8_mask(v, v_nl)) & valid_mask;
        if (special != 0)
        {
            const uint32_t pos = (uint32_t)__builtin_ctzll(special);
            p += pos;
            if (*p == '"')
            {
                stopped = BUSTER_FSTRING_CLOSED;
                break;
            }
            else if (*p == '\\')
            {
                has_escape = true;
                p += 1;
                if (p < end)
                {
                    if (*p == '\r' && p + 1 < end && p[1] == '\n')
                    {
                        p += 2;
                    }
                    else
                    {
                        p += 1;
                    }
                }
                continue;
            }
            else
            {
                stopped = BUSTER_FSTRING_NEWLINE;
                break;
            }
        }
        p += avail;
    }
    *out_has_escape = has_escape;
    *out_stopped = stopped;
    return p;
}

// Lex string literal starting at '"' using AVX-512 to scan for '"' or '\' or '\n'
static FastLexed fast_lex_string(FastPP* pp, const uint8_t* p, const uint8_t* end, uint32_t flags_in, uint16_t prefix_kind)
{
    p += 1; // skip opening '"'
    const uint8_t* const start = p;
    uint32_t tok_kind = FTOK_EOF;
    uint32_t tok_extra = 0;
    uint32_t tok_data = 0;
    bool has_escape = false;
    uint32_t stopped = 0;
    p = fast_scan_string_close(p, end, &has_escape, &stopped);
    const bool closed = stopped == BUSTER_FSTRING_CLOSED;
    if (stopped == BUSTER_FSTRING_NEWLINE)
    {
        // Unescaped newline in string literal
        fast_pp_bail(pp, "newline in string");
    }

    if (!closed)
    {
        fast_pp_bail(pp, "unterminated string");
    }
    else
    {
        const uint32_t raw_len = (uint32_t)(p - start);
        p += 1; // skip closing '"'
        tok_kind = FTOK_STR_LIT;
        tok_extra = prefix_kind;
        if (!has_escape)
        {
            tok_data = fast_pp_add_lit(pp, (const char*)start, raw_len);
        }
        else if (pp->lit_count < pp->lit_cap && pp->lit_bytes_used + raw_len + 1u <= pp->lit_bytes_cap)
        {
            const uint32_t off = (uint32_t)pp->lit_bytes_used;
            char* dst = pp->lit_bytes + off;
            uint32_t out_len = 0;
            const uint8_t* q = start;
            const uint8_t* const q_end = start + raw_len;
            bool ok = true;
            while (q < q_end && ok)
            {
                if (*q == '\\')
                {
                    q += 1;
                    if (q < q_end && (*q == '\n' || (*q == '\r' && q + 1 < q_end && q[1] == '\n')))
                    {
                        q += *q == '\r' ? 2 : 1;
                    }
                    else
                    {
                        const uint32_t ch = fast_decode_escape(&q, q_end, &ok);
                        dst[out_len++] = (char)(ch & 0xffu);
                    }
                }
                else
                {
                    dst[out_len++] = (char)*q++;
                }
            }
            if (!ok)
            {
                fast_pp_bail(pp, "bad string escape");
            }
            dst[out_len] = '\0';
            pp->lit_bytes_used += out_len + 1u;
            const uint32_t idx = pp->lit_count++;
            pp->lit_slices[idx] = (FastLitSlice){ .offset = off, .length = out_len };
            tok_data = idx;
        }
        else
        {
            fast_pp_bail(pp, "lit_pool overflow");
        }
    }
    return (FastLexed){ .p = p, .tok = fast_tok_make(tok_kind, flags_in, tok_extra, tok_data) };
}

// Lex character literal starting at '\''
static FastLexed fast_lex_char(FastPP* pp, const uint8_t* p, const uint8_t* end, uint32_t flags_in, uint16_t prefix_kind)
{
    p += 1; // skip opening '\''
    uint32_t val = 0;
    uint32_t chars = 0;
    bool ok = true;
    FastTok tok = fast_tok_make(FTOK_EOF, flags_in, 0, 0);
    while (p < end && *p != '\'' && *p != '\n' && ok)
    {
        uint32_t ch = 0;
        if (*p == '\\')
        {
            p += 1;
            ch = fast_decode_escape(&p, end, &ok);
        }
        else
        {
            ch = *p++;
        }
        val = (val << 8) | (ch & 0xffu);
        chars += 1u;
    }
    if (!ok || p >= end || *p != '\'' || chars == 0 || chars > 4u)
    {
        fast_pp_bail(pp, "invalid char literal");
    }
    else
    {
        p += 1; // skip closing '\''
        if (chars == 1u && prefix_kind == 0 && !pp->unsigned_char)
        {
            val = (uint32_t)(int32_t)(int8_t)(val & 0xffu);
        }
        tok = fast_tok_make(FTOK_CHAR_LIT, flags_in, prefix_kind, val);
    }
    return (FastLexed){ .p = p, .tok = tok };
}

// Lex ONE raw preprocessing token from [p, end), updating *line_count and *at_bol.
// Returns pointer past the lexed token (or end if FTOK_EOF).
// If stop_at_eol is true, returns FTOK_EOF upon reaching '\n' without consuming '\n'.
static BUSTER_DIRECT_HOT const uint8_t* fast_lex_one(FastPP* pp, const uint8_t* p, const uint8_t* end, uint32_t* line_count,
                                   bool* at_bol, bool stop_at_eol, FastTok* out_tok)
{
    uint8_t flags = 0;
    bool bol = *at_bol;
    // The token's fields stay in registers and are stored once, whole (fast_tok_make).
    uint32_t tok_kind = FTOK_EOF;
    uint32_t tok_flags = 0;
    uint32_t tok_data = 0;
    FastLexed literal = { 0 };
    bool is_literal = false;

    for (;;)
    {
        // Fast skip horizontal whitespace
        if (p < end && (fast_char_class[*p] & 8u) != 0)
        {
            flags |= BUSTER_FTOK_SPACE;
            p = fast_skip_hspace(p, end);
        }
        if (p >= end)
        {
            break;
        }
        const uint8_t c = *p;
        if (c == '\n')
        {
            if (stop_at_eol)
            {
                break;
            }
            p += 1;
            *line_count += 1u;
            bol = true;
            flags |= BUSTER_FTOK_SPACE;
            continue;
        }
        if (c == '\\' && p + 1 < end && (p[1] == '\n' || (p[1] == '\r' && p + 2 < end && p[2] == '\n')))
        {
            p += p[1] == '\r' ? 3 : 2;
            *line_count += 1u;
            continue;
        }
        if (c == '/' && p + 1 < end)
        {
            if (p[1] == '*')
            {
                flags |= BUSTER_FTOK_SPACE;
                p = fast_skip_block_comment(p + 2, end, line_count);
                if (p == NULL)
                {
                    fast_pp_bail(pp, "unterminated block comment");
                    p = end;
                    break;
                }
                continue;
            }
            if (p[1] == '/')
            {
                flags |= BUSTER_FTOK_SPACE;
                p = fast_skip_line_comment(p + 2, end, line_count);
                continue;
            }
        }

        // We have a real token!
        if (bol)
        {
            flags |= BUSTER_FTOK_BOL;
        }
        bol = false;
        tok_flags = flags;

        const uint8_t cls = fast_char_class[c];
        if ((cls & 2u) != 0)
        {
            // Identifier or wide/UTF string/char prefix (L"..", u"..", U"..", u8"..")
            if (p + 1 < end && (c == 'L' || c == 'u' || c == 'U') && (p[1] == '"' || p[1] == '\''))
            {
                const uint16_t pk = c == 'L' ? 1u : c == 'u' ? 2u : 3u;
                literal = p[1] == '"' ? fast_lex_string(pp, p + 1, end, flags, pk) : fast_lex_char(pp, p + 1, end, flags, pk);
                is_literal = true;
                break;
            }
            if (c == 'u' && p + 2 < end && p[1] == '8' && (p[2] == '"' || p[2] == '\''))
            {
                literal = p[2] == '"' ? fast_lex_string(pp, p + 2, end, flags, 4u) : fast_lex_char(pp, p + 2, end, flags, 4u);
                is_literal = true;
                break;
            }
            const uint32_t len = fast_scan_ident_len(p, end);
            // Check for backslash-newline splice continuing identifier
            if (__builtin_expect(p + len < end && p[len] == '\\', 0))
            {
                fast_pp_bail(pp, "spliced identifier");
                p = end;
                break;
            }
            uint8_t kw_resolved = FTOK_IDENT;
            const uint32_t id = (len <= 16u && p + 16 <= end) ?
                                fast_ident_lookup_16(&pp->idents, p, len, &kw_resolved) :
                                fast_ident_intern(&pp->idents, p, len);
            if (__builtin_expect(id == 0, 0))
            {
                fast_pp_bail(pp, "ident table full");
                p = end;
                break;
            }
            if (len > 16u || p + 16 > end)
            {
                const uint8_t kw = pp->idents.entries[id].kw_kind;
                kw_resolved = kw != 0 ? kw : FTOK_IDENT;
            }
            tok_kind = kw_resolved;
            tok_data = id;
            p += len;
            break;
        }
        if ((cls & 4u) != 0)
        {
            literal = fast_lex_number(pp, p, end, flags);
            is_literal = true;
            break;
        }
        if (c == '"')
        {
            literal = fast_lex_string(pp, p, end, flags, 0);
            is_literal = true;
            break;
        }
        if (c == '\'')
        {
            literal = fast_lex_char(pp, p, end, flags, 0);
            is_literal = true;
            break;
        }

        // Punctuators
        const uint8_t c1 = p + 1 < end ? p[1] : 0;
        const uint8_t c2 = p + 2 < end ? p[2] : 0;
        switch (c)
        {
            case '(': tok_kind = FTOK_LPAREN; p += 1; break;
            case ')': tok_kind = FTOK_RPAREN; p += 1; break;
            case '{': tok_kind = FTOK_LBRACE; p += 1; break;
            case '}': tok_kind = FTOK_RBRACE; p += 1; break;
            case '[': tok_kind = FTOK_LBRACKET; p += 1; break;
            case ']': tok_kind = FTOK_RBRACKET; p += 1; break;
            case ';': tok_kind = FTOK_SEMI; p += 1; break;
            case ',': tok_kind = FTOK_COMMA; p += 1; break;
            case '?': tok_kind = FTOK_QUESTION; p += 1; break;
            case ':': tok_kind = FTOK_COLON; p += 1; break;
            case '~': tok_kind = FTOK_TILDE; p += 1; break;
            case '.':
                if (c1 >= '0' && c1 <= '9') { literal = fast_lex_number(pp, p, end, flags); is_literal = true; }
                else if (c1 == '.' && c2 == '.') { tok_kind = FTOK_ELLIPSIS; p += 3; }
                else { tok_kind = FTOK_DOT; p += 1; }
                break;
            case '-':
                if (c1 == '>') { tok_kind = FTOK_ARROW; p += 2; }
                else if (c1 == '-') { tok_kind = FTOK_DEC; p += 2; }
                else if (c1 == '=') { tok_kind = FTOK_SUB_ASSIGN; p += 2; }
                else { tok_kind = FTOK_MINUS; p += 1; }
                break;
            case '+':
                if (c1 == '+') { tok_kind = FTOK_INC; p += 2; }
                else if (c1 == '=') { tok_kind = FTOK_ADD_ASSIGN; p += 2; }
                else { tok_kind = FTOK_PLUS; p += 1; }
                break;
            case '*':
                if (c1 == '=') { tok_kind = FTOK_MUL_ASSIGN; p += 2; }
                else { tok_kind = FTOK_STAR; p += 1; }
                break;
            case '/':
                if (c1 == '=') { tok_kind = FTOK_DIV_ASSIGN; p += 2; }
                else { tok_kind = FTOK_SLASH; p += 1; }
                break;
            case '%':
                if (c1 == '=') { tok_kind = FTOK_MOD_ASSIGN; p += 2; }
                else { tok_kind = FTOK_PERCENT; p += 1; }
                break;
            case '&':
                if (c1 == '&') { tok_kind = FTOK_LAND; p += 2; }
                else if (c1 == '=') { tok_kind = FTOK_AND_ASSIGN; p += 2; }
                else { tok_kind = FTOK_AMP; p += 1; }
                break;
            case '|':
                if (c1 == '|') { tok_kind = FTOK_LOR; p += 2; }
                else if (c1 == '=') { tok_kind = FTOK_OR_ASSIGN; p += 2; }
                else { tok_kind = FTOK_PIPE; p += 1; }
                break;
            case '^':
                if (c1 == '=') { tok_kind = FTOK_XOR_ASSIGN; p += 2; }
                else { tok_kind = FTOK_CARET; p += 1; }
                break;
            case '!':
                if (c1 == '=') { tok_kind = FTOK_NE; p += 2; }
                else { tok_kind = FTOK_BANG; p += 1; }
                break;
            case '=':
                if (c1 == '=') { tok_kind = FTOK_EQ; p += 2; }
                else { tok_kind = FTOK_ASSIGN; p += 1; }
                break;
            case '<':
                if (c1 == '<')
                {
                    if (c2 == '=') { tok_kind = FTOK_SHL_ASSIGN; p += 3; }
                    else { tok_kind = FTOK_LSHIFT; p += 2; }
                }
                else if (c1 == '=') { tok_kind = FTOK_LE; p += 2; }
                else { tok_kind = FTOK_LT; p += 1; }
                break;
            case '>':
                if (c1 == '>')
                {
                    if (c2 == '=') { tok_kind = FTOK_SHR_ASSIGN; p += 3; }
                    else { tok_kind = FTOK_RSHIFT; p += 2; }
                }
                else if (c1 == '=') { tok_kind = FTOK_GE; p += 2; }
                else { tok_kind = FTOK_GT; p += 1; }
                break;
            case '#':
                if (c1 == '#') { tok_kind = FTOK_HASHHASH; p += 2; }
                else { tok_kind = FTOK_HASH; p += 1; }
                break;
            default:
                tok_kind = FTOK_PP_OTHER;
                tok_data = c;
                p += 1;
                break;
        }
        break;
    }

    *at_bol = bol;
    if (is_literal)
    {
        *out_tok = literal.tok;
        p = literal.p;
    }
    else
    {
        *out_tok = fast_tok_make(tok_kind, tok_flags, 0, tok_data);
    }
    return p;
}

// ---------------------------------------------------------------------------------------------------------------------
// The text of a conditional group that is not taken: nothing in it is lexed. fast_pp_skip_inactive looks for the
// directive that ends the group, 64 bytes at a time, and leaves the loop only at a '#' that is the first character of
// its line and at the start of a comment.
//
// Returns the directive (FPP_KW_ELIF, ELSE, ENDIF, ELIFDEF, ELIFNDEF) with f->ptr just after its keyword, or 0 where the
// file ends first.
//
// Per block, in masks:
// - a line's first character: the line starts (after each newline, and the block's first byte when the text before it
//   ended in a line's leading blanks) added to the blanks carry through each line's leading blanks to it;
// - a comment's start, seen at its second character: a '*' or a '/' after a '/' (the '/' may be the last byte of the
//   block before, so no byte past the block is read);
// - what a literal may hide: a quote taints the bytes after it up to a newline that no backslash precedes, by the
//   same carry. A '#' or a comment's start in tainted bytes is rare, and is decided by scanning that line's literal the
//   way this function always did (to its closing quote or its line's end, a backslash taking the next character).
// Everything else (a '#' inside a line, a lone '/', quotes with nothing after them to hide) is passed in bulk.
// f->at_bol is false after a directive; where the file ends first the compile fails and nothing reads it.
static BUSTER_DIRECT_HOT uint8_t fast_pp_skip_inactive(FastPP* pp, FastIncludeFile* f)
{
    const uint8_t* p = f->ptr;
    const uint8_t* const end = f->end;
    uint32_t line = f->line;
    uint32_t nested = 0;
    uint8_t found_kw = 0;
    // What crosses a block's end.
    uint64_t bol_in = f->at_bol ? 1u : 0u;   // the text before p ends in a line's leading blanks
    uint64_t taint_in = 0;                   // a quote is before p on its line
    uint64_t backslash_in = 0;               // the byte before p is a backslash
    uint64_t slash_in = 0;                   // the byte before p is a '/'
    uint64_t slash_first = 0;                // ... which is its line's first character
    uint64_t slash_tainted = 0;              // ... and is tainted
    const uint8_t* clean_from = p;           // no literal is open here: where the last decision left the text
    bool done = false;

    const __m512i v_nl = _mm512_set1_epi8('\n');
    const __m512i v_hash = _mm512_set1_epi8('#');
    const __m512i v_slash = _mm512_set1_epi8('/');
    const __m512i v_star = _mm512_set1_epi8('*');
    const __m512i v_dq = _mm512_set1_epi8('"');
    const __m512i v_sq = _mm512_set1_epi8('\'');
    const __m512i v_bs = _mm512_set1_epi8('\\');
    const __m512i v_space = _mm512_set1_epi8(' ');
    const __m512i v_tab = _mm512_set1_epi8('\t');
    const __m512i v_four = _mm512_set1_epi8(4);

    while (p < end && !done)
    {
        uint32_t avail = 0;
        const __m512i v = fast_load64_bounded(p, end, &avail);
        const __mmask64 valid = _bzhi_u64(~UINT64_C(0), avail);
        const __mmask64 nl = _mm512_cmpeq_epi8_mask(v, v_nl);
        const __mmask64 hash = _mm512_cmpeq_epi8_mask(v, v_hash);
        const __mmask64 slash = _mm512_cmpeq_epi8_mask(v, v_slash);
        const __mmask64 star = _mm512_cmpeq_epi8_mask(v, v_star);
        const __mmask64 quote = _mm512_cmpeq_epi8_mask(v, v_dq) | _mm512_cmpeq_epi8_mask(v, v_sq);
        const __mmask64 backslash = _mm512_cmpeq_epi8_mask(v, v_bs);
        // Blanks are fast_char_class's 8: ' ', and '\t' '\v' '\f' '\r' (9 to 13 without the newline). The bytes past
        // the file's end count as blanks, so that what is carried out of the block is the state at the file's end.
        const __mmask64 blank = _mm512_cmpeq_epi8_mask(v, v_space) | (_mm512_cmple_epu8_mask(_mm512_sub_epi8(v, v_tab), v_four) & ~nl) | ~valid;

        const __mmask64 starts = (nl << 1) | bol_in;
        unsigned long long lead_sum = 0;
        const uint64_t lead_out = _addcarry_u64(0, blank, starts & blank, &lead_sum);
        const __mmask64 first = (starts | lead_sum) & ~blank;

        const __mmask64 opens = (star | slash) & ((slash << 1) | slash_in);

        const __mmask64 passes = ~(nl & ~((backslash << 1) | backslash_in));
        unsigned long long taint_sum = 0;
        const uint64_t taint_out = _addcarry_u64((unsigned char)taint_in, passes, quote, &taint_sum);
        const __mmask64 tainted = passes & ~taint_sum;

        const __mmask64 directives = hash & first;
        const __mmask64 events = (directives | opens) & valid;
        if (__builtin_expect(events == 0, 1))
        {
            line += (uint32_t)__builtin_popcountll(nl);
            bol_in = lead_out | (nl >> 63);
            taint_in = taint_out;
            backslash_in = backslash >> 63;
            slash_in = slash >> 63;
            slash_first = first >> 63;
            slash_tainted = tainted >> 63;
            p += avail;
        }
        else
        {
            const uint32_t pos = (uint32_t)__builtin_ctzll(events);
            const bool directive = ((directives >> pos) & 1u) != 0;
            // A comment's event is at its second character.
            const uint32_t slash_pos = pos - 1u;
            const uint8_t* const at = directive ? p + pos : p + pos - 1;
            const bool at_first = directive || (pos != 0 ? ((first >> slash_pos) & 1u) != 0 : slash_first != 0);
            const bool at_tainted = directive ? ((tainted >> pos) & 1u) != 0 : (pos != 0 ? ((tainted >> slash_pos) & 1u) != 0 : slash_tainted != 0);
            line += (uint32_t)__builtin_popcountll(nl & _bzhi_u64(~UINT64_C(0), pos));
            // What the decision leaves: no literal open, and nothing carried.
            const uint8_t* next = at;
            uint64_t next_bol = 0;
            bool decided = false;

            if (__builtin_expect(at_tainted, 0))
            {
                // The line's start (a newline that no backslash precedes, as above), no further back than where the
                // text was last left with no literal open, and the first quote from there.
                const uint8_t* s = at;
                while (s > clean_from && !(s[-1] == '\n' && !(s - 1 > clean_from && s[-2] == '\\')))
                {
                    s -= 1;
                }
                while (s < at && *s != '"' && *s != '\'')
                {
                    s += 1;
                }
                if (s < at)
                {
                    // The newlines between the quote and here are counted again by the literal's scan.
                    for (const uint8_t* c = s; c < at; c += 1)
                    {
                        line -= *c == '\n' ? 1u : 0u;
                    }
                    const uint8_t quote_char = *s;
                    s += 1;
                    while (s < end && *s != quote_char && *s != '\n')
                    {
                        if (*s == '\\' && s + 1 < end)
                        {
                            if (s[1] == '\n') { line += 1u; }
                            s += 2;
                        }
                        else
                        {
                            s += 1;
                        }
                    }
                    if (s < end && *s == quote_char) { s += 1; }
                    next = s;
                    decided = true;
                }
            }

            if (!decided && directive)
            {
                next = fast_skip_hspace(at + 1, end);
                if (next < end && (fast_char_class[*next] & 2u) != 0)
                {
                    const uint32_t len = fast_scan_ident_len(next, end);
                    uint8_t kw = 0;
                    if (len == 2u && next[0] == 'i' && next[1] == 'f') { kw = FPP_KW_IF; }
                    else if (len == 5u && memcmp(next, "ifdef", 5) == 0) { kw = FPP_KW_IFDEF; }
                    else if (len == 6u && memcmp(next, "ifndef", 6) == 0) { kw = FPP_KW_IFNDEF; }
                    else if (len == 5u && memcmp(next, "endif", 5) == 0) { kw = FPP_KW_ENDIF; }
                    else if (len == 4u && memcmp(next, "else", 4) == 0) { kw = FPP_KW_ELSE; }
                    else if (len == 4u && memcmp(next, "elif", 4) == 0) { kw = FPP_KW_ELIF; }
                    else if (len == 7u && memcmp(next, "elifdef", 7) == 0) { kw = FPP_KW_ELIFDEF; }
                    else if (len == 8u && memcmp(next, "elifndef", 8) == 0) { kw = FPP_KW_ELIFNDEF; }
                    next += len;
                    if (kw == FPP_KW_IF || kw == FPP_KW_IFDEF || kw == FPP_KW_IFNDEF)
                    {
                        nested += 1u;
                    }
                    else if (kw == FPP_KW_ENDIF)
                    {
                        if (nested == 0) { found_kw = kw; done = true; }
                        else { nested -= 1u; }
                    }
                    else if (nested == 0 && (kw == FPP_KW_ELSE || kw == FPP_KW_ELIF || kw == FPP_KW_ELIFDEF || kw == FPP_KW_ELIFNDEF))
                    {
                        found_kw = kw;
                        done = true;
                    }
                }
            }
            else if (!decided)
            {
                // A comment is a blank: the line's first character may still follow it. Its lines are counted apart, so
                // that the loop's count stays in a register.
                uint32_t comment_lines = 0;
                next_bol = at_first ? 1u : 0u;
                if (at[1] == '*')
                {
                    next = fast_skip_block_comment(at + 2, end, &comment_lines);
                    if (next == NULL)
                    {
                        fast_pp_bail(pp, "unterminated comment in skip");
                        next = end;
                        done = true;
                    }
                }
                else
                {
                    next = fast_skip_line_comment(at + 2, end, &comment_lines);
                }
                line += comment_lines;
            }

            p = next;
            clean_from = next;
            bol_in = next_bol;
            taint_in = 0;
            backslash_in = 0;
            slash_in = 0;
        }
    }

    if (found_kw == 0 && !pp->bailout)
    {
        fast_pp_bail(pp, "unterminated #if");
    }
    f->ptr = p;
    f->line = line;
    f->at_bol = bol_in != 0 && found_kw == 0;
    return found_kw;
}

// ---------------------------------------------------------------------------------------------------------------------
// Include guard cache & file resolution

static inline uint32_t fast_guard_hash(const char* s, uint32_t len)
{
    const uint64_t h = fast_hash_ident((const uint8_t*)s, len);
    return (uint32_t)(h ^ (h >> 32)) | 1u;
}

static FastGuardEntry* fast_guard_lookup(FastPP* pp, const char* path, uint32_t len, bool create)
{
    const uint32_t h = fast_guard_hash(path, len);
    const uint32_t mask = BUSTER_FPP_MAX_GUARDS - 1u;
    uint32_t idx = h & mask;
    FastGuardEntry* result = NULL;
    for (uint32_t probe = 0; probe < BUSTER_FPP_MAX_GUARDS; probe += 1)
    {
        FastGuardEntry* e = pp->guards + idx;
        if (e->used == 0)
        {
            if (create && pp->lit_bytes_used + len + 1u <= pp->lit_bytes_cap)
            {
                const uint32_t off = (uint32_t)pp->lit_bytes_used;
                memcpy(pp->lit_bytes + off, path, len);
                pp->lit_bytes[off + len] = '\0';
                pp->lit_bytes_used += len + 1u;
                e->hash = h;
                e->path_off = off;
                e->path_len = (uint16_t)len;
                e->once = 0;
                e->guard_ident = 0;
                e->used = 1;
                result = e;
            }
            break;
        }
        if (e->hash == h && e->path_len == len && memcmp(pp->lit_bytes + e->path_off, path, len) == 0)
        {
            result = e;
            break;
        }
        idx = (idx + 1u) & mask;
    }
    return result;
}

static bool fast_pp_push_memory(FastPP* pp, const char* name, const uint8_t* bytes, uint64_t len, uint32_t dir_idx,
                                bool owned_mmap, bool is_system)
{
    bool ok = pp->file_depth < BUSTER_FPP_MAX_INCLUDES;
    if (!ok)
    {
        fast_pp_bail(pp, "include depth exceeded");
    }
    else
    {
        const uint32_t name_len = (uint32_t)strlen(name);
        char* saved = (char*)fast_arena_alloc(pp->arena, name_len + 1u, 1);
        if (saved == NULL)
        {
            fast_pp_bail(pp, "arena full for path");
            ok = false;
        }
        else
        {
            memcpy(saved, name, name_len + 1u);
            pp->files[pp->file_depth++] = (FastIncludeFile){
                .base = bytes,
                .ptr = bytes,
                .end = bytes + len,
                .map_size = len,
                .path = saved,
                .path_len = name_len,
                .line = 1u,
                .dir_idx = dir_idx,
                .cond_base = pp->cond_depth,
                .guard_ident = 0,
                .serial = ++pp->file_serial,
                .guard_state = 0,
                .at_bol = true,
                .owned_mmap = owned_mmap,
                .is_system = is_system,
            };
        }
    }
    return ok;
}

static void fast_pp_pop_file(FastPP* pp)
{
    if (pp->file_depth != 0)
    {
        FastIncludeFile* f = pp->files + (--pp->file_depth);
        if (f->cond_base != pp->cond_depth)
        {
            fast_pp_bail(pp, "unmatched #if in file");
        }
        if (f->guard_state == 4u && f->guard_ident != 0)
        {
            FastGuardEntry* ge = fast_guard_lookup(pp, f->path, f->path_len, true);
            if (ge != NULL)
            {
                ge->guard_ident = f->guard_ident;
            }
        }
        if (f->owned_mmap && f->base != NULL && f->map_size != 0)
        {
            (void)munmap((void*)(uintptr_t)f->base, f->map_size);
        }
    }
}

static bool fast_pp_open_and_push(FastPP* pp, const char* path, uint32_t path_len, uint32_t dir_idx, bool is_system)
{
    // Check include-guard cache BEFORE open()!
    const FastGuardEntry* ge = fast_guard_lookup(pp, path, path_len, false);
    if (ge != NULL && (ge->once != 0 || (ge->guard_ident != 0 && fast_ident_is_macro(&pp->idents, ge->guard_ident))))
    {
        return true; // Skipped with ZERO syscalls!
    }
    const int fd = open(path, O_RDONLY);
    bool ok = false;
    if (fd >= 0)
    {
        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode))
        {
            const uint64_t sz = (uint64_t)st.st_size;
            if (sz == 0)
            {
                static const uint8_t empty_byte = 0;
                ok = fast_pp_push_memory(pp, path, &empty_byte, 0, dir_idx, false, is_system);
            }
            else
            {
                void* mem = mmap(NULL, sz, PROT_READ, MAP_PRIVATE, fd, 0);
                if (mem != MAP_FAILED)
                {
                    ok = fast_pp_push_memory(pp, path, (const uint8_t*)mem, sz, dir_idx, true, is_system);
                    if (!ok)
                    {
                        (void)munmap(mem, sz);
                    }
                }
            }
        }
        (void)close(fd);
    }
    return ok;
}

static bool fast_pp_resolve_include(FastPP* pp, const char* name, uint32_t name_len, bool angled, bool is_next,
                                    char* out_path, uint32_t out_cap, uint32_t* out_dir_idx, bool* out_system)
{
    bool found = false;
    if (name_len > 0 && name[0] == '/')
    {
        if (name_len < out_cap)
        {
            memcpy(out_path, name, name_len);
            out_path[name_len] = '\0';
            if (access(out_path, R_OK) == 0)
            {
                *out_dir_idx = UINT32_MAX;
                *out_system = false;
                found = true;
            }
        }
        return found;
    }

    const FastIncludeFile* cur = pp->file_depth > 0 ? pp->files + (pp->file_depth - 1u) : NULL;
    uint32_t start_dir = 0;
    if (is_next && cur != NULL)
    {
        start_dir = cur->dir_idx == UINT32_MAX ? 0u : cur->dir_idx + 1u;
    }
    else
    {
        // Quoted include searches current file's directory first
        if (!angled && cur != NULL)
        {
            const char* slash = NULL;
            for (uint32_t i = cur->path_len; i > 0; i -= 1)
            {
                if (cur->path[i - 1u] == '/')
                {
                    slash = cur->path + i;
                    break;
                }
            }
            const uint32_t dlen = slash != NULL ? (uint32_t)(slash - cur->path) : 0u;
            if (dlen + name_len + 1u < out_cap)
            {
                if (dlen != 0)
                {
                    memcpy(out_path, cur->path, dlen);
                }
                memcpy(out_path + dlen, name, name_len);
                out_path[dlen + name_len] = '\0';
                const uint32_t total = dlen + name_len;
                if (fast_guard_lookup(pp, out_path, total, false) != NULL || access(out_path, R_OK) == 0)
                {
                    *out_dir_idx = UINT32_MAX;
                    *out_system = cur->is_system;
                    found = true;
                }
            }
        }
        start_dir = angled ? pp->angled_first : 0u;
    }

    for (uint32_t d = start_dir; !found && d < pp->dir_count; d += 1)
    {
        const uint32_t dlen = pp->dir_lens[d];
        if (dlen + 1u + name_len + 1u < out_cap)
        {
            memcpy(out_path, pp->dirs[d], dlen);
            out_path[dlen] = '/';
            memcpy(out_path + dlen + 1u, name, name_len);
            out_path[dlen + 1u + name_len] = '\0';
            const uint32_t total = dlen + 1u + name_len;
            if (fast_guard_lookup(pp, out_path, total, false) != NULL || access(out_path, R_OK) == 0)
            {
                *out_dir_idx = d;
                *out_system = pp->dir_is_system[d] != 0;
                found = true;
            }
        }
    }
    return found;
}

// ---------------------------------------------------------------------------------------------------------------------
// Macro expansion engine (iterative, supports object & function-like macros, #, ##, variadics, hide sets)

static inline bool fast_pp_is_hidden(const FastPP* pp, uint32_t id)
{
    bool hidden = false;
    for (uint32_t i = 0; i < pp->hide_count; i += 1)
    {
        if (pp->hide_ids[i] == id)
        {
            hidden = true;
            break;
        }
    }
    return hidden;
}

// Note: fast_pp_try_expand is strictly self-contained and iterative (NEVER calls fast_pp_expand_buffer -> ZERO cycle!).

// Read next raw token from top expansion frame (>= min_expand_depth) or current file (when allow_file).
static bool fast_pp_next_raw_for_args(FastPP* pp, FastTok* out_tok, bool stop_at_eol,
                                      uint32_t min_expand_depth, bool allow_file)
{
    bool got = false;
    while (pp->expand_depth > min_expand_depth)
    {
        FastExpandFrame* ef = pp->expand + (pp->expand_depth - 1u);
        if (ef->pos < ef->count)
        {
            *out_tok = ef->toks[ef->pos++];
            got = true;
            break;
        }
        // Pop exhausted expansion frame and its hide-set entry (do NOT rewind exp_used while collecting child args!)
        if (ef->macro_ident != 0 && pp->hide_count > 0)
        {
            pp->hide_count -= 1u;
        }
        pp->expand_depth -= 1u;
    }
    if (!got && allow_file && pp->file_depth > 0)
    {
        FastIncludeFile* f = pp->files + (pp->file_depth - 1u);
        fast_pp_read_tok(pp, f, stop_at_eol, out_tok);
        got = out_tok->kind != FTOK_EOF;
    }
    return got;
}

// Peek whether next non-space token is '(' without consuming it unless it IS '('!
static bool fast_pp_consume_lparen(FastPP* pp, bool stop_at_eol, uint32_t min_expand_depth, bool allow_file)
{
    bool found = false;
    // Check active expansion frames down to min_expand_depth first
    for (int32_t i = (int32_t)pp->expand_depth - 1; i >= (int32_t)min_expand_depth; i -= 1)
    {
        FastExpandFrame* ef = pp->expand + i;
        if (ef->pos < ef->count)
        {
            if (ef->toks[ef->pos].kind == FTOK_LPAREN)
            {
                ef->pos += 1u;
                found = true;
            }
            return found;
        }
    }
    if (allow_file && pp->file_depth > 0)
    {
        FastIncludeFile* f = pp->files + (pp->file_depth - 1u);
        const uint8_t* saved_ptr = f->ptr;
        const uint32_t saved_line = f->line;
        const bool saved_bol = f->at_bol;
        const uint32_t saved_generation = pp->win.generation;
        const uint32_t saved_pos = pp->win.pos;
        FastTok peek = { 0 };
        fast_pp_read_tok(pp, f, stop_at_eol, &peek);
        if (peek.kind == FTOK_LPAREN)
        {
            found = true;
        }
        else
        {
            // The token is given back: the file's position, and the window's place where the window is the same.
            f->ptr = saved_ptr;
            f->line = saved_line;
            f->at_bol = saved_bol;
            pp->win.pos = pp->win.generation == saved_generation ? saved_pos : 0u;
        }
    }
    return found;
}

// Format a FastTok back to text (for # stringification and ## pasting)
static uint32_t fast_tok_to_text(const FastPP* pp, FastTok tok, char* buf, uint32_t cap)
{
    uint32_t n = 0;
    if (tok.kind == FTOK_IDENT || (tok.kind >= 64u && tok.kind < 126u))
    {
        const FastIdentEntry* e = pp->idents.entries + tok.data;
        if (e->length < cap)
        {
            memcpy(buf, pp->idents.strings + e->str_offset, e->length);
            n = e->length;
        }
    }
    else if (tok.kind == FTOK_INT_LIT)
    {
        const uint64_t v = fast_tok_int_val(pp, tok);
        const char* suf = (tok.flags & BUSTER_FTOK_UNSIGNED) != 0 ?
                          ((tok.flags & BUSTER_FTOK_LONGLONG) != 0 ? "ULL" : (tok.flags & BUSTER_FTOK_LONG) != 0 ? "UL" : "U") :
                          ((tok.flags & BUSTER_FTOK_LONGLONG) != 0 ? "LL" : (tok.flags & BUSTER_FTOK_LONG) != 0 ? "L" : "");
        const int r = snprintf(buf, cap, "%llu%s", (unsigned long long)v, suf);
        n = r > 0 && (uint32_t)r < cap ? (uint32_t)r : 0u;
    }
    else if (tok.kind == FTOK_FLOAT_LIT)
    {
        const FastLitSlice sl = pp->lit_slices[tok.extra];
        if (sl.length < cap)
        {
            memcpy(buf, pp->lit_bytes + sl.offset, sl.length);
            n = sl.length;
        }
    }
    else if (tok.kind == FTOK_STR_LIT)
    {
        const FastLitSlice sl = pp->lit_slices[tok.data];
        if (sl.length + 2u < cap)
        {
            buf[n++] = '"';
            for (uint32_t i = 0; i < sl.length && n + 2u < cap; i += 1)
            {
                const char c = pp->lit_bytes[sl.offset + i];
                if (c == '"' || c == '\\') { buf[n++] = '\\'; }
                buf[n++] = c;
            }
            buf[n++] = '"';
        }
    }
    else if (tok.kind == FTOK_CHAR_LIT)
    {
        const int r = snprintf(buf, cap, "'%c'", (char)(tok.data & 0xffu));
        n = r > 0 && (uint32_t)r < cap ? (uint32_t)r : 0u;
    }
    else if (tok.kind == FTOK_PP_OTHER)
    {
        if (cap > 1u) { buf[n++] = (char)(tok.data & 0xffu); }
    }
    else
    {
        static const char* const puncts[] = {
            [FTOK_LPAREN] = "(", [FTOK_RPAREN] = ")", [FTOK_LBRACE] = "{", [FTOK_RBRACE] = "}",
            [FTOK_LBRACKET] = "[", [FTOK_RBRACKET] = "]", [FTOK_SEMI] = ";", [FTOK_COMMA] = ",",
            [FTOK_DOT] = ".", [FTOK_ARROW] = "->", [FTOK_ELLIPSIS] = "...", [FTOK_QUESTION] = "?",
            [FTOK_COLON] = ":", [FTOK_TILDE] = "~", [FTOK_BANG] = "!", [FTOK_INC] = "++",
            [FTOK_DEC] = "--", [FTOK_PLUS] = "+", [FTOK_MINUS] = "-", [FTOK_STAR] = "*",
            [FTOK_SLASH] = "/", [FTOK_PERCENT] = "%", [FTOK_AMP] = "&", [FTOK_PIPE] = "|",
            [FTOK_CARET] = "^", [FTOK_LSHIFT] = "<<", [FTOK_RSHIFT] = ">>", [FTOK_LT] = "<",
            [FTOK_LE] = "<=", [FTOK_GT] = ">", [FTOK_GE] = ">=", [FTOK_EQ] = "==", [FTOK_NE] = "!=",
            [FTOK_LAND] = "&&", [FTOK_LOR] = "||", [FTOK_ASSIGN] = "=", [FTOK_ADD_ASSIGN] = "+=",
            [FTOK_SUB_ASSIGN] = "-=", [FTOK_MUL_ASSIGN] = "*=", [FTOK_DIV_ASSIGN] = "/=",
            [FTOK_MOD_ASSIGN] = "%=", [FTOK_AND_ASSIGN] = "&=", [FTOK_OR_ASSIGN] = "|=",
            [FTOK_XOR_ASSIGN] = "^=", [FTOK_SHL_ASSIGN] = "<<=", [FTOK_SHR_ASSIGN] = ">>=",
            [FTOK_HASH] = "#", [FTOK_HASHHASH] = "##",
        };
        const char* s = tok.kind < (sizeof(puncts) / sizeof(puncts[0])) ? puncts[tok.kind] : NULL;
        if (s != NULL)
        {
            const uint32_t slen = (uint32_t)strlen(s);
            if (slen < cap) { memcpy(buf, s, slen); n = slen; }
        }
    }
    if (n < cap) { buf[n] = '\0'; }
    return n;
}

// Paste two tokens into one by concatenating their text and re-lexing
static __attribute__((noinline)) FastTok fast_pp_paste(FastPP* pp, FastTok lhs, FastTok rhs)
{
    char buf[512];
    const uint32_t n1 = fast_tok_to_text(pp, lhs, buf, sizeof(buf) / 2u);
    const uint32_t n2 = fast_tok_to_text(pp, rhs, buf + n1, sizeof(buf) - n1);
    const uint32_t total = n1 + n2;
    FastTok res = { 0 };
    if (total == 0)
    {
        res.kind = FTOK_EOF;
    }
    else
    {
        uint32_t dummy_line = 1u;
        bool dummy_bol = false;
        const uint8_t* p = (const uint8_t*)buf;
        const uint8_t* end = p + total;
        p = fast_lex_one(pp, p, end, &dummy_line, &dummy_bol, false, &res);
        if (p != end)
        {
            fast_pp_bail(pp, "invalid token paste");
        }
        res.flags = (uint8_t)(res.flags | (lhs.flags & BUSTER_FTOK_SPACE));
    }
    return res;
}

// Stringify a slice of tokens into a single FTOK_STR_LIT
static __attribute__((noinline)) FastTok fast_pp_stringify(FastPP* pp, const FastTok* toks, uint32_t count)
{
    char buf[2048];
    uint32_t n = 0;
    for (uint32_t i = 0; i < count && n + 8u < sizeof(buf); i += 1)
    {
        if (i > 0 && (toks[i].flags & BUSTER_FTOK_SPACE) != 0)
        {
            buf[n++] = ' ';
        }
        char tmp[512];
        const uint32_t tn = fast_tok_to_text(pp, toks[i], tmp, sizeof(tmp));
        if (n + tn < sizeof(buf))
        {
            memcpy(buf + n, tmp, tn);
            n += tn;
        }
        else
        {
            fast_pp_bail(pp, "stringify overflow");
            break;
        }
    }
    FastTok res = { 0 };
    res.kind = FTOK_STR_LIT;
    res.data = fast_pp_add_lit(pp, buf, n);
    return res;
}

// Expand builtin dynamic or object-like macro. Sets *out_is_func = true if tok is an active unhidden function-like macro.
static bool fast_pp_try_expand_nonfunc(FastPP* pp, FastTok tok, bool nested, FastTok* out_replacement, bool* out_is_func)
{
    *out_is_func = false;
    if ((tok.flags & BUSTER_FTOK_BLUE) != 0)
    {
        return false;
    }
    const uint32_t id = tok.data;
    const FastIdentEntry* ie = pp->idents.entries + id;

    // Builtin dynamic macros (__LINE__, __FILE__, __COUNTER__, __DATE__, __TIME__, etc.)
    if (ie->pp_kw >= FPP_KW_LINE_MACRO && ie->pp_kw <= FPP_KW_PRAGMA_OP)
    {
        const FastIncludeFile* cur = pp->file_depth > 0 ? pp->files + (pp->file_depth - 1u) : NULL;
        if (ie->pp_kw == FPP_KW_LINE_MACRO)
        {
            *out_replacement = (FastTok){ .kind = FTOK_INT_LIT, .flags = tok.flags, .data = cur != NULL ? cur->line : 1u };
            return true;
        }
        if (ie->pp_kw == FPP_KW_FILE_MACRO)
        {
            const char* fn = cur != NULL ? cur->path : "";
            *out_replacement = (FastTok){ .kind = FTOK_STR_LIT, .flags = tok.flags, .data = fast_pp_add_lit(pp, fn, (uint32_t)strlen(fn)) };
            return true;
        }
        if (ie->pp_kw == FPP_KW_BASE_FILE_MACRO)
        {
            const char* fn = pp->base_file != NULL ? pp->base_file : "";
            *out_replacement = (FastTok){ .kind = FTOK_STR_LIT, .flags = tok.flags, .data = fast_pp_add_lit(pp, fn, (uint32_t)strlen(fn)) };
            return true;
        }
        if (ie->pp_kw == FPP_KW_COUNTER_MACRO)
        {
            *out_replacement = (FastTok){ .kind = FTOK_INT_LIT, .flags = tok.flags, .data = pp->counter_val++ };
            return true;
        }
        if (ie->pp_kw == FPP_KW_INCLUDE_LEVEL_MACRO)
        {
            *out_replacement = (FastTok){ .kind = FTOK_INT_LIT, .flags = tok.flags, .data = pp->file_depth > 0 ? pp->file_depth - 1u : 0u };
            return true;
        }
        if (ie->pp_kw == FPP_KW_DATE_MACRO)
        {
            *out_replacement = (FastTok){ .kind = FTOK_STR_LIT, .flags = tok.flags, .data = fast_pp_add_lit(pp, "Nov 24 2026", 11u) };
            return true;
        }
        if (ie->pp_kw == FPP_KW_TIME_MACRO)
        {
            *out_replacement = (FastTok){ .kind = FTOK_STR_LIT, .flags = tok.flags, .data = fast_pp_add_lit(pp, "00:00:00", 8u) };
            return true;
        }
        if (ie->pp_kw == FPP_KW_PRAGMA_OP)
        {
            fast_pp_bail(pp, "_Pragma operator");
            return false;
        }
    }

    if (ie->macro_idx == 0 || fast_pp_is_hidden(pp, id))
    {
        return false;
    }
    const FastMacro* m = pp->macros + ie->macro_idx;
    if (m->active == 0)
    {
        return false;
    }
    if (m->is_func)
    {
        *out_is_func = true;
        return false;
    }

    // Object-like macro fast path
    if (m->body_count == 0)
    {
        out_replacement->kind = FTOK_EOF;
        return true;
    }
    if (m->body_count == 1u && !m->has_ops)
    {
        const FastTok bt = pp->macro_toks[m->body_start];
        if (bt.kind != FTOK_IDENT || !fast_ident_is_macro(&pp->idents, bt.data))
        {
            *out_replacement = bt;
            out_replacement->flags = (uint8_t)((bt.flags & ~BUSTER_FTOK_BOL) | (tok.flags & (BUSTER_FTOK_SPACE | BUSTER_FTOK_BOL)));
            return true;
        }
    }
    if (pp->expand_depth >= BUSTER_FPP_MAX_EXPAND || pp->hide_count >= BUSTER_FPP_MAX_HIDE)
    {
        fast_pp_bail(pp, "macro expand depth");
        return false;
    }
    if (!m->has_ops)
    {
        pp->hide_ids[pp->hide_count++] = id;
        pp->expand[pp->expand_depth++] = (FastExpandFrame){
            .toks = pp->macro_toks + m->body_start,
            .pos = 0,
            .count = m->body_count,
            .macro_ident = id,
            .saved_scratch = nested ? UINT32_MAX : pp->exp_used,
        };
        out_replacement->kind = FTOK_EOF;
        return true;
    }
    // Object-like macro with ##
    const uint32_t saved = pp->exp_used;
    const uint32_t out_start = pp->exp_used;
    for (uint32_t i = 0; i < m->body_count; i += 1)
    {
        const FastTok bt = pp->macro_toks[m->body_start + i];
        if (bt.kind == FTOK_HASHHASH && pp->exp_used > out_start && i + 1u < m->body_count)
        {
            pp->exp_buf[pp->exp_used - 1u] = fast_pp_paste(pp, pp->exp_buf[pp->exp_used - 1u], pp->macro_toks[m->body_start + i + 1u]);
            i += 1u;
        }
        else if (pp->exp_used < pp->exp_cap)
        {
            pp->exp_buf[pp->exp_used++] = bt;
        }
        else
        {
            fast_pp_bail(pp, "exp_buf overflow");
            break;
        }
    }
    pp->hide_ids[pp->hide_count++] = id;
    pp->expand[pp->expand_depth++] = (FastExpandFrame){
        .toks = pp->exp_buf + out_start,
        .pos = 0,
        .count = pp->exp_used - out_start,
        .macro_ident = id,
        .saved_scratch = nested ? UINT32_MAX : saved,
    };
    out_replacement->kind = FTOK_EOF;
    return true;
}

// Strictly iterative macro expansion (ZERO recursion, zero calls to fast_pp_expand_buffer!).
// Uses pp->call_stack + pp->arg_buf for nested function-like macro argument prescanning.
static BUSTER_DIRECT_HOT bool fast_pp_try_expand_ex(FastPP* pp, FastTok tok, bool stop_at_eol,
                                                    uint32_t min_expand_depth, bool allow_file, FastTok* out_replacement)
{
    bool is_func = false;
    if (fast_pp_try_expand_nonfunc(pp, tok, false, out_replacement, &is_func))
    {
        return true;
    }
    if (!is_func)
    {
        return false;
    }
    if (!fast_pp_consume_lparen(pp, stop_at_eol, min_expand_depth, allow_file))
    {
        return false;
    }

    uint32_t cdepth = 1u;
    FastMacroCall* root = pp->call_stack;
    root->macro_id = tok.data;
    root->saved_exp_used = pp->exp_used;
    root->saved_arg_used = pp->arg_used;
    root->min_expand_depth = min_expand_depth;
    root->tok_flags = tok.flags;
    root->state = 0;
    root->allow_file = allow_file;

    while (cdepth > 0 && !pp->bailout)
    {
        FastMacroCall* cf = pp->call_stack + (cdepth - 1u);
        const FastMacro* m = pp->macros + pp->idents.entries[cf->macro_id].macro_idx;
        const uint32_t max_named = m->param_count;
        const FastTok* const body = pp->macro_toks + m->body_start;
        const uint32_t bcount = m->body_count;

        if (cf->state == 0)
        {
            // Collect raw arguments into pp->exp_buf
            uint32_t arg_count = 0;
            uint32_t cur_start = pp->exp_used;
            uint32_t paren_depth = 1u;
            bool saw_any_tok = false;
            while (paren_depth > 0 && !pp->bailout)
            {
                FastTok at = { 0 };
                if (!fast_pp_next_raw_for_args(pp, &at, false, cf->min_expand_depth, cf->allow_file))
                {
                    fast_pp_bail(pp, "unterminated macro args");
                    break;
                }
                if (at.kind == FTOK_LPAREN)
                {
                    paren_depth += 1u;
                    if (pp->exp_used < pp->exp_cap) { pp->exp_buf[pp->exp_used++] = at; }
                    else { fast_pp_bail(pp, "exp_buf overflow"); break; }
                    saw_any_tok = true;
                }
                else if (at.kind == FTOK_RPAREN)
                {
                    paren_depth -= 1u;
                    if (paren_depth == 0)
                    {
                        if (max_named > 0 || m->is_variadic || saw_any_tok || arg_count > 0)
                        {
                            if (arg_count <= BUSTER_FPP_MAX_PARAMS)
                            {
                                cf->arg_starts[arg_count] = cur_start;
                                cf->arg_counts[arg_count] = pp->exp_used - cur_start;
                                arg_count += 1u;
                            }
                        }
                        break;
                    }
                    if (pp->exp_used < pp->exp_cap) { pp->exp_buf[pp->exp_used++] = at; }
                    else { fast_pp_bail(pp, "exp_buf overflow"); break; }
                    saw_any_tok = true;
                }
                else if (at.kind == FTOK_COMMA && paren_depth == 1u && (!m->is_variadic || arg_count < max_named))
                {
                    if (arg_count < BUSTER_FPP_MAX_PARAMS)
                    {
                        cf->arg_starts[arg_count] = cur_start;
                        cf->arg_counts[arg_count] = pp->exp_used - cur_start;
                        arg_count += 1u;
                        cur_start = pp->exp_used;
                        saw_any_tok = true;
                    }
                    else
                    {
                        fast_pp_bail(pp, "too many macro args");
                        break;
                    }
                }
                else
                {
                    if (pp->exp_used < pp->exp_cap) { pp->exp_buf[pp->exp_used++] = at; }
                    else { fast_pp_bail(pp, "exp_buf overflow"); break; }
                    saw_any_tok = true;
                }
            }
            if (pp->bailout) { break; }

            if (max_named == 0 && !m->is_variadic)
            {
                if (arg_count > 1u || (arg_count == 1u && cf->arg_counts[0] != 0))
                {
                    fast_pp_bail(pp, "macro arg count mismatch");
                    break;
                }
            }
            else if (!m->is_variadic && arg_count != max_named)
            {
                fast_pp_bail(pp, "macro arg count mismatch");
                break;
            }
            else if (m->is_variadic && arg_count < max_named)
            {
                fast_pp_bail(pp, "variadic macro too few args");
                break;
            }
            else if (m->is_variadic && arg_count == max_named)
            {
                cf->arg_starts[max_named] = pp->exp_used;
                cf->arg_counts[max_named] = 0;
                arg_count += 1u;
            }
            cf->arg_count = arg_count;
            cf->scan_body_idx = 0;
            if (arg_count > 0)
            {
                memset(cf->exp_arg_done, 0, arg_count);
                memset(cf->exp_arg_in_argbuf, 0, arg_count);
            }
            cf->state = 1u;
        }
        else if (cf->state == 1u)
        {
            // Scan body for next parameter requiring prescan
            bool started_prescan = false;
            while (cf->scan_body_idx < bcount)
            {
                const uint32_t i = cf->scan_body_idx++;
                const FastTok bt = body[i];
                if ((bt.kind == FTOK_IDENT || bt.kind >= 64u) && bt.extra != 0)
                {
                    const uint32_t pidx = (uint32_t)bt.extra - 1u;
                    const bool prev_op = i > 0 && (body[i - 1u].kind == FTOK_HASH || body[i - 1u].kind == FTOK_HASHHASH);
                    const bool next_paste = i + 1u < bcount && body[i + 1u].kind == FTOK_HASHHASH;
                    if (!prev_op && !next_paste && !cf->exp_arg_done[pidx])
                    {
                        cf->exp_arg_done[pidx] = 1u;
                        const uint32_t ac = cf->arg_counts[pidx];
                        if (ac == 0)
                        {
                            cf->exp_arg_starts[pidx] = cf->arg_starts[pidx];
                            cf->exp_arg_counts[pidx] = 0;
                            continue;
                        }
                        // Zero-copy fast path (~90% of macro args!): skip prescan if arg has NO active macro identifiers!
                        const FastTok* raw = pp->exp_buf + cf->arg_starts[pidx];
                        bool has_macro = false;
                        for (uint32_t k = 0; k < ac; k += 1)
                        {
                            const FastTok rt = raw[k];
                            if ((rt.kind == FTOK_IDENT || rt.kind >= 64u) &&
                                (rt.flags & BUSTER_FTOK_BLUE) == 0 &&
                                fast_ident_is_macro(&pp->idents, rt.data))
                            {
                                has_macro = true;
                                break;
                            }
                        }
                        if (!has_macro)
                        {
                            cf->exp_arg_starts[pidx] = cf->arg_starts[pidx];
                            cf->exp_arg_counts[pidx] = ac;
                            cf->exp_arg_in_argbuf[pidx] = 0;
                            continue;
                        }
                        if (pp->expand_depth >= BUSTER_FPP_MAX_EXPAND)
                        {
                            fast_pp_bail(pp, "arg prescan depth");
                            break;
                        }
                        cf->active_pidx = pidx;
                        cf->arg_out_start = pp->arg_used;
                        cf->base_expand_depth = pp->expand_depth;
                        pp->expand[pp->expand_depth++] = (FastExpandFrame){
                            .toks = raw,
                            .pos = 0,
                            .count = ac,
                            .macro_ident = 0,
                            .saved_scratch = UINT32_MAX,
                        };
                        cf->state = 2u;
                        started_prescan = true;
                        break;
                    }
                }
            }
            if (!started_prescan && !pp->bailout)
            {
                cf->state = 3u;
            }
        }
        else if (cf->state == 2u)
        {
            // Iteratively prescan active argument into pp->arg_buf
            bool pushed_child = false;
            while (pp->expand_depth > cf->base_expand_depth && !pp->bailout)
            {
                FastExpandFrame* ef = pp->expand + (pp->expand_depth - 1u);
                if (ef->pos >= ef->count)
                {
                    if (ef->macro_ident != 0 && pp->hide_count > 0)
                    {
                        pp->hide_count -= 1u;
                    }
                    pp->expand_depth -= 1u;
                    continue;
                }
                FastTok at = ef->toks[ef->pos++];
                if ((at.kind == FTOK_IDENT || at.kind >= 64u) && fast_ident_is_macro(&pp->idents, at.data))
                {
                    FastTok rep = { 0 };
                    bool child_is_func = false;
                    if (fast_pp_try_expand_nonfunc(pp, at, true, &rep, &child_is_func))
                    {
                        if (rep.kind != FTOK_EOF)
                        {
                            if (pp->arg_used < pp->arg_cap) { pp->arg_buf[pp->arg_used++] = rep; }
                            else { fast_pp_bail(pp, "arg_buf overflow"); break; }
                        }
                        continue;
                    }
                    if (child_is_func && fast_pp_consume_lparen(pp, false, cf->base_expand_depth, false))
                    {
                        if (cdepth >= BUSTER_FPP_MAX_CALLS)
                        {
                            fast_pp_bail(pp, "macro call depth");
                            break;
                        }
                        FastMacroCall* child = pp->call_stack + cdepth++;
                        child->macro_id = at.data;
                        child->saved_exp_used = pp->exp_used;
                        child->saved_arg_used = pp->arg_used;
                        child->min_expand_depth = cf->base_expand_depth;
                        child->tok_flags = at.flags;
                        child->state = 0;
                        child->allow_file = false;
                        pushed_child = true;
                        break;
                    }
                    if (fast_pp_is_hidden(pp, at.data))
                    {
                        at.flags |= BUSTER_FTOK_BLUE;
                    }
                }
                if (pp->arg_used < pp->arg_cap)
                {
                    pp->arg_buf[pp->arg_used++] = at;
                }
                else
                {
                    fast_pp_bail(pp, "arg_buf overflow");
                    break;
                }
            }
            if (!pushed_child && !pp->bailout)
            {
                const uint32_t pidx = cf->active_pidx;
                cf->exp_arg_in_argbuf[pidx] = 1u;
                cf->exp_arg_starts[pidx] = cf->arg_out_start;
                cf->exp_arg_counts[pidx] = pp->arg_used - cf->arg_out_start;
                cf->state = 1u;
            }
        }
        else
        {
            // state == 3: Substitute body into pp->exp_buf, restore pp->arg_used, and push substituted frame!
            const uint32_t subst_start = pp->exp_used;
            for (uint32_t i = 0; i < bcount && !pp->bailout; i += 1)
            {
                const FastTok bt = body[i];
                if (bt.kind == FTOK_HASH && i + 1u < bcount &&
                    (body[i + 1u].kind == FTOK_IDENT || body[i + 1u].kind >= 64u) && body[i + 1u].extra != 0)
                {
                    const uint32_t pidx = (uint32_t)body[i + 1u].extra - 1u;
                    const FastTok st = fast_pp_stringify(pp, pp->exp_buf + cf->arg_starts[pidx], cf->arg_counts[pidx]);
                    if (pp->exp_used < pp->exp_cap) { pp->exp_buf[pp->exp_used++] = st; }
                    else { fast_pp_bail(pp, "exp_buf overflow"); break; }
                    i += 1u;
                    continue;
                }
                if (bt.kind == FTOK_HASHHASH && i + 1u < bcount)
                {
                    const FastTok next = body[i + 1u];
                    const bool next_is_param = (next.kind == FTOK_IDENT || next.kind >= 64u) && next.extra != 0;
                    if (m->is_variadic && next_is_param && next.extra == (uint16_t)(max_named + 1u) && cf->arg_counts[max_named] == 0)
                    {
                        if (pp->exp_used > subst_start && pp->exp_buf[pp->exp_used - 1u].kind == FTOK_COMMA)
                        {
                            pp->exp_used -= 1u;
                        }
                        i += 1u;
                        continue;
                    }
                    if (next_is_param)
                    {
                        const uint32_t pidx = (uint32_t)next.extra - 1u;
                        const uint32_t rc = cf->arg_counts[pidx];
                        if (rc > 0)
                        {
                            const FastTok* rt = pp->exp_buf + cf->arg_starts[pidx];
                            if (pp->exp_used > subst_start)
                            {
                                pp->exp_buf[pp->exp_used - 1u] = fast_pp_paste(pp, pp->exp_buf[pp->exp_used - 1u], rt[0]);
                                for (uint32_t k = 1u; k < rc; k += 1)
                                {
                                    if (pp->exp_used < pp->exp_cap) { pp->exp_buf[pp->exp_used++] = rt[k]; }
                                    else { fast_pp_bail(pp, "exp_buf overflow"); break; }
                                }
                            }
                            else
                            {
                                for (uint32_t k = 0; k < rc; k += 1)
                                {
                                    if (pp->exp_used < pp->exp_cap) { pp->exp_buf[pp->exp_used++] = rt[k]; }
                                    else { fast_pp_bail(pp, "exp_buf overflow"); break; }
                                }
                            }
                        }
                    }
                    else if (pp->exp_used > subst_start)
                    {
                        pp->exp_buf[pp->exp_used - 1u] = fast_pp_paste(pp, pp->exp_buf[pp->exp_used - 1u], next);
                    }
                    else if (pp->exp_used < pp->exp_cap)
                    {
                        pp->exp_buf[pp->exp_used++] = next;
                    }
                    i += 1u;
                    continue;
                }
                if ((bt.kind == FTOK_IDENT || bt.kind >= 64u) && bt.extra != 0)
                {
                    const uint32_t pidx = (uint32_t)bt.extra - 1u;
                    const bool next_paste = i + 1u < bcount && body[i + 1u].kind == FTOK_HASHHASH;
                    const FastTok* src_arr = next_paste ? (pp->exp_buf + cf->arg_starts[pidx]) :
                                             ((cf->exp_arg_in_argbuf[pidx] != 0 ? pp->arg_buf : pp->exp_buf) + cf->exp_arg_starts[pidx]);
                    const uint32_t src_cnt = next_paste ? cf->arg_counts[pidx] : cf->exp_arg_counts[pidx];
                    if (pp->exp_used + src_cnt <= pp->exp_cap)
                    {
                        for (uint32_t k = 0; k < src_cnt; k += 1)
                        {
                            FastTok at = src_arr[k];
                            if (k == 0) { at.flags = (uint8_t)((at.flags & ~BUSTER_FTOK_SPACE) | (bt.flags & BUSTER_FTOK_SPACE)); }
                            pp->exp_buf[pp->exp_used++] = at;
                        }
                    }
                    else
                    {
                        fast_pp_bail(pp, "exp_buf overflow");
                        break;
                    }
                    continue;
                }
                if (pp->exp_used < pp->exp_cap)
                {
                    FastTok ot = bt;
                    if (ot.kind == FTOK_IDENT || ot.kind >= 64u) { ot.extra = 0; }
                    pp->exp_buf[pp->exp_used++] = ot;
                }
                else
                {
                    fast_pp_bail(pp, "exp_buf overflow");
                    break;
                }
            }
            pp->arg_used = cf->saved_arg_used;
            if (!pp->bailout && pp->expand_depth < BUSTER_FPP_MAX_EXPAND && pp->hide_count < BUSTER_FPP_MAX_HIDE)
            {
                if (pp->exp_used > subst_start)
                {
                    pp->exp_buf[subst_start].flags = (uint8_t)(pp->exp_buf[subst_start].flags | (cf->tok_flags & (BUSTER_FTOK_SPACE | BUSTER_FTOK_BOL)));
                }
                pp->hide_ids[pp->hide_count++] = cf->macro_id;
                pp->expand[pp->expand_depth++] = (FastExpandFrame){
                    .toks = pp->exp_buf + subst_start,
                    .pos = 0,
                    .count = pp->exp_used - subst_start,
                    .macro_ident = cf->macro_id,
                    .saved_scratch = cdepth == 1u ? cf->saved_exp_used : UINT32_MAX,
                };
            }
            else if (!pp->bailout)
            {
                fast_pp_bail(pp, "expand stack full");
            }
            cdepth -= 1u;
        }
    }

    if (pp->bailout)
    {
        pp->arg_used = root->saved_arg_used;
        return false;
    }
    out_replacement->kind = FTOK_EOF;
    return true;
}

static inline bool fast_pp_try_expand(FastPP* pp, FastTok tok, bool stop_at_eol, FastTok* out_replacement)
{
    return fast_pp_try_expand_ex(pp, tok, stop_at_eol, 0u, true, out_replacement);
}

// Expand a token slice `in_toks[0..in_count]` into `out_toks[0..out_cap]`.
// Used for prescanning macro arguments AND for expanding `#if` / `#elif` expressions!
static bool fast_pp_expand_buffer(FastPP* pp, const FastTok* in_toks, uint32_t in_count,
                                  FastTok* out_toks, uint32_t out_cap, uint32_t* out_count, bool eval_defined)
{
    const uint32_t base_depth = pp->expand_depth;
    if (pp->expand_depth >= BUSTER_FPP_MAX_EXPAND)
    {
        fast_pp_bail(pp, "expand_buffer depth");
        *out_count = 0;
        return false;
    }
    pp->expand[pp->expand_depth++] = (FastExpandFrame){
        .toks = in_toks,
        .pos = 0,
        .count = in_count,
        .macro_ident = 0,
        .saved_scratch = pp->exp_used,
    };

    uint32_t oc = 0;
    while (pp->expand_depth > base_depth && !pp->bailout)
    {
        FastExpandFrame* ef = pp->expand + (pp->expand_depth - 1u);
        if (ef->pos >= ef->count)
        {
            if (ef->macro_ident != 0 && pp->hide_count > 0)
            {
                pp->hide_count -= 1u;
            }
            pp->expand_depth -= 1u;
            if (pp->expand_depth == 0) { pp->exp_used = 0; }
            else if (ef->saved_scratch != UINT32_MAX && ef->saved_scratch < pp->exp_used) { pp->exp_used = ef->saved_scratch; }
            continue;
        }
        FastTok tok = ef->toks[ef->pos++];
        if (eval_defined && (tok.kind == FTOK_IDENT || tok.kind >= 64u))
        {
            const uint8_t pkw = pp->idents.entries[tok.data].pp_kw;
            if (pkw == FPP_KW_DEFINED)
            {
                // Read X or (X) without macro expansion
                FastTok nt = { 0 };
                bool has_paren = false;
                if (ef->pos < ef->count) { nt = ef->toks[ef->pos++]; }
                if (nt.kind == FTOK_LPAREN)
                {
                    has_paren = true;
                    nt.kind = FTOK_EOF;
                    if (ef->pos < ef->count) { nt = ef->toks[ef->pos++]; }
                }
                uint32_t is_def = 0;
                if (nt.kind == FTOK_IDENT || (nt.kind >= 64u && nt.kind < 126u))
                {
                    is_def = fast_ident_is_macro(&pp->idents, nt.data) ? 1u : 0u;
                }
                else
                {
                    fast_pp_bail(pp, "bad defined()");
                }
                if (has_paren)
                {
                    if (ef->pos >= ef->count || ef->toks[ef->pos].kind != FTOK_RPAREN)
                    {
                        fast_pp_bail(pp, "missing ) in defined");
                    }
                    else
                    {
                        ef->pos += 1u;
                    }
                }
                if (oc < out_cap) { out_toks[oc++] = (FastTok){ .kind = FTOK_INT_LIT, .data = is_def }; }
                continue;
            }
            if (pkw == FPP_KW_HAS_BUILTIN || pkw == FPP_KW_HAS_ATTRIBUTE || pkw == FPP_KW_HAS_FEATURE ||
                pkw == FPP_KW_HAS_EXTENSION || pkw == FPP_KW_HAS_C_ATTRIBUTE || pkw == FPP_KW_HAS_INCLUDE ||
                pkw == FPP_KW_HAS_INCLUDE_NEXT || pkw == FPP_KW_BUILDING_MODULE || pkw == FPP_KW_HAS_WARNING ||
                pkw == FPP_KW_IS_IDENTIFIER)
            {
                // Parse (...) argument for __has_*
                uint32_t val = 0;
                if (ef->pos < ef->count && ef->toks[ef->pos].kind == FTOK_LPAREN)
                {
                    ef->pos += 1u;
                    const uint32_t arg_start = ef->pos;
                    uint32_t pdepth = 1u;
                    while (pdepth > 0 && ef->pos < ef->count)
                    {
                        const uint8_t k = ef->toks[ef->pos++].kind;
                        if (k == FTOK_LPAREN) { pdepth += 1u; }
                        else if (k == FTOK_RPAREN) { pdepth -= 1u; }
                    }
                    const uint32_t arg_end = ef->pos > arg_start ? ef->pos - 1u : arg_start;
                    const FastTok arg = arg_start < arg_end ? ef->toks[arg_start] : (FastTok){ 0 };
                    if (pkw == FPP_KW_HAS_BUILTIN && (arg.kind == FTOK_IDENT || arg.kind >= 64u))
                    {
                        const FastIdentEntry* ae = pp->idents.entries + arg.data;
                        const char* s = pp->idents.strings + ae->str_offset;
                        if (strcmp(s, "__builtin_clzll") == 0 || strcmp(s, "__builtin_offsetof") == 0 ||
                            strcmp(s, "__builtin_va_start") == 0 || strcmp(s, "__builtin_va_arg") == 0 ||
                            strcmp(s, "__builtin_va_end") == 0 || strcmp(s, "__builtin_va_copy") == 0 ||
                            strcmp(s, "__builtin_inff") == 0 || strcmp(s, "__builtin_expect") == 0 ||
                            strcmp(s, "__builtin_constant_p") == 0 || strcmp(s, "__builtin_unreachable") == 0)
                        {
                            val = 1u;
                        }
                    }
                    else if (pkw == FPP_KW_HAS_ATTRIBUTE && (arg.kind == FTOK_IDENT || arg.kind >= 64u))
                    {
                        const FastIdentEntry* ae = pp->idents.entries + arg.data;
                        const char* s = pp->idents.strings + ae->str_offset;
                        if (strcmp(s, "noinline") == 0 || strcmp(s, "__noinline__") == 0 ||
                            strcmp(s, "always_inline") == 0 || strcmp(s, "__always_inline__") == 0 ||
                            strcmp(s, "unused") == 0 || strcmp(s, "__unused__") == 0 ||
                            strcmp(s, "packed") == 0 || strcmp(s, "__packed__") == 0 ||
                            strcmp(s, "aligned") == 0 || strcmp(s, "__aligned__") == 0 ||
                            strcmp(s, "fallthrough") == 0 || strcmp(s, "__fallthrough__") == 0 ||
                            strcmp(s, "noreturn") == 0 || strcmp(s, "__noreturn__") == 0 ||
                            strcmp(s, "format") == 0 || strcmp(s, "__format__") == 0 ||
                            strcmp(s, "nonnull") == 0 || strcmp(s, "__nonnull__") == 0 ||
                            strcmp(s, "pure") == 0 || strcmp(s, "__pure__") == 0 ||
                            strcmp(s, "const") == 0 || strcmp(s, "__const__") == 0 ||
                            strcmp(s, "malloc") == 0 || strcmp(s, "__malloc__") == 0 ||
                            strcmp(s, "deprecated") == 0 || strcmp(s, "__deprecated__") == 0 ||
                            strcmp(s, "visibility") == 0 || strcmp(s, "__visibility__") == 0 ||
                            strcmp(s, "weak") == 0 || strcmp(s, "__weak__") == 0 ||
                            strcmp(s, "alias") == 0 || strcmp(s, "__alias__") == 0)
                        {
                            val = 1u;
                        }
                    }
                    else if (pkw == FPP_KW_HAS_INCLUDE || pkw == FPP_KW_HAS_INCLUDE_NEXT)
                    {
                        char hname[512];
                        uint32_t hlen = 0;
                        bool angled = false;
                        if (arg.kind == FTOK_STR_LIT)
                        {
                            const FastLitSlice sl = pp->lit_slices[arg.data];
                            if (sl.length < sizeof(hname))
                            {
                                memcpy(hname, pp->lit_bytes + sl.offset, sl.length);
                                hlen = sl.length;
                                hname[hlen] = '\0';
                            }
                        }
                        else if (arg_start + 2u <= arg_end && arg.kind == FTOK_LT && ef->toks[arg_end - 1u].kind == FTOK_GT)
                        {
                            angled = true;
                            for (uint32_t hi = arg_start + 1u; hi + 1u < arg_end && hlen + 8u < sizeof(hname); hi += 1)
                            {
                                hlen += fast_tok_to_text(pp, ef->toks[hi], hname + hlen, sizeof(hname) - hlen);
                            }
                            hname[hlen] = '\0';
                        }
                        if (hlen > 0)
                        {
                            char tmp_path[1024];
                            uint32_t d = 0;
                            bool sys = false;
                            val = fast_pp_resolve_include(pp, hname, hlen, angled,
                                                          pkw == FPP_KW_HAS_INCLUDE_NEXT, tmp_path, sizeof(tmp_path), &d, &sys) ? 1u : 0u;
                        }
                    }
                }
                if (oc < out_cap) { out_toks[oc++] = (FastTok){ .kind = FTOK_INT_LIT, .data = val }; }
                continue;
            }
        }
        if ((tok.kind == FTOK_IDENT || tok.kind >= 64u) && fast_ident_is_macro(&pp->idents, tok.data))
        {
            FastTok rep = { 0 };
            if (fast_pp_try_expand_ex(pp, tok, false, base_depth, false, &rep))
            {
                if (rep.kind != FTOK_EOF && oc < out_cap)
                {
                    out_toks[oc++] = rep;
                }
                continue;
            }
            if (fast_pp_is_hidden(pp, tok.data))
            {
                tok.flags |= BUSTER_FTOK_BLUE;
            }
        }
        if (oc < out_cap)
        {
            out_toks[oc++] = tok;
        }
        else
        {
            fast_pp_bail(pp, "expand_buffer overflow");
            break;
        }
    }
    *out_count = oc;
    return !pp->bailout;
}

// ---------------------------------------------------------------------------------------------------------------------
// Iterative #if / #elif 64-bit integer constant expression evaluator (ZERO recursion!)

typedef struct FastPPVal FastPPVal;
struct FastPPVal
{
    uint64_t val;
    bool is_unsigned;
};

static uint8_t fast_pp_prec(uint8_t kind)
{
    uint8_t p = 0;
    switch (kind)
    {
        case FTOK_QUESTION: p = 1; break;
        case FTOK_LOR:      p = 2; break;
        case FTOK_LAND:     p = 3; break;
        case FTOK_PIPE:     p = 4; break;
        case FTOK_CARET:    p = 5; break;
        case FTOK_AMP:      p = 6; break;
        case FTOK_EQ: case FTOK_NE: p = 7; break;
        case FTOK_LT: case FTOK_LE: case FTOK_GT: case FTOK_GE: p = 8; break;
        case FTOK_LSHIFT: case FTOK_RSHIFT: p = 9; break;
        case FTOK_PLUS: case FTOK_MINUS: p = 10; break;
        case FTOK_STAR: case FTOK_SLASH: case FTOK_PERCENT: p = 11; break;
        default: p = 0; break;
    }
    return p;
}

#define BUSTER_FPP_OP_UNARY_PLUS  128u
#define BUSTER_FPP_OP_UNARY_MINUS 129u
#define BUSTER_FPP_OP_UNARY_TILDE 130u
#define BUSTER_FPP_OP_UNARY_BANG  131u
#define BUSTER_FPP_OP_LPAREN      132u
#define BUSTER_FPP_OP_TERNARY     133u

static void fast_pp_apply_op(FastPP* pp, uint16_t op, FastPPVal* vals, uint32_t* vsp)
{
    if (op >= BUSTER_FPP_OP_UNARY_PLUS && op <= BUSTER_FPP_OP_UNARY_BANG)
    {
        if (*vsp < 1u) { fast_pp_bail(pp, "pp expr underflow"); return; }
        FastPPVal* a = vals + (*vsp - 1u);
        if (op == BUSTER_FPP_OP_UNARY_MINUS) { a->val = (uint64_t)(-(int64_t)a->val); }
        else if (op == BUSTER_FPP_OP_UNARY_TILDE) { a->val = ~a->val; }
        else if (op == BUSTER_FPP_OP_UNARY_BANG) { a->val = a->val == 0 ? 1u : 0u; a->is_unsigned = false; }
        return;
    }
    if (op == BUSTER_FPP_OP_TERNARY)
    {
        if (*vsp < 3u) { fast_pp_bail(pp, "pp ternary underflow"); return; }
        const FastPPVal f = vals[--(*vsp)];
        const FastPPVal t = vals[--(*vsp)];
        const FastPPVal c = vals[--(*vsp)];
        const bool u = t.is_unsigned || f.is_unsigned;
        vals[(*vsp)++] = (FastPPVal){ .val = c.val != 0 ? t.val : f.val, .is_unsigned = u };
        return;
    }
    if (*vsp < 2u) { fast_pp_bail(pp, "pp binop underflow"); return; }
    const FastPPVal b = vals[--(*vsp)];
    const FastPPVal a = vals[--(*vsp)];
    const bool u = a.is_unsigned || b.is_unsigned;
    FastPPVal r = { .val = 0, .is_unsigned = u };
    switch (op)
    {
        case FTOK_LOR:    r.val = (a.val != 0 || b.val != 0) ? 1u : 0u; r.is_unsigned = false; break;
        case FTOK_LAND:   r.val = (a.val != 0 && b.val != 0) ? 1u : 0u; r.is_unsigned = false; break;
        case FTOK_PIPE:   r.val = a.val | b.val; break;
        case FTOK_CARET:  r.val = a.val ^ b.val; break;
        case FTOK_AMP:    r.val = a.val & b.val; break;
        case FTOK_EQ:     r.val = a.val == b.val ? 1u : 0u; r.is_unsigned = false; break;
        case FTOK_NE:     r.val = a.val != b.val ? 1u : 0u; r.is_unsigned = false; break;
        case FTOK_LT:     r.val = (u ? (a.val < b.val) : ((int64_t)a.val < (int64_t)b.val)) ? 1u : 0u; r.is_unsigned = false; break;
        case FTOK_LE:     r.val = (u ? (a.val <= b.val) : ((int64_t)a.val <= (int64_t)b.val)) ? 1u : 0u; r.is_unsigned = false; break;
        case FTOK_GT:     r.val = (u ? (a.val > b.val) : ((int64_t)a.val > (int64_t)b.val)) ? 1u : 0u; r.is_unsigned = false; break;
        case FTOK_GE:     r.val = (u ? (a.val >= b.val) : ((int64_t)a.val >= (int64_t)b.val)) ? 1u : 0u; r.is_unsigned = false; break;
        case FTOK_LSHIFT: r.val = a.val << (b.val & 63u); r.is_unsigned = a.is_unsigned; break;
        case FTOK_RSHIFT: r.val = a.is_unsigned ? (a.val >> (b.val & 63u)) : (uint64_t)((int64_t)a.val >> (b.val & 63u)); r.is_unsigned = a.is_unsigned; break;
        case FTOK_PLUS:   r.val = a.val + b.val; break;
        case FTOK_MINUS:  r.val = a.val - b.val; break;
        case FTOK_STAR:   r.val = a.val * b.val; break;
        case FTOK_SLASH:
            if (b.val == 0) { fast_pp_bail(pp, "pp div by zero"); }
            else { r.val = u ? (a.val / b.val) : (uint64_t)((int64_t)a.val / (int64_t)b.val); }
            break;
        case FTOK_PERCENT:
            if (b.val == 0) { fast_pp_bail(pp, "pp mod by zero"); }
            else { r.val = u ? (a.val % b.val) : (uint64_t)((int64_t)a.val % (int64_t)b.val); }
            break;
        default:
            fast_pp_bail(pp, "pp unknown op");
            break;
    }
    vals[(*vsp)++] = r;
}

static bool fast_pp_eval_if_expr(FastPP* pp, const FastTok* toks, uint32_t count)
{
    FastPPVal vals[128];
    uint16_t ops[128];
    uint32_t vsp = 0;
    uint32_t osp = 0;
    bool expect_operand = true;
    uint32_t skip_depth = 0; // non-zero when short-circuiting inactive RHS of &&, ||, ?:

    for (uint32_t i = 0; i < count && !pp->bailout; i += 1)
    {
        const FastTok t = toks[i];
        if (expect_operand)
        {
            if (t.kind == FTOK_LPAREN)
            {
                if (osp < 128u) { ops[osp++] = BUSTER_FPP_OP_LPAREN; }
                else { fast_pp_bail(pp, "pp op stack"); }
            }
            else if (t.kind == FTOK_PLUS)  { if (osp < 128u) { ops[osp++] = BUSTER_FPP_OP_UNARY_PLUS; } }
            else if (t.kind == FTOK_MINUS) { if (osp < 128u) { ops[osp++] = BUSTER_FPP_OP_UNARY_MINUS; } }
            else if (t.kind == FTOK_TILDE) { if (osp < 128u) { ops[osp++] = BUSTER_FPP_OP_UNARY_TILDE; } }
            else if (t.kind == FTOK_BANG)  { if (osp < 128u) { ops[osp++] = BUSTER_FPP_OP_UNARY_BANG; } }
            else if (t.kind == FTOK_INT_LIT)
            {
                const uint64_t v = fast_tok_int_val(pp, t);
                const bool u = (t.flags & BUSTER_FTOK_UNSIGNED) != 0 || ((t.flags & BUSTER_FTOK_HEX) != 0 && v > (uint64_t)INT64_MAX);
                if (vsp < 128u) { vals[vsp++] = (FastPPVal){ .val = v, .is_unsigned = u }; }
                expect_operand = false;
            }
            else if (t.kind == FTOK_CHAR_LIT)
            {
                if (vsp < 128u) { vals[vsp++] = (FastPPVal){ .val = (uint64_t)(int64_t)(int32_t)t.data, .is_unsigned = false }; }
                expect_operand = false;
            }
            else if (t.kind == FTOK_IDENT || (t.kind >= 64u && t.kind < 126u))
            {
                // Unexpanded identifier in #if evaluates to 0 (true -> 1 in C23)
                const uint64_t v = (pp->c23 && strcmp(pp->idents.strings + pp->idents.entries[t.data].str_offset, "true") == 0) ? 1u : 0u;
                if (vsp < 128u) { vals[vsp++] = (FastPPVal){ .val = v, .is_unsigned = false }; }
                expect_operand = false;
            }
            else
            {
                fast_pp_bail(pp, "pp unexpected operand");
            }
        }
        else
        {
            if (t.kind == FTOK_RPAREN)
            {
                while (osp > 0 && ops[osp - 1u] != BUSTER_FPP_OP_LPAREN)
                {
                    fast_pp_apply_op(pp, ops[--osp], vals, &vsp);
                }
                if (osp == 0) { fast_pp_bail(pp, "pp unmatched )"); break; }
                osp -= 1u;
            }
            else if (t.kind == FTOK_COLON)
            {
                while (osp > 0 && ops[osp - 1u] != FTOK_QUESTION && ops[osp - 1u] != BUSTER_FPP_OP_LPAREN)
                {
                    fast_pp_apply_op(pp, ops[--osp], vals, &vsp);
                }
                if (osp == 0 || ops[osp - 1u] != FTOK_QUESTION) { fast_pp_bail(pp, "pp unmatched :"); break; }
                ops[osp - 1u] = BUSTER_FPP_OP_TERNARY;
                expect_operand = true;
            }
            else if (t.kind == FTOK_COMMA)
            {
                while (osp > 0 && ops[osp - 1u] != BUSTER_FPP_OP_LPAREN)
                {
                    fast_pp_apply_op(pp, ops[--osp], vals, &vsp);
                }
                if (vsp > 0) { vsp -= 1u; }
                expect_operand = true;
            }
            else
            {
                const uint8_t prec = fast_pp_prec(t.kind);
                if (prec == 0)
                {
                    fast_pp_bail(pp, "pp unexpected operator");
                    break;
                }
                while (osp > 0 && ops[osp - 1u] != BUSTER_FPP_OP_LPAREN)
                {
                    const uint16_t top = ops[osp - 1u];
                    const uint8_t tprec = (top >= BUSTER_FPP_OP_UNARY_PLUS && top <= BUSTER_FPP_OP_UNARY_BANG) ? 12u :
                                          top == BUSTER_FPP_OP_TERNARY ? 1u : fast_pp_prec((uint8_t)top);
                    const bool right_assoc = (t.kind == FTOK_QUESTION);
                    if (tprec > prec || (tprec == prec && !right_assoc))
                    {
                        fast_pp_apply_op(pp, ops[--osp], vals, &vsp);
                    }
                    else
                    {
                        break;
                    }
                }
                if (osp < 128u) { ops[osp++] = t.kind; }
                expect_operand = true;
            }
        }
    }
    (void)skip_depth;
    while (osp > 0 && !pp->bailout)
    {
        if (ops[osp - 1u] == BUSTER_FPP_OP_LPAREN || ops[osp - 1u] == FTOK_QUESTION)
        {
            fast_pp_bail(pp, "pp unclosed ( or ?");
            break;
        }
        fast_pp_apply_op(pp, ops[--osp], vals, &vsp);
    }
    if (vsp != 1u && !pp->bailout)
    {
        fast_pp_bail(pp, "pp bad expr result");
    }
    return !pp->bailout && vsp == 1u && vals[0].val != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Directive handling

static void fast_pp_skip_to_eol(FastIncludeFile* f)
{
    const uint8_t* p = f->ptr;
    const uint8_t* const end = f->end;
    uint32_t line = f->line;
    while (p < end)
    {
        if (*p == '\n')
        {
            p += 1;
            line += 1u;
            break;
        }
        if (*p == '\\' && p + 1 < end && (p[1] == '\n' || (p[1] == '\r' && p + 2 < end && p[2] == '\n')))
        {
            p += p[1] == '\r' ? 3 : 2;
            line += 1u;
            continue;
        }
        if (*p == '/' && p + 1 < end && p[1] == '*')
        {
            const uint8_t* next = fast_skip_block_comment(p + 2, end, &line);
            p = next != NULL ? next : end;
            continue;
        }
        p += 1;
    }
    f->ptr = p;
    f->line = line;
    f->at_bol = true;
}

static __attribute__((noinline)) void fast_pp_handle_define(FastPP* pp, FastIncludeFile* f)
{
    FastTok name_tok = { 0 };
    fast_pp_read_tok(pp, f, true, &name_tok);
    if (name_tok.kind != FTOK_IDENT && !(name_tok.kind >= 64u && name_tok.kind < 126u))
    {
        fast_pp_bail(pp, "bad #define name");
        return;
    }
    const uint32_t name_id = name_tok.data;
    if (f->guard_state == 1u && name_id == f->guard_ident)
    {
        f->guard_state = 2u; // Confirmed #ifndef GUARD / #define GUARD!
    }
    if (pp->macro_count + 1u >= BUSTER_FPP_MAX_MACROS)
    {
        fast_pp_bail(pp, "too many macros");
        return;
    }

    const bool is_func = f->ptr < f->end && *f->ptr == '(';
    uint32_t params[BUSTER_FPP_MAX_PARAMS];
    uint16_t param_count = 0;
    bool is_variadic = false;
    uint32_t vararg_name = 0;

    if (is_func)
    {
        f->ptr += 1; // skip '('
        FastTok pt = { 0 };
        fast_pp_read_tok(pp, f, true, &pt);
        while (pt.kind != FTOK_RPAREN && pt.kind != FTOK_EOF && !pp->bailout)
        {
            if (pt.kind == FTOK_ELLIPSIS)
            {
                is_variadic = true;
                fast_pp_read_tok(pp, f, true, &pt);
                break;
            }
            if (pt.kind == FTOK_IDENT || (pt.kind >= 64u && pt.kind < 126u))
            {
                const uint32_t pid = pt.data;
                fast_pp_read_tok(pp, f, true, &pt);
                if (pt.kind == FTOK_ELLIPSIS)
                {
                    is_variadic = true;
                    vararg_name = pid;
                    fast_pp_read_tok(pp, f, true, &pt);
                    break;
                }
                if (param_count < BUSTER_FPP_MAX_PARAMS)
                {
                    params[param_count++] = pid;
                }
                else
                {
                    fast_pp_bail(pp, "too many macro params");
                }
                if (pt.kind == FTOK_COMMA)
                {
                    fast_pp_read_tok(pp, f, true, &pt);
                }
            }
            else
            {
                fast_pp_bail(pp, "bad macro param");
                break;
            }
        }
        if (pt.kind != FTOK_RPAREN)
        {
            fast_pp_bail(pp, "unterminated macro params");
            return;
        }
    }

    const uint32_t body_start = pp->macro_tok_count;
    bool has_ops = false;
    bool first = true;
    for (;;)
    {
        FastTok bt = { 0 };
        fast_pp_read_tok(pp, f, true, &bt);
        if (bt.kind == FTOK_EOF)
        {
            break;
        }
        if (first)
        {
            bt.flags &= (uint8_t)~(BUSTER_FTOK_SPACE | BUSTER_FTOK_BOL);
            first = false;
        }
        if (bt.kind == FTOK_HASH || bt.kind == FTOK_HASHHASH)
        {
            has_ops = true;
        }
        if (is_func && (bt.kind == FTOK_IDENT || (bt.kind >= 64u && bt.kind < 126u)))
        {
            bt.extra = 0;
            if (is_variadic && (pp->idents.entries[bt.data].pp_kw == FPP_KW_VA_ARGS ||
                                (vararg_name != 0 && bt.data == vararg_name)))
            {
                bt.extra = (uint16_t)(param_count + 1u);
            }
            else if (is_variadic && pp->idents.entries[bt.data].pp_kw == FPP_KW_VA_OPT)
            {
                fast_pp_bail(pp, "__VA_OPT__");
                break;
            }
            else
            {
                for (uint16_t k = 0; k < param_count; k += 1)
                {
                    if (params[k] == bt.data)
                    {
                        bt.extra = (uint16_t)(k + 1u);
                        break;
                    }
                }
            }
        }
        if (pp->macro_tok_count < pp->macro_tok_cap)
        {
            pp->macro_toks[pp->macro_tok_count++] = bt;
        }
        else
        {
            fast_pp_bail(pp, "macro_toks overflow");
            break;
        }
    }
    fast_pp_skip_to_eol(f);

    const uint32_t midx = ++pp->macro_count;
    const uint32_t bcnt = pp->macro_tok_count - body_start;
    uint8_t fmode = 0;
    uint8_t fkind = 0;
    uint32_t fdata = 0;
    if (!is_func && bcnt == 1u && !has_ops)
    {
        const FastTok bt = pp->macro_toks[body_start];
        // Pre-encode into SoA (kind, data) so fast_pp_drain replaces 1-tok object macros IN PLACE with ZERO breakout!
        if (bt.kind == FTOK_INT_LIT)
        {
            const uint8_t fl = bt.flags;
            if ((fl & (BUSTER_FTOK_UNSIGNED | BUSTER_FTOK_LONG | BUSTER_FTOK_LONGLONG | BUSTER_FTOK_IN_POOL)) == 0 && bt.data <= (uint32_t)INT32_MAX)
            {
                fmode = 1u; fkind = FTOK_INT_LIT; fdata = bt.data;
            }
            else
            {
                const uint64_t v = fast_tok_int_val(pp, bt);
                const bool u = (fl & BUSTER_FTOK_UNSIGNED) != 0;
                const bool is_l = (fl & (BUSTER_FTOK_LONG | BUSTER_FTOK_LONGLONG)) != 0;
                const bool is_hex = (fl & BUSTER_FTOK_HEX) != 0;
                if (!is_l && !u && v <= (uint64_t)INT32_MAX) { fmode = 1u; fkind = FTOK_INT_LIT; fdata = (uint32_t)v; }
                else if (!is_l && (u || is_hex) && v <= UINT32_MAX) { fmode = 1u; fkind = FTOK_INT_U32; fdata = (uint32_t)v; }
                else if (!u && v <= (uint64_t)INT64_MAX) { fmode = 1u; fkind = FTOK_INT_I64; fdata = (fl & BUSTER_FTOK_IN_POOL) != 0 ? bt.data : fast_pp_add_num(pp, v); }
                else { fmode = 1u; fkind = FTOK_INT_U64; fdata = (fl & BUSTER_FTOK_IN_POOL) != 0 ? bt.data : fast_pp_add_num(pp, v); }
            }
        }
        else if ((bt.kind >= 64u && bt.kind <= 121u) || (bt.kind >= 6u && bt.kind <= 51u) || bt.kind == FTOK_CHAR_LIT ||
                 (bt.kind == FTOK_STR_LIT && bt.extra == 0))
        {
            fmode = 1u; fkind = bt.kind; fdata = bt.data;
        }
        else if (bt.kind == FTOK_IDENT)
        {
            // Safe if we verify !macro_bits[fdata] at expansion time!
            fmode = 1u; fkind = FTOK_IDENT; fdata = bt.data;
        }
    }
    else if (!is_func && !has_ops && bcnt == 0)
    {
        // Empty object-like macro (SQLITE_API, SQLITE_APICALL, etc.): elided in-place inside AVX-512 loop!
        fmode = 4u;
    }
    else if (!is_func && !has_ops && bcnt >= 2u && bcnt <= 8u)
    {
        // Short 2..8 token pure-constant object macro (NULL -> ((void*)0), (-1), unsigned char, long long, etc.)!
        bool simple_multi = true;
        for (uint32_t bi = 0; bi < bcnt; bi += 1)
        {
            const FastTok bt = pp->macro_toks[body_start + bi];
            const bool is_simple_int = bt.kind == FTOK_INT_LIT && bt.flags == 0;
            const bool is_kw_or_punct = (bt.kind >= 64u && bt.kind <= 121u) || (bt.kind >= 6u && bt.kind <= 51u);
            if (!is_simple_int && !is_kw_or_punct)
            {
                simple_multi = false;
                break;
            }
        }
        if (simple_multi)
        {
            fmode = 6u;
        }
    }
    else if (is_func && !has_ops)
    {
        // Check if body references ANY parameter! (Matches assert(x)->((void)(0)), testcase(x)->empty, VVA_ONLY(x)->empty!)
        bool uses_param = false;
        for (uint32_t bi = 0; bi < bcnt; bi += 1)
        {
            const FastTok bt = pp->macro_toks[body_start + bi];
            if ((bt.kind == FTOK_IDENT || bt.kind >= 64u) && bt.extra != 0) { uses_param = true; break; }
        }
        if (!uses_param)
        {
            fmode = 2u;
            // Detect glibc `assert(expr) -> (__ASSERT_VOID_CAST (0))` / `((void)0)` / `((void)(0))` pure no-op func macro
            // WITHOUT speculative macro expansion at #define time!
            const FastTok* const bt = pp->macro_toks + body_start;
            if ((bcnt == 7u && bt[0].kind == FTOK_LPAREN && bt[1].kind == FTOK_LPAREN &&
                 bt[2].kind == FTOK_KW_VOID && bt[3].kind == FTOK_RPAREN &&
                 bt[4].kind == FTOK_LPAREN && bt[5].kind == FTOK_INT_LIT && bt[5].data == 0u &&
                 bt[6].kind == FTOK_RPAREN) ||
                (bcnt == 6u && bt[0].kind == FTOK_LPAREN && bt[1].kind == FTOK_LPAREN &&
                 bt[2].kind == FTOK_KW_VOID && bt[3].kind == FTOK_RPAREN &&
                 bt[4].kind == FTOK_INT_LIT && bt[4].data == 0u && bt[5].kind == FTOK_RPAREN))
            {
                fmode = 3u;
            }
            else if (bcnt == 5u && bt[0].kind == FTOK_LPAREN && bt[1].kind == FTOK_IDENT &&
                     bt[2].kind == FTOK_LPAREN && bt[3].kind == FTOK_INT_LIT && bt[3].data == 0u &&
                     bt[4].kind == FTOK_RPAREN)
            {
                const uint32_t cid = pp->idents.entries[bt[1].data].macro_idx;
                if (cid != 0 && pp->macros[cid].active != 0 && pp->macros[cid].is_func == 0 &&
                    pp->macros[cid].body_count == 3u)
                {
                    const FastTok* const cb = pp->macro_toks + pp->macros[cid].body_start;
                    if (cb[0].kind == FTOK_LPAREN && cb[1].kind == FTOK_KW_VOID && cb[2].kind == FTOK_RPAREN)
                    {
                        fmode = 3u;
                    }
                }
            }
        }
        else if (param_count == 1u && !is_variadic)
        {
            // Detect 1-param identity wrapper macro `#define ALWAYS(X) (X)` / `#define NEVER(X) (X)`!
            const FastTok* const bt = pp->macro_toks + body_start;
            if (bcnt == 3u && bt[0].kind == FTOK_LPAREN &&
                (bt[1].kind == FTOK_IDENT || bt[1].kind >= 64u) && bt[1].extra == 1u &&
                bt[2].kind == FTOK_RPAREN)
            {
                fmode = 5u;
            }
        }
    }
    pp->macros[midx] = (FastMacro){
        .body_start = body_start,
        .body_count = bcnt,
        .fast_data = fdata,
        .param_count = param_count,
        .is_func = is_func ? 1u : 0u,
        .is_variadic = is_variadic ? 1u : 0u,
        .has_ops = has_ops ? 1u : 0u,
        .active = 1u,
        .fast_mode = fmode,
        .fast_kind = fkind,
    };
    pp->idents.entries[name_id].macro_idx = midx;
    fast_ident_set_macro_bit(&pp->idents, name_id, true);
}

static bool fast_pp_eval_if_line(FastPP* pp, FastIncludeFile* f)
{
    FastTok raw[512];
    FastTok expanded[1024];
    uint32_t rc = 0;
    for (;;)
    {
        FastTok t = { 0 };
        fast_pp_read_tok(pp, f, true, &t);
        if (t.kind == FTOK_EOF)
        {
            break;
        }
        if (rc < 512u) { raw[rc++] = t; }
        else { fast_pp_bail(pp, "long #if line"); break; }
    }
    fast_pp_skip_to_eol(f);
    uint32_t ec = 0;
    fast_pp_expand_buffer(pp, raw, rc, expanded, 1024u, &ec, true);
    return !pp->bailout && fast_pp_eval_if_expr(pp, expanded, ec);
}

static __attribute__((noinline)) void fast_pp_handle_include(FastPP* pp, FastIncludeFile* f, bool is_next)
{
    f->ptr = fast_skip_hspace(f->ptr, f->end);
    char name_buf[512];
    uint32_t name_len = 0;
    bool angled = false;

    if (f->ptr < f->end && (*f->ptr == '<' || *f->ptr == '"'))
    {
        angled = *f->ptr == '<';
        const uint8_t close_ch = angled ? '>' : '"';
        f->ptr += 1;
        const uint8_t* start = f->ptr;
        while (f->ptr < f->end && *f->ptr != close_ch && *f->ptr != '\n')
        {
            f->ptr += 1;
        }
        if (f->ptr >= f->end || *f->ptr != close_ch)
        {
            fast_pp_bail(pp, "bad #include header");
            return;
        }
        name_len = (uint32_t)(f->ptr - start);
        if (name_len >= sizeof(name_buf))
        {
            fast_pp_bail(pp, "long #include path");
            return;
        }
        memcpy(name_buf, start, name_len);
        name_buf[name_len] = '\0';
        f->ptr += 1;
        fast_pp_skip_to_eol(f);
    }
    else
    {
        // Macro-expanded #include
        FastTok raw[128];
        FastTok exp[256];
        uint32_t rc = 0;
        for (;;)
        {
            FastTok t = { 0 };
            fast_pp_read_tok(pp, f, true, &t);
            if (t.kind == FTOK_EOF) { break; }
            if (rc < 128u) { raw[rc++] = t; }
        }
        fast_pp_skip_to_eol(f);
        uint32_t ec = 0;
        fast_pp_expand_buffer(pp, raw, rc, exp, 256u, &ec, false);
        if (ec == 1u && exp[0].kind == FTOK_STR_LIT)
        {
            const FastLitSlice sl = pp->lit_slices[exp[0].data];
            if (sl.length < sizeof(name_buf))
            {
                memcpy(name_buf, pp->lit_bytes + sl.offset, sl.length);
                name_len = sl.length;
                name_buf[name_len] = '\0';
            }
        }
        else if (ec >= 3u && exp[0].kind == FTOK_LT && exp[ec - 1u].kind == FTOK_GT)
        {
            angled = true;
            for (uint32_t i = 1u; i + 1u < ec && name_len + 8u < sizeof(name_buf); i += 1)
            {
                name_len += fast_tok_to_text(pp, exp[i], name_buf + name_len, sizeof(name_buf) - name_len);
            }
        }
        else
        {
            fast_pp_bail(pp, "computed #include failed");
            return;
        }
    }

    char resolved[1024];
    uint32_t dir_idx = 0;
    bool is_sys = false;
    if (!fast_pp_resolve_include(pp, name_buf, name_len, angled, is_next, resolved, sizeof(resolved), &dir_idx, &is_sys))
    {
        fast_pp_bail(pp, "include file not found");
        return;
    }
    if (!fast_pp_open_and_push(pp, resolved, (uint32_t)strlen(resolved), dir_idx, is_sys))
    {
        fast_pp_bail(pp, "include open failed");
    }
}

static __attribute__((noinline)) void fast_pp_handle_directive(FastPP* pp, FastIncludeFile* f)
{
    FastTok kw_tok = { 0 };
    fast_pp_read_tok(pp, f, true, &kw_tok);
    if (kw_tok.kind == FTOK_EOF)
    {
        fast_pp_skip_to_eol(f);
        return; // null directive '#'
    }
    if (kw_tok.kind != FTOK_IDENT && !(kw_tok.kind >= 64u && kw_tok.kind < 126u))
    {
        // #line-marker `# 123 "file"` in preprocessed files
        if (kw_tok.kind == FTOK_INT_LIT)
        {
            f->line = kw_tok.data;
            fast_pp_skip_to_eol(f);
            return;
        }
        fast_pp_bail(pp, "invalid directive");
        return;
    }
    const uint8_t pkw = pp->idents.entries[kw_tok.data].pp_kw;
    if (pkw != FPP_KW_IFNDEF && pkw != FPP_KW_DEFINE && pkw != FPP_KW_ENDIF && f->guard_state == 0)
    {
        f->guard_state = 3u;
    }

    switch (pkw)
    {
        case FPP_KW_DEFINE:
            fast_pp_handle_define(pp, f);
            break;
        case FPP_KW_UNDEF:
        {
            FastTok nt = { 0 };
            fast_pp_read_tok(pp, f, true, &nt);
            if (nt.kind == FTOK_IDENT || (nt.kind >= 64u && nt.kind < 126u))
            {
                pp->idents.entries[nt.data].macro_idx = 0;
                fast_ident_set_macro_bit(&pp->idents, nt.data, false);
            }
            fast_pp_skip_to_eol(f);
        } break;
        case FPP_KW_INCLUDE:
            fast_pp_handle_include(pp, f, false);
            break;
        case FPP_KW_INCLUDE_NEXT:
            fast_pp_handle_include(pp, f, true);
            break;
        case FPP_KW_IF:
        case FPP_KW_IFDEF:
        case FPP_KW_IFNDEF:
        {
            if (pp->cond_depth >= BUSTER_FPP_MAX_CONDS)
            {
                fast_pp_bail(pp, "cond stack overflow");
                break;
            }
            bool cond = false;
            bool is_guard = false;
            if (pkw == FPP_KW_IF)
            {
                cond = fast_pp_eval_if_line(pp, f);
            }
            else
            {
                FastTok nt = { 0 };
                fast_pp_read_tok(pp, f, true, &nt);
                if (nt.kind != FTOK_IDENT && !(nt.kind >= 64u && nt.kind < 126u))
                {
                    fast_pp_bail(pp, "bad #ifdef name");
                    break;
                }
                const bool defined = fast_ident_is_macro(&pp->idents, nt.data);
                cond = pkw == FPP_KW_IFDEF ? defined : !defined;
                if (pkw == FPP_KW_IFNDEF && f->guard_state == 0 && pp->cond_depth == f->cond_base)
                {
                    f->guard_ident = nt.data;
                    f->guard_state = 1u;
                    is_guard = true;
                }
                fast_pp_skip_to_eol(f);
            }
            pp->conds[pp->cond_depth++] = (FastCondFrame){
                .active = cond ? 1u : 0u,
                .taken = cond ? 1u : 0u,
                .saw_else = 0u,
                .is_guard = is_guard ? 1u : 0u,
            };
        } break;
        case FPP_KW_ELIF:
        case FPP_KW_ELIFDEF:
        case FPP_KW_ELIFNDEF:
        case FPP_KW_ELSE:
        {
            if (pp->cond_depth <= f->cond_base)
            {
                fast_pp_bail(pp, "stray #else/#elif");
                break;
            }
            FastCondFrame* cf = pp->conds + (pp->cond_depth - 1u);
            if (cf->is_guard) { f->guard_state = 3u; cf->is_guard = 0; }
            if (cf->saw_else) { fast_pp_bail(pp, "duplicate #else"); break; }
            if (pkw == FPP_KW_ELSE) { cf->saw_else = 1u; }
            // Since we were in an active branch, the new branch is definitely inactive!
            cf->active = 0;
            fast_pp_skip_to_eol(f);
        } break;
        case FPP_KW_ENDIF:
        {
            if (pp->cond_depth <= f->cond_base)
            {
                fast_pp_bail(pp, "stray #endif");
                break;
            }
            const FastCondFrame cf = pp->conds[--pp->cond_depth];
            if (cf.is_guard && f->guard_state == 2u)
            {
                f->guard_state = 4u; // Closed outer include guard!
            }
            fast_pp_skip_to_eol(f);
        } break;
        case FPP_KW_LINE:
        {
            FastTok lt = { 0 };
            fast_pp_read_tok(pp, f, true, &lt);
            if (lt.kind == FTOK_INT_LIT) { f->line = lt.data; }
            fast_pp_skip_to_eol(f);
        } break;
        case FPP_KW_PRAGMA:
        {
            FastTok pt = { 0 };
            fast_pp_read_tok(pp, f, true, &pt);
            if (pt.kind == FTOK_IDENT && pp->idents.entries[pt.data].pp_kw == FPP_KW_ONCE)
            {
                FastGuardEntry* ge = fast_guard_lookup(pp, f->path, f->path_len, true);
                if (ge != NULL) { ge->once = 1u; }
                fast_pp_skip_to_eol(f);
            }
            else if (pt.kind == FTOK_IDENT)
            {
                const char* s = pp->idents.strings + pp->idents.entries[pt.data].str_offset;
                if (strcmp(s, "GCC") == 0 || strcmp(s, "clang") == 0 || strcmp(s, "pack") == 0 ||
                    strcmp(s, "weak") == 0 || strcmp(s, "redefine_extname") == 0 ||
                    strcmp(s, "push_macro") == 0 || strcmp(s, "pop_macro") == 0 || strcmp(s, "STDC") == 0)
                {
                    // Check if GCC system_header (safe to ignore) vs diagnostic/visibility (bail)
                    FastTok pt2 = { 0 };
                    fast_pp_read_tok(pp, f, true, &pt2);
                    const char* s2 = (pt2.kind == FTOK_IDENT || pt2.kind >= 64u) ?
                                     pp->idents.strings + pp->idents.entries[pt2.data].str_offset : "";
                    if (strcmp(s, "GCC") == 0 && strcmp(s2, "system_header") == 0)
                    {
                        f->is_system = true;
                        fast_pp_skip_to_eol(f);
                    }
                    else if (strcmp(s, "push_macro") == 0 && f->path != NULL && strcmp(f->path, "<predefines>") == 0)
                    {
                        fast_pp_skip_to_eol(f);
                    }
                    else
                    {
                        fast_pp_bail(pp, "semantic pragma");
                    }
                }
                else
                {
                    fast_pp_skip_to_eol(f);
                }
            }
            else
            {
                fast_pp_skip_to_eol(f);
            }
        } break;
        case FPP_KW_ERROR:
        case FPP_KW_WARNING:
        case FPP_KW_EMBED:
            fast_pp_bail(pp, "error/warning/embed directive");
            break;
        default:
            fast_pp_bail(pp, "unknown directive");
            break;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Main fused lex + preprocess loop: fills pp->out with the entire preprocessed token stream (or streams chunks)

static bool fast_pp_init(FastPP* pp, FastArena* arena, FastArena* scratch, bool small)
{
    memset(pp, 0, sizeof(*pp));
    pp->arena = arena;
    pp->scratch = scratch;
    bool ok = fast_ident_init(&pp->idents, arena, small);
    if (ok)
    {
        pp->out_cap = 1024u * 1024u;
        pp->num_cap = 32768u;
        pp->lit_bytes_cap = UINT64_C(512) * 1024u;
        pp->lit_cap = 32768u;
        pp->macro_tok_cap = 128u * 1024u;
        pp->macro_param_cap = 16384u;
        pp->exp_cap = 32768u;
        pp->arg_cap = 16384u;

        pp->out_kind = (uint8_t*)fast_arena_alloc(arena, (uint64_t)pp->out_cap + 64u, 64);
        pp->out_data = (uint32_t*)fast_arena_alloc(arena, ((uint64_t)pp->out_cap + 64u) * sizeof(uint32_t), 64);
        pp->num_pool = (uint64_t*)fast_arena_alloc(arena, (uint64_t)pp->num_cap * sizeof(uint64_t), 64);
        pp->lit_bytes = (char*)fast_arena_alloc(arena, pp->lit_bytes_cap, 64);
        pp->lit_slices = (FastLitSlice*)fast_arena_alloc(arena, (uint64_t)pp->lit_cap * sizeof(FastLitSlice), 64);
        pp->macros = (FastMacro*)fast_arena_alloc(arena, (uint64_t)BUSTER_FPP_MAX_MACROS * sizeof(FastMacro), 64);
        pp->macro_toks = (FastTok*)fast_arena_alloc(arena, (uint64_t)pp->macro_tok_cap * sizeof(FastTok), 64);
        pp->macro_params = (uint32_t*)fast_arena_alloc(arena, (uint64_t)pp->macro_param_cap * sizeof(uint32_t), 64);
        pp->exp_buf = (FastTok*)fast_arena_alloc(scratch, (uint64_t)pp->exp_cap * sizeof(FastTok), 64);
        pp->arg_buf = (FastTok*)fast_arena_alloc(scratch, (uint64_t)pp->arg_cap * sizeof(FastTok), 64);
        pp->call_stack = (FastMacroCall*)fast_arena_alloc(scratch, (uint64_t)BUSTER_FPP_MAX_CALLS * sizeof(FastMacroCall), 64);
        pp->win.kinds = (uint8_t*)fast_arena_alloc(scratch, BUSTER_FWIN_CAPACITY + 64u, 64);
        pp->win.starts = (uint32_t*)fast_arena_alloc(scratch, (BUSTER_FWIN_CAPACITY + 64u) * sizeof(uint32_t), 64);
        pp->win.lens = (uint8_t*)fast_arena_alloc(scratch, BUSTER_FWIN_CAPACITY + 64u, 64);

        ok = pp->out_kind != NULL && pp->out_data != NULL && pp->num_pool != NULL && pp->lit_bytes != NULL &&
             pp->lit_slices != NULL && pp->macros != NULL && pp->macro_toks != NULL && pp->macro_params != NULL &&
             pp->exp_buf != NULL && pp->arg_buf != NULL && pp->call_stack != NULL &&
             pp->win.kinds != NULL && pp->win.starts != NULL && pp->win.lens != NULL;
    }
    return ok;
}

static void fast_pp_add_dir(FastPP* pp, const char* dir, bool is_system)
{
    if (pp->dir_count < BUSTER_FPP_MAX_DIRS && dir != NULL && dir[0] != '\0')
    {
        const uint32_t idx = pp->dir_count++;
        pp->dirs[idx] = dir;
        pp->dir_lens[idx] = (uint32_t)strlen(dir);
        pp->dir_is_system[idx] = is_system ? 1u : 0u;
    }
}

// 128-byte ASCII -> FastTokKind LUT for 1-uop Zen 5 AVX-512 vpermi2b classification!
// Whitespace/control -> 0; [A-Za-z_$] -> FTOK_IDENT; [0-9] -> FTOK_INT_LIT; punctuators -> their 1-char FastTokKind.
_Alignas(64) static const uint8_t fast_ascii_to_kind[128] = {
    ['!'] = FTOK_BANG,
    ['#'] = FTOK_HASH,
    ['$'] = FTOK_IDENT,
    ['%'] = FTOK_PERCENT,
    ['&'] = FTOK_AMP,
    ['('] = FTOK_LPAREN,
    [')'] = FTOK_RPAREN,
    ['*'] = FTOK_STAR,
    ['+'] = FTOK_PLUS,
    [','] = FTOK_COMMA,
    ['-'] = FTOK_MINUS,
    ['.'] = FTOK_DOT,
    ['/'] = FTOK_SLASH,
    ['0'] = FTOK_INT_LIT, ['1'] = FTOK_INT_LIT, ['2'] = FTOK_INT_LIT, ['3'] = FTOK_INT_LIT, ['4'] = FTOK_INT_LIT,
    ['5'] = FTOK_INT_LIT, ['6'] = FTOK_INT_LIT, ['7'] = FTOK_INT_LIT, ['8'] = FTOK_INT_LIT, ['9'] = FTOK_INT_LIT,
    [':'] = FTOK_COLON,
    [';'] = FTOK_SEMI,
    ['<'] = FTOK_LT,
    ['='] = FTOK_ASSIGN,
    ['>'] = FTOK_GT,
    ['?'] = FTOK_QUESTION,
    ['A'] = FTOK_IDENT, ['B'] = FTOK_IDENT, ['C'] = FTOK_IDENT, ['D'] = FTOK_IDENT, ['E'] = FTOK_IDENT, ['F'] = FTOK_IDENT,
    ['G'] = FTOK_IDENT, ['H'] = FTOK_IDENT, ['I'] = FTOK_IDENT, ['J'] = FTOK_IDENT, ['K'] = FTOK_IDENT, ['L'] = FTOK_IDENT,
    ['M'] = FTOK_IDENT, ['N'] = FTOK_IDENT, ['O'] = FTOK_IDENT, ['P'] = FTOK_IDENT, ['Q'] = FTOK_IDENT, ['R'] = FTOK_IDENT,
    ['S'] = FTOK_IDENT, ['T'] = FTOK_IDENT, ['U'] = FTOK_IDENT, ['V'] = FTOK_IDENT, ['W'] = FTOK_IDENT, ['X'] = FTOK_IDENT,
    ['Y'] = FTOK_IDENT, ['Z'] = FTOK_IDENT,
    ['['] = FTOK_LBRACKET,
    [']'] = FTOK_RBRACKET,
    ['^'] = FTOK_CARET,
    ['_'] = FTOK_IDENT,
    ['a'] = FTOK_IDENT, ['b'] = FTOK_IDENT, ['c'] = FTOK_IDENT, ['d'] = FTOK_IDENT, ['e'] = FTOK_IDENT, ['f'] = FTOK_IDENT,
    ['g'] = FTOK_IDENT, ['h'] = FTOK_IDENT, ['i'] = FTOK_IDENT, ['j'] = FTOK_IDENT, ['k'] = FTOK_IDENT, ['l'] = FTOK_IDENT,
    ['m'] = FTOK_IDENT, ['n'] = FTOK_IDENT, ['o'] = FTOK_IDENT, ['p'] = FTOK_IDENT, ['q'] = FTOK_IDENT, ['r'] = FTOK_IDENT,
    ['s'] = FTOK_IDENT, ['t'] = FTOK_IDENT, ['u'] = FTOK_IDENT, ['v'] = FTOK_IDENT, ['w'] = FTOK_IDENT, ['x'] = FTOK_IDENT,
    ['y'] = FTOK_IDENT, ['z'] = FTOK_IDENT,
    ['{'] = FTOK_LBRACE,
    ['|'] = FTOK_PIPE,
    ['}'] = FTOK_RBRACE,
    ['~'] = FTOK_TILDE,
};

_Alignas(64) static const uint8_t fast_iota64[64] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
};

static inline void fast_pp_emit_soa(FastPP* pp, FastTok tok)
{
    if (__builtin_expect(pp->out_count >= pp->out_cap, 0))
    {
        fast_pp_bail(pp, "out overflow");
        return;
    }
    uint8_t kind = tok.kind;
    uint32_t data = tok.data;
    if (kind == FTOK_INT_LIT)
    {
        const uint8_t fl = tok.flags;
        if ((fl & (BUSTER_FTOK_UNSIGNED | BUSTER_FTOK_LONG | BUSTER_FTOK_LONGLONG | BUSTER_FTOK_IN_POOL)) != 0 || data > (uint32_t)INT32_MAX)
        {
            const uint64_t v = fast_tok_int_val(pp, tok);
            const bool u = (fl & BUSTER_FTOK_UNSIGNED) != 0;
            const bool is_l = (fl & (BUSTER_FTOK_LONG | BUSTER_FTOK_LONGLONG)) != 0;
            const bool is_hex = (fl & BUSTER_FTOK_HEX) != 0;
            if (!is_l && !u && v <= (uint64_t)INT32_MAX)
            {
                kind = FTOK_INT_LIT;
                data = (uint32_t)v;
            }
            else if (!is_l && (u || is_hex) && v <= UINT32_MAX)
            {
                kind = FTOK_INT_U32;
                data = (uint32_t)v;
            }
            else if (!u && (!is_hex || v <= (uint64_t)INT64_MAX) && v <= (uint64_t)INT64_MAX)
            {
                kind = FTOK_INT_I64;
                data = (fl & BUSTER_FTOK_IN_POOL) != 0 ? tok.data : fast_pp_add_num(pp, v);
            }
            else
            {
                kind = FTOK_INT_U64;
                data = (fl & BUSTER_FTOK_IN_POOL) != 0 ? tok.data : fast_pp_add_num(pp, v);
            }
        }
    }
    else if (kind == FTOK_FLOAT_LIT)
    {
        if ((tok.flags & BUSTER_FTOK_LONG) != 0)
        {
            kind = FTOK_FLOAT32;
            double d = 0.0;
            memcpy(&d, &pp->num_pool[data], 8);
            const float f = (float)d;
            uint32_t fb = 0;
            memcpy(&fb, &f, 4);
            data = fb;
        }
        else if ((tok.flags & BUSTER_FTOK_LONGLONG) != 0)
        {
            kind = FTOK_FLOAT80;
        }
    }
    else if (kind == FTOK_STR_LIT && tok.extra >= 1u && tok.extra <= 3u)
    {
        fast_pp_bail(pp, "wide string literal");
        return;
    }
    const uint32_t idx = pp->out_count++;
    pp->out_kind[idx] = kind;
    pp->out_data[idx] = data;
}

// AVX-512 64-byte balanced-paren skipper for 0-param-use function-like macros (assert(...), testcase(...), VVA_ONLY(...)).
// Returns pointer right after closing ')' on fast-path success, or NULL if quotes/comments/arg-mismatch require fallback!
static inline const uint8_t* fast_pp_skip_unused_call_args(const uint8_t* p, const uint8_t* end, uint32_t* io_line,
                                                           uint16_t param_count, bool is_variadic)
{
    uint32_t line = *io_line;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
    {
        if (*p == '\n') { line += 1u; }
        p += 1;
    }
    if (p >= end || *p != '(') { return NULL; }
    p += 1;
    const __m512i v_lp = _mm512_set1_epi8('(');
    const __m512i v_rp = _mm512_set1_epi8(')');
    const __m512i v_cm = _mm512_set1_epi8(',');
    const __m512i v_dq = _mm512_set1_epi8('"');
    const __m512i v_sq = _mm512_set1_epi8('\'');
    const __m512i v_sl = _mm512_set1_epi8('/');
    const __m512i v_bs = _mm512_set1_epi8('\\');
    const __m512i v_nl = _mm512_set1_epi8('\n');

    int32_t depth = 1;
    uint32_t commas = 0;
    while (p < end)
    {
        const uint32_t avail = (uint32_t)(end - p) >= 64u ? 64u : (uint32_t)(end - p);
        const __mmask64 valid = avail == 64u ? ~UINT64_C(0) : ((UINT64_C(1) << avail) - 1u);
        const __m512i v = avail == 64u ? _mm512_loadu_si512((const void*)p) : _mm512_maskz_loadu_epi8(valid, p);
        const uint64_t m_spec = (uint64_t)(
            _mm512_mask_cmpeq_epi8_mask(valid, v, v_dq) |
            _mm512_mask_cmpeq_epi8_mask(valid, v, v_sq) |
            _mm512_mask_cmpeq_epi8_mask(valid, v, v_sl) |
            _mm512_mask_cmpeq_epi8_mask(valid, v, v_bs));
        const uint64_t m_lp = (uint64_t)_mm512_mask_cmpeq_epi8_mask(valid, v, v_lp);
        const uint64_t m_rp = (uint64_t)_mm512_mask_cmpeq_epi8_mask(valid, v, v_rp);
        const uint64_t m_cm = (uint64_t)_mm512_mask_cmpeq_epi8_mask(valid, v, v_cm);
        const uint64_t m_nl = (uint64_t)_mm512_mask_cmpeq_epi8_mask(valid, v, v_nl);
        uint64_t ev = m_lp | m_rp | m_cm | m_spec;
        while (ev != 0)
        {
            const uint32_t b = (uint32_t)__builtin_ctzll(ev);
            ev &= ev - 1u;
            const uint64_t bit = UINT64_C(1) << b;
            if ((m_spec & bit) != 0) { return NULL; } // string/char/comment/backslash -> general fallback!
            if ((m_lp & bit) != 0) { depth += 1; }
            else if ((m_rp & bit) != 0)
            {
                depth -= 1;
                if (depth == 0)
                {
                    if (!is_variadic && param_count > 0 && (commas + 1u) != param_count) { return NULL; }
                    if (!is_variadic && param_count == 0 && commas > 0) { return NULL; }
                    const uint64_t nl_before = m_nl & (b == 0 ? 0u : ((UINT64_C(1) << b) - 1u));
                    *io_line = line + (uint32_t)__builtin_popcountll(nl_before);
                    return p + b + 1u;
                }
            }
            else if (depth == 1)
            {
                commas += 1u;
            }
        }
        line += (uint32_t)__builtin_popcountll(m_nl);
        p += avail;
    }
    return NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// The window: the 64-byte loop writes raw tokens, and its readers take them (FastWindow, #2850)

// A punctuator's length in bytes by its kind.
static const uint8_t fast_punct_len[64] = {
    [FTOK_LPAREN] = 1, [FTOK_RPAREN] = 1, [FTOK_LBRACE] = 1, [FTOK_RBRACE] = 1, [FTOK_LBRACKET] = 1, [FTOK_RBRACKET] = 1,
    [FTOK_SEMI] = 1, [FTOK_COMMA] = 1, [FTOK_DOT] = 1, [FTOK_ARROW] = 2, [FTOK_ELLIPSIS] = 3, [FTOK_QUESTION] = 1,
    [FTOK_COLON] = 1, [FTOK_TILDE] = 1, [FTOK_BANG] = 1, [FTOK_INC] = 2, [FTOK_DEC] = 2, [FTOK_PLUS] = 1, [FTOK_MINUS] = 1,
    [FTOK_STAR] = 1, [FTOK_SLASH] = 1, [FTOK_PERCENT] = 1, [FTOK_AMP] = 1, [FTOK_PIPE] = 1, [FTOK_CARET] = 1,
    [FTOK_LSHIFT] = 2, [FTOK_RSHIFT] = 2, [FTOK_LT] = 1, [FTOK_LE] = 2, [FTOK_GT] = 1, [FTOK_GE] = 2, [FTOK_EQ] = 2,
    [FTOK_NE] = 2, [FTOK_LAND] = 2, [FTOK_LOR] = 2, [FTOK_ASSIGN] = 1, [FTOK_ADD_ASSIGN] = 2, [FTOK_SUB_ASSIGN] = 2,
    [FTOK_MUL_ASSIGN] = 2, [FTOK_DIV_ASSIGN] = 2, [FTOK_MOD_ASSIGN] = 2, [FTOK_AND_ASSIGN] = 2, [FTOK_OR_ASSIGN] = 2,
    [FTOK_XOR_ASSIGN] = 2, [FTOK_SHL_ASSIGN] = 3, [FTOK_SHR_ASSIGN] = 3, [FTOK_HASH] = 1, [FTOK_HASHHASH] = 2,
};

// The escapes a character constant of four bytes may hold ('\n', '\0', '\''): fast_decode_escape's one-byte ones.
static const uint8_t fast_simple_escape[256] = {
    ['n'] = 1, ['t'] = 1, ['r'] = 1, ['a'] = 1, ['b'] = 1, ['f'] = 1, ['v'] = 1, ['\\'] = 1, ['\''] = 1, ['"'] = 1,
    ['?'] = 1, ['e'] = 1, ['0'] = 1, ['1'] = 1, ['2'] = 1, ['3'] = 1, ['4'] = 1, ['5'] = 1, ['6'] = 1, ['7'] = 1,
};

// The newlines in [p, end).
static uint32_t fast_count_newlines(const uint8_t* p, const uint8_t* end)
{
    const __m512i v_nl = _mm512_set1_epi8('\n');
    uint64_t count = 0;
    while (p < end)
    {
        uint32_t avail = 0;
        const __m512i v = fast_load64_bounded(p, end, &avail);
        const __mmask64 valid_mask = avail == 64u ? ~UINT64_C(0) : ((UINT64_C(1) << avail) - 1u);
        count += (uint64_t)__builtin_popcountll(_mm512_cmpeq_epi8_mask(v, v_nl) & valid_mask);
        p += avail;
    }
    return (uint32_t)count;
}

// Moves f's text position forward to `target`, with its line: the bulk emitter leaves both behind while it takes the
// window's tokens, and brings them up where something reads them (a directive, a macro's expansion, fast_lex_one).
static inline void fast_pp_sync_file(FastIncludeFile* f, const uint8_t* target)
{
    if (f->ptr < target)
    {
        f->line += fast_count_newlines(f->ptr, target);
        f->ptr = target;
    }
}

// The punctuators of two characters by three tables: the first character's class times eight, the second character's
// class, and the pair's kind by the two classes (0 where the two characters are no punctuator).
_Alignas(64) static const uint8_t fast_pair_first[128] = {
    ['-'] = 1 << 3, ['+'] = 2 << 3, ['*'] = 3 << 3, ['%'] = 4 << 3, ['&'] = 5 << 3, ['|'] = 6 << 3, ['^'] = 7 << 3,
    ['<'] = 8 << 3, ['>'] = 9 << 3, ['='] = 10 << 3, ['!'] = 11 << 3,
};

_Alignas(64) static const uint8_t fast_pair_second[128] = {
    ['='] = 1, ['>'] = 2, ['-'] = 3, ['+'] = 4, ['&'] = 5, ['|'] = 6, ['<'] = 7,
};

_Alignas(64) static const uint8_t fast_pair_kind[128] = {
    [(1 << 3) | 2] = FTOK_ARROW, [(1 << 3) | 3] = FTOK_DEC, [(1 << 3) | 1] = FTOK_SUB_ASSIGN,
    [(2 << 3) | 4] = FTOK_INC, [(2 << 3) | 1] = FTOK_ADD_ASSIGN,
    [(3 << 3) | 1] = FTOK_MUL_ASSIGN,
    [(4 << 3) | 1] = FTOK_MOD_ASSIGN,
    [(5 << 3) | 5] = FTOK_LAND, [(5 << 3) | 1] = FTOK_AND_ASSIGN,
    [(6 << 3) | 6] = FTOK_LOR, [(6 << 3) | 1] = FTOK_OR_ASSIGN,
    [(7 << 3) | 1] = FTOK_XOR_ASSIGN,
    [(8 << 3) | 7] = FTOK_LSHIFT, [(8 << 3) | 1] = FTOK_LE,
    [(9 << 3) | 2] = FTOK_RSHIFT, [(9 << 3) | 1] = FTOK_GE,
    [(10 << 3) | 1] = FTOK_EQ,
    [(11 << 3) | 1] = FTOK_NE,
};

// A character's class for the window's lexer, one bit a property (bytes of 128 and more are told apart from the
// byte's sign): fast_lex_one's whitespace, the newline, a byte that needs a decision (a comment's or an operator's '/',
// '#', a quote, a backslash, and every character with no token kind), an identifier's or a number's character, a '.',
// and what makes a '.' before it a number's or an ellipsis's (a digit, a '.'). test-direct checks it against
// fast_ascii_to_kind and fast_char_class.
#define BUSTER_FCLASS_SPACE    0x01u
#define BUSTER_FCLASS_NEWLINE  0x02u
#define BUSTER_FCLASS_SPECIAL  0x04u
#define BUSTER_FCLASS_RUN      0x08u
#define BUSTER_FCLASS_DOT      0x10u
#define BUSTER_FCLASS_DOT_NEXT 0x20u
_Alignas(64) static const uint8_t fast_lex_class[128] = {
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x01, 0x03, 0x01, 0x01, 0x01, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x01, 0x00, 0x04, 0x04, 0x08, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x04,
    0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x04, 0x00, 0x00, 0x08,
    0x04, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x04,
};

_Alignas(64) static const uint8_t fast_iota64_plus1[64] = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64,
};

// Lexes a window of f's text from `p`, where a line starts if `bol`. A line's window (`line_mode`) ends at the newline
// that ends the line, as fast_lex_one's stop_at_eol does. Its tokens are fast_lex_one's tokens of the same text, byte
// for byte, or are marked as fast_lex_one's (BUSTER_FWIN_SCALAR), which the readers lex there:
//   - a '.' before a digit or a '.', a two-character punctuator before a character that could go on with it ("<<=",
//     ">>=") or at a block's last byte, an identifier or number of more than 255 bytes;
//   - a number before a '.' or with an exponent's sign is a plain run here, and its readers look at the byte after it.
// What it can't promise even that for ends the window (BUSTER_FWIN_STOP_SCALAR): a backslash, a byte that is neither
// ASCII nor a C character, a literal with a prefix, a string a newline cuts, a character constant that isn't 'c' or a
// one-byte escape.
//
// The loop steps 64 bytes at a time while a block holds none of the bytes that need a decision: the next block's
// address doesn't wait for this block's masks, so the blocks' work overlaps. A block does no branch on its tokens and
// reads nothing back from the window (a 64-byte store that crosses a cache line does not forward to a load on Zen 5):
// what crosses its end is carried in registers: a run that goes on (its token's index and first byte; its length is
// written when its end is found). The classes are one table's lookup and a bit test each; the two-character punctuators are a lookup by
// the two characters' classes, taken at even places of each run of candidates (the longest match from the left); and
// the tokens' kinds, first bytes and lengths are three compresses and an expand.
static BUSTER_DIRECT_HOT void fast_pp_lex_window(FastPP* pp, const FastIncludeFile* f, const uint8_t* p, bool bol, bool line_mode)
{
    FastWindow* const w = &pp->win;
    const uint8_t* const base = f->base;
    const uint8_t* const end = f->end;
    uint8_t* const wk = w->kinds;
    uint32_t* const ws = w->starts;
    uint8_t* const wl = w->lens;
    const __m512i v_lut_lo = _mm512_load_si512((const void*)(fast_ascii_to_kind));
    const __m512i v_lut_hi = _mm512_load_si512((const void*)(fast_ascii_to_kind + 64));
    const __m512i v_class_lo = _mm512_load_si512((const void*)(fast_lex_class));
    const __m512i v_class_hi = _mm512_load_si512((const void*)(fast_lex_class + 64));
    const __m512i v_first_lo = _mm512_load_si512((const void*)(fast_pair_first));
    const __m512i v_first_hi = _mm512_load_si512((const void*)(fast_pair_first + 64));
    const __m512i v_second_lo = _mm512_load_si512((const void*)(fast_pair_second));
    const __m512i v_second_hi = _mm512_load_si512((const void*)(fast_pair_second + 64));
    const __m512i v_pair_lo = _mm512_load_si512((const void*)(fast_pair_kind));
    const __m512i v_pair_hi = _mm512_load_si512((const void*)(fast_pair_kind + 64));
    const __m512i v_iota = _mm512_load_si512((const void*)fast_iota64);
    const __m512i v_iota1 = _mm512_load_si512((const void*)fast_iota64_plus1);
    const __m512i v_space_bit = _mm512_set1_epi8((char)BUSTER_FCLASS_SPACE);
    const __m512i v_newline_bit = _mm512_set1_epi8((char)BUSTER_FCLASS_NEWLINE);
    const __m512i v_special_bits = _mm512_set1_epi8((char)(BUSTER_FCLASS_SPECIAL | (line_mode ? BUSTER_FCLASS_NEWLINE : 0u)));
    const __m512i v_run_bit = _mm512_set1_epi8((char)BUSTER_FCLASS_RUN);
    const __m512i v_dot_bit = _mm512_set1_epi8((char)BUSTER_FCLASS_DOT);
    const __m512i v_dot_next_bit = _mm512_set1_epi8((char)BUSTER_FCLASS_DOT_NEXT);
    const uint64_t even_bits = UINT64_C(0x5555555555555555);
    const uint64_t top_bit = UINT64_C(1) << 63;
    // A slot past the window's tokens for the stores that have nothing to write.
    const uint32_t spare = BUSTER_FWIN_CAPACITY + 63u;
    uint32_t n = 0;
    uint32_t stop = BUSTER_FWIN_STOP_CAPACITY;
    uint32_t unused_line = 0;
    // What crosses a block's end.
    uint64_t run_carry = 0;      // 1 if the last byte's identifier or number goes on in this block
    uint64_t tail_run = 0;       // 1 if the byte before this block is an identifier's or a number's
    uint32_t open_idx = spare;   // the token whose run goes on
    uint32_t open_start = 0;     // ... and its first byte's offset

    while (n + 64u <= BUSTER_FWIN_CAPACITY)
    {
        if (__builtin_expect(p + 80 > end, 0))
        {
            // The identifiers' 16-byte loads and this loop's loads stay inside the file.
            stop = BUSTER_FWIN_STOP_TAIL;
            break;
        }
        const uint32_t block_offset = (uint32_t)(p - base);
        const __m512i v = _mm512_loadu_si512((const void*)p);
        const __m512i v1 = _mm512_loadu_si512((const void*)(p + 1));
        const __m512i kinds_raw = _mm512_permutex2var_epi8(v_lut_lo, v, v_lut_hi);
        const __m512i classes = _mm512_permutex2var_epi8(v_class_lo, v, v_class_hi);
        // The tables are indexed by seven bits: a byte of 128 and more stops the block before any of its masks is used.
        const __mmask64 special = _mm512_test_epi8_mask(classes, v_special_bits) | _mm512_movepi8_mask(v);
        const __mmask64 space = _mm512_test_epi8_mask(classes, v_space_bit);
        const __mmask64 newlines = _mm512_test_epi8_mask(classes, v_newline_bit);
        const __mmask64 run_chars = _mm512_test_epi8_mask(classes, v_run_bit);
        const __mmask64 dots = _mm512_test_epi8_mask(classes, v_dot_bit);
        const __mmask64 dot_nexts = _mm512_test_epi8_mask(classes, v_dot_next_bit);
        const uint32_t limit = (uint32_t)_tzcnt_u64(special);
        const __mmask64 lmask = _bzhi_u64(~UINT64_C(0), limit);

        // Identifiers' and numbers' runs. The byte after the block says whether the last run goes on.
        const uint32_t next_byte = p[64];
        const uint64_t next_run = fast_char_class[next_byte] & 1u;
        const __mmask64 runs = run_chars & lmask;
        const __mmask64 run_ends = runs & ~((runs >> 1) | (next_run << 63));
        const __mmask64 run_starts = runs & ~((runs << 1) | run_carry);
        const __mmask64 punct = ~(space | run_chars) & lmask;
        // A character that goes with the one after it has that one among the block's lexed bytes, or is the block's last
        // byte (whose token is fast_lex_one's below, whatever the next block's first byte is).
        const __mmask64 with_next = punct & ((lmask >> 1) | top_bit);

        // The two-character punctuators: a candidate wherever two characters' classes name one, and of each run of
        // candidates the ones at even places from its start, since a taken pair uses up the next candidate's first
        // character. The run's first candidate is free: no candidate is before it.
        const __m512i pair_kinds = _mm512_permutex2var_epi8(v_pair_lo,
                                                            _mm512_or_si512(_mm512_permutex2var_epi8(v_first_lo, v, v_first_hi),
                                                                            _mm512_permutex2var_epi8(v_second_lo, v1, v_second_hi)),
                                                            v_pair_hi);
        const __mmask64 candidates = _mm512_test_epi8_mask(pair_kinds, pair_kinds) & with_next;
        const __mmask64 candidate_starts = candidates & ~(candidates << 1);
        const __mmask64 from_even = candidates & ~(candidates + (candidate_starts & even_bits));
        const __mmask64 pairs = (from_even & even_bits) | (candidates & ~from_even & ~even_bits);
        const __mmask64 tok_starts = (run_starts | punct) & ~(pairs << 1);
        // fast_lex_one's tokens: a '.' before a digit or a '.', a pair before a candidate (the third character may
        // go on with it) and a pair at the last byte (its second and third characters are not in this block: the next
        // block lexes them as their own tokens, which fast_pp_window_pass passes or lexes again after this one).
        const __mmask64 scalars = (dots & with_next & ((dot_nexts >> 1) | ((uint64_t)((fast_lex_class[next_byte & 127u] >> 5) & 1u) << 63))) |
                                  (pairs & ((candidates >> 1) | top_bit));

        // A run that came from the block before ends here, and its token's length is known (or the run goes on: the
        // length so far, written again by the block that ends it). A length of more than a byte is fast_lex_one's.
        const uint32_t first_end = (uint32_t)_tzcnt_u64(run_ends);
        const uint32_t carried_idx = run_carry != 0 ? open_idx : spare;
        const uint32_t carried_len = block_offset + first_end + 1u - open_start;
        wl[carried_idx] = (uint8_t)carried_len;
        wk[run_carry != 0 && carried_len > 255u ? open_idx : spare] = BUSTER_FWIN_SCALAR;

        // The tokens' kinds and first bytes, packed. A run's length is the byte after its last less its first: those
        // bytes, packed in the runs' order (without the end of the run that came from the block before), are spread
        // back to the tokens that are runs. A punctuator has no length here: its kind's (fast_punct_len).
        const uint32_t m_count = (uint32_t)__builtin_popcountll(tok_starts);
        const __m512i kinds_vec = _mm512_mask_blend_epi8(pairs, kinds_raw, pair_kinds);
        const __m512i comp_kinds = _mm512_maskz_compress_epi8(tok_starts, kinds_vec);
        const __m512i comp_pos = _mm512_maskz_compress_epi8(tok_starts, v_iota);
        const __mmask64 paired_ends = run_ends & ~(_blsi_u64(run_ends) & (0u - run_carry));
        const __m512i comp_ends = _mm512_maskz_compress_epi8(paired_ends, v_iota1);
        const __mmask64 run_lanes = _pext_u64(run_starts, tok_starts);
        _mm512_storeu_si512((void*)(wk + n), comp_kinds);
        _mm512_storeu_si512((void*)(wl + n), _mm512_sub_epi8(_mm512_maskz_expand_epi8(run_lanes, comp_ends), comp_pos));
        const __m512i v_offset = _mm512_set1_epi32((int32_t)block_offset);
        _mm512_storeu_si512((void*)(ws + n), _mm512_add_epi32(v_offset, _mm512_cvtepu8_epi32(_mm512_castsi512_si128(comp_pos))));
        _mm512_storeu_si512((void*)(ws + n + 16u), _mm512_add_epi32(v_offset, _mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(comp_pos, 1))));
        if (__builtin_expect(m_count > 32u, 0))
        {
            _mm512_storeu_si512((void*)(ws + n + 32u), _mm512_add_epi32(v_offset, _mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(comp_pos, 2))));
            _mm512_storeu_si512((void*)(ws + n + 48u), _mm512_add_epi32(v_offset, _mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(comp_pos, 3))));
        }
        if (__builtin_expect(scalars != 0, 0))
        {
            uint64_t rest = scalars;
            do
            {
                // Marked, with the bytes it has as the window splits the text: a pair's two, a '.' alone.
                const uint32_t bit = (uint32_t)__builtin_ctzll(rest);
                const uint32_t idx = n + (uint32_t)__builtin_popcountll(tok_starts & _bzhi_u64(~UINT64_C(0), bit));
                wk[idx] = BUSTER_FWIN_SCALAR;
                wl[idx] = (uint8_t)(1u + (uint32_t)((pairs >> bit) & 1u));
                rest &= rest - 1u;
            } while (rest != 0);
        }

        // What crosses into the next block: the last run, if the byte after the block goes on with it (its token is
        // this block's last one, or still the one from before), and a pair that starts at the last byte.
        const bool starts_run = run_starts != 0;
        open_start = starts_run ? block_offset + 63u - (uint32_t)_lzcnt_u64(run_starts) : open_start;
        open_idx = starts_run ? n + m_count - 1u : open_idx;
        run_carry = (runs >> 63) & next_run;

        // Whether a line starts after these bytes: a newline past the last byte that is no whitespace.
        const __mmask64 solid = (~space) & lmask;
        const __mmask64 chunk_nl = newlines & lmask;
        bol = solid != 0 ? chunk_nl > solid : (bol || chunk_nl != 0);
        n += m_count;

        if (limit == 64u)
        {
            tail_run = runs >> 63;
            p += 64;
            continue;
        }
        p += limit;

        // The byte that needs a decision. No run crosses into it, and no pair. What stands directly before it may
        // belong with it: the identifier or number that ends there, which is the window's last token.
        const bool after_run = limit != 0 ? ((runs >> (limit - 1u)) & 1u) != 0 : tail_run != 0;
        const __mmask64 starts_before = run_starts & lmask;
        const uint8_t* const run = base + (starts_before != 0 ? block_offset + 63u - (uint32_t)_lzcnt_u64(starts_before) : open_start);
        tail_run = 0;
        const uint8_t ch = p[0];
        if (ch == '/')
        {
            const uint8_t ch1 = p[1];
            if (ch1 == '*')
            {
                const uint8_t* after = fast_skip_block_comment(p + 2, end, &unused_line);
                if (after == NULL)
                {
                    fast_pp_bail(pp, "unterminated block comment");
                    stop = BUSTER_FWIN_STOP_BAIL;
                    break;
                }
                p = after;
                continue;
            }
            if (ch1 == '/')
            {
                p = fast_skip_line_comment(p + 2, end, &unused_line);
                continue;
            }
            const uint32_t length = ch1 == '=' ? 2u : 1u;
            wk[n] = ch1 == '=' ? FTOK_DIV_ASSIGN : FTOK_SLASH;
            ws[n] = (uint32_t)(p - base);
            wl[n] = (uint8_t)length;
            n += 1u;
            p += length;
            bol = false;
            continue;
        }
        if (ch == '#')
        {
            if (bol)
            {
                stop = BUSTER_FWIN_STOP_DIRECTIVE;
                break;
            }
            // A '#' or '##' inside a line: a macro body's operator.
            const uint32_t length = p[1] == '#' ? 2u : 1u;
            wk[n] = length == 2u ? FTOK_HASHHASH : FTOK_HASH;
            ws[n] = (uint32_t)(p - base);
            wl[n] = (uint8_t)length;
            n += 1u;
            p += length;
            continue;
        }
        if (ch == '\n')
        {
            // Only a line's window has the newline among these bytes.
            stop = BUSTER_FWIN_STOP_EOL;
            break;
        }
        // A literal's prefix (L"..", u8'c'), a number's digit separator, a spliced identifier or number: that token
        // is fast_lex_one's too.
        if (after_run)
        {
            const uint32_t run_len = (uint32_t)(p - run);
            const bool prefix = (run_len == 1u && (run[0] == 'L' || run[0] == 'u' || run[0] == 'U')) ||
                                (run_len == 2u && run[0] == 'u' && run[1] == '8');
            const bool number = (fast_char_class[run[0]] & 4u) != 0;
            if (ch == '\\' || (ch == '\'' && number) || ((ch == '"' || ch == '\'') && prefix))
            {
                n -= 1u;
                p = run;
                stop = BUSTER_FWIN_STOP_SCALAR;
                break;
            }
        }
        if (ch == '"')
        {
            bool has_escape = false;
            uint32_t stopped = 0;
            const uint8_t* const close = fast_scan_string_close(p + 1, end, &has_escape, &stopped);
            if (stopped == BUSTER_FSTRING_CLOSED)
            {
                wk[n] = FTOK_STR_LIT;
                ws[n] = (uint32_t)(p - base);
                wl[n] = 0;
                n += 1u;
                p = close + 1;
                bol = false;
                continue;
            }
        }
        else if (ch == '\'')
        {
            const uint8_t c1 = p[1];
            uint32_t length = 0;
            if (c1 != '\\' && c1 != '\'' && c1 != '\n' && p[2] == '\'')
            {
                length = 3u;
            }
            else if (c1 == '\\' && fast_simple_escape[p[2]] != 0 && p[3] == '\'')
            {
                length = 4u;
            }
            if (length != 0)
            {
                wk[n] = FTOK_CHAR_LIT;
                ws[n] = (uint32_t)(p - base);
                wl[n] = (uint8_t)length;
                n += 1u;
                p += length;
                bol = false;
                continue;
            }
        }
        stop = BUSTER_FWIN_STOP_SCALAR;
        break;
    }

    // A window never ends inside a run: one that goes on is lexed again by the next window.
    if (run_carry != 0)
    {
        n = open_idx;
        p = base + open_start;
        bol = false;
    }

    w->count = n;
    w->pos = 0;
    w->serial = f->serial;
    w->generation += 1u;
    w->lex_ptr = p;
    w->stop = (uint8_t)stop;
    w->lex_bol = bol;
    w->line_mode = line_mode;
}

// Passes the window's tokens that start before the file's byte `offset`. False where the last of them goes on past it:
// what moved the position there split the text another way than the window (fast_lex_one takes "<<=" where the window
// has "<<" and "=="; a pair at a block's last byte takes the first byte of the next block's pair), and the text is lexed
// again from the position. A string has no length here: whatever passed its first byte took it whole.
static inline bool fast_pp_window_pass(FastWindow* w, uint32_t offset)
{
    const uint32_t count = w->count;
    const uint32_t* const starts = w->starts;
    uint32_t i = w->pos;
    bool whole = true;
    while (i < count && starts[i] < offset)
    {
        i += 1u;
    }
    if (i > w->pos)
    {
        const uint32_t last = i - 1u;
        const uint32_t kind = w->kinds[last];
        uint32_t length = 1u;
        if (kind > FTOK_STR_LIT && kind < 64u)
        {
            length = fast_punct_len[kind];
        }
        else if (kind != FTOK_STR_LIT)
        {
            length = w->lens[last];
        }
        whole = starts[last] + length <= offset;
    }
    w->pos = i;
    return whole;
}

// Moves the window's place to the first token at or after f's text position, or drops the window where the position
// is past it, inside one of its tokens, or it is another file's.
static inline bool fast_pp_window_seek(FastPP* pp, const FastIncludeFile* f)
{
    FastWindow* const w = &pp->win;
    const bool covers = w->serial == f->serial && f->ptr <= w->lex_ptr && fast_pp_window_pass(w, (uint32_t)(f->ptr - f->base));
    if (!covers)
    {
        w->serial = 0;
    }
    return covers;
}

// The window's token `t` as fast_lex_one gives it, with `flags`; `*out_end` is where its text ends.
static FastTok fast_pp_window_tok(FastPP* pp, const FastIncludeFile* f, uint32_t t, uint32_t flags, const uint8_t** out_end)
{
    const FastWindow* const w = &pp->win;
    const uint8_t* const s = f->base + w->starts[t];
    const uint32_t kind = w->kinds[t];
    const uint32_t len = w->lens[t];
    FastTok tok;
    const uint8_t* text_end = s + len;
    if (kind == FTOK_IDENT)
    {
        uint8_t ident_kind = FTOK_IDENT;
        uint32_t id;
        if (__builtin_expect(len <= 16u, 1))
        {
            id = fast_ident_lookup_16(&pp->idents, s, len, &ident_kind);
        }
        else
        {
            id = fast_ident_lookup_long(&pp->idents, s, len, &ident_kind);
        }
        if (__builtin_expect(id == 0, 0))
        {
            fast_pp_bail(pp, "ident table full");
            tok = fast_tok_make(FTOK_EOF, flags, 0, 0);
            text_end = f->end;
        }
        else
        {
            tok = fast_tok_make(ident_kind, flags, 0, id);
        }
    }
    else if (kind == FTOK_INT_LIT)
    {
        const FastLexed lexed = fast_lex_number(pp, s, f->end, flags);
        tok = lexed.tok;
        text_end = lexed.p;
    }
    else if (kind == FTOK_STR_LIT)
    {
        const FastLexed lexed = fast_lex_string(pp, s, f->end, flags, 0);
        tok = lexed.tok;
        text_end = lexed.p;
    }
    else if (kind == FTOK_CHAR_LIT)
    {
        const FastLexed lexed = fast_lex_char(pp, s, f->end, flags, 0);
        tok = lexed.tok;
        text_end = lexed.p;
    }
    else
    {
        tok = fast_tok_make(kind, flags, 0, 0);
        text_end = s + fast_punct_len[kind & 63u];
    }
    *out_end = text_end;
    return tok;
}

// The next token of f's text for the directives and for a macro call's arguments: fast_lex_one's token and its state
// after it (f->ptr, f->line, f->at_bol), from the window where the bytes before the token are horizontal space (and
// newlines, for a reader that crosses lines). fast_lex_one lexes it where they hold a comment or a splice, and
// wherever the window stops.
static BUSTER_DIRECT_HOT void fast_pp_read_tok(FastPP* pp, FastIncludeFile* f, bool stop_at_eol, FastTok* out_tok)
{
    FastWindow* const w = &pp->win;
    bool lexed_here = false;
    bool done = false;
    while (!done)
    {
        if (fast_pp_window_seek(pp, f))
        {
            if (w->pos < w->count)
            {
                const uint32_t t = w->pos;
                const uint8_t* const s = f->base + w->starts[t];
                const uint8_t* gap = f->ptr;
                uint32_t flags = f->at_bol ? BUSTER_FTOK_BOL : 0u;
                uint32_t newlines = 0;
                bool plain = true;
                bool at_eol = false;
                while (gap < s)
                {
                    const uint8_t c = *gap;
                    if (c == '\n')
                    {
                        if (stop_at_eol)
                        {
                            at_eol = true;
                            break;
                        }
                        newlines += 1u;
                        flags |= BUSTER_FTOK_SPACE | BUSTER_FTOK_BOL;
                    }
                    else if ((fast_char_class[c] & 8u) != 0)
                    {
                        flags |= BUSTER_FTOK_SPACE;
                    }
                    else
                    {
                        plain = false;
                        break;
                    }
                    gap += 1;
                }
                if (at_eol)
                {
                    f->ptr = gap;
                    *out_tok = fast_tok_make(FTOK_EOF, 0, 0, 0);
                    return;
                }
                if (plain && w->kinds[t] != BUSTER_FWIN_SCALAR)
                {
                    const uint8_t* text_end = s;
                    f->line += newlines;
                    *out_tok = fast_pp_window_tok(pp, f, t, flags, &text_end);
                    f->ptr = text_end;
                    f->at_bol = false;
                    w->pos = t + 1u;
                    return;
                }
                done = true;
            }
            else if (w->stop == BUSTER_FWIN_STOP_CAPACITY)
            {
                fast_pp_lex_window(pp, f, w->lex_ptr, w->lex_bol, w->line_mode);
                lexed_here = true;
            }
            else if (w->stop == BUSTER_FWIN_STOP_EOL && stop_at_eol)
            {
                // Nothing but space and comments up to the newline that ends the line.
                fast_pp_sync_file(f, w->lex_ptr);
                *out_tok = fast_tok_make(FTOK_EOF, 0, 0, 0);
                return;
            }
            else
            {
                done = true;
            }
        }
        else if (!lexed_here && f->ptr + 80 <= f->end)
        {
            fast_pp_lex_window(pp, f, f->ptr, f->at_bol, stop_at_eol);
            lexed_here = true;
        }
        else
        {
            done = true;
        }
    }
    f->ptr = fast_lex_one(pp, f->ptr, f->end, &f->line, &f->at_bol, stop_at_eol, out_tok);
}

// Goes on at byte `p` of f's text after a macro's name or its arguments: in the window where it reaches that far, or
// with a new one from `p`, where no line starts.
static inline void fast_pp_window_goto(FastPP* pp, FastIncludeFile* f, const uint8_t* p)
{
    FastWindow* const w = &pp->win;
    if (!(w->serial == f->serial && p <= w->lex_ptr && fast_pp_window_pass(w, (uint32_t)(p - f->base))))
    {
        fast_pp_sync_file(f, p);
        f->ptr = p;
        f->at_bol = false;
        w->serial = 0;
    }
}

// A macro's name at the window's place, the tokens before it emitted: what the macro's kind asks for. Returns true
// when the drain's loop has to go on (an expansion's frames wait, or the compile bailed out).
static __attribute__((noinline)) bool fast_pp_window_macro(FastPP* pp, FastIncludeFile* f, uint32_t id, uint8_t kind)
{
    FastWindow* const w = &pp->win;
    const uint32_t t = w->pos;
    const uint8_t* const end = f->end;
    const uint8_t* const after_name = f->base + w->starts[t] + w->lens[t];
    const uint32_t out_limit = pp->out_cap - 64u;
    const uint32_t fmidx = pp->idents.entries[id].macro_idx;
    w->pos = t + 1u;
    if (fmidx != 0)
    {
        const FastMacro* fm = pp->macros + fmidx;
        const uint8_t fmode = fm->fast_mode;
        // An empty object macro (SQLITE_API): its name goes.
        if (fmode == 4u)
        {
            return false;
        }
        // A one-parameter identity wrapper (ALWAYS(x) is (x)): its name goes, and `(x)` is the window's next tokens.
        if (fmode == 5u)
        {
            const uint8_t* q = after_name;
            while (q < end && (*q == ' ' || *q == '\t')) { q += 1; }
            if (q < end && *q == '(')
            {
                return false;
            }
        }
        // A short constant object macro (NULL is ((void*)0)): its body's tokens.
        if (fmode == 6u && pp->out_count + fm->body_count < out_limit)
        {
            const FastTok* const bt = pp->macro_toks + fm->body_start;
            uint32_t out_count = pp->out_count;
            for (uint32_t bi = 0; bi < fm->body_count; bi += 1)
            {
                pp->out_kind[out_count] = bt[bi].kind;
                pp->out_data[out_count] = bt[bi].data;
                out_count += 1u;
            }
            pp->out_count = out_count;
            return false;
        }
        // A function-like macro whose body names no parameter (assert, testcase): its arguments are skipped by the
        // parentheses' count, and no token of them is lexed.
        if (fmode == 2u || fmode == 3u)
        {
            uint32_t skip_line = 0;
            const uint8_t* after_args = fast_pp_skip_unused_call_args(after_name, end, &skip_line, fm->param_count, fm->is_variadic != 0);
            if (after_args != NULL)
            {
                if (fm->body_count == 0)
                {
                    fast_pp_window_goto(pp, f, after_args);
                    return false;
                }
                if (fmode == 3u)
                {
                    // `assert(...);` directly before its `;` leaves the `;` alone.
                    const uint8_t* q = after_args;
                    while (q < end && (*q == ' ' || *q == '\t')) { q += 1; }
                    if (q < end && *q == ';')
                    {
                        fast_pp_window_goto(pp, f, q);
                        return false;
                    }
                    if (pp->out_count + 6u < out_limit)
                    {
                        static const uint8_t v0_k[6] = { FTOK_LPAREN, FTOK_LPAREN, FTOK_KW_VOID, FTOK_RPAREN, FTOK_INT_LIT, FTOK_RPAREN };
                        static const uint32_t v0_d[6] = { 0, 0, 0, 0, 0, 0 };
                        memcpy(pp->out_kind + pp->out_count, v0_k, 6);
                        memcpy(pp->out_data + pp->out_count, v0_d, 24);
                        pp->out_count += 6u;
                        fast_pp_window_goto(pp, f, after_args);
                        return false;
                    }
                }
                // A constant body: its tokens through the expansion stack, the arguments unread.
                fast_pp_sync_file(f, after_args);
                f->at_bol = false;
                if (pp->expand_depth < BUSTER_FPP_MAX_EXPAND && pp->hide_count < BUSTER_FPP_MAX_HIDE)
                {
                    pp->hide_ids[pp->hide_count++] = id;
                    pp->expand[pp->expand_depth++] = (FastExpandFrame){
                        .toks = pp->macro_toks + fm->body_start,
                        .pos = 0,
                        .count = fm->body_count,
                        .macro_ident = id,
                        .saved_scratch = pp->exp_used,
                    };
                    return true;
                }
            }
        }
    }

    // Any other macro: the expansion engine's, which reads its arguments through the window.
    if (f->ptr <= after_name)
    {
        fast_pp_sync_file(f, after_name);
    }
    f->ptr = after_name;
    f->at_bol = false;
    FastTok rep = fast_tok_make(FTOK_EOF, 0, 0, 0);
    const FastTok mtok = fast_tok_make(kind, 0, 0, id);
    if (fast_pp_try_expand(pp, mtok, false, &rep))
    {
        if (rep.kind != FTOK_EOF)
        {
            fast_pp_emit_soa(pp, rep);
        }
        if (pp->expand_depth == 0 && !pp->bailout)
        {
            fast_pp_window_goto(pp, f, f->ptr);
            f->at_bol = false;
            return false;
        }
        return true;
    }
    // A function-like macro's name without its '(': the identifier itself.
    pp->out_kind[pp->out_count] = kind;
    pp->out_data[pp->out_count] = id;
    pp->out_count += 1u;
    fast_pp_window_goto(pp, f, f->ptr);
    f->at_bol = false;
    return pp->bailout;
}

// Whether the number whose run of digits and letters is s[0..len) goes on past it, as fast_lex_number takes it: a '.'
// follows, or a sign after an exponent's letter.
static inline bool fast_number_goes_on(const uint8_t* s, uint32_t len)
{
    const uint8_t after = s[len];
    const uint8_t last = s[len - 1u] | 0x20u;
    return after == '.' || ((after == '+' || after == '-') && (last == 'e' || last == 'p'));
}

// One token of f's text by fast_lex_one, and what the drain does with it: the file's end, a directive, a macro's name.
static void fast_pp_single_token(FastPP* pp, FastIncludeFile* f)
{
    FastTok tok = { 0 };
    f->ptr = fast_lex_one(pp, f->ptr, f->end, &f->line, &f->at_bol, false, &tok);
    if (tok.kind == FTOK_EOF)
    {
        fast_pp_pop_file(pp);
    }
    else if (tok.kind == FTOK_HASH && (tok.flags & BUSTER_FTOK_BOL) != 0)
    {
        fast_pp_handle_directive(pp, f);
    }
    else
    {
        bool expanded = false;
        if (f->guard_state == 0 || f->guard_state == 4u)
        {
            f->guard_state = 3u;
        }
        if ((tok.kind == FTOK_IDENT || tok.kind >= 64u) && fast_ident_is_macro(&pp->idents, tok.data))
        {
            FastTok rep = { 0 };
            if (fast_pp_try_expand(pp, tok, false, &rep))
            {
                if (rep.kind != FTOK_EOF) { fast_pp_emit_soa(pp, rep); }
                expanded = true;
            }
        }
        if (!expanded)
        {
            fast_pp_emit_soa(pp, tok);
        }
    }
}

// The window's next token is fast_lex_one's, and the tokens before it are emitted: lexed and handled as the drain's
// own tokens are, and the window goes on after it. Returns true when the drain's loop has to go on instead (an
// expansion's frames wait, the file ended, the window is dropped, the compile bailed out).
static __attribute__((noinline)) bool fast_pp_window_scalar(FastPP* pp, FastIncludeFile* f)
{
    FastWindow* const w = &pp->win;
    const uint32_t depth = pp->file_depth;
    bool leave = true;
    fast_pp_sync_file(f, f->base + w->starts[w->pos]);
    f->at_bol = false;
    fast_pp_single_token(pp, f);
    if (pp->file_depth != depth)
    {
        w->serial = 0;
    }
    else if (pp->expand_depth == 0 && !pp->bailout)
    {
        leave = !fast_pp_window_seek(pp, f);
    }
    return leave;
}

// Emits the window's tokens from its place on: 64 kinds a store, and a loop over the identifiers, numbers and
// literals among them, which are the tokens with something to look up or convert. A macro's name stops the block: the
// tokens before it stay, and fast_pp_window_macro says where the window goes on.
static BUSTER_DIRECT_HOT void fast_pp_emit_window(FastPP* pp, FastIncludeFile* f)
{
    FastWindow* const w = &pp->win;
    const uint8_t* const base = f->base;
    uint8_t* const out_kind = pp->out_kind;
    uint32_t* const out_data = pp->out_data;
    const uint32_t out_limit = pp->out_cap - 64u;
    const uint64_t* const macro_bits = pp->idents.macro_bits;
    const __m512i v_work_max = _mm512_set1_epi8(FTOK_STR_LIT);
    uint32_t out_count = pp->out_count;
    uint32_t i = w->pos;
    uint32_t n = w->count;
    bool leave = false;

    // Tokens that are no directive's: the file's include guard no longer covers the whole file.
    if (f->guard_state == 0 || f->guard_state == 4u)
    {
        f->guard_state = 3u;
    }

    while (i < n && out_count < out_limit && !leave)
    {
        const uint32_t cnt = n - i < 64u ? n - i : 64u;
        const __m512i kv = _mm512_loadu_si512((const void*)(w->kinds + i));
        _mm512_storeu_si512((void*)(out_kind + out_count), kv);
        uint64_t work = (uint64_t)_mm512_cmple_epu8_mask(kv, v_work_max) & _bzhi_u64(~UINT64_C(0), cnt);
        uint32_t taken = cnt;
        while (work != 0)
        {
            const uint32_t k = (uint32_t)__builtin_ctzll(work);
            work &= work - 1u;
            const uint32_t t = i + k;
            const uint32_t kind = w->kinds[t];
            const uint8_t* const s = base + w->starts[t];
            const uint32_t len = w->lens[t];
            if (__builtin_expect(kind == FTOK_IDENT, 1))
            {
                uint8_t ident_kind = FTOK_IDENT;
                uint32_t id;
                if (__builtin_expect(len <= 16u, 1))
                {
                    id = fast_ident_lookup_16(&pp->idents, s, len, &ident_kind);
                }
                else
                {
                    id = fast_ident_lookup_long(&pp->idents, s, len, &ident_kind);
                }
                if (__builtin_expect(id == 0, 0))
                {
                    fast_pp_bail(pp, "ident table full");
                    taken = k;
                    leave = true;
                    break;
                }
                out_data[out_count + k] = id;
                out_kind[out_count + k] = ident_kind;
                if (__builtin_expect((macro_bits[id >> 6] & (UINT64_C(1) << (id & 63u))) != 0, 0))
                {
                    // A one-token object macro (SQLITE_OK, OP_*) is replaced where it stands.
                    const uint32_t midx = pp->idents.entries[id].macro_idx;
                    if (midx != 0)
                    {
                        const FastMacro* fm = pp->macros + midx;
                        if (fm->fast_mode == 1u &&
                            (fm->fast_kind != FTOK_IDENT ||
                             (macro_bits[fm->fast_data >> 6] & (UINT64_C(1) << (fm->fast_data & 63u))) == 0))
                        {
                            out_kind[out_count + k] = fm->fast_kind;
                            out_data[out_count + k] = fm->fast_data;
                            continue;
                        }
                    }
                    out_count += k;
                    w->pos = t;
                    pp->out_count = out_count;
                    leave = fast_pp_window_macro(pp, f, id, ident_kind);
                    out_count = pp->out_count;
                    i = w->serial == f->serial ? w->pos : 0u;
                    n = w->serial == f->serial ? w->count : 0u;
                    taken = 0;
                    break;
                }
            }
            else if (kind == FTOK_INT_LIT && !fast_number_goes_on(s, len))
            {
                if (__builtin_expect(len == 1u, 1))
                {
                    out_data[out_count + k] = (uint32_t)(s[0] - '0');
                }
                else
                {
                    // A decimal number of two to four digits (10, 16, 255, 1024) in place.
                    uint32_t v = (uint32_t)(s[0] - '0');
                    bool all_dig = len <= 4u && s[0] != '0';
                    for (uint32_t di = 1u; all_dig && di < len; di += 1)
                    {
                        const uint32_t d = (uint32_t)(s[di] - '0');
                        all_dig = d <= 9u;
                        v = v * 10u + d;
                    }
                    if (all_dig)
                    {
                        out_data[out_count + k] = v;
                    }
                    else
                    {
                        const FastLexed lexed = fast_lex_number(pp, s, s + len, 0);
                        pp->out_count = out_count + k;
                        fast_pp_emit_soa(pp, lexed.tok);
                    }
                }
            }
            else if (kind == FTOK_STR_LIT || kind == FTOK_CHAR_LIT)
            {
                const FastLexed lexed = kind == FTOK_STR_LIT ? fast_lex_string(pp, s, f->end, 0, 0) : fast_lex_char(pp, s, f->end, 0, 0);
                pp->out_count = out_count + k;
                fast_pp_emit_soa(pp, lexed.tok);
            }
            else
            {
                // fast_lex_one's token: one the window marked, or a number that goes on past its run.
                out_count += k;
                w->pos = t;
                pp->out_count = out_count;
                leave = fast_pp_window_scalar(pp, f);
                out_count = pp->out_count;
                i = w->serial == f->serial ? w->pos : 0u;
                n = w->serial == f->serial ? w->count : 0u;
                taken = 0;
                break;
            }
        }
        out_count += taken;
        i += taken;
        leave = leave || pp->bailout;
    }
    w->pos = i;
    pp->out_count = out_count;
}

// The preprocessor's loop: an expansion's pending tokens first, then the top file's text, a window of raw tokens at a
// time (FastWindow). A window's tokens are emitted in bulk; a directive, a token the window leaves to fast_lex_one and
// a file's last bytes are handled where the window ends.
static BUSTER_DIRECT_HOT bool fast_pp_drain(FastPP* pp)
{
    while ((pp->file_depth > 0 || pp->expand_depth > 0) && !pp->bailout)
    {
        if (__builtin_expect(pp->expand_depth > 0, 0))
        {
            FastExpandFrame* ef = pp->expand + (pp->expand_depth - 1u);
            {
                // The frame's tokens up to a macro's name, with the frame's place and the output's count in registers:
                // an expansion's tokens are a quarter of sqlite3.c's, and this loop's iteration went through memory for
                // both. A number's and a literal's kinds are fast_pp_emit_soa's to settle.
                const FastTok* const toks = ef->toks;
                const uint64_t* const macro_bits = pp->idents.macro_bits;
                uint8_t* const out_kind = pp->out_kind;
                uint32_t* const out_data = pp->out_data;
                const uint32_t count = ef->count;
                const uint32_t out_cap = pp->out_cap;
                uint32_t pos = ef->pos;
                uint32_t out_count = pp->out_count;
                bool stop = false;
                while (pos < count && !stop)
                {
                    const FastTok tok = toks[pos];
                    const uint32_t kind = tok.kind;
                    if ((kind == FTOK_IDENT || kind >= 64u) && (macro_bits[tok.data >> 6] & (UINT64_C(1) << (tok.data & 63u))) != 0)
                    {
                        stop = true;
                    }
                    else if (kind - FTOK_INT_LIT <= FTOK_STR_LIT - FTOK_INT_LIT || out_count >= out_cap)
                    {
                        pp->out_count = out_count;
                        fast_pp_emit_soa(pp, tok);
                        out_count = pp->out_count;
                        pos += 1u;
                        stop = pp->bailout;
                    }
                    else
                    {
                        out_kind[out_count] = (uint8_t)kind;
                        out_data[out_count] = tok.data;
                        out_count += 1u;
                        pos += 1u;
                    }
                }
                ef->pos = pos;
                pp->out_count = out_count;
                if (pp->bailout)
                {
                    break;
                }
            }
            if (ef->pos >= ef->count)
            {
                if (ef->macro_ident != 0 && pp->hide_count > 0) { pp->hide_count -= 1u; }
                pp->expand_depth -= 1u;
                if (pp->expand_depth == 0) { pp->exp_used = 0; pp->arg_used = 0; }
                else if (ef->saved_scratch != UINT32_MAX && ef->saved_scratch < pp->exp_used) { pp->exp_used = ef->saved_scratch; }
                continue;
            }
            const FastTok tok = ef->toks[ef->pos++];
            if ((tok.kind == FTOK_IDENT || tok.kind >= 64u) && fast_ident_is_macro(&pp->idents, tok.data))
            {
                FastTok rep = { 0 };
                if (fast_pp_try_expand(pp, tok, false, &rep))
                {
                    if (rep.kind != FTOK_EOF) { fast_pp_emit_soa(pp, rep); }
                    continue;
                }
            }
            fast_pp_emit_soa(pp, tok);
            continue;
        }

        FastIncludeFile* f = pp->files + (pp->file_depth - 1u);

        if (__builtin_expect(pp->cond_depth > f->cond_base && pp->conds[pp->cond_depth - 1u].active == 0, 0))
        {
            const uint8_t kw = fast_pp_skip_inactive(pp, f);
            if (pp->bailout) { break; }
            FastCondFrame* cf = pp->conds + (pp->cond_depth - 1u);
            if (kw == FPP_KW_ENDIF)
            {
                if (cf->is_guard && f->guard_state == 2u) { f->guard_state = 4u; }
                pp->cond_depth -= 1u;
                fast_pp_skip_to_eol(f);
            }
            else if (kw == FPP_KW_ELSE)
            {
                if (cf->is_guard) { f->guard_state = 3u; cf->is_guard = 0; }
                if (cf->saw_else) { fast_pp_bail(pp, "duplicate #else"); break; }
                cf->saw_else = 1u;
                if (!cf->taken) { cf->active = 1u; cf->taken = 1u; }
                fast_pp_skip_to_eol(f);
            }
            else if (kw == FPP_KW_ELIF || kw == FPP_KW_ELIFDEF || kw == FPP_KW_ELIFNDEF)
            {
                if (cf->is_guard) { f->guard_state = 3u; cf->is_guard = 0; }
                if (cf->saw_else) { fast_pp_bail(pp, "#elif after #else"); break; }
                if (!cf->taken)
                {
                    bool cond = false;
                    if (kw == FPP_KW_ELIF) { cond = fast_pp_eval_if_line(pp, f); }
                    else
                    {
                        FastTok nt = { 0 };
                        fast_pp_read_tok(pp, f, true, &nt);
                        const bool def = (nt.kind == FTOK_IDENT || nt.kind >= 64u) && fast_ident_is_macro(&pp->idents, nt.data);
                        cond = kw == FPP_KW_ELIFDEF ? def : !def;
                        fast_pp_skip_to_eol(f);
                    }
                    if (cond) { cf->active = 1u; cf->taken = 1u; }
                }
                else
                {
                    fast_pp_skip_to_eol(f);
                }
            }
            continue;
        }

        // The window that covers f's position, or a new one from there.
        FastWindow* const w = &pp->win;
        if (!fast_pp_window_seek(pp, f) && f->ptr + 80 <= f->end && pp->out_count + 64u < pp->out_cap)
        {
            if (f->at_bol)
            {
                // A directive at the line's start needs no window.
                const uint8_t* const q = fast_skip_hspace(f->ptr, f->end);
                if (q < f->end && *q == '#')
                {
                    f->ptr = q + 1;
                    f->at_bol = false;
                    fast_pp_handle_directive(pp, f);
                    continue;
                }
            }
            fast_pp_lex_window(pp, f, f->ptr, f->at_bol, false);
        }
        if (w->serial == f->serial)
        {
            const bool room = pp->out_count + 64u < pp->out_cap;
            if (w->pos < w->count)
            {
                if (room)
                {
                    fast_pp_emit_window(pp, f);
                    continue;
                }
                // The output is nearly full: a token at a time from the window's next one, up to the overflow's bailout.
                fast_pp_sync_file(f, f->base + w->starts[w->pos]);
                f->at_bol = false;
                w->serial = 0;
            }
            else if (w->stop == BUSTER_FWIN_STOP_CAPACITY && room)
            {
                fast_pp_lex_window(pp, f, w->lex_ptr, w->lex_bol, w->line_mode);
                continue;
            }
            else
            {
                // The window's end: f's position there, and what stopped the lexing.
                const uint32_t stop = w->stop;
                fast_pp_sync_file(f, w->lex_ptr);
                f->at_bol = w->lex_bol;
                w->serial = 0;
                if (stop == BUSTER_FWIN_STOP_DIRECTIVE)
                {
                    f->ptr += 1;
                    f->at_bol = false;
                    fast_pp_handle_directive(pp, f);
                    continue;
                }
                if (pp->bailout)
                {
                    break;
                }
            }
        }

        // A file's last bytes, and a token the window leaves to fast_lex_one.
        fast_pp_single_token(pp, f);
    }
    return !pp->bailout;
}

static bool fast_pp_run_buffer(FastPP* pp, const char* name, const char* bytes, uint64_t len)
{
    bool ok = fast_pp_push_memory(pp, name, (const uint8_t*)bytes, len, UINT32_MAX, false, false);
    if (ok)
    {
        ok = fast_pp_drain(pp);
    }
    return ok;
}

static bool fast_pp_run_file(FastPP* pp, const char* path)
{
    pp->base_file = path;
    bool ok = fast_pp_open_and_push(pp, path, (uint32_t)strlen(path), UINT32_MAX, false);
    if (ok)
    {
        ok = fast_pp_drain(pp);
        if (ok && pp->out_count < pp->out_cap)
        {
            pp->out_kind[pp->out_count] = FTOK_EOF;
            pp->out_data[pp->out_count] = 0;
        }
    }
    return ok;
}

#endif
