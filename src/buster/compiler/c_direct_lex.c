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
#include <math.h>
#include <time.h>
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
    // A number the full pipeline diagnoses where it is used, as clang does (#3577): an invalid suffix, a base without
    // digits, an integer past 64 bits, a floating value out of its type's range. A macro may hold one; the compiler's
    // stream, #if and # or ## refuse it.
    FTOK_BAD_LITERAL = 54,

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
// A character constant with an escape or a prefix keeps its spelling in the literal pool, at the token's extra (#3577:
// # and ## spell it as written); an integer with a base or a suffix always does.
#define BUSTER_FTOK_SPELLED     BUSTER_FTOK_HEX

// Compact 8-byte token
typedef struct FastTok FastTok;
struct FastTok
{
    uint8_t kind;
    uint8_t flags;
    uint16_t extra;   // string/literal flags or wide/prefix kind or param index in macro body
    uint32_t data;    // ident_id (IDENT/KW), inline u32 or num_pool index (INT/FLOAT), lit_pool index (STR)
};
_Static_assert(sizeof(FastTok) == 8u && offsetof(FastTok, kind) == 0u && offsetof(FastTok, flags) == 1u, "a token's word: its kind's byte, then its flags'");

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
    uint32_t escaped;   // a string literal's text had an escape: its value doesn't spell it (#3577)
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
    // Sparse small compiles and concurrent workers use base pages. Larger single-worker compiles inherit system THP.
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

#define BUSTER_FIDENT_SMALL_GROUPS    64u
#define BUSTER_FIDENT_RESERVED_GROUPS 512u
#define BUSTER_FIDENT_MAX_IDS         49152u
#define BUSTER_FIDENT_CACHE_BITS      14u
// Small units start with 64 hash groups (20 KiB touched on demand) and 2,048 cache slots (16 KiB).
// #3592 measured a unit with six standard headers on the 9700X. Starting with 64 groups saved two
// hash-table rebuilds compared with 16 groups. Growing the spelling cache at four names per slot
// reduced both user and kernel cycles compared with growing it at one name per slot.
#define BUSTER_FIDENT_SMALL_CACHE_BITS 11u
#define BUSTER_FIDENT_CACHE_GROW_BITS  2u
#define BUSTER_FIDENT_CACHE_GROW_LOAD  4u

typedef struct FastIdentCacheSlot FastIdentCacheSlot;
struct FastIdentCacheSlot
{
    uint32_t offset;
    uint16_t id;
    uint8_t len;
    uint8_t tok_kind;
};
_Static_assert(sizeof(FastIdentCacheSlot) == 8u && BUSTER_FIDENT_MAX_IDS <= UINT16_MAX, "identifier cache layout");

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
    FPP_KW_TIMESTAMP_MACRO,
    FPP_KW_FILE_NAME_MACRO,
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
    FPP_KW_HAS_DECLSPEC_ATTRIBUTE,
    FPP_KW_HAS_EMBED,
    FPP_KW_HAS_CONSTEXPR_BUILTIN,
    FPP_KW_IS_TARGET_ARCH,
    FPP_KW_IS_TARGET_VENDOR,
    FPP_KW_IS_TARGET_OS,
    FPP_KW_IS_TARGET_ENVIRONMENT,
    FPP_KW_IS_TARGET_VARIANT_OS,
    FPP_KW_IS_TARGET_VARIANT_ENVIRONMENT,
    FPP_KW_ONCE,
    FPP_KW_ELIFDEF,
    FPP_KW_ELIFNDEF,
    FPP_KW_FORMAT_FUNCTION, // printf, fprintf, sprintf, snprintf: the full pipeline checks their formats (#3577)
    // #3577: library functions whose calls the full pipeline checks by name: the memcpy family's sizes
    // (-Wsizeof-pointer-memaccess, a superset of its names: MEMACCESS to FORTIFY_BCOPY), the destinations of those that
    // write and of strcpy, stpcpy and strcat (-Wfortify-source, by how the amount is given: FORTIFY_MEMORY to
    // STRING_COPY), and the integer and floating absolute values' arguments (-Wabsolute-value).
    FPP_KW_MEMACCESS_FUNCTION,
    FPP_KW_FORTIFY_MEMORY_FUNCTION,
    FPP_KW_FORTIFY_LIMITED_FUNCTION,
    FPP_KW_FORTIFY_BZERO_FUNCTION,
    FPP_KW_FORTIFY_BCOPY_FUNCTION,
    FPP_KW_STRING_COPY_FUNCTION,
    FPP_KW_ABS_FUNCTION,
    FPP_KW_FABS_FUNCTION
} FastPPKw;

// The full pipeline's builtin macros (#3577), __LINE__ to __is_target_variant_environment: each is defined (defined(),
// #ifdef), and none may be defined or undefined quietly. The direct path expands __LINE__ to _Pragma and answers the
// queries in #if; __TIMESTAMP__, __FILE_NAME__ and a query outside #if are refused.
#define BUSTER_FPP_KW_LAST_BUILTIN FPP_KW_IS_TARGET_VARIANT_ENVIRONMENT

typedef struct FastIdentTable FastIdentTable;
struct FastIdentTable
{
    uint8_t* ctrl;             // 64-byte aligned control bytes (group_count * 64)
    uint32_t* slots;           // slot -> 1-based IdentId (group_count * 64)
    uint32_t group_count;      // power of 2
    uint32_t group_mask;
    uint32_t grow_threshold;

    FastIdentEntry* entries;   // 1-based [1 .. count]
    uint32_t count;
    uint32_t capacity;

    uint64_t* macro_bits;      // 1 bit per IdentId (L1-resident fast check!)

    char* strings;             // contiguous NUL-terminated identifier text buffer
    uint64_t str_used;
    uint64_t str_cap;

    FastIdentCacheSlot* cache; // direct-mapped spelling cache for identifiers up to 64 bytes, 1 << cache_bits slots
    uint32_t cache_bits;
    uint32_t cache_shift;      // 64 - cache_bits: a key's hash >> cache_shift is its slot
    uint32_t cache_grow_at;    // the count of names at which the cache grows (UINT32_MAX at its full size)
    uint32_t va_opt_id;        // __VA_OPT__'s IdentId, a seed's (#3577: a macro body's check)

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
    // The small table already owns the entire initial reservation. Rebuild from entry hashes
    // in insertion order so growth needs neither the old slot array nor any new arena space.
    const bool reuse = t->group_count < BUSTER_FIDENT_RESERVED_GROUPS;
    const uint32_t new_groups = reuse ? BUSTER_FIDENT_RESERVED_GROUPS : t->group_count * 2u;
    const uint64_t new_slots_count = (uint64_t)new_groups * 64u;
    uint8_t* new_ctrl = reuse ? t->ctrl : (uint8_t*)fast_arena_alloc(t->arena, new_slots_count, 64);
    uint32_t* new_slots = reuse ? t->slots : (uint32_t*)fast_arena_alloc(t->arena, new_slots_count * sizeof(uint32_t), 64);
    bool ok = new_ctrl != NULL && new_slots != NULL;
    if (ok)
    {
        memset(new_ctrl, 0, reuse ? (uint64_t)t->group_count * 64u : new_slots_count);
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
        if (__builtin_expect(t->count >= t->grow_threshold, 0))
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

// The cache's key hash, of a name's first eight bytes (masked to its length) and, past eight bytes, its last eight and
// its length (`mix`, BUSTER_FIDENT_CACHE_MIX; 0 up to eight bytes): fast_ident_lookup's, and fast_ident_cache_grow's for
// a slot's stored key. Its top cache_bits bits are the slot. Macros rather than functions so that the compile's count
// has no call a lookup in it (#3592).
#define BUSTER_FIDENT_CACHE_MIX(last, len) (((last) * UINT64_C(0x9e3779b97f4a7c15)) ^ ((uint64_t)(len) << 56))
#define BUSTER_FIDENT_CACHE_HASH(first, mix) (((first) ^ (mix)) * UINT64_C(0xbf58476d1ce4e5b9))

// Rebuild a cache slot's hash from the permanent spelling when the cache grows.
static uint64_t fast_ident_cached_hash(const FastIdentTable* t, const FastIdentCacheSlot* slot)
{
    const char* spelling = t->strings + slot->offset;
    const uint32_t len = slot->len;
    uint64_t first;
    uint64_t last = 0;
    memcpy(&first, spelling, sizeof(first));
    if (len <= 8u)
    {
        first = _bzhi_u64(first, len * 8u);
    }
    else
    {
        memcpy(&last, spelling + len - 8u, sizeof(last));
    }
    return BUSTER_FIDENT_CACHE_HASH(first, len > 8u ? BUSTER_FIDENT_CACHE_MIX(last, len) : 0u);
}

static void fast_ident_cache_set(FastIdentTable* t, FastIdentCacheSlot* cache, uint32_t bits)
{
    t->cache = cache;
    t->cache_bits = bits;
    t->cache_shift = 64u - bits;
    t->cache_grow_at = bits < BUSTER_FIDENT_CACHE_BITS ? BUSTER_FIDENT_CACHE_GROW_LOAD << bits : UINT32_MAX;
}

// Once there are four names per slot, rehash the occupied slots into a larger zero-filled block using their
// permanent spellings. Distinct old slots have different hash prefixes, so they stay distinct at the larger size.
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
                new_cache[fast_ident_cached_hash(t, &old_cache[i]) >> new_shift] = old_cache[i];
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
    t->group_count = small ? BUSTER_FIDENT_SMALL_GROUPS : BUSTER_FIDENT_RESERVED_GROUPS;
    t->group_mask = t->group_count - 1u;
    const uint64_t slot_count = (uint64_t)BUSTER_FIDENT_RESERVED_GROUPS * 64u;
    t->grow_threshold = t->group_count * 48u;
    t->capacity = BUSTER_FIDENT_MAX_IDS;
    t->str_cap = UINT64_C(1) * 1024u * 1024u;

    t->ctrl = (uint8_t*)fast_arena_alloc(arena, slot_count, 64);
    t->slots = (uint32_t*)fast_arena_alloc(arena, slot_count * sizeof(uint32_t), 64);
    t->entries = (FastIdentEntry*)fast_arena_alloc(arena, (uint64_t)t->capacity * sizeof(FastIdentEntry), 64);
    t->macro_bits = (uint64_t*)fast_arena_alloc(arena, ((uint64_t)t->capacity / 64u + 1u) * sizeof(uint64_t), 64);
    // A short cached spelling can be compared with one masked eight-byte word, even at the end.
    t->strings = (char*)fast_arena_alloc(arena, t->str_cap + 7u, 64);
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
            // _Float128, _Float32, _Float64, _Float32x and _Float64x are identifiers, as the full pipeline lexes them:
            // glibc's floatn headers name them with typedefs (#3577).
            { "__float128", FTOK_KW_FLOAT128, 0 },
            { "_Float16", FTOK_KW_FLOAT16, 0 },
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
            { "__TIMESTAMP__", 0, FPP_KW_TIMESTAMP_MACRO },
            { "__FILE_NAME__", 0, FPP_KW_FILE_NAME_MACRO },
            { "__has_declspec_attribute", 0, FPP_KW_HAS_DECLSPEC_ATTRIBUTE },
            { "__has_embed", 0, FPP_KW_HAS_EMBED },
            { "__has_constexpr_builtin", 0, FPP_KW_HAS_CONSTEXPR_BUILTIN },
            { "__is_target_arch", 0, FPP_KW_IS_TARGET_ARCH },
            { "__is_target_vendor", 0, FPP_KW_IS_TARGET_VENDOR },
            { "__is_target_os", 0, FPP_KW_IS_TARGET_OS },
            { "__is_target_environment", 0, FPP_KW_IS_TARGET_ENVIRONMENT },
            { "__is_target_variant_os", 0, FPP_KW_IS_TARGET_VARIANT_OS },
            { "__is_target_variant_environment", 0, FPP_KW_IS_TARGET_VARIANT_ENVIRONMENT },
            { "once", 0, FPP_KW_ONCE },
            { "printf", 0, FPP_KW_FORMAT_FUNCTION },
            { "fprintf", 0, FPP_KW_FORMAT_FUNCTION },
            { "sprintf", 0, FPP_KW_FORMAT_FUNCTION },
            { "snprintf", 0, FPP_KW_FORMAT_FUNCTION },
            { "memcpy", 0, FPP_KW_FORTIFY_MEMORY_FUNCTION },
            { "memmove", 0, FPP_KW_FORTIFY_MEMORY_FUNCTION },
            { "memset", 0, FPP_KW_FORTIFY_MEMORY_FUNCTION },
            { "memcmp", 0, FPP_KW_MEMACCESS_FUNCTION },
            { "bcmp", 0, FPP_KW_MEMACCESS_FUNCTION },
            { "bcopy", 0, FPP_KW_FORTIFY_BCOPY_FUNCTION },
            { "bzero", 0, FPP_KW_FORTIFY_BZERO_FUNCTION },
            { "mempcpy", 0, FPP_KW_FORTIFY_MEMORY_FUNCTION },
            { "memccpy", 0, FPP_KW_MEMACCESS_FUNCTION },
            { "strncpy", 0, FPP_KW_FORTIFY_LIMITED_FUNCTION },
            { "strncat", 0, FPP_KW_FORTIFY_LIMITED_FUNCTION },
            { "strncmp", 0, FPP_KW_MEMACCESS_FUNCTION },
            { "strncasecmp", 0, FPP_KW_MEMACCESS_FUNCTION },
            { "strndup", 0, FPP_KW_MEMACCESS_FUNCTION },
            { "stpncpy", 0, FPP_KW_FORTIFY_LIMITED_FUNCTION },
            { "strcpy", 0, FPP_KW_STRING_COPY_FUNCTION },
            { "stpcpy", 0, FPP_KW_STRING_COPY_FUNCTION },
            { "strcat", 0, FPP_KW_STRING_COPY_FUNCTION },
            { "abs", 0, FPP_KW_ABS_FUNCTION },
            { "labs", 0, FPP_KW_ABS_FUNCTION },
            { "llabs", 0, FPP_KW_ABS_FUNCTION },
            { "imaxabs", 0, FPP_KW_ABS_FUNCTION },
            { "fabs", 0, FPP_KW_FABS_FUNCTION },
            { "fabsf", 0, FPP_KW_FABS_FUNCTION },
            { "fabsl", 0, FPP_KW_FABS_FUNCTION },
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
            t->va_opt_id = seeds[i].pp == FPP_KW_VA_OPT ? id : t->va_opt_id;
            if (seeds[i].pp >= FPP_KW_LINE_MACRO && seeds[i].pp <= BUSTER_FPP_KW_LAST_BUILTIN)
            {
                t->macro_bits[id >> 6] |= (UINT64_C(1) << (id & 63u));
            }
        }
        // t->cache comes from zero-filled anonymous mmap and fast_ident_intern_cstr does not populate it.
    }
    return ok;
}

// An identifier is fully determined by its first and last words up to 16 bytes.
// Only the short spelling needs a mask. Its caller guarantees eight readable bytes.
static inline uint32_t fast_ident_lookup(FastIdentTable* t, const uint8_t* p, uint32_t len, uint8_t* out_kind)
{
    uint64_t first;
    memcpy(&first, p, sizeof(first));
    uint64_t last = 0;
    uint64_t mix = 0;
    if (len <= 8u)
    {
        first = _bzhi_u64(first, len * 8u);
    }
    else
    {
        memcpy(&last, p + len - 8u, sizeof(last));
        mix = BUSTER_FIDENT_CACHE_MIX(last, len);
    }
    const uint64_t hash = BUSTER_FIDENT_CACHE_HASH(first, mix);
    FastIdentCacheSlot* slot = t->cache + (hash >> t->cache_shift);
    uint32_t id = 0;
    uint8_t kind = FTOK_IDENT;
    if (__builtin_expect(slot->len == len, 1))
    {
        const uint8_t* saved = (const uint8_t*)t->strings + slot->offset;
        bool equal;
        if (len <= 8u)
        {
            uint64_t word;
            memcpy(&word, saved, 8);
            equal = _bzhi_u64(word, len * 8u) == first;
        }
        else if (len <= 16u)
        {
            uint64_t saved_first;
            uint64_t saved_last;
            memcpy(&saved_first, saved, 8);
            memcpy(&saved_last, saved + len - 8u, 8);
            equal = first == saved_first && last == saved_last;
        }
        else
        {
            const __mmask64 mask = _bzhi_u64(UINT64_MAX, len);
            equal = _mm512_mask_cmpneq_epi8_mask(mask, _mm512_maskz_loadu_epi8(mask, p), _mm512_maskz_loadu_epi8(mask, saved)) == 0;
        }
        if (equal)
        {
            id = slot->id;
            kind = slot->tok_kind;
        }
    }
    if (__builtin_expect(id == 0, 0))
    {
        id = fast_ident_intern(t, p, len);
        if (id != 0)
        {
            const uint8_t kw = t->entries[id].kw_kind;
            kind = kw != 0 ? kw : FTOK_IDENT;
            if (len <= 64u)
            {
                if (__builtin_expect(t->count >= t->cache_grow_at, 0) && fast_ident_cache_grow(t))
                {
                    slot = t->cache + (hash >> t->cache_shift);
                }
                slot->offset = t->entries[id].str_offset;
                slot->id = (uint16_t)id;
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

// Raw string literals (#3600): R"d(...)d", in clang's GNU modes from C99 and the full pipeline's (pp_raw_strings), and
// also with C11's prefixes. The direct path doesn't lex one: a unit that has one leaves it, wherever the literal is,
// active text, a directive's line or a group the conditionals skip.

// Whether the identifier of `len` characters at `p`, a '"' after it, is a raw string literal's prefix: R or LR, and
// where `raw_strings` is 2 also u8R, uR and UR. Never where `raw_strings` is 0.
static bool fast_raw_prefix(const uint8_t* p, uint32_t len, uint8_t raw_strings)
{
    const bool utf = raw_strings == 2u;
    return raw_strings != 0 && ((len == 1u && p[0] == 'R') ||
                                (len == 2u && p[1] == 'R' && (p[0] == 'L' || (utf && (p[0] == 'u' || p[0] == 'U')))) ||
                                (len == 3u && utf && p[0] == 'u' && p[1] == '8' && p[2] == 'R'));
}

// Whether the '"' at `quote`, which opens a literal, opens a raw string literal: a raw prefix right before it, at a
// token's start (no identifier or number character, nor a byte past ASCII, before the prefix). Its callers ask only
// where the mode has raw string literals; `raw_strings` 2 takes C11's prefixes too.
static bool fast_raw_string_before(const uint8_t* base, const uint8_t* quote, uint8_t raw_strings)
{
    const uint8_t* start = quote - 1;
    bool result = quote > base && start[0] == 'R';
    if (result && raw_strings == 2u && start - base >= 2 && start[-2] == 'u' && start[-1] == '8')
    {
        start -= 2;
    }
    else if (result && start > base && (start[-1] == 'L' || (raw_strings == 2u && (start[-1] == 'u' || start[-1] == 'U'))))
    {
        start -= 1;
    }
    result = result && (start == base || ((fast_char_class[start[-1]] & 1u) == 0 && start[-1] < 0x80u));
    return result;
}

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

// A search directory's descriptor (FastPP.dir_fds): not opened yet, open, or not a directory that can be opened, where
// no search finds anything (#3586).
#define BUSTER_FPP_DIR_UNOPENED    0u
#define BUSTER_FPP_DIR_OPEN        1u
#define BUSTER_FPP_DIR_MISSING     2u

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
    uint16_t plain_count;    // a function-like macro's parameters its body names plainly (not after '#' or '##', not
    uint32_t plain_start;    // before '##'), each once in the order of its first such name: macro_params[plain_start..]
    uint16_t use_count;      // a body without '#' and '##': where each parameter's name stands (position << 8 | number),
    uint8_t plain_subst;     // macro_params[plain_start + plain_count..], which the substitution copies around
    uint8_t language_defined; // a __STDC macro of the predefines, or __FLT_EVAL_METHOD__: the full pipeline's builtin (#3577)
};

// #3577: a macro's definition as written after its name, to the line's end, for a redefinition's check
// (fast_pp_same_definition): FastPP's macro_texts, beside the macros every expansion reads.
typedef struct FastMacroText FastMacroText;
struct FastMacroText
{
    const uint8_t* text;
    uint32_t length;
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
    uint32_t dependency;     // -MD (#3592): the file's record + 1, when the request collects them
    uint8_t guard_state;     // 0=start, 1=saw_ifndef, 2=saw_define, 3=invalidated, 4=closed
    bool at_bol;
    bool owned_mmap;
    bool is_system;          // a system header's inclusion: the file's own level, or its includer's (#3592)
};

// A window of a file's raw tokens (#2850). The lexer's 64-byte loop writes it and does nothing else: no identifier is
// looked up there and no macro asked about, so the loop has no per-token branch, and the readers below it (the bulk
// emitter, the directives' and the macro arguments' reader) take their tokens from it and never lex a byte twice.
// A token is its kind as the classifier gives it (FTOK_IDENT, FTOK_INT_LIT for a number's run, FTOK_STR_LIT and
// FTOK_CHAR_LIT for a plain literal's opening quote, or a punctuator's), its first byte's offset from the window's
// first byte (16 bits: a window's tokens start within 64 KB of it), and its length (a string's where a byte holds it,
// else 0). Whatever the loop can't decide that way ends the window, and fast_lex_one lexes the token there, as it lexes
// every token of a file's last 80 bytes.
#define BUSTER_FWIN_CAPACITY       2048u
// The window's arrays, one region: kinds, lengths, then first bytes' offsets from the window's first byte (16 bits),
// each with 64 slots past the capacity (the loop stores whole vectors).
#define BUSTER_FWIN_LENS_OFFSET    (BUSTER_FWIN_CAPACITY + 64u)
#define BUSTER_FWIN_STARTS_OFFSET  (2u * (BUSTER_FWIN_CAPACITY + 64u))
#define BUSTER_FWIN_REGION_BYTES   (4u * (BUSTER_FWIN_CAPACITY + 64u))
// A window's tokens start fewer than 65,536 bytes after its first byte: a block starts below this distance.
#define BUSTER_FWIN_SPAN           (65536u - 64u)
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
    uint16_t* starts;        // a token's first byte, from the window's
    uint8_t* lens;
    uint32_t origin;         // the window's first byte's offset in the file
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

// A file the compile has opened, by its path (#3586): its mapping, which the compile keeps, so that a header with no
// guard, as glibc's <bits/mathcalls.h> and clang's <stddef.h>, is included again with no system call (sqlite3.c
// opened 155 files 216 times, and mapped and unmapped each time), and its path. In the persistent arena, where only a
// file the compile opens writes one: the guard table's entries stay 16 bytes, and the table, which FastPP holds and
// every unit clears, as small as it was.
typedef struct FastFileRecord FastFileRecord;
struct FastFileRecord
{
    const uint8_t* bytes;    // the file's text, or NULL where no mapping is kept
    uint64_t size;           // the mapping's size: 0 for an empty file, which maps nothing
    char path[];             // NUL-terminated
};

typedef struct FastGuardEntry FastGuardEntry;
struct FastGuardEntry
{
    uint32_t hash;
    uint32_t record;         // the file's FastFileRecord, by its offset in the persistent arena
    uint16_t path_len;
    uint8_t once;
    uint8_t used;
    uint32_t guard_ident;
};

// -MD and its kin (#3592): the most files a unit that collects them may read; and in a guard entry's record (FastPP's
// guard_dependencies), the bit that #pragma GCC system_header set, which makes the file a system header from then on,
// in its later inclusions too (clang's MarkFileSystemHeader, the full pipeline's PPFile.system).
#define BUSTER_FPP_MAX_DEPENDENCIES 4096u
#define BUSTER_FPP_SYSTEM_PRAGMA    0x80000000u

// A path an #include's search found no readable file at: the search asks once (#2850). A header that the third
// directory holds is asked for in the first two each time it is included, 3 times in 4 of sqlite3.c's 290 includes.
#define BUSTER_FPP_MAX_MISSING 1024u
typedef struct FastMissingPath FastMissingPath;
struct FastMissingPath
{
    uint32_t hash;           // 0 for an empty slot
    uint32_t length;
    const char* path;
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
    uint32_t macro_idx;      // its FastMacro's index, which no directive changes while the call runs
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
    uint8_t exp_arg_in_argbuf[BUSTER_FPP_MAX_PARAMS + 1u];
    uint8_t tok_flags;
    uint8_t state;
    bool allow_file;
    // The variadic argument was left out (clang's isVarargsElidedUse): F(x) for F(x, ...), and F() for F(...) or
    // F(x, ...). Only then does `, ## __VA_ARGS__` drop its comma; F(x,) passes an empty one and keeps it.
    bool va_elided;
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
    // One bit a literal: spelled with an encoding prefix or a numeric escape, which an attribute's string and an asm
    // label can't take as their bytes (#3595). The arena's pages are zero, and only such a literal writes one.
    uint64_t* lit_coded;

    // Macro definitions
    FastMacro* macros;
    FastMacroText* macro_texts; // each macro's text as written (#3577), beside macros
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
    // Each directory's descriptor (O_PATH), opened by its first search: BUSTER_FPP_DIR_* (#3586)
    int dir_fds[BUSTER_FPP_MAX_DIRS];
    uint8_t dir_states[BUSTER_FPP_MAX_DIRS];
    uint32_t dir_count;
    uint32_t angled_first;

    // Include guard cache (open-addressed)
    FastGuardEntry guards[BUSTER_FPP_MAX_GUARDS];
    // The paths the include search found nothing at (open-addressed)
    FastMissingPath missing[BUSTER_FPP_MAX_MISSING];
    uint32_t missing_count;

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

    // -MD and its kin (#3592), when the request collects them (dependency_records isn't NULL): the files the unit read,
    // a record each in the order of their first reading, and their devices and inodes, by which a file's record is found
    // (fast_pp_dependency_find); a guard entry's record (by the entry's place), for an include its guard or #pragma once
    // skips; the record the last file reading made or found; and the main file's record.
    CDirectDependency* dependency_records;
    uint64_t* dependency_devices;
    uint64_t* dependency_inodes;
    uint32_t* guard_dependencies;
    uint32_t dependency_count;
    uint32_t dependency_last;
    uint32_t dependency_main;
    // The main file's push, or 0 before it: #pragma once and GCC system_header there are the full pipeline's warnings
    // (#3577), which leave the direct path.
    uint32_t main_serial;

    // Current source state
    uint32_t counter_val;
    const char* base_file;
    bool bailout;
    const char* bailout_reason;
    bool unsigned_char;
    bool c23;
    bool strict_iso;          // CDirectRequest.strict_iso: no GNU extensions (-std=cNN)
    bool trigraphs;           // CDirectRequest.trigraphs: a file with a trigraph leaves the direct path

    // #3577: what the full pipeline answers and checks.
    uint32_t (*feature_query)(void* context, uint32_t query, const uint8_t* name, uint32_t length); // __has_* and __is_target_*
    void* feature_context;
    bool in_predefines;       // running the predefines: a __STDC macro defined there is a builtin
    bool time_made;           // __DATE__'s and __TIME__'s text, made at the first use
    char date_text[16];
    char time_text[16];
    // Included files stay mapped until the end, so that a macro's text can be read again by a redefinition.
    const uint8_t** mapped_bases;
    uint64_t* mapped_sizes;
    uint32_t mapped_count;
    uint32_t mapped_cap;
    uint8_t raw_strings;      // CDirectRequest.raw_strings: a raw string literal leaves the direct path (#3600)
};

static void fast_pp_read_tok(FastPP* pp, FastIncludeFile* f, bool stop_at_eol, FastTok* out_tok);
static uint32_t fast_pp_read_line(FastPP* pp, FastIncludeFile* f, FastTok* out, uint32_t capacity);

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
    if (__builtin_expect(((uint32_t)(pp->lit_count < pp->lit_cap) & (uint32_t)(pp->lit_bytes_used + len + 1u <= pp->lit_bytes_cap)) != 0, 1))
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
// Whether an integer literal's suffix is one C has (#3577): u or U, and l, L, ll or LL (the two of one case), in either
// order; the full pipeline refuses any other.
static bool fast_integer_suffix_valid(const uint8_t* s, uint32_t n)
{
    uint32_t i = 0;
    const bool u_first = n != 0 && (s[0] == 'u' || s[0] == 'U');
    i += u_first ? 1u : 0u;
    if (i < n && (s[i] == 'l' || s[i] == 'L'))
    {
        const uint8_t l = s[i];
        i += 1u;
        if (i < n && s[i] == l) { i += 1u; }
    }
    if (!u_first && i < n && (s[i] == 'u' || s[i] == 'U')) { i += 1u; }
    return i == n;
}

// Whether a floating literal's text before its exponent has a digit other than 0 (#3577): such a literal that reads
// as 0 underflowed, which the full pipeline warns of (-Wliteral-range).
static __attribute__((cold, noinline)) bool fast_float_mantissa_nonzero(const char* text, uint32_t length)
{
    const bool hex = length > 1u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    bool nonzero = false;
    for (uint32_t i = hex ? 2u : 0u; i < length; i += 1)
    {
        const char c = text[i];
        if (hex ? (c == 'p' || c == 'P') : (c == 'e' || c == 'E')) { break; }
        nonzero = nonzero || (c >= '1' && c <= '9') || (hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
    }
    return nonzero;
}

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
        uint32_t float_kind = FTOK_FLOAT_LIT;
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
            double d = strtod(buf, &ep);
            // #3577: the literal is read whole but for an f or F suffix (another, a long double's l or L included, is
            // refused: the full pipeline reads them as it does), and its magnitude is in its type's range and doesn't
            // round to 0 (-Wliteral-range); a float's value is read as a float, as the full pipeline rounds it once.
            const uint32_t rest = (uint32_t)(buf + len - ep);
            const bool single = rest == 1u && (ep[0] == 'f' || ep[0] == 'F');
            const bool extended = rest == 1u && (ep[0] == 'l' || ep[0] == 'L');
            if (single)
            {
                d = (double)strtof(buf, NULL);
            }
            if ((rest != 0 && !single && !extended) || (!extended && (isinf(d) || (d == 0.0 && fast_float_mantissa_nonzero(buf, len)))))
            {
                float_kind = FTOK_BAD_LITERAL;
            }
            uint64_t bits = 0;
            memcpy(&bits, &d, 8);
            float_data = fast_pp_add_num(pp, bits);
            float_extra = (uint16_t)fast_pp_add_lit(pp, buf, len);
        }
        else
        {
            fast_pp_bail(pp, "long float literal");
        }
        return (FastLexed){ .p = p, .tok = fast_tok_make(float_kind, float_flags, float_extra, float_data) };
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
    // #3577: a suffix C has, digits after 0x or 0b, and a value in 64 bits, or the full pipeline's error where the
    // number is used (FTOK_BAD_LITERAL).
    uint32_t too_large = (uint32_t)((is_hex || is_bin) && dig_end <= dig + 2);
    if (dig_end != p && !fast_integer_suffix_valid(dig_end, (uint32_t)(p - dig_end)))
    {
        too_large = 1u;
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
            too_large |= (uint32_t)((val >> 60) != 0);
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
            too_large |= (uint32_t)((val >> 63) != 0);
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
            too_large |= (uint32_t)((val >> 61) != 0);
            val = (val << 3) | (uint64_t)(c - '0');
        }
    }
    else
    {
        // #3577: past 2^64 - 1 the full pipeline's error; only 20 digits or more can get there.
        const bool long_literal = dig_end - dig >= 20;
        while (dig < dig_end)
        {
            const uint8_t c = *dig++;
            if (c < '0' || c > '9')
            {
                fast_pp_bail(pp, "invalid dec literal");
                break;
            }
            if (long_literal)
            {
                too_large |= (uint32_t)(val > (UINT64_MAX - (uint64_t)(c - '0')) / 10u);
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
    const uint32_t int_kind = FTOK_INT_LIT ^ ((FTOK_INT_LIT ^ FTOK_BAD_LITERAL) & (UINT32_C(0) - (uint32_t)(too_large != 0)));
    // #3577: a base or a suffix keeps the spelling, which # and ## give as written.
    uint32_t int_extra = 0;
    if ((int_flags & (BUSTER_FTOK_UNSIGNED | BUSTER_FTOK_LONG | BUSTER_FTOK_LONGLONG | BUSTER_FTOK_HEX)) != 0)
    {
        int_extra = fast_pp_add_lit(pp, (const char*)start, len);
    }
    return (FastLexed){ .p = p, .tok = fast_tok_make(int_kind, int_flags, int_extra, int_data) };
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
                // #3577: past a byte the full pipeline's error ("hex escape sequence out of range"), a wide literal's too
                // (refused here); the bound is checked at each digit, so that leading zeros stay valid.
                bool any = false;
                bool big = false;
                while (p < end)
                {
                    const uint8_t h = *p;
                    if (h >= '0' && h <= '9') { val = (val << 4) | (uint32_t)(h - '0'); p += 1; any = true; }
                    else if (h >= 'a' && h <= 'f') { val = (val << 4) | (uint32_t)(h - 'a' + 10); p += 1; any = true; }
                    else if (h >= 'A' && h <= 'F') { val = (val << 4) | (uint32_t)(h - 'A' + 10); p += 1; any = true; }
                    else { break; }
                    big = big || val > 0xffu;
                }
                *ok = *ok && any && !big;
            } break;
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
            {
                val = (uint32_t)(c - '0');
                for (uint32_t i = 0; i < 2u && p < end && *p >= '0' && *p <= '7'; i += 1)
                {
                    val = (val << 3) | (uint32_t)(*p++ - '0');
                }
                // #3577: \400 to \777 are out of a byte's range, the full pipeline's error.
                *ok = *ok && val <= 0xffu;
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
    bool coded = prefix_kind != 0;
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
                        // A numeric escape (a backslash and x, an octal digit, u or U) is one clang refuses in an attribute.
                        coded = coded || (q < q_end && (*q == 'x' || (*q >= '0' && *q <= '7') || *q == 'u' || *q == 'U'));
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
            pp->lit_slices[idx] = (FastLitSlice){ .offset = off, .length = out_len, .escaped = 1u };
            tok_data = idx;
        }
        else
        {
            fast_pp_bail(pp, "lit_pool overflow");
        }
        if (coded)
        {
            pp->lit_coded[tok_data >> 6] |= UINT64_C(1) << (tok_data & 63u);
        }
    }
    return (FastLexed){ .p = p, .tok = fast_tok_make(tok_kind, flags_in, tok_extra, tok_data) };
}

// Lex character literal starting at '\''
static FastLexed fast_lex_char(FastPP* pp, const uint8_t* p, const uint8_t* end, uint32_t flags_in, uint16_t prefix_kind)
{
    // The spelling's start: the prefix (L, u or U, or u8) before the quote.
    const uint8_t* const spelling = p - (prefix_kind == 4u ? 2u : prefix_kind != 0 ? 1u : 0u);
    p += 1; // skip opening '\''
    uint32_t val = 0;
    uint32_t chars = 0;
    bool ok = true;
    bool escaped = false;
    FastTok tok = fast_tok_make(FTOK_EOF, flags_in, 0, 0);
    while (p < end && *p != '\'' && *p != '\n' && ok)
    {
        uint32_t ch = 0;
        if (*p == '\\')
        {
            p += 1;
            escaped = true;
            ch = fast_decode_escape(&p, end, &ok);
        }
        else
        {
            ch = *p++;
        }
        val = (val << 8) | (ch & 0xffu);
        chars += 1u;
    }
    // #3577: a constant of more than one character is the full pipeline's warning (-Wmultichar), or its error when wide.
    if (!ok || p >= end || *p != '\'' || chars != 1u)
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
        // #3577: an escape or a prefix keeps the spelling (BUSTER_FTOK_SPELLED), which # and ## give as written and
        // which tells a u, U or u8 constant, whose type isn't int, apart.
        uint32_t char_flags = flags_in;
        uint32_t char_extra = 0;
        if (escaped || prefix_kind != 0)
        {
            char_flags |= BUSTER_FTOK_SPELLED;
            char_extra = fast_pp_add_lit(pp, (const char*)spelling, (uint32_t)(p - spelling));
        }
        tok = fast_tok_make(FTOK_CHAR_LIT, char_flags, char_extra, val);
    }
    return (FastLexed){ .p = p, .tok = tok };
}

// The first byte at or after `p` that isn't part of a line splice: each backslash followed by blanks and a newline ("\r\n"
// and "\n\r" one newline) is passed, as the full pipeline's lexer reads them (c_lexer.c's escaped_newline_size).
static const uint8_t* fast_skip_splices(const uint8_t* p, const uint8_t* end)
{
    const uint8_t* at = p;
    bool spliced = true;
    while (spliced && at < end && *at == '\\')
    {
        const uint8_t* q = at + 1;
        while (q < end && (*q == ' ' || *q == '\t' || *q == '\f' || *q == '\v'))
        {
            q += 1;
        }
        spliced = q < end && (*q == '\n' || *q == '\r');
        if (spliced)
        {
            const uint8_t newline = *q;
            q += 1;
            q += q < end && (*q == '\n' || *q == '\r') && *q != newline ? 1 : 0;
            at = q;
        }
    }
    return at;
}

// Whether the backslash at `p` starts a universal character name: a 'u', 'U' or 'N' after it, past any line splices.
// The full pipeline's lexer reports a basic, control or surrogate character's in active text, a directive's line, a
// macro's definition, an unused argument and (#3655) a skipped group; the direct path leaves at every one (#3577).
static inline bool fast_backslash_starts_ucn(const uint8_t* p, const uint8_t* end)
{
    const uint8_t* const letter = fast_skip_splices(p + 1, end);
    return letter < end && (*letter == 'u' || *letter == 'U' || *letter == 'N');
}

// A stray backslash at `p` (not a splice's) that starts a UCN leaves the direct path for `reason`. Out of line and cold:
// such a backslash is rare, and its walk inline made the lexer's hot paths longer.
static __attribute__((noinline, cold)) void fast_pp_stray_backslash(FastPP* pp, const uint8_t* p, const uint8_t* end, const char* reason)
{
    if (fast_backslash_starts_ucn(p, end))
    {
        fast_pp_bail(pp, reason);
    }
}

// Whether the quote at `s`, past the file's first byte, may be C23's digit separator, which opens no literal: after a
// name's or a number's character, a '.', or an exponent's sign (`1'000`, `1.'5`, `1e+'5`; a prefix's quote too, `L'x'`).
// Out of line and cold, as the skip of an inactive group asks it only where a literal hides an event.
static __attribute__((noinline, cold)) bool fast_quote_may_separate(const uint8_t* base, const uint8_t* s)
{
    const uint8_t before = s[-1];
    const bool sign = (before == '+' || before == '-') && s - 1 > base && ((s[-2] | 0x20) == 'e' || (s[-2] | 0x20) == 'p');
    return (fast_char_class[before] & 1u) != 0 || before == '.' || sign;
}

// The text after the line splices that start at `p` (fast_skip_splices's), with the newlines they hold in *lines; `p`
// where no splice starts there.
static __attribute__((noinline, cold)) const uint8_t* fast_pass_splices(const uint8_t* p, const uint8_t* end, uint32_t* lines)
{
    const uint8_t* const joined = fast_skip_splices(p, end);
    uint32_t newlines = 0;
    for (const uint8_t* c = p; c < joined; c += 1)
    {
        newlines += *c == '\n' ? 1u : 0u;
    }
    *lines = newlines;
    return joined;
}

// A conditional's keyword (FPP_KW_*) for the name of `length` bytes at `name`, or 0.
static uint32_t fast_conditional_keyword(const uint8_t* name, uint32_t length)
{
    uint32_t kw = 0;
    if (length == 2u && name[0] == 'i' && name[1] == 'f') { kw = FPP_KW_IF; }
    else if (length == 5u && memcmp(name, "ifdef", 5) == 0) { kw = FPP_KW_IFDEF; }
    else if (length == 6u && memcmp(name, "ifndef", 6) == 0) { kw = FPP_KW_IFNDEF; }
    else if (length == 5u && memcmp(name, "endif", 5) == 0) { kw = FPP_KW_ENDIF; }
    else if (length == 4u && memcmp(name, "else", 4) == 0) { kw = FPP_KW_ELSE; }
    else if (length == 4u && memcmp(name, "elif", 4) == 0) { kw = FPP_KW_ELIF; }
    else if (length == 7u && memcmp(name, "elifdef", 7) == 0) { kw = FPP_KW_ELIFDEF; }
    else if (length == 8u && memcmp(name, "elifndef", 8) == 0) { kw = FPP_KW_ELIFNDEF; }
    return kw;
}

// A backslash at `at` right after a skipped directive's name, or after its '#' and blanks (#3577): where it starts a
// line splice, the name goes on past it. Where the name the full pipeline reads then is another conditional than the one
// the skip read (`#el\` + newline + `se` is an #else, `#endif\` + newline + `x` no directive), or a backslash in the
// name starts no splice (a UCN's), the unit leaves the direct path. Text before `at` that isn't a '#', blanks and a name
// is no directive's, and is left as it is.
static __attribute__((noinline, cold)) void fast_check_spliced_directive(FastPP* pp, const uint8_t* base, const uint8_t* at, const uint8_t* end)
{
    const uint8_t* name = at;
    while (name > base && (fast_char_class[name[-1]] & 1u) != 0)
    {
        name -= 1;
    }
    const uint8_t* after_hash = name;
    while (after_hash > base && (fast_char_class[after_hash[-1]] & 8u) != 0)
    {
        after_hash -= 1;
    }
    if (after_hash > base && after_hash[-1] == '#')
    {
        const uint32_t read = fast_conditional_keyword(name, (uint32_t)(at - name));
        const uint8_t* p = name;
        bool leave = false;
        bool before = true;
        while (before && !leave && p < end)
        {
            const uint8_t* const joined = fast_skip_splices(p, end);
            before = joined != p || (fast_char_class[*p] & 8u) != 0;
            p = joined != p ? joined : p + (before ? 1 : 0);
        }
        uint8_t spelled[8] = { 0 };
        uint32_t length = 0;
        bool more = true;
        while (more && !leave && p < end)
        {
            if ((fast_char_class[*p] & 1u) != 0)
            {
                spelled[length & 7u] = *p;
                length += 1;
                p += 1;
            }
            else if (*p == '\\')
            {
                const uint8_t* const joined = fast_skip_splices(p, end);
                leave = joined == p;
                p = joined;
            }
            else
            {
                more = false;
            }
        }
        if (leave || fast_conditional_keyword(spelled, length) != read)
        {
            fast_pp_bail(pp, "directive name across a line splice in a skipped group");
        }
    }
}

// The skip's event at a backslash that may start a line splice (#3577): the text after the splices, with their newlines
// in *lines. A backslash before blanks that starts none leaves the direct path.
static __attribute__((noinline, cold)) const uint8_t* fast_skip_line_splices(FastPP* pp, const uint8_t* base, const uint8_t* at, const uint8_t* end,
                                                                             uint32_t* lines)
{
    fast_check_spliced_directive(pp, base, at, end);
    const uint8_t* joined = fast_pass_splices(at, end, lines);
    if (joined == at)
    {
        fast_pp_bail(pp, "backslash before a blank in a skipped group");
        joined = end;
    }
    return joined;
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
            const uint8_t after = p + len < end ? p[len] : 0u;
            // A raw string literal where the mode has them (#3600): its prefix is an identifier with a '"' right after it.
            if (__builtin_expect(after == '"', 0) && fast_raw_prefix(p, len, pp->raw_strings))
            {
                fast_pp_bail(pp, "raw string literal");
                p = end;
                break;
            }
            // Check for backslash-newline splice continuing identifier
            if (__builtin_expect(after == '\\', 0))
            {
                fast_pp_bail(pp, "spliced identifier");
                p = end;
                break;
            }
            uint8_t kw_resolved = FTOK_IDENT;
            const uint32_t id = ((size_t)(end - p) >= 8u) ?
                                fast_ident_lookup(&pp->idents, p, len, &kw_resolved) :
                                fast_ident_intern(&pp->idents, p, len);
            if (__builtin_expect(id == 0, 0))
            {
                fast_pp_bail(pp, "ident table full");
                p = end;
                break;
            }
            if ((size_t)(end - p) < 8u)
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
                // #3577: a stray backslash that starts a UCN, in text, a definition or an argument nothing uses.
                if (__builtin_expect(c == '\\', 0))
                {
                    fast_pp_stray_backslash(pp, p, end, "universal character name");
                }
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
//   way this function always did (to its closing quote or its line's end, a backslash taking the next character);
// - where the mode has raw string literals (#3600), a '"' right after an 'R' that a raw prefix at a token's start may
//   end (the rare block with an R before a '"' tests each, so "ERROR" makes none): decided the same way, one literal
//   of its line at a time, until it is inside one or opens one. A literal that a raw prefix opens leaves the direct
//   path. Without raw string literals the blocks don't look for them.
// - a line splice whose newline the line starts would take for a line's end (#3577), seen at the byte after its
//   backslash: decided as a comment is, a blank, so that a '#' after it starts a directive only where its line had
//   nothing but blanks before the splice; and one at the block's first byte, which may go on a directive's name;
// - a universal character name's start, seen at the byte after a backslash: a 'u', 'U' or 'N', or another backslash,
//   which a line splice between the backslash and its letter starts. The full pipeline's raw lexer still reports a
//   basic, control or surrogate character's UCN that starts a token in a skipped group (#3655, `pp_read_skipped_ucns`),
//   not one in a literal or a comment; a UCN's start outside them leaves the direct path (#3577), whatever it names.
// Everything else (a '#' inside a line, a lone '/', quotes with nothing after them to hide) is passed in bulk.
// f->at_bol is false after a directive; where the file ends first the compile fails and nothing reads it.
static inline __attribute__((always_inline)) uint8_t fast_pp_skip_inactive_in(FastPP* pp, FastIncludeFile* f, const bool raw_on)
{
    const uint8_t* p = f->ptr;
    const uint8_t* const end = f->end;
    uint32_t line = f->line;
    uint32_t nested = 0;
    uint8_t found_kw = 0;
    // #3577: a nested group's #else seen (a bit for each depth, 1 to 63; deeper is refused), and what the full pipeline
    // diagnoses in a skipped group: a nested #else or #elif after its group's #else, an #elifdef or #elifndef before C23.
    uint64_t nested_else = 0;
    uint32_t skipped_bad = 0;
    // What crosses a block's end.
    uint64_t bol_in = f->at_bol ? 1u : 0u;   // the text before p ends in a line's leading blanks
    uint64_t taint_in = 0;                   // a quote is before p on its line
    uint64_t backslash_in = 0;               // the byte before p is a backslash
    uint64_t slash_in = 0;                   // the byte before p is a '/'
    uint64_t slash_first = 0;                // ... which is its line's first character
    uint64_t slash_tainted = 0;              // ... and is tainted
    uint64_t r_in = 0;                       // the byte before p is an 'R' (only where the mode has raw strings)
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
    const __m512i v_upper_r = _mm512_set1_epi8('R');
    const __m512i v_case = _mm512_set1_epi8(0x20);
    const __m512i v_lower_u = _mm512_set1_epi8('u');
    const __m512i v_upper_n = _mm512_set1_epi8('N');

    // Two loops (#3577): the inner one passes the blocks with no event and leaves at a block with one, with the masks the
    // event's decision reads; the outer one decides it. On their own, the inner loop's registers don't depend on the
    // decisions' calls and code: as one loop, sqlite3.c's skips took 8.16 M instructions on the 9700X, and any change to a
    // decision moved that count by up to half a million; as two, 7.44 M with this function's splice reads.
    while (p < end && !done)
    {
        __mmask64 events = 0;
        __mmask64 nl = 0;
        __mmask64 first = 0;
        __mmask64 tainted = 0;
        __mmask64 directives = 0;
        __mmask64 ucns = 0;
        __mmask64 raws = 0;
        while (events == 0 && p < end)
        {
            uint32_t avail = 0;
            const __m512i v = fast_load64_bounded(p, end, &avail);
            const __mmask64 valid = _bzhi_u64(~UINT64_C(0), avail);
            nl = _mm512_cmpeq_epi8_mask(v, v_nl);
            const __mmask64 hash = _mm512_cmpeq_epi8_mask(v, v_hash);
            const __mmask64 slash = _mm512_cmpeq_epi8_mask(v, v_slash);
            const __mmask64 star = _mm512_cmpeq_epi8_mask(v, v_star);
            const __mmask64 dq = _mm512_cmpeq_epi8_mask(v, v_dq);
            const __mmask64 quote = dq | _mm512_cmpeq_epi8_mask(v, v_sq);
            const __mmask64 backslash = _mm512_cmpeq_epi8_mask(v, v_bs);
            // Blanks are fast_char_class's 8: ' ', and '\t' '\v' '\f' '\r' (9 to 13 without the newline). The bytes past
            // the file's end count as blanks, so that what is carried out of the block is the state at the file's end.
            const __mmask64 blank = _mm512_cmpeq_epi8_mask(v, v_space) | (_mm512_cmple_epu8_mask(_mm512_sub_epi8(v, v_tab), v_four) & ~nl) | ~valid;

            const __mmask64 starts = (nl << 1) | bol_in;
            unsigned long long lead_sum = 0;
            const uint64_t lead_out = _addcarry_u64(0, blank, starts & blank, &lead_sum);
            first = (starts | lead_sum) & ~blank;

            const __mmask64 opens = (star | slash) & ((slash << 1) | slash_in);

            const __mmask64 passes = ~(nl & ~((backslash << 1) | backslash_in));
            unsigned long long taint_sum = 0;
            const uint64_t taint_out = _addcarry_u64((unsigned char)taint_in, passes, quote, &taint_sum);
            tainted = passes & ~taint_sum;

            directives = hash & first;
            // A UCN's letters are looked for only in a block with a byte after a backslash, which most blocks have none
            // of, and so are line splices.
            const __mmask64 escaped = (backslash << 1) | backslash_in;
            ucns = 0;
            if (__builtin_expect(escaped != 0, 0))
            {
                ucns = (_mm512_cmpeq_epi8_mask(_mm512_or_si512(v, v_case), v_lower_u) | _mm512_cmpeq_epi8_mask(v, v_upper_n) | backslash) & escaped;
                // #3577: a line splice's newline (after a backslash and any blanks) ends no line, but the line starts
                // above take it for one. That misreads a line's first character only where the text after the splice,
                // past its blanks, is a '#', a '/' or a backslash (the bytes whose first-ness the decisions ask), or where
                // the splice or those blanks reach the block's end. A splice at the block's first byte follows a decision
                // that stopped at it, a directive's name or the blanks after its '#', which it may go on. There the byte
                // after each backslash that a splice may start with is an event, decided as a comment is.
                const __mmask64 heads = escaped & (nl | blank);
                unsigned long long run = 0;
                const uint64_t run_out = _addcarry_u64(0, blank, escaped & blank, &run);
                const __mmask64 spliced = nl & (escaped | run);
                unsigned long long lead = 0;
                const uint64_t lead_on = _addcarry_u64(0, blank, (spliced << 1) & blank, &lead);
                // The masks the loop computes above, again from a copy of the block the compiler can't see through:
                // shared, they would be kept from above to here in registers the loop spills.
                __m512i opaque = v;
                __asm__("" : "+v"(opaque));
                const __mmask64 marks = _mm512_cmpeq_epi8_mask(opaque, v_hash) | _mm512_cmpeq_epi8_mask(opaque, v_slash) | backslash;
                const uint64_t misread = (((spliced << 1) | lead) & marks) | lead_on | run_out | (spliced >> 63) | (heads & 2u);
                ucns |= misread != 0 ? heads : 0u;
            }
            raws = 0;
            uint64_t r_out = 0;
            if (raw_on)
            {
                const __mmask64 upper_r = _mm512_cmpeq_epi8_mask(v, v_upper_r);
                uint64_t candidates = dq & ((upper_r << 1) | r_in);
                r_out = upper_r >> 63;
                while (__builtin_expect(candidates != 0, 0))
                {
                    const uint32_t at = (uint32_t)__builtin_ctzll(candidates);
                    candidates &= candidates - 1u;
                    raws |= fast_raw_string_before(f->base, p + at, pp->raw_strings) ? UINT64_C(1) << at : 0u;
                }
            }
            events = (directives | opens | raws | ucns) & valid;
            if (__builtin_expect(events == 0, 1))
            {
                line += (uint32_t)__builtin_popcountll(nl);
                bol_in = lead_out | (nl >> 63);
                taint_in = taint_out;
                backslash_in = backslash >> 63;
                slash_in = slash >> 63;
                slash_first = first >> 63;
                slash_tainted = tainted >> 63;
                r_in = r_out;
                p += avail;
            }
        }
        if (events != 0)
        {
            const uint32_t pos = (uint32_t)__builtin_ctzll(events);
            const bool directive = ((directives >> pos) & 1u) != 0;
            const bool raw = ((raws >> pos) & 1u) != 0;
            const bool ucn = ((ucns >> pos) & 1u) != 0;
            // A comment's event is at its second character, and a UCN's at the byte after its backslash.
            const uint32_t slash_pos = pos - 1u;
            const uint8_t* const at = directive || raw ? p + pos : p + pos - 1;
            const bool at_first = directive || (pos != 0 ? ((first >> slash_pos) & 1u) != 0 : slash_first != 0);
            const bool at_tainted = raw || (directive ? ((tainted >> pos) & 1u) != 0 : (pos != 0 ? ((tainted >> slash_pos) & 1u) != 0 : slash_tainted != 0));
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
                // A raw event's own quote may be the first.
                const uint8_t* const stop = raw ? at + 1 : at;
                while (s < stop && *s != '"' && *s != '\'')
                {
                    s += 1;
                }
                if (raw_on && s < stop && *s == '"' && fast_raw_string_before(f->base, s, pp->raw_strings))
                {
                    fast_pp_bail(pp, "raw string literal in a skipped group");
                    next = end;
                    done = true;
                    decided = true;
                }
                else if (pp->c23 && s < stop && *s == '\'' && s > f->base && fast_quote_may_separate(f->base, s))
                {
                    // #3577: C23's digit separator (`1'000`, `1.'5`, `1e+'5`) opens no literal, so what this one would hide
                    // (a comment's start, a UCN the full pipeline reports) isn't hidden; a quote after a name's or a
                    // number's character, a '.', or an exponent's sign (a prefix's too, `L'x'`) leaves.
                    fast_pp_bail(pp, "quote after a name or number in a skipped group");
                    next = end;
                    done = true;
                    decided = true;
                }
                else if (s < stop)
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
                        if (*s == '\\' && s + 1 < end && (fast_char_class[s[1]] & 8u) != 0)
                        {
                            // #3577: a splice with blanks or "\r\n" before its newline goes on too.
                            uint32_t splice_lines = 0;
                            const uint8_t* const joined = fast_pass_splices(s, end, &splice_lines);
                            line += splice_lines;
                            s = joined != s ? joined : s + 2;
                        }
                        else if (*s == '\\' && s + 1 < end)
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
                    skipped_bad |= ((uint32_t)(kw == FPP_KW_ELIFDEF) | (uint32_t)(kw == FPP_KW_ELIFNDEF)) & (uint32_t)!pp->c23;
                    if (kw == FPP_KW_IF || kw == FPP_KW_IFDEF || kw == FPP_KW_IFNDEF)
                    {
                        nested += 1u;
                        skipped_bad |= (uint32_t)(nested > 63u);
                        nested_else &= ~(UINT64_C(1) << (nested & 63u));
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
                    else if (kw != 0)
                    {
                        // A nested #else or #elif: after its group's #else, the full pipeline's error.
                        const uint64_t level = UINT64_C(1) << (nested & 63u);
                        skipped_bad |= (uint32_t)((nested_else & level) != 0);
                        nested_else |= level & (UINT64_C(0) - (uint64_t)(kw == FPP_KW_ELSE));
                    }
                }
            }
            // A splice's event, a newline or a blank after the backslash, is decided as a comment's (#3577).
            else if (!decided && ucn && at[1] != '\n' && (fast_char_class[at[1]] & 8u) == 0)
            {
                // The backslash and, past any line splices, its letter: a UCN's start leaves (#3577); a backslash before a
                // backslash that starts none is passed, and the text goes on from that one.
                const uint8_t* const letter = fast_skip_splices(at + 1, end);
                if (letter < end && (*letter == 'u' || *letter == 'U' || *letter == 'N'))
                {
                    fast_pp_bail(pp, "universal character name in a skipped group");
                    next = end;
                    done = true;
                }
                else
                {
                    next = at + 1;
                }
            }
            else if (!decided)
            {
                // A comment is a blank: the line's first character may still follow it. Its lines are counted apart, so
                // that the loop's count stays in a register. So is a line splice (#3577), the event at a newline or a blank
                // after a backslash.
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
                else if (at[1] == '/')
                {
                    next = fast_skip_line_comment(at + 2, end, &comment_lines);
                }
                else
                {
                    next = fast_skip_line_splices(pp, f->base, at, end, &comment_lines);
                }
                line += comment_lines;
            }

            p = next;
            clean_from = next;
            bol_in = next_bol;
            taint_in = 0;
            backslash_in = 0;
            slash_in = 0;
            r_in = 0;
        }
    }

    if (found_kw == 0 && !pp->bailout)
    {
        fast_pp_bail(pp, "unterminated #if");
    }
    if (skipped_bad != 0)
    {
        fast_pp_bail(pp, "skipped group the full pipeline diagnoses");
    }
    f->ptr = p;
    f->line = line;
    f->at_bol = bol_in != 0 && found_kw == 0;
    return found_kw;
}

// fast_pp_skip_inactive_in with the mode's raw string literals a constant: the loop is compiled once for each, so a
// mode without them does none of their work a block (#3600).
static BUSTER_DIRECT_HOT uint8_t fast_pp_skip_inactive(FastPP* pp, FastIncludeFile* f)
{
    return pp->raw_strings != 0 ? fast_pp_skip_inactive_in(pp, f, true) : fast_pp_skip_inactive_in(pp, f, false);
}

// ---------------------------------------------------------------------------------------------------------------------
// Include guard cache & file resolution

static inline uint32_t fast_guard_hash(const char* s, uint32_t len)
{
    const uint64_t h = fast_hash_ident((const uint8_t*)s, len);
    return (uint32_t)(h ^ (h >> 32)) | 1u;
}

static inline FastFileRecord* fast_guard_record(const FastPP* pp, const FastGuardEntry* e)
{
    return (FastFileRecord*)(void*)(pp->arena->base + e->record);
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
            FastFileRecord* const record = create ? (FastFileRecord*)fast_arena_alloc(pp->arena, sizeof(FastFileRecord) + len + 1u, 8) : NULL;
            if (record != NULL)
            {
                record->bytes = NULL;
                record->size = 0;
                memcpy(record->path, path, len);
                record->path[len] = '\0';
                e->hash = h;
                e->record = (uint32_t)((const uint8_t*)record - pp->arena->base);
                e->path_len = (uint16_t)len;
                e->once = 0;
                e->guard_ident = 0;
                e->used = 1;
                result = e;
            }
            break;
        }
        if (e->hash == h && e->path_len == len && memcmp(fast_guard_record(pp, e)->path, path, len) == 0)
        {
            result = e;
            break;
        }
        idx = (idx + 1u) & mask;
    }
    return result;
}

// Whether `len` bytes at `p` hold a trigraph, "??" and one of = ( / ) ' < ! > -, which a unit compiled with trigraphs
// (-std=c99, c11 or c17, or -trigraphs) replaces in translation phase 1, comments and literals included, and the direct
// path doesn't (#3592). 64 places a step from loads at p + i and p + i + 1, the last step's masked to `len`; where two
// '?' meet, the byte after them by a 64-entry table on its low six bits, for the bytes 0x20 to 0x3f.
_Alignas(64) static const uint8_t fast_trigraph_third[64] = {
    ['!' & 63] = 1, ['\'' & 63] = 1, ['(' & 63] = 1, [')' & 63] = 1, ['-' & 63] = 1,
    ['/' & 63] = 1, ['<' & 63] = 1, ['=' & 63] = 1, ['>' & 63] = 1,
};

static bool fast_has_trigraph(const uint8_t* p, uint64_t len)
{
    bool found = false;
    const __m512i question = _mm512_set1_epi8('?');
    const __m512i table = _mm512_load_si512((const void*)fast_trigraph_third);
    for (uint64_t i = 0; !found && i + 2u < len; i += 64u)
    {
        const uint64_t left = len - i;
        const __mmask64 first = left >= 64u ? ~UINT64_C(0) : _bzhi_u64(~UINT64_C(0), (uint32_t)left);
        const __mmask64 second = left - 1u >= 64u ? ~UINT64_C(0) : _bzhi_u64(~UINT64_C(0), (uint32_t)(left - 1u));
        const __m512i b0 = _mm512_maskz_loadu_epi8(first, (const void*)(p + i));
        const __m512i b1 = _mm512_maskz_loadu_epi8(second, (const void*)(p + i + 1u));
        const uint64_t pairs = _mm512_mask_cmpeq_epi8_mask(second, b0, question) & _mm512_mask_cmpeq_epi8_mask(second, b1, question);
        if (pairs != 0)
        {
            const __mmask64 third = left - 2u >= 64u ? ~UINT64_C(0) : _bzhi_u64(~UINT64_C(0), (uint32_t)(left - 2u));
            const __m512i b2 = _mm512_maskz_loadu_epi8(third, (const void*)(p + i + 2u));
            const __m512i marks = _mm512_permutexvar_epi8(b2, table);
            const uint64_t low = _mm512_cmpeq_epi8_mask(_mm512_and_si512(b2, _mm512_set1_epi8((char)0xc0)), _mm512_setzero_si512());
            found = (pairs & third & low & _mm512_test_epi8_mask(marks, marks)) != 0;
        }
    }
    return found;
}

// -MD and its kin (#3592): the tables of the files a unit reads, from the arena, whose pages are zero.
static __attribute__((cold, noinline)) bool fast_pp_collect_dependencies(FastPP* pp)
{
    pp->dependency_records = (CDirectDependency*)fast_arena_alloc(pp->arena, (uint64_t)BUSTER_FPP_MAX_DEPENDENCIES * sizeof(CDirectDependency), 64);
    pp->dependency_devices = (uint64_t*)fast_arena_alloc(pp->arena, (uint64_t)BUSTER_FPP_MAX_DEPENDENCIES * sizeof(uint64_t), 64);
    pp->dependency_inodes = (uint64_t*)fast_arena_alloc(pp->arena, (uint64_t)BUSTER_FPP_MAX_DEPENDENCIES * sizeof(uint64_t), 64);
    pp->guard_dependencies = (uint32_t*)fast_arena_alloc(pp->arena, (uint64_t)BUSTER_FPP_MAX_GUARDS * sizeof(uint32_t), 64);
    const bool ok = pp->dependency_records != NULL && pp->dependency_devices != NULL && pp->dependency_inodes != NULL && pp->guard_dependencies != NULL;
    pp->dependency_records = ok ? pp->dependency_records : NULL;
    return ok;
}

// The record of the file with this device and inode, + 1, or 0 for none: eight records a step, their inodes and devices
// compared as 64-bit lanes (#3592).
static uint32_t fast_pp_dependency_find(const FastPP* pp, uint64_t device, uint64_t inode)
{
    const __m512i want_device = _mm512_set1_epi64((long long)device);
    const __m512i want_inode = _mm512_set1_epi64((long long)inode);
    uint32_t result = 0;
    for (uint32_t i = 0; result == 0 && i < pp->dependency_count; i += 8u)
    {
        const uint32_t left = pp->dependency_count - i;
        const __mmask8 live = left >= 8u ? (__mmask8)0xffu : (__mmask8)((1u << left) - 1u);
        const __m512i devices = _mm512_maskz_loadu_epi64(live, (const void*)(pp->dependency_devices + i));
        const __m512i inodes = _mm512_maskz_loadu_epi64(live, (const void*)(pp->dependency_inodes + i));
        const uint32_t same = (uint32_t)(_mm512_mask_cmpeq_epi64_mask(live, devices, want_device) & _mm512_mask_cmpeq_epi64_mask(live, inodes, want_inode));
        result = same != 0 ? i + (uint32_t)__builtin_ctz(same) + 1u : 0u;
    }
    return result;
}

// A reading of the file at `path` (`length` bytes), with the device and inode its stat gave, as a system header's or not
// (#3592): a new record, under this spelling, or the file's, which a reading as a user file's makes a user file's, as the
// full pipeline's pp_dependency_observe has it. Returns the record + 1, or 0 when the unit read more files than the
// tables hold or memory ran out, and then the unit leaves.
static __attribute__((cold, noinline)) uint32_t fast_pp_dependency_observe(FastPP* pp, const char* path, uint32_t length, const struct stat* status,
                                                                          bool system)
{
    uint32_t result = fast_pp_dependency_find(pp, (uint64_t)status->st_dev, (uint64_t)status->st_ino);
    if (result != 0)
    {
        pp->dependency_records[result - 1u].system &= system;
    }
    else if (pp->dependency_count < BUSTER_FPP_MAX_DEPENDENCIES)
    {
        char* const copy = (char*)fast_arena_alloc(pp->arena, (uint64_t)length + 1u, 1);
        if (copy != NULL)
        {
            memcpy(copy, path, length);
            copy[length] = '\0';
            const uint32_t index = pp->dependency_count;
            pp->dependency_records[index] = (CDirectDependency){
                .path = copy,
                .length = length,
                .used = true,
                .system = system,
            };
            pp->dependency_devices[index] = (uint64_t)status->st_dev;
            pp->dependency_inodes[index] = (uint64_t)status->st_ino;
            pp->dependency_count = index + 1u;
            result = index + 1u;
        }
    }
    if (result == 0)
    {
        fast_pp_bail(pp, "too many files for a dependency rule");
    }
    pp->dependency_last = result;
    return result;
}

// A reading of the file a guard entry names, whose record the entry keeps (#3592): an include its guard or #pragma once
// skips, a reading from the mapping the compile kept of it (#3586), or a __has_include, of a file the unit read before.
// An entry without a record leaves.
static __attribute__((cold, noinline)) void fast_pp_dependency_again(FastPP* pp, const FastGuardEntry* ge, bool system)
{
    const uint32_t entry = pp->guard_dependencies[ge - pp->guards];
    const uint32_t record = entry & ~BUSTER_FPP_SYSTEM_PRAGMA;
    if (record != 0)
    {
        pp->dependency_records[record - 1u].system &= system || (entry & BUSTER_FPP_SYSTEM_PRAGMA) != 0;
    }
    else
    {
        fast_pp_bail(pp, "a file read without its dependency record");
    }
    pp->dependency_last = record;
}

static bool fast_pp_push_memory(FastPP* pp, const char* name, const uint8_t* bytes, uint64_t len, uint32_t dir_idx,
                                bool owned_mmap, bool is_system)
{
    bool ok = pp->file_depth < BUSTER_FPP_MAX_INCLUDES;
    if (!ok)
    {
        fast_pp_bail(pp, "include depth exceeded");
    }
    else if (pp->trigraphs && fast_has_trigraph(bytes, len))
    {
        fast_pp_bail(pp, "trigraph");
        ok = false;
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
                if (pp->dependency_records != NULL)
                {
                    pp->guard_dependencies[ge - pp->guards] = (pp->guard_dependencies[ge - pp->guards] & BUSTER_FPP_SYSTEM_PRAGMA) | f->dependency;
                }
            }
        }
        if (f->owned_mmap && f->base != NULL && f->map_size != 0)
        {
            // Unmapped at the end (fast_pp_release_files, #3577): the macros the file defined keep their text.
            if (pp->mapped_count < pp->mapped_cap)
            {
                pp->mapped_bases[pp->mapped_count] = f->base;
                pp->mapped_sizes[pp->mapped_count] = f->map_size;
                pp->mapped_count += 1u;
            }
            else
            {
                (void)munmap((void*)(uintptr_t)f->base, f->map_size);
                fast_pp_bail(pp, "too many included files");
            }
        }
    }
}

// Reads the file at `path`, found in search directory `dir_idx` (UINT32_MAX for none), as a system header's inclusion or
// not (`is_system`), unless its guard or #pragma once skips it: from the mapping the compile kept of it, if it has read
// it before (#3586), else mapped whole from `fd` (the include search's descriptor, which this closes, or -1 to open
// `path`). A file the guard table has no room for is mapped for this push alone. With -MD (#3592), the reading is
// recorded either way, as the full pipeline's pp_enter_file observes a file before its guard; a file's guard entry
// keeps its record from its first reading.
static bool fast_pp_open_and_push(FastPP* pp, const char* path, uint32_t path_len, uint32_t dir_idx, bool is_system, int fd, const struct stat* opened_stat)
{
    static const uint8_t empty_byte = 0;
    bool ok = false;
    // Check include-guard cache BEFORE open()!
    FastGuardEntry* ge = fast_guard_lookup(pp, path, path_len, false);
    const FastFileRecord* const known = ge != NULL ? fast_guard_record(pp, ge) : NULL;
    if (ge != NULL && (ge->once != 0 || (ge->guard_ident != 0 && fast_ident_is_macro(&pp->idents, ge->guard_ident))))
    {
        if (pp->dependency_records != NULL)
        {
            fast_pp_dependency_again(pp, ge, is_system);
        }
        ok = true; // Skipped with ZERO syscalls!
    }
    else
    {
        // A file #pragma GCC system_header marked is a system header from its start.
        is_system |= ge != NULL && pp->dependency_records != NULL && (pp->guard_dependencies[ge - pp->guards] & BUSTER_FPP_SYSTEM_PRAGMA) != 0;
        if (known != NULL && known->bytes != NULL)
        {
            if (pp->dependency_records != NULL)
            {
                fast_pp_dependency_again(pp, ge, is_system);
            }
            ok = fast_pp_push_memory(pp, path, known->bytes, known->size, dir_idx, false, is_system);
            if (ok)
            {
                pp->files[pp->file_depth - 1u].dependency = pp->dependency_records != NULL ? pp->dependency_last : 0u;
            }
        }
        else
        {
            fd = fd >= 0 ? fd : open(path, O_RDONLY | O_CLOEXEC);
            struct stat st;
            const struct stat* const info = opened_stat != NULL ? opened_stat : &st;
            if (fd >= 0 && (opened_stat != NULL || fstat(fd, &st) == 0) && S_ISREG(info->st_mode))
            {
                const uint32_t record = pp->dependency_records != NULL ? fast_pp_dependency_observe(pp, path, path_len, info, is_system) : 0u;
                const uint64_t sz = (uint64_t)info->st_size;
                // The whole file is read: its pages are mapped by this call and not by a fault each sixteen pages.
                void* const mem = sz == 0 ? (void*)(uintptr_t)&empty_byte : mmap(NULL, sz, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
                if (mem != MAP_FAILED)
                {
                    FastGuardEntry* const entry = ge != NULL ? ge : fast_guard_lookup(pp, path, path_len, true);
                    FastFileRecord* const kept = entry != NULL ? fast_guard_record(pp, entry) : NULL;
                    if (kept != NULL)
                    {
                        kept->bytes = (const uint8_t*)mem;
                        kept->size = sz;
                    }
                    if (entry != NULL && pp->dependency_records != NULL)
                    {
                        pp->guard_dependencies[entry - pp->guards] = (pp->guard_dependencies[entry - pp->guards] & BUSTER_FPP_SYSTEM_PRAGMA) | record;
                    }
                    const bool owned = kept == NULL && sz != 0;
                    ok = fast_pp_push_memory(pp, path, (const uint8_t*)mem, sz, dir_idx, owned, is_system);
                    if (ok)
                    {
                        pp->files[pp->file_depth - 1u].dependency = record;
                    }
                    else if (owned)
                    {
                        (void)munmap(mem, sz);
                    }
                }
            }
        }
    }
    if (fd >= 0)
    {
        (void)close(fd);
    }
    return ok;
}

// The file mappings the compile kept and the search directories' descriptors, released (#3586). A compile that bailed
// out leaves its process to the full pipeline, and a caller that goes on running asks for its memory back; a process
// that exits after its compile leaves them to its exit, which tears the whole address space down at once.
static void fast_pp_release_files(FastPP* pp)
{
    // #3577: the files mapped for their push alone (the guard table had no room), whose unmapping waited for the end: the
    // macros they defined keep their text.
    for (uint32_t i = 0; i < pp->mapped_count; i += 1)
    {
        (void)munmap((void*)(uintptr_t)pp->mapped_bases[i], pp->mapped_sizes[i]);
    }
    pp->mapped_count = 0;
    for (uint32_t i = 0; i < BUSTER_FPP_MAX_GUARDS; i += 1)
    {
        if (pp->guards[i].used != 0)
        {
            FastFileRecord* const record = fast_guard_record(pp, pp->guards + i);
            if (record->bytes != NULL && record->size != 0)
            {
                (void)munmap((void*)(uintptr_t)record->bytes, record->size);
            }
            record->bytes = NULL;
            record->size = 0;
        }
    }
    // A compile that bailed out inside a file: the mappings of its own that the include stack holds.
    for (uint32_t i = 0; i < pp->file_depth; i += 1)
    {
        FastIncludeFile* const f = pp->files + i;
        if (f->owned_mmap && f->base != NULL && f->map_size != 0)
        {
            (void)munmap((void*)(uintptr_t)f->base, f->map_size);
        }
        f->owned_mmap = false;
    }
    for (uint32_t d = 0; d < pp->dir_count; d += 1)
    {
        if (pp->dir_states[d] == BUSTER_FPP_DIR_OPEN)
        {
            (void)close(pp->dir_fds[d]);
        }
        pp->dir_states[d] = BUSTER_FPP_DIR_UNOPENED;
    }
}

// Search directory `d`'s descriptor, which its first search opens, or -1 where it can't be opened (#3586). A file is
// looked up from it by its name alone: the kernel walks the directory's path once, not again for each name.
static int fast_pp_dir_fd(FastPP* pp, uint32_t d)
{
    if (pp->dir_states[d] == BUSTER_FPP_DIR_UNOPENED)
    {
        const int fd = open(pp->dirs[d], O_PATH | O_DIRECTORY | O_CLOEXEC);
        pp->dir_fds[d] = fd;
        pp->dir_states[d] = fd >= 0 ? BUSTER_FPP_DIR_OPEN : BUSTER_FPP_DIR_MISSING;
    }
    return pp->dir_states[d] == BUSTER_FPP_DIR_OPEN ? pp->dir_fds[d] : -1;
}

// Whether the include search may take `path`: a file the unit has read already, or a readable one. A path that was
// not is remembered, and not asked for again in this compile. `name` is the path's part after search directory `dir`
// (UINT32_MAX for a path of its own), which is looked up from the directory's descriptor (#3586). With `fd`, a file
// found readable is opened, and `*fd` is its descriptor (-1 where it has been read before), which the caller closes;
// without, it is only asked for.
static bool fast_pp_include_candidate(FastPP* pp, const char* path, uint32_t length, uint32_t dir, const char* name, int* fd)
{
    if (fd != NULL)
    {
        *fd = -1;
    }
    bool found = fast_guard_lookup(pp, path, length, false) != NULL;
    if (!found)
    {
        const uint32_t hash = fast_guard_hash(path, length);
        const uint32_t mask = BUSTER_FPP_MAX_MISSING - 1u;
        uint32_t index = hash & mask;
        bool known_missing = false;
        FastMissingPath* slot = pp->missing + index;
        while (slot->hash != 0)
        {
            if (slot->hash == hash && slot->length == length && memcmp(slot->path, path, length) == 0)
            {
                known_missing = true;
                break;
            }
            index = (index + 1u) & mask;
            slot = pp->missing + index;
        }
        if (!known_missing)
        {
            const int at = dir == UINT32_MAX ? AT_FDCWD : fast_pp_dir_fd(pp, dir);
            const char* const looked = dir == UINT32_MAX ? path : name;
            if (at == AT_FDCWD || at >= 0)
            {
                if (fd != NULL)
                {
                    *fd = openat(at, looked, O_RDONLY | O_CLOEXEC);
                    found = *fd >= 0;
                }
                else
                {
                    found = faccessat(at, looked, R_OK, 0) == 0;
                }
            }
            // Half full at most, so that a search for a free slot ends.
            if (!found && pp->missing_count < BUSTER_FPP_MAX_MISSING / 2u)
            {
                char* const saved = (char*)fast_arena_alloc(pp->arena, (uint64_t)length + 1u, 1);
                if (saved != NULL)
                {
                    memcpy(saved, path, length);
                    saved[length] = '\0';
                    slot->hash = hash;
                    slot->length = length;
                    slot->path = saved;
                    pp->missing_count += 1u;
                }
            }
        }
    }
    return found;
}

// The file `name` names, by the include search: its path, its directory's index and whether it is a system header.
// `fd` as fast_pp_include_candidate's.
static bool fast_pp_resolve_include(FastPP* pp, const char* name, uint32_t name_len, bool angled, bool is_next,
                                    char* out_path, uint32_t out_cap, uint32_t* out_dir_idx, bool* out_system, int* fd)
{
    if (fd != NULL)
    {
        *fd = -1;
    }
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
                if (fast_pp_include_candidate(pp, out_path, total, UINT32_MAX, NULL, fd))
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
            if (fast_pp_include_candidate(pp, out_path, total, d, out_path + dlen + 1u, fd))
            {
                *out_dir_idx = d;
                *out_system = pp->dir_is_system[d] != 0;
                found = true;
            }
        }
    }
    return found;
}

// Whether `id` names a macro, for defined, #ifdef, #ifndef, #elifdef and #elifndef: a macro, or __has_include or
// __has_include_next, which clang and the full pipeline define as builtin macros and this path answers (#3592: glibc's
// <bits/statx.h> and <bits/unistd_ext.h> include other files where they aren't defined).
static inline bool fast_pp_defined(const FastPP* pp, uint32_t id)
{
    const uint8_t pkw = pp->idents.entries[id].pp_kw;
    return fast_ident_is_macro(&pp->idents, id) || pkw == FPP_KW_HAS_INCLUDE || pkw == FPP_KW_HAS_INCLUDE_NEXT;
}

// -MD (#3592): a __has_include that finds a file reads it, as the full pipeline's pp_builtin_value observes it, as much a
// system header's as the file found (`system`) and the file that asks are. A file the unit read before has its guard
// entry's record; another is stat'd for its device and inode, and one that isn't a regular file leaves (the search took
// a directory, where the full pipeline's goes on).
static __attribute__((cold, noinline)) void fast_pp_dependency_probe(FastPP* pp, const char* path, bool system)
{
    const bool level = system || (pp->file_depth != 0 && pp->files[pp->file_depth - 1u].is_system);
    const uint32_t length = (uint32_t)strlen(path);
    const FastGuardEntry* const ge = fast_guard_lookup(pp, path, length, false);
    struct stat status;
    if (ge != NULL)
    {
        fast_pp_dependency_again(pp, ge, level);
    }
    else if (stat(path, &status) == 0 && S_ISREG(status.st_mode))
    {
        (void)fast_pp_dependency_observe(pp, path, length, &status, level);
    }
    else
    {
        fast_pp_bail(pp, "__has_include found no regular file");
    }
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
    else if ((tok.kind == FTOK_INT_LIT && (tok.flags & (BUSTER_FTOK_UNSIGNED | BUSTER_FTOK_LONG | BUSTER_FTOK_LONGLONG | BUSTER_FTOK_HEX)) != 0) ||
             (tok.kind == FTOK_CHAR_LIT && (tok.flags & BUSTER_FTOK_SPELLED) != 0))
    {
        // As written (#3577): a base or a suffix, an escape or a prefix.
        const FastLitSlice sl = pp->lit_slices[tok.extra];
        if (sl.length < cap)
        {
            memcpy(buf, pp->lit_bytes + sl.offset, sl.length);
            n = sl.length;
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
// Whether fast_tok_to_text spells a token as it was written (#3577): an integer and a character constant are (their
// spelling is kept where their value doesn't give it), and a string literal without a prefix or an escape, whose value
// is its spelling; a prefixed or escaped string isn't, nor a number the full pipeline diagnoses, and a stray backslash
// stringized makes an invalid string.
// (A macro, not a function: every pasted and stringized token asks.) 1 when exact, else 0.
#define BUSTER_FAST_TOK_SPELLING_EXACT(pp, tok) \
    ((((uint32_t)((tok).kind == FTOK_STR_LIT) & ((uint32_t)((tok).extra != 0) | (uint32_t)((tok).kind == FTOK_STR_LIT && (pp)->lit_slices[(tok).data].escaped != 0))) | \
      ((uint32_t)((tok).kind == FTOK_PP_OTHER) & (uint32_t)(((tok).data & 0xffu) == '\\')) | (uint32_t)((tok).kind == FTOK_BAD_LITERAL)) == 0)

static __attribute__((noinline)) FastTok fast_pp_paste(FastPP* pp, FastTok lhs, FastTok rhs)
{
    if ((BUSTER_FAST_TOK_SPELLING_EXACT(pp, lhs) & BUSTER_FAST_TOK_SPELLING_EXACT(pp, rhs)) == 0)
    {
        fast_pp_bail(pp, "paste of a token spelled from its value");
    }
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
        // #3577: one token, which a comment's start (// or /*) is not: the full pipeline's error.
        if (p != end || res.kind == FTOK_EOF)
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
        if (!BUSTER_FAST_TOK_SPELLING_EXACT(pp, toks[i]))
        {
            fast_pp_bail(pp, "stringize of a token spelled from its value");
            break;
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

// __DATE__'s and __TIME__'s text (#3577), as the full pipeline makes it: the unit's time in local time, "Nov 25 2026" and
// "09:41:00", at the first use of either; a time that can't be converted, or a year outside 0000 to 9999, is its error.
static __attribute__((cold, noinline)) bool fast_pp_unit_time(FastPP* pp)
{
    if (!pp->time_made)
    {
        static const char months[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        const time_t now = time(NULL);
        struct tm calendar;
        const bool converted = now != (time_t)-1 && localtime_r(&now, &calendar) != NULL;
        const bool valid = converted && calendar.tm_year >= -1900 && calendar.tm_year <= 9999 - 1900 && calendar.tm_mon >= 0 &&
                           calendar.tm_mon < 12 && calendar.tm_mday >= 1 && calendar.tm_mday <= 31 && calendar.tm_hour >= 0 &&
                           calendar.tm_hour < 24 && calendar.tm_min >= 0 && calendar.tm_min < 60 && calendar.tm_sec >= 0 &&
                           calendar.tm_sec <= 60;
        if (valid)
        {
            snprintf(pp->date_text, sizeof(pp->date_text), "%s %2d %04d", months[calendar.tm_mon], calendar.tm_mday, calendar.tm_year + 1900);
            snprintf(pp->time_text, sizeof(pp->time_text), "%02d:%02d:%02d", calendar.tm_hour, calendar.tm_min, calendar.tm_sec);
            pp->time_made = true;
        }
        else
        {
            fast_pp_bail(pp, "the unit's time");
        }
    }
    return pp->time_made;
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
    if (ie->pp_kw >= FPP_KW_LINE_MACRO && ie->pp_kw <= BUSTER_FPP_KW_LAST_BUILTIN)
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
        if (ie->pp_kw == FPP_KW_DATE_MACRO || ie->pp_kw == FPP_KW_TIME_MACRO)
        {
            // #3577: the full pipeline's text, of the unit's time.
            if (!fast_pp_unit_time(pp))
            {
                return false;
            }
            const char* text = ie->pp_kw == FPP_KW_DATE_MACRO ? pp->date_text : pp->time_text;
            *out_replacement = (FastTok){ .kind = FTOK_STR_LIT, .flags = tok.flags, .data = fast_pp_add_lit(pp, text, (uint32_t)strlen(text)) };
            return true;
        }
        // _Pragma, __TIMESTAMP__, __FILE_NAME__, and a query (__has_attribute, ...) outside #if, which the full
        // pipeline expands or reports.
        fast_pp_bail(pp, "builtin macro the direct path doesn't expand");
        return false;
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

// A function-like macro's call whose '(' was read: the call stack's root, which collects the arguments next.
static inline FastMacroCall* fast_pp_call_start(FastPP* pp, FastTok tok, uint32_t min_expand_depth, bool allow_file)
{
    FastMacroCall* const root = pp->call_stack;
    root->macro_id = tok.data;
    root->macro_idx = pp->idents.entries[tok.data].macro_idx;
    root->saved_exp_used = pp->exp_used;
    root->saved_arg_used = pp->arg_used;
    root->min_expand_depth = min_expand_depth;
    root->tok_flags = tok.flags;
    root->state = 0;
    root->allow_file = allow_file;
    return root;
}

// A call's `arg_count` arguments are in exp_buf (cf->arg_starts, cf->arg_counts), `saw_any` whether any token stood
// between its parentheses: their count checked against the macro's parameters, an empty variable argument added where
// none was given, and the call moved on to its prescans. False where the compile bailed out.
static bool fast_pp_call_collected(FastPP* pp, FastMacroCall* cf, const FastMacro* m, uint32_t arg_count, bool saw_any)
{
    const uint32_t max_named = m->param_count;
    cf->va_elided = m->is_variadic && (arg_count == max_named || (max_named == 0 && !saw_any));
    if (max_named == 0 && !m->is_variadic)
    {
        if (arg_count > 1u || (arg_count == 1u && cf->arg_counts[0] != 0))
        {
            fast_pp_bail(pp, "macro arg count mismatch");
        }
    }
    else if (!m->is_variadic && arg_count != max_named)
    {
        fast_pp_bail(pp, "macro arg count mismatch");
    }
    else if (m->is_variadic && arg_count < max_named)
    {
        fast_pp_bail(pp, "variadic macro too few args");
    }
    else if (m->is_variadic && arg_count == max_named)
    {
        cf->arg_starts[max_named] = pp->exp_used;
        cf->arg_counts[max_named] = 0;
        arg_count += 1u;
    }
    if (!pp->bailout)
    {
        cf->arg_count = arg_count;
        cf->scan_body_idx = 0;
        cf->state = 1u;
    }
    return !pp->bailout;
}

_Alignas(64) static const uint8_t fast_iota64[64] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
};

// `n` tokens from src to dst (which don't overlap), four a 32-byte load and store: the copies are read back soon, and
// Zen 5 forwards a 32-byte store to a narrower load anywhere inside it.
static inline void fast_tok_copy(FastTok* dst, const FastTok* src, uint32_t n)
{
    uint32_t k = 0;
    for (; k + 4u <= n; k += 4u)
    {
        _mm256_storeu_si256((__m256i*)(void*)(dst + k), _mm256_loadu_si256((const __m256i*)(const void*)(src + k)));
    }
    for (; k < n; k += 1u)
    {
        dst[k] = src[k];
    }
}

// The parentheses' depth after each of 64 tokens, `depth` before the first, from the tokens that are '(' and ')': a
// prefix sum of +1 at a '(' and -1 at a ')', six steps of a shift across the register and an add (bytes: the depth
// before 64 tokens stays below 64).
static inline __m512i fast_paren_depths(uint64_t opens, uint64_t closes, uint32_t depth)
{
    const __m512i v_iota = _mm512_load_si512((const void*)fast_iota64);
    const __m512i v_one = _mm512_set1_epi8(1);
    __m512i v = _mm512_sub_epi8(_mm512_maskz_mov_epi8(opens, v_one), _mm512_maskz_mov_epi8(closes, v_one));
    v = _mm512_mask_add_epi8(v, 1u, v, _mm512_set1_epi8((char)depth));
    for (uint32_t shift = 1u; shift < 64u; shift *= 2u)
    {
        const __m512i from = _mm512_sub_epi8(v_iota, _mm512_set1_epi8((char)shift));
        v = _mm512_add_epi8(v, _mm512_maskz_permutexvar_epi8(~UINT64_C(0) << shift, from, v));
    }
    return v;
}

// A call's closing ')' and its arguments' separating commas from 64 of its tokens' '(', ')' and ',' (bits; `live` the
// tokens there are), `*io_depth` the depth before them: the ')' is the first token at depth 0 (fast_paren_depths), the
// separators the commas at depth 1 before it, the first `named` arguments' of a variadic macro. The arguments found are
// recorded in cf (their starts from `base`, `*io_start` the next one's, relative to the call's first token, and
// `offset` this block's first token's place there). Returns the ')''s bit (0 where these tokens have none, and
// *io_depth is the depth after them), or UINT64_MAX where the call isn't taken in bulk (more arguments than a call
// holds, a depth past what the bytes count).
static inline uint64_t fast_call_block(uint64_t opens, uint64_t closes, uint64_t commas, uint64_t live, uint32_t* io_depth,
                                       const FastMacro* m, FastMacroCall* cf, uint32_t base, uint32_t offset, uint32_t* io_start,
                                       uint32_t* io_arg_count)
{
    // Most calls hold no '(' before their first ')', at depth 1: that ')' closes the call, and every comma before it
    // is at depth 1. Only the others wait on the prefix sum's chain of shifts.
    const uint64_t first_close = _blsi_u64(closes & live);
    const bool flat = *io_depth == 1u && (opens & live & (first_close - 1u)) == 0;
    uint64_t closing = first_close;
    uint64_t at_one = live;
    uint32_t depth = 1u;
    if (!flat)
    {
        const __m512i depths = fast_paren_depths(opens, closes, *io_depth);
        closing = _blsi_u64(_mm512_testn_epi8_mask(depths, depths) & closes & live);
        at_one = _mm512_cmpeq_epi8_mask(depths, _mm512_set1_epi8(1));
        depth = (uint32_t)(int32_t)(int8_t)_mm_extract_epi8(_mm512_extracti32x4_epi32(depths, 3), 15);
    }
    uint64_t separators = commas & at_one & live & (closing - 1u);
    uint32_t arg_count = *io_arg_count;
    uint32_t arg_start = *io_start;
    bool taken = true;
    while (separators != 0 && taken && (m->is_variadic == 0 || arg_count < m->param_count))
    {
        const uint32_t at = offset + (uint32_t)__builtin_ctzll(separators);
        separators &= separators - 1u;
        taken = arg_count < BUSTER_FPP_MAX_PARAMS;
        cf->arg_starts[arg_count] = base + arg_start;
        cf->arg_counts[arg_count] = at - arg_start;
        arg_count += taken ? 1u : 0u;
        arg_start = at + 1u;
    }
    *io_depth = depth;
    *io_start = arg_start;
    *io_arg_count = arg_count;
    return taken && (closing != 0 || depth < 64u) ? closing : UINT64_MAX;
}

// The last argument of a call taken in bulk, `span` tokens after its '(', and the arguments' count. An empty call of a
// macro without parameters has one empty argument here, which fast_pp_call_collected takes as none.
static inline uint32_t fast_call_last_arg(FastMacroCall* cf, uint32_t base, uint32_t span, uint32_t arg_start, uint32_t arg_count)
{
    cf->arg_starts[arg_count] = base + arg_start;
    cf->arg_counts[arg_count] = span - arg_start;
    return arg_count + 1u;
}

// A call's arguments from the expansion frame on top, where it holds the call to its ')' after the '(' it gave: the
// tokens up to the ')' copied into exp_buf, the arguments split as fast_pp_window_args splits them, the frame's place
// after the ')'. False, with nothing changed, where the call isn't taken in bulk (the frame is the call's caller's,
// spent, or ends before the ')'); fast_pp_run_calls then reads the arguments a token at a time.
static bool fast_pp_frame_args(FastPP* pp, FastMacroCall* cf, const FastMacro* m, uint32_t* out_arg_count)
{
    // The frame on top where the call has one of its own (a call from the file's text has none open).
    const bool framed = pp->expand_depth > cf->min_expand_depth;
    FastExpandFrame* const ef = framed ? pp->expand + (pp->expand_depth - 1u) : NULL;
    const uint32_t first = framed ? ef->pos : 0u;
    const uint32_t count = framed ? ef->count : 0u;
    const uint32_t base_used = pp->exp_used;
    const __m512i v_low = _mm512_set1_epi64(0xff);
    const __m512i v_lparen = _mm512_set1_epi64(FTOK_LPAREN);
    const __m512i v_rparen = _mm512_set1_epi64(FTOK_RPAREN);
    const __m512i v_comma = _mm512_set1_epi64(FTOK_COMMA);
    uint32_t depth = 1u;
    uint32_t close = UINT32_MAX;
    uint32_t arg_count = 0;
    uint32_t arg_start = 0;
    bool taken = first < count;
    for (uint32_t b = first; taken && close == UINT32_MAX && b < count; b += 64u)
    {
        // The kinds of eight tokens a load, as bits.
        const uint32_t lanes = count - b < 64u ? count - b : 64u;
        uint64_t opens = 0;
        uint64_t closes = 0;
        uint64_t commas = 0;
        for (uint32_t j = 0; j < lanes; j += 8u)
        {
            const __mmask8 present = (__mmask8)_bzhi_u32(0xffu, lanes - j);
            const __m512i kinds = _mm512_and_si512(_mm512_maskz_loadu_epi64(present, (const void*)(ef->toks + b + j)), v_low);
            opens |= (uint64_t)_mm512_mask_cmpeq_epi64_mask(present, kinds, v_lparen) << j;
            closes |= (uint64_t)_mm512_mask_cmpeq_epi64_mask(present, kinds, v_rparen) << j;
            commas |= (uint64_t)_mm512_mask_cmpeq_epi64_mask(present, kinds, v_comma) << j;
        }
        const uint64_t closing = fast_call_block(opens, closes, commas, _bzhi_u64(~UINT64_C(0), lanes), &depth, m, cf, base_used, b - first,
                                                 &arg_start, &arg_count);
        taken = closing != UINT64_MAX;
        close = taken && closing != 0 ? b + (uint32_t)__builtin_ctzll(closing) : UINT32_MAX;
    }
    const uint32_t span = close - first;
    taken = taken && close != UINT32_MAX && base_used + span <= pp->exp_cap;
    if (taken)
    {
        *out_arg_count = fast_call_last_arg(cf, base_used, span, arg_start, arg_count);
        fast_tok_copy(pp->exp_buf + base_used, ef->toks + first, span);
        pp->exp_used = base_used + span;
        ef->pos = close + 1u;
    }
    return taken;
}

// A call's body without '#' and '##' (FastMacro.plain_subst) with its parameters replaced: the stretches of body tokens
// between the parameters' names (the macro's uses) and each name's prescanned argument copied into exp_buf, the
// argument's first token with the name's BUSTER_FTOK_SPACE. False, with nothing written, where exp_buf has no room for
// them all (fast_pp_run_calls's token loop then bails out where it runs out).
static bool fast_pp_substitute_plain(FastPP* pp, const FastMacroCall* cf, const FastMacro* m, const FastTok* body)
{
    const uint32_t* const uses = pp->macro_params + m->plain_start + m->plain_count;
    const uint32_t use_count = m->use_count;
    uint32_t total = m->body_count - use_count;
    for (uint32_t u = 0; u < use_count; u += 1)
    {
        total += cf->exp_arg_counts[uses[u] & 0xffu];
    }
    const bool room = pp->exp_used + total <= pp->exp_cap;
    if (room)
    {
        FastTok* out = pp->exp_buf + pp->exp_used;
        uint32_t i = 0;
        for (uint32_t u = 0; u < use_count; u += 1)
        {
            const uint32_t at = uses[u] >> 8;
            const uint32_t pidx = uses[u] & 0xffu;
            const uint32_t count = cf->exp_arg_counts[pidx];
            fast_tok_copy(out, body + i, at - i);
            out += at - i;
            if (count != 0)
            {
                const FastTok* const src = (cf->exp_arg_in_argbuf[pidx] != 0 ? pp->arg_buf : pp->exp_buf) + cf->exp_arg_starts[pidx];
                fast_tok_copy(out, src, count);
                // The first token again, whole, with the name's space.
                const FastTok first = src[0];
                out[0] = fast_tok_make(first.kind, (first.flags & ~BUSTER_FTOK_SPACE) | (body[at].flags & BUSTER_FTOK_SPACE), first.extra, first.data);
                out += count;
            }
            i = at + 1u;
        }
        fast_tok_copy(out, body + i, m->body_count - i);
        out += m->body_count - i;
        pp->exp_used = (uint32_t)(out - pp->exp_buf);
    }
    return room;
}

// Strictly iterative macro expansion (ZERO recursion, zero calls to fast_pp_expand_buffer!): the call stack from its
// root (fast_pp_call_start) on. Uses pp->call_stack + pp->arg_buf for nested function-like macro argument prescanning.
static BUSTER_DIRECT_HOT bool fast_pp_run_calls(FastPP* pp, FastTok* out_replacement)
{
    uint32_t cdepth = 1u;
    FastMacroCall* const root = pp->call_stack;
    while (cdepth > 0 && !pp->bailout)
    {
        FastMacroCall* cf = pp->call_stack + (cdepth - 1u);
        const FastMacro* m = pp->macros + cf->macro_idx;
        const uint32_t max_named = m->param_count;
        const FastTok* const body = pp->macro_toks + m->body_start;
        const uint32_t bcount = m->body_count;

        uint32_t bulk_count = 0;
        if (cf->state == 0 && fast_pp_frame_args(pp, cf, m, &bulk_count))
        {
            // The arguments from the frame on top, in bulk. A call taken in bulk has its last argument recorded, so a
            // token stood between its parentheses where it has two arguments or a first that isn't empty.
            fast_pp_call_collected(pp, cf, m, bulk_count, bulk_count > 1u || cf->arg_counts[0] != 0);
        }
        else if (cf->state == 0)
        {
            // Collect raw arguments into pp->exp_buf, a token at a time
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
            if (pp->bailout || !fast_pp_call_collected(pp, cf, m, arg_count, saw_any_tok)) { break; }
        }
        else if (cf->state == 1u)
        {
            // The next argument the body names plainly (the macro's plain list, fast_pp_handle_define): an empty one
            // and one without an active macro's name are used as they are, any other is prescanned (state 2).
            const uint32_t* const plain = pp->macro_params + m->plain_start;
            bool started_prescan = false;
            while (cf->scan_body_idx < m->plain_count && !started_prescan && !pp->bailout)
            {
                const uint32_t pidx = plain[cf->scan_body_idx++];
                const uint32_t ac = cf->arg_counts[pidx];
                const FastTok* const raw = pp->exp_buf + cf->arg_starts[pidx];
                cf->exp_arg_starts[pidx] = cf->arg_starts[pidx];
                cf->exp_arg_counts[pidx] = ac;
                cf->exp_arg_in_argbuf[pidx] = 0;
                bool has_macro = false;
                for (uint32_t k = 0; k < ac && !has_macro; k += 1)
                {
                    const FastTok rt = raw[k];
                    has_macro = (rt.kind == FTOK_IDENT || rt.kind >= 64u) && (rt.flags & BUSTER_FTOK_BLUE) == 0 &&
                                fast_ident_is_macro(&pp->idents, rt.data);
                }
                if (has_macro && pp->expand_depth >= BUSTER_FPP_MAX_EXPAND)
                {
                    fast_pp_bail(pp, "arg prescan depth");
                }
                else if (has_macro)
                {
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
                        child->macro_idx = pp->idents.entries[at.data].macro_idx;
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
            // state == 3: Substitute body into pp->exp_buf, restore pp->arg_used, and push substituted frame! A body
            // without '#' and '##' by copies (fast_pp_substitute_plain), any other a token at a time.
            const uint32_t subst_start = pp->exp_used;
            const bool copied = m->plain_subst != 0 && fast_pp_substitute_plain(pp, cf, m, body);
            for (uint32_t i = 0; i < bcount && !pp->bailout && !copied; i += 1)
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
                    const bool variadic_next = m->is_variadic && next_is_param && next.extra == (uint16_t)(max_named + 1u);
                    const bool comma_before = pp->exp_used > subst_start && pp->exp_buf[pp->exp_used - 1u].kind == FTOK_COMMA;
                    // GNU's `, ## __VA_ARGS__` drops the comma before a left-out variadic argument, as clang does
                    // (TokenLexer::MaybeRemoveCommaBeforeVaArgs): not before an empty one, and without GNU's
                    // extensions not where the variadic parameter is the macro's only one.
                    if (variadic_next && cf->arg_counts[max_named] == 0 && cf->va_elided && !(pp->strict_iso && max_named == 0))
                    {
                        if (comma_before)
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
                            // And before a variadic argument with tokens it pastes nothing: the argument follows the
                            // comma unexpanded (clang's VaArgsPseudoPaste, #3592), where `,` ## `x` would be no token.
                            if (pp->exp_used > subst_start && !(variadic_next && comma_before))
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
                    // The whole token again: the frame reads it next, and a one-byte store would not forward to that.
                    const FastTok first = pp->exp_buf[subst_start];
                    pp->exp_buf[subst_start] = fast_tok_make(first.kind, first.flags | (cf->tok_flags & (BUSTER_FTOK_SPACE | BUSTER_FTOK_BOL)), first.extra, first.data);
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

static BUSTER_DIRECT_HOT bool fast_pp_try_expand_ex(FastPP* pp, FastTok tok, bool stop_at_eol,
                                                    uint32_t min_expand_depth, bool allow_file, FastTok* out_replacement)
{
    bool is_func = false;
    bool expanded = fast_pp_try_expand_nonfunc(pp, tok, false, out_replacement, &is_func);
    if (!expanded && is_func && fast_pp_consume_lparen(pp, stop_at_eol, min_expand_depth, allow_file))
    {
        fast_pp_call_start(pp, tok, min_expand_depth, allow_file);
        expanded = fast_pp_run_calls(pp, out_replacement);
    }
    return expanded;
}

static inline bool fast_pp_try_expand(FastPP* pp, FastTok tok, bool stop_at_eol, FastTok* out_replacement)
{
    return fast_pp_try_expand_ex(pp, tok, stop_at_eol, 0u, true, out_replacement);
}

// Expand a token slice `in_toks[0..in_count]` into `out_toks[0..out_cap]`.
// Used for prescanning macro arguments AND for expanding `#if` / `#elif` expressions!
// The question a builtin feature query asks the full pipeline's tables (CDirectQuery, #3577), or UINT32_MAX for the ones
// this path answers itself or refuses.
static uint32_t fast_pp_feature_query(uint8_t pkw)
{
    uint32_t query = UINT32_MAX;
    switch (pkw)
    {
        case FPP_KW_HAS_ATTRIBUTE: query = C_DIRECT_QUERY_HAS_ATTRIBUTE; break;
        case FPP_KW_HAS_BUILTIN: query = C_DIRECT_QUERY_HAS_BUILTIN; break;
        case FPP_KW_HAS_FEATURE: query = C_DIRECT_QUERY_HAS_FEATURE; break;
        case FPP_KW_HAS_EXTENSION: query = C_DIRECT_QUERY_HAS_EXTENSION; break;
        case FPP_KW_HAS_C_ATTRIBUTE: query = C_DIRECT_QUERY_HAS_C_ATTRIBUTE; break;
        case FPP_KW_HAS_DECLSPEC_ATTRIBUTE: query = C_DIRECT_QUERY_HAS_DECLSPEC_ATTRIBUTE; break;
        case FPP_KW_HAS_CONSTEXPR_BUILTIN: query = C_DIRECT_QUERY_HAS_CONSTEXPR_BUILTIN; break;
        case FPP_KW_IS_TARGET_ARCH: query = C_DIRECT_QUERY_IS_TARGET_ARCH; break;
        case FPP_KW_IS_TARGET_VENDOR: query = C_DIRECT_QUERY_IS_TARGET_VENDOR; break;
        case FPP_KW_IS_TARGET_OS: query = C_DIRECT_QUERY_IS_TARGET_OS; break;
        case FPP_KW_IS_TARGET_ENVIRONMENT: query = C_DIRECT_QUERY_IS_TARGET_ENVIRONMENT; break;
        case FPP_KW_IS_TARGET_VARIANT_OS: query = C_DIRECT_QUERY_IS_TARGET_VARIANT_OS; break;
        case FPP_KW_IS_TARGET_VARIANT_ENVIRONMENT: query = C_DIRECT_QUERY_IS_TARGET_VARIANT_ENVIRONMENT; break;
        case FPP_KW_BUILDING_MODULE: query = C_DIRECT_QUERY_BUILDING_MODULE; break;
        default: break;
    }
    return query;
}

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
                    is_def = fast_pp_defined(pp, nt.data) ? 1u : 0u;
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
            if (pkw >= FPP_KW_HAS_BUILTIN && pkw <= BUSTER_FPP_KW_LAST_BUILTIN)
            {
                // #3577: the full pipeline's answer, or a refusal. A feature query `( name )` asks the full pipeline's
                // tables (the request's feature_query; a name that is a macro, which it expands, is refused);
                // __has_include and __has_include_next search here, as it does, for a plain string or a header name
                // spelled without blanks; __has_warning, __is_identifier and __has_embed, and any other form, are
                // refused.
                uint32_t val = 0;
                const uint32_t query = fast_pp_feature_query(pkw);
                if (pkw == FPP_KW_HAS_INCLUDE || pkw == FPP_KW_HAS_INCLUDE_NEXT)
                {
                    uint32_t arg_start = ef->pos;
                    uint32_t arg_end = ef->pos;
                    bool closed = false;
                    if (ef->pos < ef->count && ef->toks[ef->pos].kind == FTOK_LPAREN)
                    {
                        ef->pos += 1u;
                        arg_start = ef->pos;
                        while (ef->pos < ef->count && ef->toks[ef->pos].kind != FTOK_RPAREN)
                        {
                            ef->pos += 1u;
                        }
                        arg_end = ef->pos;
                        closed = ef->pos < ef->count;
                        ef->pos += closed ? 1u : 0u;
                    }
                    char hname[512];
                    uint32_t hlen = 0;
                    bool angled = false;
                    const FastTok arg = arg_start < arg_end ? ef->toks[arg_start] : (FastTok){ 0 };
                    if (closed && arg_end == arg_start + 1u && arg.kind == FTOK_STR_LIT && arg.extra == 0 &&
                        pp->lit_slices[arg.data].escaped == 0 && pp->lit_slices[arg.data].length < sizeof(hname))
                    {
                        const FastLitSlice sl = pp->lit_slices[arg.data];
                        memcpy(hname, pp->lit_bytes + sl.offset, sl.length);
                        hlen = sl.length;
                    }
                    else if (closed && arg_start + 2u <= arg_end && arg.kind == FTOK_LT && ef->toks[arg_end - 1u].kind == FTOK_GT)
                    {
                        angled = true;
                        for (uint32_t hi = arg_start + 1u; hi + 1u < arg_end && hlen + 8u < sizeof(hname) && !pp->bailout; hi += 1)
                        {
                            if ((ef->toks[hi].flags & BUSTER_FTOK_SPACE) != 0 || (ef->toks[hi + 1u].flags & BUSTER_FTOK_SPACE) != 0 ||
                                !BUSTER_FAST_TOK_SPELLING_EXACT(pp, ef->toks[hi]))
                            {
                                fast_pp_bail(pp, "__has_include header name");
                            }
                            hlen += fast_tok_to_text(pp, ef->toks[hi], hname + hlen, sizeof(hname) - hlen);
                        }
                    }
                    hname[hlen] = '\0';
                    if (hlen == 0)
                    {
                        fast_pp_bail(pp, "__has_include operand");
                    }
                    else
                    {
                        char tmp_path[1024];
                        uint32_t d = 0;
                        bool sys = false;
                        val = fast_pp_resolve_include(pp, hname, hlen, angled, pkw == FPP_KW_HAS_INCLUDE_NEXT, tmp_path, sizeof(tmp_path), &d, &sys, NULL) ? 1u : 0u;
                        if (val != 0 && pp->dependency_records != NULL)
                        {
                            fast_pp_dependency_probe(pp, tmp_path, sys);
                        }
                    }
                }
                else if (query != UINT32_MAX)
                {
                    const bool shaped = ef->pos + 3u <= ef->count && ef->toks[ef->pos].kind == FTOK_LPAREN && ef->toks[ef->pos + 2u].kind == FTOK_RPAREN;
                    const FastTok arg = shaped ? ef->toks[ef->pos + 1u] : (FastTok){ 0 };
                    const bool named = arg.kind == FTOK_IDENT || (arg.kind >= 64u && arg.kind < 126u);
                    if (!shaped || !named || fast_ident_is_macro(&pp->idents, arg.data) || pp->feature_query == NULL)
                    {
                        fast_pp_bail(pp, "feature query the direct path can't answer");
                    }
                    else
                    {
                        const FastIdentEntry* ae = pp->idents.entries + arg.data;
                        val = pp->feature_query(pp->feature_context, query, (const uint8_t*)(pp->idents.strings + ae->str_offset), ae->length);
                        ef->pos += 3u;
                    }
                }
                else
                {
                    fast_pp_bail(pp, "builtin query the direct path doesn't answer");
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
        // #3577: negating INT64_MIN overflows (the full pipeline's warning); the negation is then defined.
        if (((uint32_t)(op == BUSTER_FPP_OP_UNARY_MINUS) & (uint32_t)!a->is_unsigned & (uint32_t)(a->val == (uint64_t)INT64_MIN)) != 0)
        {
            fast_pp_bail(pp, "pp overflow");
            return;
        }
        if (op == BUSTER_FPP_OP_UNARY_MINUS) { a->val = UINT64_C(0) - a->val; }
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
    const bool u = a.is_unsigned | b.is_unsigned;
    FastPPVal r = { .val = 0, .is_unsigned = u };
    // #3577: what the full pipeline diagnoses in an integer constant expression: a negative signed operand made unsigned
    // by the other's type (#if's "converted from negative value to unsigned"; not for shifts, whose operands aren't
    // converted together, nor && and ||), a signed overflow (+ - *, INT64_MIN / -1, which also traps on x86), a shift
    // count out of range or negative, and a negative value shifted left; the computations below are then defined.
    // (Branch-free: #if lines and constant expressions apply operators by the thousand.)
    // (A test of the operator first: #if lines apply operators by the thousand, and each asks its own question.)
    const uint32_t shift = (uint32_t)(op == FTOK_LSHIFT) | (uint32_t)(op == FTOK_RSHIFT);
    const uint32_t logical = (uint32_t)(op == FTOK_LAND) | (uint32_t)(op == FTOK_LOR);
    const uint32_t a_negative = (uint32_t)!a.is_unsigned & (uint32_t)((int64_t)a.val < 0);
    const uint32_t b_negative = (uint32_t)!b.is_unsigned & (uint32_t)((int64_t)b.val < 0);
    uint32_t diagnosed = (uint32_t)u & (shift ^ 1u) & (logical ^ 1u) & (a_negative | b_negative);
    if (!u && (op == FTOK_PLUS || op == FTOK_MINUS || op == FTOK_STAR || op == FTOK_SLASH || op == FTOK_PERCENT))
    {
        int64_t ignored = 0;
        if (op == FTOK_PLUS) { diagnosed |= (uint32_t)__builtin_add_overflow((int64_t)a.val, (int64_t)b.val, &ignored); }
        else if (op == FTOK_MINUS) { diagnosed |= (uint32_t)__builtin_sub_overflow((int64_t)a.val, (int64_t)b.val, &ignored); }
        else if (op == FTOK_STAR) { diagnosed |= (uint32_t)__builtin_mul_overflow((int64_t)a.val, (int64_t)b.val, &ignored); }
        else { diagnosed |= (uint32_t)(a.val == (uint64_t)INT64_MIN) & (uint32_t)((int64_t)b.val == -1); }
    }
    else if (shift != 0)
    {
        // A left shift of a non-negative signed value by 1 to 63 loses bits when any is at or above bit 63 - count (the
        // count is masked where it is out of range, which is refused anyway).
        const uint32_t bits_lost = (uint32_t)(b.val != 0) & (uint32_t)((a.val >> ((63u - b.val) & 63u)) != 0);
        diagnosed |= b_negative | (uint32_t)(b.val >= 64u) |
                     ((uint32_t)(op == FTOK_LSHIFT) & (uint32_t)!a.is_unsigned & ((uint32_t)((int64_t)a.val < 0) | bits_lost));
    }
    if (diagnosed != 0)
    {
        fast_pp_bail(pp, "pp constant expression the full pipeline diagnoses");
        return;
    }
    switch (op)
    {
        // (The comparisons and the right shift select their signed and unsigned results without a branch.)
        case FTOK_LOR:    r.val = (uint64_t)((a.val | b.val) != 0); r.is_unsigned = false; break;
        case FTOK_LAND:   r.val = (uint64_t)((uint32_t)(a.val != 0) & (uint32_t)(b.val != 0)); r.is_unsigned = false; break;
        case FTOK_PIPE:   r.val = a.val | b.val; break;
        case FTOK_CARET:  r.val = a.val ^ b.val; break;
        case FTOK_AMP:    r.val = a.val & b.val; break;
        case FTOK_EQ:     r.val = (uint64_t)(a.val == b.val); r.is_unsigned = false; break;
        case FTOK_NE:     r.val = (uint64_t)(a.val != b.val); r.is_unsigned = false; break;
        case FTOK_LT:     r.val = (uint64_t)(((uint32_t)u & (uint32_t)(a.val < b.val)) | ((uint32_t)!u & (uint32_t)((int64_t)a.val < (int64_t)b.val))); r.is_unsigned = false; break;
        case FTOK_LE:     r.val = (uint64_t)(((uint32_t)u & (uint32_t)(a.val <= b.val)) | ((uint32_t)!u & (uint32_t)((int64_t)a.val <= (int64_t)b.val))); r.is_unsigned = false; break;
        case FTOK_GT:     r.val = (uint64_t)(((uint32_t)u & (uint32_t)(a.val > b.val)) | ((uint32_t)!u & (uint32_t)((int64_t)a.val > (int64_t)b.val))); r.is_unsigned = false; break;
        case FTOK_GE:     r.val = (uint64_t)(((uint32_t)u & (uint32_t)(a.val >= b.val)) | ((uint32_t)!u & (uint32_t)((int64_t)a.val >= (int64_t)b.val))); r.is_unsigned = false; break;
        case FTOK_LSHIFT: r.val = a.val << (b.val & 63u); r.is_unsigned = a.is_unsigned; break;
        case FTOK_RSHIFT:
        {
            const uint64_t logical_shift = a.val >> (b.val & 63u);
            const uint64_t arithmetic_shift = (uint64_t)((int64_t)a.val >> (b.val & 63u));
            r.val = arithmetic_shift ^ ((arithmetic_shift ^ logical_shift) & (UINT64_C(0) - (uint64_t)a.is_unsigned));
            r.is_unsigned = a.is_unsigned;
        } break;
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

// The rest of a directive's line, lexed as clang lexes it (#3603): a comment may go on past the line's end, and a
// literal holds no comment. A literal that the line's end cuts leaves the direct path, as clang warns of it, and so
// does a raw string literal (#3600). fast_pp_skip_to_eol's out-of-line part: its registers are saved only here.
static __attribute__((noinline)) void fast_pp_skip_rest_of_line(FastPP* pp, FastIncludeFile* f)
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
        // #3577: what this passes unread (an #elif's operands after a taken group, a pragma nothing acts on) the full
        // pipeline's lexer reads: a UCN's start leaves.
        if (*p == '\\')
        {
            fast_pp_stray_backslash(pp, p, end, "universal character name in a directive's line");
        }
        if (*p == '/' && p + 1 < end && p[1] == '*')
        {
            const uint8_t* next = fast_skip_block_comment(p + 2, end, &line);
            p = next != NULL ? next : end;
            continue;
        }
        if (*p == '/' && p + 1 < end && p[1] == '/')
        {
            uint32_t comment_lines = 0;
            p = fast_skip_line_comment(p + 2, end, &comment_lines);
            line += comment_lines;
            continue;
        }
        if (*p == '"' || *p == '\'')
        {
            const uint8_t quote = *p;
            bool closed = !(pp->raw_strings != 0 && quote == '"' && fast_raw_string_before(f->base, p, pp->raw_strings));
            p += 1;
            while (closed && p < end && *p != quote && *p != '\n')
            {
                if (*p == '\\' && p + 1 < end)
                {
                    line += p[1] == '\n' ? 1u : 0u;
                    p += 2;
                }
                else
                {
                    p += 1;
                }
            }
            closed = closed && p < end && *p == quote;
            if (!closed)
            {
                fast_pp_bail(pp, "a literal on a directive's line that the direct path doesn't take");
                p = end;
            }
            else
            {
                p += 1;
            }
            continue;
        }
        p += 1;
    }
    f->ptr = p;
    f->line = line;
    f->at_bol = true;
}

// A macro definition's text after its name, as C compares two definitions (#3577): line splices removed, each run of
// blanks and comments one space, literals as written, no space at either end, and in a function-like macro's parameter
// list none at all, nor one between the list and the replacement. UINT32_MAX if it doesn't fit `cap` bytes.
static __attribute__((cold, noinline)) uint32_t fast_pp_definition_normalize(const uint8_t* p, const uint8_t* end, bool is_func, uint8_t* out, uint32_t cap)
{
    uint32_t n = 0;
    bool space = false;      // a blank is pending
    uint32_t params = is_func ? 1u : 0u; // 1 inside the parameter list, 2 just after it
    uint8_t quote = 0;
    bool ok = true;
    while (p < end && ok)
    {
        const uint8_t c = *p;
        if (c == '\\' && p + 1 < end && (p[1] == '\n' || (p[1] == '\r' && p + 2 < end && p[2] == '\n')))
        {
            p += p[1] == '\r' ? 3 : 2;
            continue;
        }
        if (quote == 0 && c == '/' && p + 1 < end && (p[1] == '*' || p[1] == '/'))
        {
            uint32_t ignored = 0;
            const uint8_t* next = p[1] == '*' ? fast_skip_block_comment(p + 2, end, &ignored) : end;
            p = next != NULL ? next : end;
            space = true;
            continue;
        }
        if (quote == 0 && (fast_char_class[c] & 8u) != 0)
        {
            p += 1;
            space = true;
            continue;
        }
        // A pending blank is kept between two tokens of the replacement list, never in or right after the parameters.
        if (space && n != 0 && params == 0)
        {
            ok = n < cap;
            out[n < cap ? n : 0] = ' ';
            n += 1u;
        }
        space = false;
        if (quote != 0)
        {
            // A literal's bytes as written: an escaped character is copied with its backslash.
            if (c == '\\' && p + 1 < end)
            {
                ok = n + 1u < cap;
                out[n < cap ? n : 0] = c;
                out[n + 1u < cap ? n + 1u : 0] = p[1];
                n += 2u;
                p += 2;
                continue;
            }
            quote = c == quote ? 0 : quote;
        }
        else if (c == '"' || c == '\'')
        {
            quote = c;
        }
        params = params == 1u && quote == 0 && c == ')' ? 2u : params == 2u ? 0u : params;
        ok = ok && n < cap;
        out[n < cap ? n : 0] = c;
        n += 1u;
        p += 1;
    }
    return ok ? n : UINT32_MAX;
}

// Whether a redefinition is the macro's definition again (#3577): of the same kind and the same text as C compares it;
// any other is the full pipeline's warning ("macro redefined"). A text that doesn't fit the buffers counts as another.
static __attribute__((cold, noinline)) bool fast_pp_same_definition(const FastMacro* old, const FastMacroText* old_text, bool is_func, const uint8_t* text, uint32_t text_length)
{
    uint8_t a[2048];
    uint8_t b[2048];
    const uint32_t na = fast_pp_definition_normalize(old_text->text, old_text->text + old_text->length, old->is_func != 0, a, sizeof(a));
    const uint32_t nb = fast_pp_definition_normalize(text, text + text_length, is_func, b, sizeof(b));
    return (old->is_func != 0) == is_func && na != UINT32_MAX && na == nb && memcmp(a, b, na) == 0;
}

// Whether a pragma's first name is one the full pipeline knows (#3577; c_check_ignored_pragma.c's rules, the clang and
// STDC namespaces, omp, align, options and comment): it acts on such a pragma or reports it, where it ignores an
// unknown one.
static bool fast_pp_pragma_known(const char* name)
{
    static const char* const known[] = {
        "clang", "STDC", "pack", "unroll", "nounroll", "unroll_and_jam", "nounroll_and_jam", "message", "push_macro", "pop_macro",
        "redefine_extname", "weak", "ms_struct", "float_control", "acc", "omp", "align", "options", "comment",
    };
    bool found = false;
    for (uint32_t i = 0; !found && i < sizeof(known) / sizeof(known[0]); i += 1)
    {
        found = strcmp(name, known[i]) == 0;
    }
    return found;
}

// The rest of a directive's line once its operands are read (#3577): only blanks and comments may be left, or the full
// pipeline warns (-Wextra-tokens), and a comment left open is its error. It skips the line as fast_pp_skip_to_eol does,
// with the same branches: what isn't a blank is noted without one.
static void fast_pp_finish_directive(FastPP* pp, FastIncludeFile* f)
{
    const uint8_t* p = f->ptr;
    const uint8_t* const end = f->end;
    uint32_t line = f->line;
    uint32_t extra = 0;
    uint32_t line_comment = 0;
    // Most directives end where their operands do, at the newline: fast_pp_skip_to_eol's test, ahead of the walk.
    const uint32_t ended = (uint32_t)(p < end) && *p == '\n';
    if (__builtin_expect(ended != 0, 1))
    {
        p += 1;
        line += 1u;
    }
    while (ended == 0 && p < end)
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
        if (*p == '/')
        {
            const uint8_t next_byte = p + 1 < end ? p[1] : 0u;
            if (next_byte == '*' && line_comment == 0)
            {
                const uint8_t* next = fast_skip_block_comment(p + 2, end, &line);
                if (next == NULL)
                {
                    fast_pp_bail(pp, "unterminated comment");
                }
                p = next != NULL ? next : end;
                continue;
            }
            line_comment |= (uint32_t)(next_byte == '/');
        }
        extra |= (uint32_t)((fast_char_class[*p] & 8u) == 0) & (line_comment ^ 1u);
        p += 1;
    }
    f->ptr = p;
    f->line = line;
    f->at_bol = true;
    if (extra != 0)
    {
        fast_pp_bail(pp, "extra tokens at the end of a directive");
    }
}

// Whether a name the predefines define is one the full pipeline takes for its own builtin (#3577): a __STDC macro or
// __FLT_EVAL_METHOD__, which no #define or #undef may change quietly.
static inline bool fast_pp_language_defined_name(const uint8_t* name, uint32_t length)
{
    return (length >= 6u && memcmp(name, "__STDC", 6) == 0) || (length == 19u && memcmp(name, "__FLT_EVAL_METHOD__", 19) == 0);
}

static inline void fast_pp_bind_macro(FastPP* pp, uint32_t name_id, FastMacro macro)
{
    const uint32_t index = ++pp->macro_count;
    pp->macros[index] = macro;
    pp->idents.entries[name_id].macro_idx = index;
    fast_ident_set_macro_bit(&pp->idents, name_id, true);
}

// The rest of a directive's line. Most directives end where their tokens do, at the newline, and pay a test of it.
static inline void fast_pp_skip_to_eol(FastPP* pp, FastIncludeFile* f)
{
    const uint8_t* const p = f->ptr;
    if (__builtin_expect(p < f->end && *p == '\n', 1))
    {
        f->ptr = p + 1;
        f->line += 1u;
        f->at_bol = true;
    }
    else
    {
        fast_pp_skip_rest_of_line(pp, f);
    }
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
    // #3577: `defined`, __VA_ARGS__, __VA_OPT__ and the builtin macros can't be defined quietly: the full pipeline
    // refuses or warns. The definition's text starts after the name.
    const uint8_t name_kw = pp->idents.entries[name_id].pp_kw;
    if (name_kw >= FPP_KW_DEFINED && name_kw <= BUSTER_FPP_KW_LAST_BUILTIN)
    {
        fast_pp_bail(pp, "#define of a builtin name");
        return;
    }
    const uint8_t* const text = f->ptr;
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

    // The rest of the line, read in bulk where it stands (fast_pp_read_line): a function-like macro's parameters up to
    // their ')', then the body, which moves to the start.
    const uint32_t body_start = pp->macro_tok_count;
    FastTok* const body = pp->macro_toks + body_start;
    const uint32_t room = pp->macro_tok_cap - body_start > 16u ? pp->macro_tok_cap - body_start - 16u : 0u;
    f->ptr += is_func ? 1 : 0;
    const uint32_t line_count = fast_pp_read_line(pp, f, body, room);
    if (line_count > room)
    {
        fast_pp_bail(pp, "macro_toks overflow");
        return;
    }
    // #3577: the definition's text ends at the line's end, where the bulk read stops (a redefinition's check).
    const uint8_t* const text_end = f->ptr;
    uint32_t at = 0;
    if (is_func)
    {
        // #3577: names separated by commas, the last of them or `...` alone variadic, and no name twice or
        // __VA_ARGS__ (a 64-bit filter of the names; the list is read again only where two share a bit); anything else
        // is the full pipeline's error.
        const FastTok none = fast_tok_make(FTOK_EOF, 0, 0, 0);
        FastTok pt = at < line_count ? body[at++] : none;
        uint64_t param_filter = 0;
        uint32_t param_collision = 0;
        bool need_param = false;
        while ((pt.kind != FTOK_RPAREN || need_param) && pt.kind != FTOK_EOF && !pp->bailout)
        {
            if (pt.kind == FTOK_ELLIPSIS)
            {
                is_variadic = true;
                pt = at < line_count ? body[at++] : none;
                break;
            }
            if (pt.kind == FTOK_IDENT || (pt.kind >= 64u && pt.kind < 126u))
            {
                const uint32_t pid = pt.data;
                const uint64_t bit = UINT64_C(1) << (pid & 63u);
                param_collision |= (uint32_t)((param_filter & bit) != 0) | (uint32_t)(pp->idents.entries[pid].pp_kw == FPP_KW_VA_ARGS);
                param_filter |= bit;
                pt = at < line_count ? body[at++] : none;
                if (pt.kind == FTOK_ELLIPSIS)
                {
                    is_variadic = true;
                    vararg_name = pid;
                    pt = at < line_count ? body[at++] : none;
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
                need_param = pt.kind == FTOK_COMMA;
                if (pt.kind == FTOK_COMMA)
                {
                    pt = at < line_count ? body[at++] : none;
                }
                else if (pt.kind != FTOK_RPAREN)
                {
                    fast_pp_bail(pp, "expected a comma in macro parameters");
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
        if (param_collision != 0)
        {
            for (uint32_t i = 0; i < param_count; i += 1)
            {
                for (uint32_t j = 0; j < i; j += 1)
                {
                    if (params[i] == params[j]) { fast_pp_bail(pp, "duplicate macro parameter"); }
                }
                if (params[i] == vararg_name || pp->idents.entries[params[i]].pp_kw == FPP_KW_VA_ARGS)
                {
                    fast_pp_bail(pp, "duplicate macro parameter");
                }
            }
            if (vararg_name != 0 && pp->idents.entries[vararg_name].pp_kw == FPP_KW_VA_ARGS)
            {
                fast_pp_bail(pp, "__VA_ARGS__ as a parameter");
            }
        }
        // (A list that ends in a comma reaches its ')' refused.)
        if (pp->bailout)
        {
            return;
        }
        memmove(body, body + at, (size_t)(line_count - at) * sizeof(FastTok));
    }
    const uint32_t body_count = line_count - at;
    bool has_ops = false;
    // #3577: an object-like macro's body that starts with no blank after the name (C99's warning), one that starts with
    // ##, and __VA_OPT__ in any but a variadic macro's (which leaves below): the full pipeline's.
    uint32_t body_bad = 0;
    const uint32_t va_opt_id = pp->idents.va_opt_id;
    // The first token without the space before it, written again whole: the loop below loads its kind and flags as one
    // word, which a one-byte store of the flags alone doesn't forward to (#3577: 0.4 M cycles a compile of sqlite3.c on
    // the 9700X; writing every token again whole measured 0.04 ms slower on its 5,757 defines, #3584). The empty asm
    // keeps Clang from narrowing the store back to the flags' byte.
    if (body_count != 0)
    {
        uint64_t first;
        memcpy(&first, body, sizeof(first));
        __asm__("" : "+r"(first));
        const uint32_t first_kind = (uint32_t)(first & 0xffu);
        const uint32_t first_flags = (uint32_t)((first >> 8) & 0xffu);
        body_bad = ((uint32_t)!is_func & (uint32_t)((first_flags & BUSTER_FTOK_SPACE) == 0)) | (uint32_t)(first_kind == FTOK_HASHHASH);
        first &= ~((uint64_t)(BUSTER_FTOK_SPACE | BUSTER_FTOK_BOL) << 8);
        memcpy(body, &first, sizeof(first));
    }
    for (uint32_t i = 0; i < body_count; i += 1)
    {
        const FastTok bt = body[i];
        has_ops = has_ops || bt.kind == FTOK_HASH || bt.kind == FTOK_HASHHASH;
        body_bad |= (uint32_t)(bt.kind == FTOK_IDENT) & (uint32_t)(bt.data == va_opt_id);
        // #3577: a macro's `(` says it is one (its data 1; the file's are 0), for -Wparentheses-equality, which a
        // macro's grouping quiets.
        if (bt.kind == FTOK_LPAREN)
        {
            body[i] = fast_tok_make(FTOK_LPAREN, bt.flags, bt.extra, 1u);
        }
        if (is_func && (bt.kind == FTOK_IDENT || (bt.kind >= 64u && bt.kind < 126u)))
        {
            uint16_t extra = 0;
            if (is_variadic && (pp->idents.entries[bt.data].pp_kw == FPP_KW_VA_ARGS ||
                                (vararg_name != 0 && bt.data == vararg_name)))
            {
                extra = (uint16_t)(param_count + 1u);
            }
            else if (is_variadic && pp->idents.entries[bt.data].pp_kw == FPP_KW_VA_OPT)
            {
                fast_pp_bail(pp, "__VA_OPT__");
                return;
            }
            else
            {
                for (uint16_t k = 0; k < param_count; k += 1)
                {
                    if (params[k] == bt.data)
                    {
                        extra = (uint16_t)(k + 1u);
                        break;
                    }
                }
            }
            body[i] = fast_tok_make(bt.kind, bt.flags, extra, bt.data);
        }
    }
    pp->macro_tok_count = body_start + body_count;
    fast_pp_skip_to_eol(pp, f);
    // #3577: '#' before anything but a parameter in a function-like macro, '##' at the end, and __VA_ARGS__ in a macro
    // whose variadic parameter has a name: the full pipeline's errors, looked for only in a body with '#' or '##' or in a
    // named variadic macro's (the loop above has marked the parameters).
    if (has_ops || vararg_name != 0)
    {
        uint32_t after_hash = 0;
        for (uint32_t i = 0; i < body_count; i += 1)
        {
            const FastTok bt = body[i];
            const uint32_t is_name = (uint32_t)(bt.kind == FTOK_IDENT) | ((uint32_t)(bt.kind >= 64u) & (uint32_t)(bt.kind < 126u));
            const uint32_t parameter = is_name & (uint32_t)is_func & (uint32_t)(bt.extra != 0);
            body_bad |= after_hash & (parameter ^ 1u);
            body_bad |= (uint32_t)(vararg_name != 0 && is_name != 0 && pp->idents.entries[bt.data].pp_kw == FPP_KW_VA_ARGS);
            after_hash = (uint32_t)is_func & (uint32_t)(bt.kind == FTOK_HASH);
        }
        body_bad |= after_hash | (uint32_t)(body_count != 0 && body[body_count - 1u].kind == FTOK_HASHHASH);
    }
    if (body_bad != 0)
    {
        fast_pp_bail(pp, "macro definition the full pipeline diagnoses");
        return;
    }
    // #3577: a macro defined again: the same definition, unless it is a builtin's, or the full pipeline's warning, which
    // a system header's line doesn't show (glibc's <stdint.h> spells the predefined __INT64_C(c) `c ## L`).
    const uint32_t previous = pp->idents.entries[name_id].macro_idx;
    if (previous != 0)
    {
        if (pp->macros[previous].language_defined != 0 ||
            (!f->is_system && !fast_pp_same_definition(pp->macros + previous, pp->macro_texts + previous, is_func, text, (uint32_t)(text_end - text))))
        {
            fast_pp_bail(pp, "macro redefined");
            return;
        }
    }

    // The parameters the body names plainly, by the walk fast_pp_run_calls's substitution makes: '#' and a parameter,
    // and '##' and the token after it, are taken together, and a parameter before '##' takes its argument as written.
    const uint32_t plain_start = pp->macro_param_count;
    uint32_t plain_count = 0;
    uint64_t named[2] = { 0, 0 };
    uint32_t walk = 0;
    while (is_func && walk < body_count)
    {
        const FastTok bt = body[walk];
        const bool has_next = walk + 1u < body_count;
        const bool next_param = has_next && (body[walk + 1u].kind == FTOK_IDENT || body[walk + 1u].kind >= 64u) && body[walk + 1u].extra != 0;
        const bool param = (bt.kind == FTOK_IDENT || bt.kind >= 64u) && bt.extra != 0;
        if ((bt.kind == FTOK_HASH && next_param) || (bt.kind == FTOK_HASHHASH && has_next))
        {
            walk += 2u;
        }
        else
        {
            const uint32_t pidx = param ? (uint32_t)bt.extra - 1u : 0u;
            if (param && !(has_next && body[walk + 1u].kind == FTOK_HASHHASH) && ((named[pidx >> 6] >> (pidx & 63u)) & 1u) == 0)
            {
                named[pidx >> 6] |= UINT64_C(1) << (pidx & 63u);
                if (pp->macro_param_count < pp->macro_param_cap)
                {
                    pp->macro_params[pp->macro_param_count++] = pidx;
                    plain_count += 1u;
                }
                else
                {
                    fast_pp_bail(pp, "macro_params overflow");
                }
            }
            walk += 1u;
        }
    }
    // A body without '#' and '##' names each parameter plainly: where each name stands, for the substitution's copies
    // (fast_pp_substitute_plain).
    uint32_t use_count = 0;
    bool plain_subst = is_func && !has_ops;
    for (uint32_t i = 0; plain_subst && i < body_count; i += 1)
    {
        const FastTok bt = body[i];
        if ((bt.kind == FTOK_IDENT || bt.kind >= 64u) && bt.extra != 0)
        {
            plain_subst = pp->macro_param_count < pp->macro_param_cap && use_count < UINT16_MAX;
            if (plain_subst)
            {
                pp->macro_params[pp->macro_param_count++] = (i << 8) | ((uint32_t)bt.extra - 1u);
                use_count += 1u;
            }
        }
    }

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
            else if (bcnt == 6u && bt[0].kind == FTOK_LPAREN && bt[1].kind == FTOK_IDENT &&
                     bt[2].kind == FTOK_LPAREN && bt[3].kind == FTOK_INT_LIT && bt[3].data == 0u &&
                     bt[4].kind == FTOK_RPAREN && bt[5].kind == FTOK_RPAREN)
            {
                // glibc's `(__ASSERT_VOID_CAST (0))` is six tokens. The test here asked for five, so no assert was
                // ever taken as the no-op it is: each of sqlite3.c's 5,327 went through the expansion stack, its body
                // a frame and __ASSERT_VOID_CAST a second one (#2850).
                const uint32_t cid = pp->idents.entries[bt[1].data].macro_idx;
                if (cid != 0 && pp->macros[cid].active != 0 && pp->macros[cid].is_func == 0 &&
                    pp->macros[cid].body_count == 3u)
                {
                    const FastTok* const cb = pp->macro_toks + pp->macros[cid].body_start;
                    if (cb[0].kind == FTOK_LPAREN && cb[1].kind == FTOK_KW_VOID && cb[2].kind == FTOK_RPAREN)
                    {
                        fmode = 3u;
                        fdata = cid;
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
    fast_pp_bind_macro(pp, name_id, (FastMacro){
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
        .plain_count = (uint16_t)plain_count,
        .plain_start = plain_start,
        .use_count = (uint16_t)use_count,
        .plain_subst = plain_subst ? 1u : 0u,
    });
    const uint32_t midx = pp->macro_count;
    pp->macro_texts[midx] = (FastMacroText){ .text = text, .length = (uint32_t)(text_end - text) };
    if (pp->in_predefines)
    {
        // The predefines' __STDC macros and __FLT_EVAL_METHOD__ are the full pipeline's builtins.
        const FastIdentEntry* const ne = pp->idents.entries + name_id;
        pp->macros[midx].language_defined = fast_pp_language_defined_name((const uint8_t*)pp->idents.strings + ne->str_offset, ne->length);
    }
}

static bool fast_pp_eval_if_line(FastPP* pp, FastIncludeFile* f)
{
    FastTok raw[512 + 16];
    FastTok expanded[1024];
    uint32_t rc = fast_pp_read_line(pp, f, raw, 512u);
    if (rc > 512u)
    {
        fast_pp_bail(pp, "long #if line");
        rc = 512u;
    }
    fast_pp_skip_to_eol(pp, f);
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
        // #3577: tokens after the name are the full pipeline's warning.
        fast_pp_finish_directive(pp, f);
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
        fast_pp_skip_to_eol(pp, f);
        uint32_t ec = 0;
        fast_pp_expand_buffer(pp, raw, rc, exp, 256u, &ec, false);
        if (ec == 1u && exp[0].kind == FTOK_STR_LIT && exp[0].extra == 0 && pp->lit_slices[exp[0].data].escaped == 0)
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
                // #3577: a header name spelled from its tokens as written, which blanks between them would change.
                if (((uint32_t)((exp[i].flags & BUSTER_FTOK_SPACE) != 0) | (uint32_t)((exp[i + 1u].flags & BUSTER_FTOK_SPACE) != 0) |
                     (uint32_t)(BUSTER_FAST_TOK_SPELLING_EXACT(pp, exp[i]) == 0)) != 0)
                {
                    fast_pp_bail(pp, "computed #include header name");
                }
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
    int fd = -1;
    if (!fast_pp_resolve_include(pp, name_buf, name_len, angled, is_next, resolved, sizeof(resolved), &dir_idx, &is_sys, &fd))
    {
        fast_pp_bail(pp, "include file not found");
        return;
    }
    // An inclusion is as much a system header's as the file and its includer are (clang's HandleHeaderIncludeOrImport, the
    // full pipeline's pp_dependency_observe): -MD's records see it (#3592).
    if (!fast_pp_open_and_push(pp, resolved, (uint32_t)strlen(resolved), dir_idx, is_sys || f->is_system, fd, NULL))
    {
        fast_pp_bail(pp, "include open failed");
    }
}

// Predefines and generated headers often contain consecutive "#define name decimal" lines.
// Accept only complete physical lines. All other replacements go through the directive reader.
static bool fast_pp_decimal_defines(FastPP* pp, FastIncludeFile* f)
{
    const uint8_t* p = f->ptr; // just after '#'
    const uint8_t* const end = f->end;
    const uint8_t* const first = p;
    while (end - p >= 11 && memcmp(p, "define ", 7) == 0 && (fast_char_class[p[7]] & 2u) != 0)
    {
        const uint8_t* const name = p + 7;
        const uint32_t length = fast_scan_ident_len(name, end);
        const uint8_t* q = name + length;
        if (end - q < 3 || *q != ' ' || q[1] < '0' || q[1] > '9') { break; }
        q += 1;
        const uint8_t* const digits = q;
        uint32_t value = 0;
        do
        {
            value = value * 10u + (*q++ - '0');
        } while (q < end && q - digits < 8 && *q >= '0' && *q <= '9');
        if (q == end || *q != '\n' || (*digits == '0' && q - digits > 1) ||
            pp->macro_count + 1u >= BUSTER_FPP_MAX_MACROS || pp->macro_tok_count >= pp->macro_tok_cap)
        {
            break;
        }
        const uint32_t id = fast_ident_intern(&pp->idents, name, length);
        // #3577: a name defined before (a redefinition is compared), a builtin's and, in the predefines, one the
        // full pipeline takes for its own (a __STDC macro, __FLT_EVAL_METHOD__) take the directive reader.
        const FastIdentEntry* const entry = pp->idents.entries + id;
        if (id == 0 || entry->kw_kind >= 126u || entry->macro_idx != 0 ||
            (entry->pp_kw >= FPP_KW_DEFINED && entry->pp_kw <= BUSTER_FPP_KW_LAST_BUILTIN) ||
            (pp->in_predefines && fast_pp_language_defined_name(name, length)))
        {
            break;
        }
        const uint32_t body = pp->macro_tok_count++;
        pp->macro_toks[body] = fast_tok_make(FTOK_INT_LIT, 0, 0, value);
        fast_pp_bind_macro(pp, id, (FastMacro){
            .body_start = body, .body_count = 1u, .fast_data = value,
            .active = 1u, .fast_mode = 1u, .fast_kind = FTOK_INT_LIT,
        });
        // #3577: the text after the name, which a redefinition compares.
        pp->macro_texts[pp->macro_count] = (FastMacroText){ .text = name + length, .length = (uint32_t)(q - (name + length)) };
        if (f->guard_state == 1u && id == f->guard_ident) { f->guard_state = 2u; }
        f->line += 1u;
        f->ptr = q + 1;
        if (f->ptr == end || *f->ptr != '#') { break; }
        p = f->ptr + 1;
    }
    const bool consumed = f->ptr != first;
    if (consumed)
    {
        f->at_bol = true;
        pp->win.serial = 0;
    }
    return consumed;
}

// The operands of #line or of a line marker, its number's token read (#3577): a simple digit sequence (no suffix, no
// base, no leading 0 but for 0 itself: the full pipeline's error or warning) under 2^32, and nothing after it. A file
// name would change __FILE__, which this path doesn't follow, and a marker's flags would change the include stack:
// both are refused. The next line is the number's.
static __attribute__((noinline)) void fast_pp_line_operands(FastPP* pp, FastIncludeFile* f, FastTok number)
{
    // The number's spelling ends where its token ended.
    const uint8_t* const end = f->ptr;
    const uint8_t* s = end;
    while (s > f->base && (fast_char_class[s[-1]] & 1u) != 0)
    {
        s -= 1;
    }
    const uint32_t length = (uint32_t)(end - s);
    bool valid = number.kind == FTOK_INT_LIT && length != 0 && length <= 10u && (s[0] != '0' || length == 1u);
    uint64_t value = 0;
    for (uint32_t i = 0; valid && i < length; i += 1)
    {
        valid = s[i] >= '0' && s[i] <= '9';
        value = value * 10u + (uint64_t)(s[i] - '0');
    }
    FastTok rest = { 0 };
    fast_pp_read_tok(pp, f, true, &rest);
    if (!valid || value > UINT32_MAX || rest.kind != FTOK_EOF)
    {
        fast_pp_bail(pp, "#line or line marker the direct path doesn't take");
        return;
    }
    fast_pp_finish_directive(pp, f);
    // finish_directive counted the newline that ends the directive: the next line is the number's.
    f->line = (uint32_t)value;
}

static __attribute__((noinline)) void fast_pp_handle_directive(FastPP* pp, FastIncludeFile* f)
{
    if (fast_pp_decimal_defines(pp, f)) { return; }
    FastTok kw_tok = { 0 };
    fast_pp_read_tok(pp, f, true, &kw_tok);
    if (kw_tok.kind == FTOK_EOF)
    {
        fast_pp_skip_to_eol(pp, f);
        return; // null directive '#'
    }
    if (kw_tok.kind != FTOK_IDENT && !(kw_tok.kind >= 64u && kw_tok.kind < 126u))
    {
        // #line-marker `# 123 "file"` in preprocessed files
        if (kw_tok.kind == FTOK_INT_LIT)
        {
            fast_pp_line_operands(pp, f, kw_tok);
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
            // #3577: a name, not a builtin's (`defined`, __LINE__, a predefined __STDC macro, ...), and nothing after it;
            // anything else is the full pipeline's error or warning.
            FastTok nt = { 0 };
            fast_pp_read_tok(pp, f, true, &nt);
            const bool named = nt.kind == FTOK_IDENT || (nt.kind >= 64u && nt.kind < 126u);
            const uint8_t nk = named ? pp->idents.entries[nt.data].pp_kw : 0u;
            const uint32_t previous = named ? pp->idents.entries[nt.data].macro_idx : 0u;
            if (!named || (nk >= FPP_KW_DEFINED && nk <= BUSTER_FPP_KW_LAST_BUILTIN) || pp->macros[previous].language_defined != 0)
            {
                fast_pp_bail(pp, "#undef the full pipeline diagnoses");
                break;
            }
            pp->idents.entries[nt.data].macro_idx = 0;
            fast_ident_set_macro_bit(&pp->idents, nt.data, false);
            fast_pp_finish_directive(pp, f);
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
                const bool defined = fast_pp_defined(pp, nt.data);
                cond = pkw == FPP_KW_IFDEF ? defined : !defined;
                if (pkw == FPP_KW_IFNDEF && f->guard_state == 0 && pp->cond_depth == f->cond_base)
                {
                    f->guard_ident = nt.data;
                    f->guard_state = 1u;
                    is_guard = true;
                }
                fast_pp_finish_directive(pp, f);
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
            // #3577: #elifdef and #elifndef before C23 are the full pipeline's warning; #else takes nothing after it.
            if ((pkw == FPP_KW_ELIFDEF || pkw == FPP_KW_ELIFNDEF) && !pp->c23) { fast_pp_bail(pp, "#elifdef before C23"); break; }
            if (pkw == FPP_KW_ELSE) { cf->saw_else = 1u; }
            // Since we were in an active branch, the new branch is definitely inactive!
            cf->active = 0;
            if (pkw == FPP_KW_ELSE)
            {
                fast_pp_finish_directive(pp, f);
            }
            else if (pkw == FPP_KW_ELIF)
            {
                fast_pp_skip_to_eol(pp, f);
            }
            else
            {
                FastTok nt = { 0 };
                fast_pp_read_tok(pp, f, true, &nt);
                if (nt.kind != FTOK_IDENT && !(nt.kind >= 64u && nt.kind < 126u)) { fast_pp_bail(pp, "bad #elifdef name"); break; }
                fast_pp_finish_directive(pp, f);
            }
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
            fast_pp_finish_directive(pp, f);
        } break;
        case FPP_KW_LINE:
        {
            FastTok lt = { 0 };
            fast_pp_read_tok(pp, f, true, &lt);
            fast_pp_line_operands(pp, f, lt);
        } break;
        case FPP_KW_PRAGMA:
        {
            FastTok pt = { 0 };
            fast_pp_read_tok(pp, f, true, &pt);
            // #3577: the pragmas the full pipeline knows it acts on or reports (c_check_ignored_pragma.c: unroll, pack,
            // align, comment, omp, acc, message, GCC's, clang's and STDC's, ...), but for once and GCC system_header outside
            // the main file (in it, its warnings) and the predefines' push_macro, which this path does; an unknown one it
            // ignores (-Wunknown-pragmas isn't on by default), its line's literals lexed (fast_pp_skip_to_eol, #3603).
            if (pt.kind == FTOK_IDENT && pp->idents.entries[pt.data].pp_kw == FPP_KW_ONCE)
            {
                if (f->serial == pp->main_serial) { fast_pp_bail(pp, "#pragma once in the main file"); break; }
                FastGuardEntry* ge = fast_guard_lookup(pp, f->path, f->path_len, true);
                if (ge != NULL) { ge->once = 1u; }
                if (ge != NULL && pp->dependency_records != NULL)
                {
                    pp->guard_dependencies[ge - pp->guards] = (pp->guard_dependencies[ge - pp->guards] & BUSTER_FPP_SYSTEM_PRAGMA) | f->dependency;
                }
                fast_pp_finish_directive(pp, f);
            }
            else if (pt.kind == FTOK_IDENT)
            {
                const char* s = pp->idents.strings + pp->idents.entries[pt.data].str_offset;
                if (strcmp(s, "GCC") == 0)
                {
                    FastTok pt2 = { 0 };
                    fast_pp_read_tok(pp, f, true, &pt2);
                    const char* s2 = (pt2.kind == FTOK_IDENT || pt2.kind >= 64u) ?
                                     pp->idents.strings + pp->idents.entries[pt2.data].str_offset : "";
                    if (strcmp(s2, "system_header") == 0 && f->serial != pp->main_serial)
                    {
                        // The rest of the file is a system header, and so are the files it includes from here and the
                        // file's later inclusions (-MD's records, #3592).
                        f->is_system = true;
                        FastGuardEntry* const ge = pp->dependency_records != NULL ? fast_guard_lookup(pp, f->path, f->path_len, true) : NULL;
                        if (ge != NULL)
                        {
                            pp->guard_dependencies[ge - pp->guards] = BUSTER_FPP_SYSTEM_PRAGMA | f->dependency;
                        }
                        fast_pp_finish_directive(pp, f);
                    }
                    else
                    {
                        fast_pp_bail(pp, "semantic pragma");
                    }
                }
                else if (strcmp(s, "push_macro") == 0 && f->path != NULL && strcmp(f->path, "<predefines>") == 0)
                {
                    fast_pp_skip_to_eol(pp, f);
                }
                else if (fast_pp_pragma_known(s))
                {
                    fast_pp_bail(pp, "semantic pragma");
                }
                else
                {
                    fast_pp_skip_to_eol(pp, f);
                }
            }
            else
            {
                fast_pp_skip_to_eol(pp, f);
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
    // The owner places this state in fresh zero-filled arena storage.
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
        pp->macro_param_cap = 2u * pp->macro_tok_cap;
        pp->exp_cap = 32768u;
        pp->arg_cap = 16384u;

        pp->out_kind = (uint8_t*)fast_arena_alloc(arena, (uint64_t)pp->out_cap + 64u, 64);
        pp->out_data = (uint32_t*)fast_arena_alloc(arena, ((uint64_t)pp->out_cap + 64u) * sizeof(uint32_t), 64);
        pp->num_pool = (uint64_t*)fast_arena_alloc(arena, (uint64_t)pp->num_cap * sizeof(uint64_t), 64);
        pp->lit_bytes = (char*)fast_arena_alloc(arena, pp->lit_bytes_cap, 64);
        pp->lit_slices = (FastLitSlice*)fast_arena_alloc(arena, (uint64_t)pp->lit_cap * sizeof(FastLitSlice), 64);
        pp->lit_coded = (uint64_t*)fast_arena_alloc(arena, (uint64_t)pp->lit_cap / 8u, 64);
        pp->macros = (FastMacro*)fast_arena_alloc(arena, (uint64_t)BUSTER_FPP_MAX_MACROS * sizeof(FastMacro), 64);
        pp->macro_texts = (FastMacroText*)fast_arena_alloc(arena, (uint64_t)BUSTER_FPP_MAX_MACROS * sizeof(FastMacroText), 64);
        pp->macro_toks = (FastTok*)fast_arena_alloc(arena, (uint64_t)pp->macro_tok_cap * sizeof(FastTok), 64);
        pp->macro_params = (uint32_t*)fast_arena_alloc(arena, (uint64_t)pp->macro_param_cap * sizeof(uint32_t), 64);
        pp->mapped_cap = 4096u;
        pp->mapped_bases = (const uint8_t**)fast_arena_alloc(arena, (uint64_t)pp->mapped_cap * sizeof(const uint8_t*), 64);
        pp->mapped_sizes = (uint64_t*)fast_arena_alloc(arena, (uint64_t)pp->mapped_cap * sizeof(uint64_t), 64);
        pp->exp_buf = (FastTok*)fast_arena_alloc(scratch, (uint64_t)pp->exp_cap * sizeof(FastTok), 64);
        pp->arg_buf = (FastTok*)fast_arena_alloc(scratch, (uint64_t)pp->arg_cap * sizeof(FastTok), 64);
        pp->call_stack = (FastMacroCall*)fast_arena_alloc(scratch, (uint64_t)BUSTER_FPP_MAX_CALLS * sizeof(FastMacroCall), 64);
        pp->win.kinds = (uint8_t*)fast_arena_alloc(scratch, BUSTER_FWIN_REGION_BYTES, 64);
        pp->win.lens = pp->win.kinds == NULL ? NULL : pp->win.kinds + BUSTER_FWIN_LENS_OFFSET;
        pp->win.starts = pp->win.kinds == NULL ? NULL : (uint16_t*)(void*)(pp->win.kinds + BUSTER_FWIN_STARTS_OFFSET);

        ok = pp->out_kind != NULL && pp->out_data != NULL && pp->num_pool != NULL && pp->lit_bytes != NULL &&
             pp->lit_slices != NULL && pp->lit_coded != NULL && pp->macros != NULL && pp->macro_texts != NULL && pp->macro_toks != NULL && pp->macro_params != NULL &&
             pp->mapped_bases != NULL && pp->mapped_sizes != NULL &&
             pp->exp_buf != NULL && pp->arg_buf != NULL && pp->call_stack != NULL &&
             pp->win.kinds != NULL && pp->win.starts != NULL && pp->win.lens != NULL;
    }
    return ok;
}

// The language mode's part of the preprocessor (#3592): `c23` (C23's `true` in #if, typeof a keyword), `strict_iso`
// (-std=cNN: clang's comma rule without GNU's extensions), `trigraphs` (a file with one leaves the direct path),
// `raw_strings` (a raw string literal leaves it, #3600: 1 its R and LR forms, 2 also u8R, uR and UR), and
// without GNU's keywords asm, and typeof before C23, as identifiers (c_intern.c's c_name_is_keyword). Before any
// lookup: the identifier cache keeps a name's kind from its first one.
static void fast_pp_set_language(FastPP* pp, bool c23, bool strict_iso, bool no_gnu_keywords, bool trigraphs, uint8_t raw_strings)
{
    pp->c23 = c23;
    pp->strict_iso = strict_iso;
    pp->trigraphs = trigraphs;
    pp->raw_strings = raw_strings;
    if (no_gnu_keywords)
    {
        pp->idents.entries[fast_ident_intern_cstr(&pp->idents, "asm")].kw_kind = 0;
    }
    if (no_gnu_keywords && !c23)
    {
        pp->idents.entries[fast_ident_intern_cstr(&pp->idents, "typeof")].kw_kind = 0;
    }
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
    else if (__builtin_expect(kind == FTOK_BAD_LITERAL, 0))
    {
        fast_pp_bail(pp, "literal the full pipeline diagnoses");
    }
    else if (__builtin_expect(((uint32_t)(kind == FTOK_CHAR_LIT) & ((uint32_t)tok.flags / BUSTER_FTOK_SPELLED)) & 1u, 0))
    {
        // #3577: a char16_t, char32_t or (C23) unsigned char constant, typed int here.
        const char first = pp->lit_bytes[pp->lit_slices[tok.extra].offset];
        if (first == 'u' || first == 'U')
        {
            fast_pp_bail(pp, "character constant of a type other than int");
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
// '#', a quote, a backslash, and every character with no token kind), an identifier's or a number's character, and a
// '.'. test-direct checks it against fast_ascii_to_kind and fast_char_class.
#define BUSTER_FCLASS_SPACE    0x01u
#define BUSTER_FCLASS_NEWLINE  0x02u
#define BUSTER_FCLASS_SPECIAL  0x04u
#define BUSTER_FCLASS_RUN      0x08u
#define BUSTER_FCLASS_DOT      0x10u
_Alignas(64) static const uint8_t fast_lex_class[128] = {
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x01, 0x03, 0x01, 0x01, 0x01, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x01, 0x00, 0x04, 0x04, 0x08, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x04,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
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

// The class of the byte after each byte, for the window's lexer: the second character's class in a two-character
// punctuator (fast_pair_second's, the low three bits), whether it goes on with an identifier's or a number's run
// (BUSTER_FCLASS_RUN), and whether it makes a '.' before it a number's or an ellipsis's (a digit or a '.', in
// BUSTER_FCLASS_DOT's place). test-direct checks it against the tables it restates.
_Alignas(64) static const uint8_t fast_lex_next_class[128] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x03, 0x10, 0x00,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x07, 0x01, 0x02, 0x00,
    0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x08,
    0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x06, 0x00, 0x00, 0x00,
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
// address doesn't wait for this block's masks, so the blocks' work overlaps. A block does no branch on its tokens but
// a rare one, and reads nothing back from the window (a 64-byte store that crosses a cache line does not forward to a
// load on Zen 5): what crosses its end is carried in registers, a run that goes on (its first byte's offset; its
// token is the window's last, and its length is written when its end is found). A byte's class and the class of the
// byte after it are a lookup each, from the block's bytes and from the bytes one further on: the runs' ends, the '.'
// that fast_lex_one takes and the two-character punctuators' second characters come from the second without a shift
// across the block's end. The punctuators are a lookup by the two characters' classes; only where two candidates
// touch ("<<=", "a+++b"), one is at the last byte or a '.' goes on as a number does, a rare branch takes the
// candidates at even places of each run of them (the longest match from the left) and marks the tokens fast_lex_one
// lexes. The tokens' kinds, first bytes and last bytes are three compresses: a token's last byte is a run's last, a
// punctuator's only or a pair's second, so the n-th last byte is the n-th token's and a length is the difference.
static BUSTER_DIRECT_HOT void fast_pp_lex_window(FastPP* pp, const FastIncludeFile* f, const uint8_t* p, bool bol, bool line_mode)
{
    FastWindow* const w = &pp->win;
    const uint8_t* const base = f->base;
    const uint8_t* const end = f->end;
    // The window's arrays are one region (fast_pp_init): one register addresses the three.
    uint8_t* const wk = w->kinds;
    uint8_t* const wl = wk + BUSTER_FWIN_LENS_OFFSET;
    uint16_t* const ws = (uint16_t*)(void*)(wk + BUSTER_FWIN_STARTS_OFFSET);
    const __m512i v_lut_lo = _mm512_load_si512((const void*)(fast_ascii_to_kind));
    const __m512i v_lut_hi = _mm512_load_si512((const void*)(fast_ascii_to_kind + 64));
    const __m512i v_class_lo = _mm512_load_si512((const void*)(fast_lex_class));
    const __m512i v_class_hi = _mm512_load_si512((const void*)(fast_lex_class + 64));
    const __m512i v_next_lo = _mm512_load_si512((const void*)(fast_lex_next_class));
    const __m512i v_next_hi = _mm512_load_si512((const void*)(fast_lex_next_class + 64));
    const __m512i v_first_lo = _mm512_load_si512((const void*)(fast_pair_first));
    const __m512i v_first_hi = _mm512_load_si512((const void*)(fast_pair_first + 64));
    const __m512i v_pair_lo = _mm512_load_si512((const void*)(fast_pair_kind));
    const __m512i v_pair_hi = _mm512_load_si512((const void*)(fast_pair_kind + 64));
    const __m512i v_iota = _mm512_load_si512((const void*)fast_iota64);
    const __m512i v_iota1 = _mm512_load_si512((const void*)fast_iota64_plus1);
    const __m512i v_space_bit = _mm512_set1_epi8((char)BUSTER_FCLASS_SPACE);
    const __m512i v_newline_bit = _mm512_set1_epi8((char)BUSTER_FCLASS_NEWLINE);
    const __m512i v_special_bits = _mm512_set1_epi8((char)(BUSTER_FCLASS_SPECIAL | (line_mode ? BUSTER_FCLASS_NEWLINE : 0u)));
    const __m512i v_run_bit = _mm512_set1_epi8((char)BUSTER_FCLASS_RUN);
    const __m512i v_dot_bit = _mm512_set1_epi8((char)BUSTER_FCLASS_DOT);
    const __m512i v_second_bits = _mm512_set1_epi8(7);
    const __m512i v_sign_bit = _mm512_set1_epi8((char)0x80);
    const uint64_t even_bits = UINT64_C(0x5555555555555555);
    const uint64_t top_bit = UINT64_C(1) << 63;
    // A slot past the window's tokens for the stores that have nothing to write.
    const uint32_t spare = BUSTER_FWIN_CAPACITY + 63u;
    // The last block starts 80 bytes before the file's end (the identifiers' 16-byte loads and this loop's loads stay
    // inside the file), and fewer than BUSTER_FWIN_SPAN bytes after the window's first.
    const uint64_t size = (uint64_t)(end - base);
    const uint64_t first_off = (uint64_t)(p - base);
    const uint64_t tail_at = size < 80u ? 0 : size - 79u;
    const uint64_t stop_at = tail_at < first_off + BUSTER_FWIN_SPAN ? tail_at : first_off + BUSTER_FWIN_SPAN;
    uint64_t off = first_off;
    uint32_t n = 0;
    uint32_t stop = BUSTER_FWIN_STOP_CAPACITY;
    uint32_t unused_line = 0;
    // What crosses a block's end.
    uint64_t run_carry = 0;      // 1 if the last byte's identifier or number goes on in this block
    uint64_t open_start = 0;     // the first byte's offset of the last run that started

    while (n + 64u <= BUSTER_FWIN_CAPACITY)
    {
        if (__builtin_expect(off >= stop_at, 0))
        {
            stop = off >= tail_at ? BUSTER_FWIN_STOP_TAIL : BUSTER_FWIN_STOP_CAPACITY;
            break;
        }
        // The text's lines are first read here (a comment's are the skipper's): the lines a kilobyte ahead are asked
        // for.
        const uint64_t block_off = off;
        _mm_prefetch((const char*)(base + off + 1024u), _MM_HINT_T0);
        const __m512i v = _mm512_loadu_si512((const void*)(base + off));
        const __m512i v1 = _mm512_loadu_si512((const void*)(base + off + 1u));
        const __m512i kinds_raw = _mm512_permutex2var_epi8(v_lut_lo, v, v_lut_hi);
        const __m512i classes = _mm512_permutex2var_epi8(v_class_lo, v, v_class_hi);
        // The tables are indexed by seven bits: a byte of 128 and more stops the block before any of its masks is used,
        // and has no class as the byte after another (no run, no punctuator, no number goes on with it).
        const __m512i next_classes = _mm512_maskz_permutex2var_epi8(_mm512_testn_epi8_mask(v1, v_sign_bit), v_next_lo, v1, v_next_hi);
        const __m512i pair_kinds = _mm512_permutex2var_epi8(v_pair_lo,
                                                            _mm512_ternarylogic_epi32(_mm512_permutex2var_epi8(v_first_lo, v, v_first_hi),
                                                                                      next_classes, v_second_bits, 0xf8),
                                                            v_pair_hi);
        const uint64_t special = _mm512_test_epi8_mask(classes, v_special_bits) | _mm512_movepi8_mask(v);
        const uint32_t limit = (uint32_t)_tzcnt_u64(special);
        const uint64_t lmask = _bzhi_u64(~UINT64_C(0), limit);
        const uint64_t solid = _mm512_testn_epi8_mask(classes, v_space_bit) & lmask;
        const uint64_t runs = _mm512_test_epi8_mask(classes, v_run_bit) & lmask;
        const uint64_t run_goes_on = runs & _mm512_test_epi8_mask(next_classes, v_run_bit);
        // A candidate's second character is a punctuator, which is no byte that stops the block.
        const uint64_t candidates = _mm512_test_epi8_mask(pair_kinds, pair_kinds) & lmask;
        const uint64_t dots = _mm512_test_epi8_mask(_mm512_and_si512(classes, next_classes), v_dot_bit) & lmask;
        uint64_t pairs = candidates;
        uint64_t scalars = 0;
        if (__builtin_expect(((candidates & ((candidates >> 1) | top_bit)) | dots) != 0, 0))
        {
            // A taken pair uses up the next candidate's first character: of each run of candidates, the ones at even
            // places from its start. A pair before a candidate (the third character may go on with it) and a pair at
            // the last byte (its second and third characters are not in this block: the next block lexes them as its
            // own tokens, which fast_pp_window_pass passes or lexes again after this one) are fast_lex_one's.
            const uint64_t candidate_starts = candidates & ~(candidates << 1);
            const uint64_t from_even = candidates & ~(candidates + (candidate_starts & even_bits));
            pairs = (from_even & even_bits) | (candidates & ~from_even & ~even_bits);
            scalars = dots | (pairs & ((candidates >> 1) | top_bit));
        }
        // A token starts at a byte that is no space, no run's byte after another, no pair's second character, and ends
        // at one that is no space, no run's byte before another and no pair's first character. The first end is the
        // run's that came from the block before, whose token is the window's last one.
        const uint64_t tok_starts = solid & ~((runs & ((runs << 1) | run_carry)) | (pairs << 1));
        const uint64_t tok_ends = solid & ~(run_goes_on | pairs);
        const uint64_t own_ends = tok_ends & (tok_ends - run_carry);
        // The run that came from the block before ends here, and its token's length is known (or the run goes on: the
        // length so far, written again by the block that ends it). A length of more than a byte is fast_lex_one's.
        const uint64_t carried_len = off + _tzcnt_u64(tok_ends) + 1u - open_start;
        wl[run_carry != 0 ? n - 1u : spare] = (uint8_t)carried_len;
        if (__builtin_expect((carried_len & (0u - run_carry)) > 255u, 0))
        {
            wk[n - 1u] = BUSTER_FWIN_SCALAR;
        }

        const uint32_t m_count = (uint32_t)__builtin_popcountll(tok_starts);
        const __m512i comp_kinds = _mm512_maskz_compress_epi8(tok_starts, _mm512_mask_blend_epi8(pairs, kinds_raw, pair_kinds));
        const __m512i comp_pos = _mm512_maskz_compress_epi8(tok_starts, v_iota);
        const __m512i comp_ends = _mm512_maskz_compress_epi8(own_ends, v_iota1);
        _mm512_storeu_si512((void*)(wk + n), comp_kinds);
        _mm512_storeu_si512((void*)(wl + n), _mm512_sub_epi8(comp_ends, comp_pos));
        const __m512i v_offset = _mm512_set1_epi16((short)(off - first_off));
        _mm512_storeu_si512((void*)(ws + n), _mm512_add_epi16(v_offset, _mm512_cvtepu8_epi16(_mm512_castsi512_si256(comp_pos))));
        if (__builtin_expect(m_count > 32u, 0))
        {
            _mm512_storeu_si512((void*)(ws + n + 32u), _mm512_add_epi16(v_offset, _mm512_cvtepu8_epi16(_mm512_extracti64x4_epi64(comp_pos, 1))));
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

        // What crosses into the next block: the last run, if the byte after the block goes on with it, and the last
        // run's first byte. Whether a line starts after these bytes: a newline past the last byte that is no
        // whitespace.
        const uint64_t run_starts = runs & tok_starts;
        open_start = run_starts != 0 ? off + 63u - _lzcnt_u64(run_starts) : open_start;
        run_carry = run_goes_on >> 63;
        const uint64_t newlines = _mm512_test_epi8_mask(classes, v_newline_bit) & lmask;
        bol = (newlines > solid) | (bol & (solid == 0));
        n += m_count;

        if (limit == 64u)
        {
            off += 64u;
            continue;
        }
        off += limit;
        p = base + off;

        // The byte that needs a decision. No run crosses into it, and no pair. What stands directly before it may
        // belong with it: the identifier or number that ends there, which is the window's last token and starts at
        // open_start. At the first byte of a block after a decision, the byte before is the decision's last: a
        // comment's, a literal's or an operator's, none of them a run's, or a line comment's last before its newline,
        // which ends a line's window before this is asked (outside one, a newline needs no decision).
        const bool after_run = (limit != 0 || off != first_off) && (fast_char_class[p[-1]] & 1u) != 0;
        const uint8_t ch = p[0];
        if (ch == '/')
        {
            // A comment that opens here and closes in this block: its '*' and its "*/" from the block's own bytes (no
            // "*/" shares the opening's '*').
            const uint64_t stars = _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8('*'));
            const uint64_t closes = (stars << 1) & _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8('/')) & ~_bzhi_u64(~UINT64_C(0), limit + 3u);
            const uint64_t closes_here = closes & (UINT64_C(0) - ((stars >> limit >> 1) & 1u));
            if (closes_here != 0)
            {
                off = block_off + _tzcnt_u64(closes_here) + 1u;
                continue;
            }
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
                off = (uint64_t)(after - base);
                continue;
            }
            if (ch1 == '/')
            {
                off = (uint64_t)(fast_skip_line_comment(p + 2, end, &unused_line) - base);
                continue;
            }
            const uint32_t length = ch1 == '=' ? 2u : 1u;
            wk[n] = ch1 == '=' ? FTOK_DIV_ASSIGN : FTOK_SLASH;
            ws[n] = (uint16_t)(off - first_off);
            wl[n] = (uint8_t)length;
            n += 1u;
            off += length;
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
            ws[n] = (uint16_t)(off - first_off);
            wl[n] = (uint8_t)length;
            n += 1u;
            off += length;
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
            const uint8_t* const run = base + open_start;
            const uint32_t run_len = (uint32_t)(p - run);
            const bool prefix = (run_len == 1u && (run[0] == 'L' || run[0] == 'u' || run[0] == 'U')) ||
                                (run_len == 2u && run[0] == 'u' && run[1] == '8') ||
                                (pp->raw_strings != 0 && ch == '"' && run_len <= 3u && run[run_len - 1u] == 'R');
            const bool number = (fast_char_class[run[0]] & 4u) != 0;
            if (ch == '\\' || (ch == '\'' && number) || ((ch == '"' || ch == '\'') && prefix))
            {
                n -= 1u;
                off = open_start;
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
                // Its length with both quotes where a byte holds it (fast_pp_window_convert sizes tokens by it), and 0
                // for a longer one.
                const uint64_t string_length = (uint64_t)(close + 1 - p);
                wk[n] = FTOK_STR_LIT;
                ws[n] = (uint16_t)(off - first_off);
                wl[n] = (uint8_t)(string_length <= 255u ? string_length : 0u);
                n += 1u;
                off = (uint64_t)(close + 1 - base);
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
                ws[n] = (uint16_t)(off - first_off);
                wl[n] = (uint8_t)length;
                n += 1u;
                off += length;
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
        n -= 1u;
        off = open_start;
        bol = false;
    }

    w->count = n;
    w->pos = 0;
    w->origin = (uint32_t)first_off;
    w->serial = f->serial;
    w->generation += 1u;
    w->lex_ptr = base + off;
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
    const uint16_t* const starts = w->starts;
    const uint32_t rel = offset > w->origin ? offset - w->origin : 0u;
    uint32_t i = w->pos;
    bool whole = true;
    while (i < count && starts[i] < rel)
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
        whole = starts[last] + length <= rel;
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
    const uint8_t* const s = f->base + w->origin + w->starts[t];
    const uint32_t kind = w->kinds[t];
    const uint32_t len = w->lens[t];
    FastTok tok;
    const uint8_t* text_end = s + len;
    if (kind == FTOK_IDENT)
    {
        uint8_t ident_kind = FTOK_IDENT;
        const uint32_t id = fast_ident_lookup(&pp->idents, s, len, &ident_kind);
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

// Whether the number whose run of digits and letters is s[0..len) goes on past it, as fast_lex_number takes it: a '.'
// follows, or a sign after an exponent's letter.
static inline bool fast_number_goes_on(const uint8_t* s, uint32_t len)
{
    const uint8_t after = s[len];
    const uint8_t last = s[len - 1u] | 0x20u;
    return after == '.' || ((after == '+' || after == '-') && (last == 'e' || last == 'p'));
}

// The first of the window's tokens t0 to t1 that fast_pp_window_convert can't take, or t1: a token the window left to
// fast_lex_one, a string longer than its length byte holds, and a number that goes on past its run (fast_lex_number
// takes the '.' or the exponent's sign after it, which are the window's next tokens).
static uint32_t fast_pp_window_limit(const FastWindow* w, const uint8_t* base, uint32_t t0, uint32_t t1)
{
    const __m512i v_string = _mm512_set1_epi8(FTOK_STR_LIT);
    const __m512i v_number = _mm512_set1_epi8(FTOK_INT_LIT);
    uint32_t limit = t1;
    for (uint32_t t = t0; t < limit; t += 64u)
    {
        const __m512i kinds = _mm512_loadu_si512((const void*)(w->kinds + t));
        const __m512i lens = _mm512_loadu_si512((const void*)(w->lens + t));
        const __mmask64 live = _bzhi_u64(~UINT64_C(0), limit - t < 64u ? limit - t : 64u);
        uint64_t refused = (_mm512_testn_epi8_mask(kinds, kinds) | (_mm512_cmpeq_epi8_mask(kinds, v_string) & _mm512_testn_epi8_mask(lens, lens))) & live;
        uint64_t numbers = _mm512_cmpeq_epi8_mask(kinds, v_number) & live & (_blsi_u64(refused) - 1u);
        while (numbers != 0)
        {
            const uint32_t k = (uint32_t)__builtin_ctzll(numbers);
            numbers &= numbers - 1u;
            if (fast_number_goes_on(base + w->origin + w->starts[t + k], w->lens[t + k]))
            {
                refused |= UINT64_C(1) << k;
                numbers = 0;
            }
        }
        limit = refused != 0 ? t + (uint32_t)__builtin_ctzll(refused) : limit;
    }
    return limit;
}

// The window's tokens t0 to t1 (none of them one fast_pp_window_limit refuses) as fast_lex_one gives them, into `out`
// (room for 15 more), each with BUSTER_FTOK_SPACE where text stands between it and the token before it: blanks, a
// comment, a newline (before the first, the text from *io_end; *io_end is set to where the last ends). No token gets
// BUSTER_FTOK_BOL: the directives and the macro calls that take these tokens never ask (fast_pp_single_token asks
// fast_lex_one's own token). Sixteen tokens at a time: their ends from the window's starts and lengths (a punctuator's
// by its kind), the gaps by comparing each start with the end before it, and the tokens' kinds and flags made in
// registers and stored whole; then the tokens with data, the identifiers to look up and the literals to convert, a
// loop over their lanes.
static void fast_pp_window_convert(FastPP* pp, const FastIncludeFile* f, uint32_t t0, uint32_t t1, const uint8_t** io_end, FastTok* out)
{
    const FastWindow* const w = &pp->win;
    const uint8_t* const base = f->base;
    const __m512i v_punct_len = _mm512_loadu_si512((const void*)fast_punct_len);
    const __m512i v_space = _mm512_set1_epi64((int64_t)BUSTER_FTOK_SPACE << 8);
    const __m128i v_one = _mm_set1_epi8(1);
    const __m128i v_sized_max = _mm_set1_epi8(FTOK_STR_LIT - 1);
    const __m512i v_origin = _mm512_set1_epi32((int32_t)w->origin);
    __m512i v_ends = _mm512_set1_epi32((int32_t)(*io_end - base));
    for (uint32_t t = t0; t < t1; t += 16u)
    {
        const uint32_t lanes = t1 - t < 16u ? t1 - t : 16u;
        const __m128i kinds = _mm_loadu_si128((const __m128i*)(w->kinds + t));
        const __m128i lens = _mm_loadu_si128((const __m128i*)(w->lens + t));
        const __m512i starts = _mm512_add_epi32(v_origin, _mm512_cvtepu16_epi32(_mm256_loadu_si256((const __m256i*)(const void*)(w->starts + t))));
        // Identifiers, numbers and literals (kinds 1 to 5) have the window's length.
        const __mmask16 sized = _mm_cmple_epu8_mask(_mm_sub_epi8(kinds, v_one), v_sized_max);
        const __m128i punct_len = _mm512_castsi512_si128(_mm512_permutexvar_epi8(_mm512_castsi128_si512(kinds), v_punct_len));
        const __m512i ends = _mm512_add_epi32(starts, _mm512_cvtepu8_epi32(_mm_mask_blend_epi8(sized, punct_len, lens)));
        const uint32_t space = (uint32_t)_mm512_cmpneq_epi32_mask(starts, _mm512_alignr_epi32(ends, v_ends, 15));
        const __m512i low = _mm512_cvtepu8_epi64(kinds);
        const __m512i high = _mm512_cvtepu8_epi64(_mm_unpackhi_epi64(kinds, kinds));
        const __m512i low_tokens = _mm512_mask_or_epi64(low, (__mmask8)space, low, v_space);
        const __m512i high_tokens = _mm512_mask_or_epi64(high, (__mmask8)(space >> 8), high, v_space);
        // 32-byte stores: a token is read back soon, and Zen 5 forwards a 64-byte store to a narrower load only inside
        // one of its halves and its first cache line.
        FastTok* const o = out + (t - t0);
        _mm256_storeu_si256((__m256i*)(void*)o, _mm512_castsi512_si256(low_tokens));
        _mm256_storeu_si256((__m256i*)(void*)(o + 4), _mm512_extracti64x4_epi64(low_tokens, 1));
        _mm256_storeu_si256((__m256i*)(void*)(o + 8), _mm512_castsi512_si256(high_tokens));
        _mm256_storeu_si256((__m256i*)(void*)(o + 12), _mm512_extracti64x4_epi64(high_tokens, 1));
        uint32_t work = (uint32_t)sized & _bzhi_u32(0xffffu, lanes);
        while (work != 0)
        {
            const uint32_t k = (uint32_t)__builtin_ctz(work);
            work &= work - 1u;
            const uint32_t ti = t + k;
            const uint8_t* const s = base + w->origin + w->starts[ti];
            const uint32_t kind = w->kinds[ti];
            const uint32_t flags = ((space >> k) & 1u) * BUSTER_FTOK_SPACE;
            FastTok tok;
            if (kind == FTOK_IDENT)
            {
                uint8_t ident_kind = FTOK_IDENT;
                const uint32_t id = fast_ident_lookup(&pp->idents, s, w->lens[ti], &ident_kind);
                if (__builtin_expect(id == 0, 0))
                {
                    fast_pp_bail(pp, "ident table full");
                }
                tok = fast_tok_make(ident_kind, flags, 0, id);
            }
            else if (kind == FTOK_INT_LIT)
            {
                tok = fast_lex_number(pp, s, f->end, flags).tok;
            }
            else if (kind == FTOK_STR_LIT)
            {
                tok = fast_lex_string(pp, s, f->end, flags, 0).tok;
            }
            else
            {
                tok = fast_lex_char(pp, s, f->end, flags, 0).tok;
            }
            o[k] = tok;
        }
        // The last token's end, which the next sixteen's first gap starts from.
        v_ends = _mm512_permutexvar_epi32(_mm512_set1_epi32((int32_t)(lanes - 1u)), ends);
    }
    *io_end = base + (uint32_t)_mm_cvtsi128_si32(_mm512_castsi512_si128(v_ends));
}

// A function-like macro's call whose '(' is the window's token `lparen`, its arguments taken from the window in bulk:
// where the window has the ')' that closes the call and none of the tokens before it is one fast_pp_window_convert
// refuses, the tokens between are converted into exp_buf and the call's arguments (cf->arg_starts, cf->arg_counts,
// *out_arg_count) are their stretches between the commas at depth 1 (a variadic macro's first param_count of them),
// as fast_pp_run_calls's collection splits them. The ')' is the first token at depth 0 (fast_paren_depths). Returns
// that ')''s index, or 0 where the call isn't taken here and nothing was converted.
static uint32_t fast_pp_window_args(FastPP* pp, const FastIncludeFile* f, uint32_t lparen, const FastMacro* m, FastMacroCall* cf,
                                    uint32_t* out_arg_count)
{
    const FastWindow* const w = &pp->win;
    const uint32_t first = lparen + 1u;
    const uint32_t count = w->count;
    const uint32_t base_used = pp->exp_used;
    const __m512i v_lparen = _mm512_set1_epi8(FTOK_LPAREN);
    const __m512i v_rparen = _mm512_set1_epi8(FTOK_RPAREN);
    const __m512i v_comma = _mm512_set1_epi8(FTOK_COMMA);
    uint32_t depth = 1u;
    uint32_t close = 0;
    uint32_t arg_count = 0;
    uint32_t arg_start = 0;
    bool taken = true;
    for (uint32_t b = first; taken && close == 0 && b < count; b += 64u)
    {
        const uint64_t live = _bzhi_u64(~UINT64_C(0), count - b < 64u ? count - b : 64u);
        const __m512i kinds = _mm512_loadu_si512((const void*)(w->kinds + b));
        const uint64_t closing = fast_call_block(_mm512_cmpeq_epi8_mask(kinds, v_lparen), _mm512_cmpeq_epi8_mask(kinds, v_rparen),
                                                 _mm512_cmpeq_epi8_mask(kinds, v_comma), live, &depth, m, cf, base_used, b - first,
                                                 &arg_start, &arg_count);
        taken = closing != UINT64_MAX;
        close = taken && closing != 0 ? b + (uint32_t)__builtin_ctzll(closing) : 0u;
    }
    const uint32_t span = close - first;
    taken = taken && close != 0 && fast_pp_window_limit(w, f->base, first, close) == close && base_used + span + 16u <= pp->exp_cap;
    if (taken)
    {
        *out_arg_count = fast_call_last_arg(cf, base_used, span, arg_start, arg_count);
        const uint8_t* end = f->base + w->origin + w->starts[lparen] + 1u;
        fast_pp_window_convert(pp, f, first, close, &end, pp->exp_buf + base_used);
        pp->exp_used = base_used + span;
    }
    return taken ? close : 0u;
}

// The rest of a directive's line into `out` (room for `capacity` tokens and 15 more): the tokens fast_pp_read_tok
// would give one at a time up to the line's end, the window's in bulk (fast_pp_window_convert) and fast_pp_read_tok's
// where the window has none to give (a token it left to fast_lex_one, a splice, the window's end). Returns how many
// tokens the line has, or capacity + 1 where it has more.
static uint32_t fast_pp_read_line(FastPP* pp, FastIncludeFile* f, FastTok* out, uint32_t capacity)
{
    FastWindow* const w = &pp->win;
    uint32_t n = 0;
    bool done = false;
    while (!done)
    {
        // A line's window ends where the line does: its tokens are the line's.
        if (fast_pp_window_seek(pp, f) && w->line_mode && w->pos < w->count)
        {
            const uint32_t t0 = w->pos;
            const uint32_t limit = fast_pp_window_limit(w, f->base, t0, w->count);
            const uint32_t t1 = limit - t0 <= capacity - n ? limit : t0 + (capacity - n);
            if (t1 > t0)
            {
                const uint8_t* end = f->ptr;
                fast_pp_window_convert(pp, f, t0, t1, &end, out + n);
                n += t1 - t0;
                w->pos = t1;
                fast_pp_sync_file(f, end);
                f->at_bol = false;
            }
            // The line's end, where the window took it all: fast_pp_read_tok's end of the line.
            done = t1 == w->count && w->stop == BUSTER_FWIN_STOP_EOL;
            if (done)
            {
                fast_pp_sync_file(f, w->lex_ptr);
            }
        }
        if (!done)
        {
            FastTok tok;
            fast_pp_read_tok(pp, f, true, &tok);
            if (tok.kind != FTOK_EOF)
            {
                if (n < capacity)
                {
                    out[n] = tok;
                }
                n += 1u;
            }
            done = tok.kind == FTOK_EOF || n > capacity || pp->bailout;
        }
    }
    return n;
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
                const uint8_t* const s = f->base + w->origin + w->starts[t];
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
    const uint8_t* const after_name = f->base + w->origin + w->starts[t] + w->lens[t];
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
        // Its `(` is the macro's (data 1, as the #define marks one, for -Wparentheses-equality): the window writes no
        // punctuator's data, so the slot it takes says so (#3577).
        if (fmode == 5u)
        {
            const uint8_t* q = after_name;
            while (q < end && (*q == ' ' || *q == '\t')) { q += 1; }
            if (q < end && *q == '(')
            {
                pp->out_data[pp->out_count] = 1u;
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
                // The object-like cast macro can be redefined between definition and invocation.
                if (fmode == 3u && (fm->fast_data == 0 ||
                    pp->idents.entries[pp->macro_toks[fm->body_start + 1u].data].macro_idx == fm->fast_data))
                {
                    // A trailing semicolon can also end a comma or conditional expression.
                    // Only an entire expression statement can be reduced to an empty statement.
                    const uint8_t prev = pp->out_count == 0 ? FTOK_EOF : pp->out_kind[pp->out_count - 1u];
                    const bool statement = prev == FTOK_LBRACE || prev == FTOK_RBRACE || prev == FTOK_SEMI ||
                                           prev == FTOK_KW_ELSE || prev == FTOK_KW_DO;
                    const uint8_t* q = after_args;
                    while (q < end && (*q == ' ' || *q == '\t')) { q += 1; }
                    if (statement && q < end && *q == ';')
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

    // Any other macro: the expansion engine's. A function-like macro's arguments come from the window in bulk where
    // it has the whole call (fast_pp_window_args), and through fast_pp_read_tok otherwise.
    if (f->ptr <= after_name)
    {
        fast_pp_sync_file(f, after_name);
    }
    f->ptr = after_name;
    f->at_bol = false;
    FastTok rep = fast_tok_make(FTOK_EOF, 0, 0, 0);
    const FastTok mtok = fast_tok_make(kind, 0, 0, id);
    bool is_func = false;
    bool expanded = fast_pp_try_expand_nonfunc(pp, mtok, false, &rep, &is_func);
    if (!expanded && is_func)
    {
        const uint32_t lparen = t + 1u;
        FastMacroCall* const root = fast_pp_call_start(pp, mtok, 0, true);
        uint32_t arg_count = 0;
        const uint32_t close = lparen < w->count && w->kinds[lparen] == FTOK_LPAREN ?
                               fast_pp_window_args(pp, f, lparen, pp->macros + fmidx, root, &arg_count) : 0u;
        if (close != 0)
        {
            // The text goes on after the ')', with its line: the prescans and the body may ask for __LINE__.
            fast_pp_sync_file(f, f->base + w->origin + w->starts[close] + 1u);
            w->pos = close + 1u;
            expanded = fast_pp_call_collected(pp, root, pp->macros + fmidx, arg_count, arg_count > 1u || root->arg_counts[0] != 0) &&
                       fast_pp_run_calls(pp, &rep);
        }
        else if (fast_pp_consume_lparen(pp, false, 0, true))
        {
            expanded = fast_pp_run_calls(pp, &rep);
        }
    }
    if (expanded)
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

// Two to eight decimal digits from a window (at least eight bytes readable at the start).
// Left-pad the digits with zeroes, then reduce adjacent pairs and groups of four in parallel.
static inline bool fast_window_decimal(const uint8_t* s, uint32_t length, uint32_t* value)
{
    bool decimal = length <= 8u && s[0] != '0';
    if (decimal)
    {
        uint64_t raw;
        memcpy(&raw, s, sizeof(raw));
        const uint64_t padded = (raw ^ UINT64_C(0x3030303030303030)) << ((8u - length) * 8u);
        const __m128i digits = _mm_cvtsi64_si128((int64_t)padded);
        decimal = _mm_cmpgt_epu8_mask(digits, _mm_set1_epi8(9)) == 0;
        if (decimal)
        {
            const __m128i pairs = _mm_maddubs_epi16(digits, _mm_set1_epi16(0x010a));
            const __m128i groups = _mm_madd_epi16(pairs, _mm_set1_epi32(0x00010064));
            *value = (uint32_t)_mm_cvtsi128_si32(groups) * 10000u + (uint32_t)_mm_extract_epi32(groups, 1);
        }
    }
    return decimal;
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
    fast_pp_sync_file(f, f->base + w->origin + w->starts[w->pos]);
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

// Each lane holds the one to eight digits after 0x. Left-padding with zero nibbles
// lets the same reductions decode all eight widths and both letter cases.
static inline bool fast_pp_hex_pairs(const uint8_t* base, __m512i starts, __m512i words, __m512i len, uint8_t* kinds_out, uint32_t* values_out)
{
    if (_mm512_cmpgt_epu64_mask(_mm512_sub_epi64(len, _mm512_set1_epi64(3)), _mm512_set1_epi64(7)) != 0)
    {
        return false;
    }
    const __m512i zero = _mm512_set1_epi8('0');
    __m512i text = _mm512_srli_epi64(words, 16);
    const __mmask8 wide = _mm512_cmpgt_epu64_mask(len, _mm512_set1_epi64(8));
    if (wide != 0) { text = _mm512_mask_i64gather_epi64(text, wide, starts, base + 2, 1); }
    const __m512i shift = _mm512_slli_epi64(_mm512_sub_epi64(_mm512_set1_epi64(10), len), 3);
    const __m512i digit = _mm512_sllv_epi64(_mm512_sub_epi8(text, zero), shift);
    const __m512i letter = _mm512_sllv_epi64(_mm512_sub_epi8(_mm512_or_si512(text, _mm512_set1_epi8(32)), _mm512_set1_epi8('a')), shift);
    const __mmask64 alpha = _mm512_cmpgt_epu8_mask(digit, _mm512_set1_epi8(9));
    if (_mm512_mask_cmpgt_epu8_mask(alpha, letter, _mm512_set1_epi8(5)) != 0) { return false; }
    const __m512i nibbles = _mm512_mask_add_epi8(digit, alpha, letter, _mm512_set1_epi8(10));
    const __m512i bytes = _mm512_maddubs_epi16(nibbles, _mm512_set1_epi16(0x0110));
    const __m512i halves = _mm512_madd_epi16(bytes, _mm512_set1_epi32(0x00010100));
    const __m512i values = _mm512_or_si512(_mm512_slli_epi32(halves, 16), _mm512_srli_epi64(halves, 32));
    _mm512_mask_storeu_epi32(values_out, 0x5555u, values);
    _mm_mask_storeu_epi8(kinds_out, _mm512_movepi32_mask(values) & 0x5555u, _mm_set1_epi8(FTOK_INT_U32));
    return true;
}

// Eight comma-terminated literals. Each window start has at least 16 readable bytes.
// Left-pad the digits within each word, then reduce pairs and groups of four in parallel.
static inline bool fast_pp_integer_pairs(const FastWindow* w, uint32_t t, const uint8_t* base, uint8_t* kinds_out, uint32_t* values_out)
{
    base += w->origin;
    const __m128i kinds = _mm_loadu_si128((const __m128i*)(w->kinds + t));
    const __m128i pair = _mm_set1_epi16(FTOK_INT_LIT | (FTOK_COMMA << 8));
    bool decoded = false;
    if (_mm_cmpeq_epi16_mask(kinds, pair) == UINT8_MAX)
    {
        const __m128i lengths = _mm_and_si128(_mm_loadu_si128((const __m128i*)(w->lens + t)), _mm_set1_epi16(255));
        const __m512i len = _mm512_cvtepu16_epi64(lengths);
        // Each 32-bit lane holds a number's 16-bit offset followed by its comma's offset.
        const __m256i offsets = _mm256_loadu_si256((const __m256i*)(const void*)(w->starts + t));
        const __m512i starts = _mm512_cvtepu32_epi64(_mm256_and_si256(offsets, _mm256_set1_epi32(UINT16_MAX)));
        const __m512i words = _mm512_i64gather_epi64(starts, base, 1);
        const __m512i zero = _mm512_set1_epi8('0');
        // Compare the first two bytes with "0X", folding the case of the second byte.
        if (_mm512_cmpeq_epi64_mask(_mm512_and_si512(words, _mm512_set1_epi64(0xdfff)), _mm512_set1_epi64(0x5830)) == UINT8_MAX)
        {
            return fast_pp_hex_pairs(base, starts, words, len, kinds_out, values_out);
        }
        const __m512i chars = _mm512_xor_si512(words, zero);
        const __m512i shift = _mm512_slli_epi64(_mm512_sub_epi64(_mm512_set1_epi64(8), len), 3);
        const __m512i digits = _mm512_sllv_epi64(chars, shift);
        const __mmask8 leading_zero = _mm512_testn_epi64_mask(chars, _mm512_set1_epi64(255));
        if (_mm512_cmpgt_epu64_mask(_mm512_sub_epi64(len, _mm512_set1_epi64(1)), _mm512_set1_epi64(7)) == 0 &&
            (leading_zero & _mm512_cmpneq_epi64_mask(len, _mm512_set1_epi64(1))) == 0 &&
            _mm512_cmpgt_epu8_mask(digits, _mm512_set1_epi8(9)) == 0)
        {
            const __m512i pairs = _mm512_maddubs_epi16(digits, _mm512_set1_epi16(0x010a));
            const __m512i groups = _mm512_madd_epi16(pairs, _mm512_set1_epi32(0x00010064));
            const __m512i values = _mm512_add_epi32(_mm512_mullo_epi32(groups, _mm512_set1_epi32(10000)), _mm512_srli_epi64(groups, 32));
            _mm512_mask_storeu_epi32(values_out, 0x5555u, values);
            decoded = true;
        }
    }
    return decoded;
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
            const uint8_t* const s = base + w->origin + w->starts[t];
            const uint32_t len = w->lens[t];
            if (__builtin_expect(kind == FTOK_IDENT, 1))
            {
                uint8_t ident_kind = FTOK_IDENT;
                const uint32_t id = fast_ident_lookup(&pp->idents, s, len, &ident_kind);
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
                    // #3577: the name's slot is the next token's where the expansion is empty, and a punctuator's data
                    // isn't written here: it would keep the name's id, which the checks read as a macro's `(` and as
                    // an access path's difference (`if (EMPTY (x == y))`, `s EMPTY .a == s.a`).
                    out_data[out_count] = 0u;
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
                if (k + 16u <= cnt && fast_pp_integer_pairs(w, t, base, out_kind + out_count + k, out_data + out_count + k))
                {
                    work &= ~_bzhi_u64(UINT64_MAX, k + 16u);
                }
                else if (__builtin_expect(len == 1u, 1))
                {
                    out_data[out_count + k] = (uint32_t)(s[0] - '0');
                }
                else
                {
                    uint32_t value;
                    if (fast_window_decimal(s, len, &value))
                    {
                        out_data[out_count + k] = value;
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
            // #3577: #endif and #else take nothing after them (the full pipeline's warning).
            if (kw == FPP_KW_ENDIF)
            {
                if (cf->is_guard && f->guard_state == 2u) { f->guard_state = 4u; }
                pp->cond_depth -= 1u;
                fast_pp_finish_directive(pp, f);
            }
            else if (kw == FPP_KW_ELSE)
            {
                if (cf->is_guard) { f->guard_state = 3u; cf->is_guard = 0; }
                if (cf->saw_else) { fast_pp_bail(pp, "duplicate #else"); break; }
                cf->saw_else = 1u;
                if (!cf->taken) { cf->active = 1u; cf->taken = 1u; }
                fast_pp_finish_directive(pp, f);
            }
            else if (kw == FPP_KW_ELIF || kw == FPP_KW_ELIFDEF || kw == FPP_KW_ELIFNDEF)
            {
                // #3577: a line splice after the name may make it another; after #endif's and #else's, the letters that
                // would go on the name are the extra tokens fast_pp_finish_directive leaves at.
                if (__builtin_expect(f->ptr < f->end && *f->ptr == '\\', 0))
                {
                    fast_check_spliced_directive(pp, f->base, f->ptr, f->end);
                    if (pp->bailout) { break; }
                }
                if (cf->is_guard) { f->guard_state = 3u; cf->is_guard = 0; }
                if (cf->saw_else) { fast_pp_bail(pp, "#elif after #else"); break; }
                if (!cf->taken)
                {
                    bool cond = false;
                    if (kw == FPP_KW_ELIF) { cond = fast_pp_eval_if_line(pp, f); }
                    else
                    {
                        // #3577: a name and nothing after it, or the full pipeline's error or warning.
                        FastTok nt = { 0 };
                        fast_pp_read_tok(pp, f, true, &nt);
                        if (nt.kind != FTOK_IDENT && !(nt.kind >= 64u && nt.kind < 126u)) { fast_pp_bail(pp, "bad #elifdef name"); break; }
                        const bool def = fast_pp_defined(pp, nt.data);
                        cond = kw == FPP_KW_ELIFDEF ? def : !def;
                        fast_pp_finish_directive(pp, f);
                    }
                    if (cond) { cf->active = 1u; cf->taken = 1u; }
                }
                else if (kw == FPP_KW_ELIF)
                {
                    fast_pp_skip_to_eol(pp, f);
                }
                else
                {
                    FastTok nt = { 0 };
                    fast_pp_read_tok(pp, f, true, &nt);
                    if (nt.kind != FTOK_IDENT && !(nt.kind >= 64u && nt.kind < 126u)) { fast_pp_bail(pp, "bad #elifdef name"); break; }
                    fast_pp_finish_directive(pp, f);
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
                fast_pp_sync_file(f, f->base + w->origin + w->starts[w->pos]);
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
    // A bailout can leave mapped parents below a buffered include.
    while (pp->file_depth != 0) { fast_pp_pop_file(pp); }
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

// Runs the file at `path` to its end: the main file (`is_main`), or one before it (the predefines file, a system header's
// as the full pipeline reads it, or an -include file, which `is_system` is when the search found it in a system directory).
static bool fast_pp_run_file(FastPP* pp, const char* path, bool is_main, bool is_system, int fd, const struct stat* opened_stat)
{
    pp->base_file = path;
    bool ok = fast_pp_open_and_push(pp, path, (uint32_t)strlen(path), UINT32_MAX, is_system, fd, opened_stat);
    if (ok && is_main)
    {
        // The main file's push, and -MD's record of it (#3592), which a guard of an -include file may have skipped.
        pp->main_serial = pp->file_depth != 0 ? pp->files[pp->file_depth - 1u].serial : 0u;
        pp->dependency_main = pp->dependency_last;
    }
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
