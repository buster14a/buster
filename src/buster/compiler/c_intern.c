// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The hash's AES round keys are wyhash's public-domain constants (THIRD_PARTY_NOTICES.md).
//
// Identifier interning: every distinct identifier spelling gets one IdentId, a dense u32 handle, so that the stages
// after the lexer compare, hash and index identifiers by a number. docs/c-refinement.md has the design and its 9700X
// measurements; c_refine.c interns a file's identifier tokens in batches with this table, and the preprocessor
// (c_preprocessor.c) interns the identifiers that pasting makes.
//
// The table is open addressing over 64-byte buckets of four 16-byte slots. A slot holds the first 12 bytes of its
// spelling (zero past the spelling's end) and the spelling's IdentId, so that one cache line answers most lookups: one
// compare of the bucket against the key's first 16 bytes, repeated in each lane, finds the slots with the key's prefix,
// and a spelling of up to 11 bytes is identified by its prefix alone, since its zero-padded prefix has a zero at its
// length (identifiers have no NUL bytes). A longer spelling is confirmed against its record: the spelling and a NUL in
// the record arena, where one masked compare of the key's bytes and its terminating zero confirms both the bytes and the
// length. Buckets are probed in order from the hash's; spellings are never removed, so a key is absent once a probe meets
// a bucket with an empty slot. (docs/c-refinement.md has the measurements that chose this over a Swiss table of 1-byte
// tags, which needed a tag group, a slot and a record: three dependent loads.)
//
// IDs are opaque: they are given in the order spellings are first interned, and nothing may be emitted in their order,
// so that interning from several threads later can't change the compiler's output. Two exceptions are fixed: id 0 is
// none, and the first IDs are the names the stages after the lexer switch on (the keywords, the directive names and
// the preprocessor's built-in names: BUSTER_C_NAMES), so that classifying a keyword is one compare against
// C_NAME_KEYWORD_COUNT.
//
// The hash (intern_hash_bytes_scalar defines it) folds each 64-byte chunk of the spelling into 128 bits: the XOR of its
// four 16-byte lanes, lane k's two qwords rotated by intern_lane_rotations[k] so that lanes don't cancel each other.
// Chunks after the first are chained with an AES round (the state's round, XORed with the chunk's fold), so that equal
// or swapped chunks don't cancel either. Two more AES rounds mix the result and the length. intern_hash_bytes computes
// it 64 bytes at a time, and intern_hash_vector from a spelling of up to 64 bytes that one masked load read (zero past
// its end), as c_refine.c does: all three must agree on every spelling (test-refinement checks it).
//
// main.c includes this file after c_lexer.c (BUSTER_INLINE) and lane.c (Arena); every function is static. Layout, in
// this order:
//   - The predefined names: BUSTER_C_NAMES (keywords, then directive names, then the preprocessor's names), IdentId,
//     the C_NAME_* ids, CLanguage and c_keyword_kind (which keyword kinds a language mode has).
//   - The table: InternTable, the hash (intern_mix, intern_chain, intern_hash_bytes_scalar, intern_fold,
//     intern_hash_vector, intern_chain_chunks, intern_hash_bytes), the bucket helpers (intern_bucket_key, intern_bucket_matches, intern_bucket_empties,
//     intern_slot_id, intern_first_bytes), intern_allocate_buckets, intern_place, intern_grow, intern_insert,
//     intern_find_hashed, intern_find_or_add_hashed, intern_find_or_add, intern_find, intern_bytes, intern_length,
//     intern_initialize, intern_release and intern_reset.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------------------
// The predefined names

typedef uint32_t IdentId;

// When a keyword is one, by language mode (CLanguage): in every mode, from C99 (or in GNU modes), from C99, from C23,
// from C23 (or in GNU modes), or only in GNU modes. Clang's TokenKinds.def has the same rules for these names.
#define BUSTER_C_KEYWORD_ALL 0
#define BUSTER_C_KEYWORD_C99_OR_GNU 1
#define BUSTER_C_KEYWORD_C99 2
#define BUSTER_C_KEYWORD_C23 3
#define BUSTER_C_KEYWORD_C23_OR_GNU 4
#define BUSTER_C_KEYWORD_GNU 5
// Not a keyword: a directive name or one of the preprocessor's names.
#define BUSTER_C_KEYWORD_NONE 6

// The names that get the first IDs, in order: the keywords of C (with GNU's and clang's spellings), the directive names
// that aren't keywords (`if` and `else` are), and the names the preprocessor gives a meaning to. X(enumerator, spelling,
// keyword rule).
#define BUSTER_C_KEYWORDS(X) \
    X(AUTO, "auto", BUSTER_C_KEYWORD_ALL) \
    X(BREAK, "break", BUSTER_C_KEYWORD_ALL) \
    X(CASE, "case", BUSTER_C_KEYWORD_ALL) \
    X(CHAR, "char", BUSTER_C_KEYWORD_ALL) \
    X(CONST, "const", BUSTER_C_KEYWORD_ALL) \
    X(CONTINUE, "continue", BUSTER_C_KEYWORD_ALL) \
    X(DEFAULT, "default", BUSTER_C_KEYWORD_ALL) \
    X(DO, "do", BUSTER_C_KEYWORD_ALL) \
    X(DOUBLE, "double", BUSTER_C_KEYWORD_ALL) \
    X(ELSE, "else", BUSTER_C_KEYWORD_ALL) \
    X(ENUM, "enum", BUSTER_C_KEYWORD_ALL) \
    X(EXTERN, "extern", BUSTER_C_KEYWORD_ALL) \
    X(FLOAT, "float", BUSTER_C_KEYWORD_ALL) \
    X(FOR, "for", BUSTER_C_KEYWORD_ALL) \
    X(GOTO, "goto", BUSTER_C_KEYWORD_ALL) \
    X(IF, "if", BUSTER_C_KEYWORD_ALL) \
    X(INT, "int", BUSTER_C_KEYWORD_ALL) \
    X(LONG, "long", BUSTER_C_KEYWORD_ALL) \
    X(REGISTER, "register", BUSTER_C_KEYWORD_ALL) \
    X(RETURN, "return", BUSTER_C_KEYWORD_ALL) \
    X(SHORT, "short", BUSTER_C_KEYWORD_ALL) \
    X(SIGNED, "signed", BUSTER_C_KEYWORD_ALL) \
    X(SIZEOF, "sizeof", BUSTER_C_KEYWORD_ALL) \
    X(STATIC, "static", BUSTER_C_KEYWORD_ALL) \
    X(STRUCT, "struct", BUSTER_C_KEYWORD_ALL) \
    X(SWITCH, "switch", BUSTER_C_KEYWORD_ALL) \
    X(TYPEDEF, "typedef", BUSTER_C_KEYWORD_ALL) \
    X(UNION, "union", BUSTER_C_KEYWORD_ALL) \
    X(UNSIGNED, "unsigned", BUSTER_C_KEYWORD_ALL) \
    X(VOID, "void", BUSTER_C_KEYWORD_ALL) \
    X(VOLATILE, "volatile", BUSTER_C_KEYWORD_ALL) \
    X(WHILE, "while", BUSTER_C_KEYWORD_ALL) \
    X(INLINE, "inline", BUSTER_C_KEYWORD_C99_OR_GNU) \
    X(RESTRICT, "restrict", BUSTER_C_KEYWORD_C99) \
    X(ALIGNAS_C11, "_Alignas", BUSTER_C_KEYWORD_ALL) \
    X(ALIGNOF_C11, "_Alignof", BUSTER_C_KEYWORD_ALL) \
    X(ATOMIC, "_Atomic", BUSTER_C_KEYWORD_ALL) \
    X(BOOL_C99, "_Bool", BUSTER_C_KEYWORD_ALL) \
    X(COMPLEX_C99, "_Complex", BUSTER_C_KEYWORD_ALL) \
    X(GENERIC, "_Generic", BUSTER_C_KEYWORD_ALL) \
    X(IMAGINARY, "_Imaginary", BUSTER_C_KEYWORD_ALL) \
    X(NORETURN, "_Noreturn", BUSTER_C_KEYWORD_ALL) \
    X(STATIC_ASSERT_C11, "_Static_assert", BUSTER_C_KEYWORD_ALL) \
    X(THREAD_LOCAL_C11, "_Thread_local", BUSTER_C_KEYWORD_ALL) \
    X(BITINT, "_BitInt", BUSTER_C_KEYWORD_ALL) \
    X(DECIMAL32, "_Decimal32", BUSTER_C_KEYWORD_ALL) \
    X(DECIMAL64, "_Decimal64", BUSTER_C_KEYWORD_ALL) \
    X(DECIMAL128, "_Decimal128", BUSTER_C_KEYWORD_ALL) \
    X(ALIGNAS, "alignas", BUSTER_C_KEYWORD_C23) \
    X(ALIGNOF, "alignof", BUSTER_C_KEYWORD_C23) \
    X(BOOL, "bool", BUSTER_C_KEYWORD_C23) \
    X(CONSTEXPR, "constexpr", BUSTER_C_KEYWORD_C23) \
    X(FALSE, "false", BUSTER_C_KEYWORD_C23) \
    X(NULLPTR, "nullptr", BUSTER_C_KEYWORD_C23) \
    X(STATIC_ASSERT, "static_assert", BUSTER_C_KEYWORD_C23) \
    X(THREAD_LOCAL, "thread_local", BUSTER_C_KEYWORD_C23) \
    X(TRUE, "true", BUSTER_C_KEYWORD_C23) \
    X(TYPEOF_UNQUAL, "typeof_unqual", BUSTER_C_KEYWORD_C23) \
    X(TYPEOF, "typeof", BUSTER_C_KEYWORD_C23_OR_GNU) \
    X(ASM, "asm", BUSTER_C_KEYWORD_GNU) \
    X(FLOAT16, "_Float16", BUSTER_C_KEYWORD_ALL) \
    X(BF16, "__bf16", BUSTER_C_KEYWORD_ALL) \
    X(FLOAT128, "__float128", BUSTER_C_KEYWORD_ALL) \
    X(IBM128, "__ibm128", BUSTER_C_KEYWORD_ALL) \
    X(INT128, "__int128", BUSTER_C_KEYWORD_ALL) \
    X(FUNC, "__func__", BUSTER_C_KEYWORD_ALL) \
    X(FUNCTION, "__FUNCTION__", BUSTER_C_KEYWORD_ALL) \
    X(PRETTY_FUNCTION, "__PRETTY_FUNCTION__", BUSTER_C_KEYWORD_ALL) \
    X(GNU_ALIGNOF, "__alignof", BUSTER_C_KEYWORD_ALL) \
    X(GNU_ALIGNOF2, "__alignof__", BUSTER_C_KEYWORD_ALL) \
    X(GNU_ASM, "__asm", BUSTER_C_KEYWORD_ALL) \
    X(GNU_ASM2, "__asm__", BUSTER_C_KEYWORD_ALL) \
    X(GNU_ATTRIBUTE, "__attribute", BUSTER_C_KEYWORD_ALL) \
    X(GNU_ATTRIBUTE2, "__attribute__", BUSTER_C_KEYWORD_ALL) \
    X(AUTO_TYPE, "__auto_type", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_CHOOSE_EXPR, "__builtin_choose_expr", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_OFFSETOF, "__builtin_offsetof", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_TYPES_COMPATIBLE_P, "__builtin_types_compatible_p", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_VA_ARG, "__builtin_va_arg", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_CONVERTVECTOR, "__builtin_convertvector", BUSTER_C_KEYWORD_ALL) \
    X(GNU_COMPLEX, "__complex", BUSTER_C_KEYWORD_ALL) \
    X(GNU_COMPLEX2, "__complex__", BUSTER_C_KEYWORD_ALL) \
    X(GNU_CONST, "__const", BUSTER_C_KEYWORD_ALL) \
    X(GNU_CONST2, "__const__", BUSTER_C_KEYWORD_ALL) \
    X(EXTENSION, "__extension__", BUSTER_C_KEYWORD_ALL) \
    X(GNU_IMAG, "__imag", BUSTER_C_KEYWORD_ALL) \
    X(GNU_IMAG2, "__imag__", BUSTER_C_KEYWORD_ALL) \
    X(GNU_INLINE, "__inline", BUSTER_C_KEYWORD_ALL) \
    X(GNU_INLINE2, "__inline__", BUSTER_C_KEYWORD_ALL) \
    X(LABEL, "__label__", BUSTER_C_KEYWORD_ALL) \
    X(GNU_REAL, "__real", BUSTER_C_KEYWORD_ALL) \
    X(GNU_REAL2, "__real__", BUSTER_C_KEYWORD_ALL) \
    X(GNU_RESTRICT, "__restrict", BUSTER_C_KEYWORD_ALL) \
    X(GNU_RESTRICT2, "__restrict__", BUSTER_C_KEYWORD_ALL) \
    X(GNU_SIGNED, "__signed", BUSTER_C_KEYWORD_ALL) \
    X(GNU_SIGNED2, "__signed__", BUSTER_C_KEYWORD_ALL) \
    X(GNU_THREAD, "__thread", BUSTER_C_KEYWORD_ALL) \
    X(GNU_TYPEOF, "__typeof", BUSTER_C_KEYWORD_ALL) \
    X(GNU_TYPEOF2, "__typeof__", BUSTER_C_KEYWORD_ALL) \
    X(GNU_TYPEOF_UNQUAL, "__typeof_unqual", BUSTER_C_KEYWORD_ALL) \
    X(GNU_TYPEOF_UNQUAL2, "__typeof_unqual__", BUSTER_C_KEYWORD_ALL) \
    X(GNU_VOLATILE, "__volatile", BUSTER_C_KEYWORD_ALL) \
    X(GNU_VOLATILE2, "__volatile__", BUSTER_C_KEYWORD_ALL) \
    X(NONNULL, "_Nonnull", BUSTER_C_KEYWORD_ALL) \
    X(NULLABLE, "_Nullable", BUSTER_C_KEYWORD_ALL) \
    X(NULLABLE_RESULT, "_Nullable_result", BUSTER_C_KEYWORD_ALL) \
    X(NULL_UNSPECIFIED, "_Null_unspecified", BUSTER_C_KEYWORD_ALL) \
    X(PRIVATE_EXTERN, "__private_extern__", BUSTER_C_KEYWORD_ALL)

#define BUSTER_C_DIRECTIVE_NAMES(X) \
    X(DEFINE, "define", BUSTER_C_KEYWORD_NONE) \
    X(UNDEF, "undef", BUSTER_C_KEYWORD_NONE) \
    X(INCLUDE, "include", BUSTER_C_KEYWORD_NONE) \
    X(INCLUDE_NEXT, "include_next", BUSTER_C_KEYWORD_NONE) \
    X(IMPORT, "import", BUSTER_C_KEYWORD_NONE) \
    X(IFDEF, "ifdef", BUSTER_C_KEYWORD_NONE) \
    X(IFNDEF, "ifndef", BUSTER_C_KEYWORD_NONE) \
    X(ELIF, "elif", BUSTER_C_KEYWORD_NONE) \
    X(ELIFDEF, "elifdef", BUSTER_C_KEYWORD_NONE) \
    X(ELIFNDEF, "elifndef", BUSTER_C_KEYWORD_NONE) \
    X(ENDIF, "endif", BUSTER_C_KEYWORD_NONE) \
    X(LINE, "line", BUSTER_C_KEYWORD_NONE) \
    X(ERROR, "error", BUSTER_C_KEYWORD_NONE) \
    X(WARNING, "warning", BUSTER_C_KEYWORD_NONE) \
    X(PRAGMA, "pragma", BUSTER_C_KEYWORD_NONE) \
    X(IDENT, "ident", BUSTER_C_KEYWORD_NONE) \
    X(SCCS, "sccs", BUSTER_C_KEYWORD_NONE) \
    X(ASSERT, "assert", BUSTER_C_KEYWORD_NONE) \
    X(UNASSERT, "unassert", BUSTER_C_KEYWORD_NONE) \
    X(EMBED, "embed", BUSTER_C_KEYWORD_NONE)

#define BUSTER_C_PREPROCESSOR_NAMES(X) \
    X(DEFINED, "defined", BUSTER_C_KEYWORD_NONE) \
    X(VA_ARGS, "__VA_ARGS__", BUSTER_C_KEYWORD_NONE) \
    X(VA_OPT, "__VA_OPT__", BUSTER_C_KEYWORD_NONE) \
    X(PRAGMA_OPERATOR, "_Pragma", BUSTER_C_KEYWORD_NONE) \
    X(FILE_MACRO, "__FILE__", BUSTER_C_KEYWORD_NONE) \
    X(LINE_MACRO, "__LINE__", BUSTER_C_KEYWORD_NONE) \
    X(COUNTER, "__COUNTER__", BUSTER_C_KEYWORD_NONE) \
    X(DATE, "__DATE__", BUSTER_C_KEYWORD_NONE) \
    X(TIME, "__TIME__", BUSTER_C_KEYWORD_NONE) \
    X(TIMESTAMP, "__TIMESTAMP__", BUSTER_C_KEYWORD_NONE) \
    X(BASE_FILE, "__BASE_FILE__", BUSTER_C_KEYWORD_NONE) \
    X(FILE_NAME, "__FILE_NAME__", BUSTER_C_KEYWORD_NONE) \
    X(INCLUDE_LEVEL, "__INCLUDE_LEVEL__", BUSTER_C_KEYWORD_NONE) \
    X(HAS_INCLUDE, "__has_include", BUSTER_C_KEYWORD_NONE) \
    X(HAS_INCLUDE_NEXT, "__has_include_next", BUSTER_C_KEYWORD_NONE) \
    X(HAS_ATTRIBUTE, "__has_attribute", BUSTER_C_KEYWORD_NONE) \
    X(HAS_BUILTIN, "__has_builtin", BUSTER_C_KEYWORD_NONE) \
    X(HAS_FEATURE, "__has_feature", BUSTER_C_KEYWORD_NONE) \
    X(HAS_EXTENSION, "__has_extension", BUSTER_C_KEYWORD_NONE) \
    X(HAS_C_ATTRIBUTE, "__has_c_attribute", BUSTER_C_KEYWORD_NONE) \
    X(HAS_CPP_ATTRIBUTE, "__has_cpp_attribute", BUSTER_C_KEYWORD_NONE) \
    X(HAS_DECLSPEC_ATTRIBUTE, "__has_declspec_attribute", BUSTER_C_KEYWORD_NONE) \
    X(HAS_WARNING, "__has_warning", BUSTER_C_KEYWORD_NONE) \
    X(HAS_EMBED, "__has_embed", BUSTER_C_KEYWORD_NONE) \
    X(IS_IDENTIFIER, "__is_identifier", BUSTER_C_KEYWORD_NONE) \
    X(IS_TARGET_ARCH, "__is_target_arch", BUSTER_C_KEYWORD_NONE) \
    X(IS_TARGET_VENDOR, "__is_target_vendor", BUSTER_C_KEYWORD_NONE) \
    X(IS_TARGET_OS, "__is_target_os", BUSTER_C_KEYWORD_NONE) \
    X(IS_TARGET_ENVIRONMENT, "__is_target_environment", BUSTER_C_KEYWORD_NONE) \
    X(BUILDING_MODULE, "__building_module", BUSTER_C_KEYWORD_NONE) \
    X(ONCE, "once", BUSTER_C_KEYWORD_NONE) \
    X(GCC, "GCC", BUSTER_C_KEYWORD_NONE) \
    X(CLANG, "clang", BUSTER_C_KEYWORD_NONE) \
    X(STDC, "STDC", BUSTER_C_KEYWORD_NONE) \
    X(SYSTEM_HEADER, "system_header", BUSTER_C_KEYWORD_NONE) \
    X(PUSH_MACRO, "push_macro", BUSTER_C_KEYWORD_NONE) \
    X(POP_MACRO, "pop_macro", BUSTER_C_KEYWORD_NONE) \
    X(POISON, "poison", BUSTER_C_KEYWORD_NONE) \
    X(DEPENDENCY, "dependency", BUSTER_C_KEYWORD_NONE)

#define BUSTER_C_NAMES(X) BUSTER_C_KEYWORDS(X) BUSTER_C_DIRECTIVE_NAMES(X) BUSTER_C_PREPROCESSOR_NAMES(X)

// C_NAME_<enumerator> is the name's IdentId. Id 0 is none, so the first name is 1.
typedef enum CName
{
    C_NAME_NONE,
#define BUSTER_C_NAME_ENUM(enumerator, spelling, rule) C_NAME_##enumerator,
    BUSTER_C_NAMES(BUSTER_C_NAME_ENUM)
#undef BUSTER_C_NAME_ENUM
    C_NAME_COUNT,
} CName;

// The keywords are ids 1 to C_NAME_KEYWORD_COUNT.
#define C_NAME_KEYWORD_COUNT C_NAME_PRIVATE_EXTERN
_Static_assert(C_NAME_PRIVATE_EXTERN + 1 == C_NAME_DEFINE, "the keywords must come first");

static const char* const c_name_spellings[C_NAME_COUNT] =
{
    "",
#define BUSTER_C_NAME_SPELLING(enumerator, spelling, rule) spelling,
    BUSTER_C_NAMES(BUSTER_C_NAME_SPELLING)
#undef BUSTER_C_NAME_SPELLING
};

static const uint8_t c_name_keyword_rules[C_NAME_COUNT] =
{
    BUSTER_C_KEYWORD_NONE,
#define BUSTER_C_NAME_RULE(enumerator, spelling, rule) rule,
    BUSTER_C_NAMES(BUSTER_C_NAME_RULE)
#undef BUSTER_C_NAME_RULE
};

// A language mode: the C standard's year in full (1989, 1999, 2011, 2017 or 2023, so that later standards compare
// greater) and whether GNU extensions are on (gnu17 rather than c17), as clang's -std= gives them.
typedef struct CLanguage CLanguage;
struct CLanguage
{
    uint32_t standard;
    bool gnu;
};

// Is the predefined name `id` a keyword in `language`?
static __attribute__((unused)) bool c_name_is_keyword(IdentId id, CLanguage language)
{
    bool result = false;
    if (id != 0 && id <= C_NAME_KEYWORD_COUNT)
    {
        switch (c_name_keyword_rules[id])
        {
            case BUSTER_C_KEYWORD_ALL: result = true; break;
            case BUSTER_C_KEYWORD_C99_OR_GNU: result = language.standard >= 1999 || language.gnu; break;
            case BUSTER_C_KEYWORD_C99: result = language.standard >= 1999; break;
            case BUSTER_C_KEYWORD_C23: result = language.standard >= 2023; break;
            case BUSTER_C_KEYWORD_C23_OR_GNU: result = language.standard >= 2023 || language.gnu; break;
            case BUSTER_C_KEYWORD_GNU: result = language.gnu; break;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The table

// A bucket is a 64-byte line of four 16-byte slots. A slot holds the first BUSTER_INTERN_PREFIX_BYTES bytes of its
// spelling, zero past the spelling's end, then the spelling's IdentId (0 in an empty slot).
#define BUSTER_INTERN_BUCKET_SLOTS 4
#define BUSTER_INTERN_SLOT_BYTES 16
#define BUSTER_INTERN_BUCKET_BYTES (BUSTER_INTERN_BUCKET_SLOTS * BUSTER_INTERN_SLOT_BYTES)
#define BUSTER_INTERN_PREFIX_BYTES 12
// A spelling up to this long is identified by its slot's prefix alone: its zero-padded prefix has a zero at its length,
// where any other spelling with the same first bytes has a byte or ends at the same place.
#define BUSTER_INTERN_SHORT_LENGTH (BUSTER_INTERN_PREFIX_BYTES - 1)
// The table grows (doubles) before more than 5/8 of its slots would be full, so that a probe almost always ends in its
// first bucket.
#define BUSTER_INTERN_LOAD_NUMERATOR 5
#define BUSTER_INTERN_LOAD_DENOMINATOR 8
// 8,192 buckets: 32,768 slots in 512 KB, room for 20,480 spellings before the first growth. A translation unit that
// includes a libc header interns thousands; the buckets are address space, touched as they are used.
#define BUSTER_INTERN_INITIAL_BUCKETS 8192
// The record arena (offsets are u32) and the id columns, reserved up front and committed as they are written.
#define BUSTER_INTERN_RECORD_BYTES_MAX (1ull << 32)
#define BUSTER_INTERN_MAX_IDS (1u << 28)
// The longest spelling the vectorized lookup handles: its masked load and the compare of its terminating zero fit one
// 64-byte vector.
#define BUSTER_INTERN_VECTOR_BYTES 64
#define BUSTER_INTERN_FAST_LENGTH (BUSTER_INTERN_VECTOR_BYTES - 1)

typedef struct InternTable InternTable;
struct InternTable
{
    // (bucket_mask + 1) buckets of BUSTER_INTERN_BUCKET_BYTES, in a mapping of their own (`bucket_arena`).
    uint8_t* buckets;
    uint64_t bucket_mask;
    Arena bucket_arena;
    // The spellings, each followed by a NUL, from offset 0.
    uint8_t* records;
    Arena record_arena;
    // By IdentId: the offset of the spelling in `records`, and its length. Id 0's are zero.
    uint32_t* record_of;
    uint32_t* length_of;
    Arena column_arena;
    // Ids in use, counting id 0.
    uint32_t count;
};

// The AES round keys: wyhash's constants (wyhash is in the public domain).
#define BUSTER_INTERN_SECRET0 0xa0761d6478bd642full
#define BUSTER_INTERN_SECRET1 0xe7037ed1a0b428dbull
#define BUSTER_INTERN_SECRET2 0x8ebc6af09c88c6e3ull
#define BUSTER_INTERN_SECRET3 0x589965cc75374cc3ull

// The rotations of a 16-byte lane's qwords, by its index in its 64-byte chunk.
static const uint8_t intern_lane_rotations[4] = { 0, 17, 31, 47 };

// Two AES rounds mix the folded lanes and the length: two FP0/FP1 ops on Zen 5, where two 64x64-bit multiplications
// need the lanes in general registers first (docs/c-refinement.md). After two rounds every bit of the result depends
// on every bit of the input.
BUSTER_INLINE uint64_t intern_mix(__m128i folded, uint64_t length)
{
    const __m128i keyed = _mm_xor_si128(folded, _mm_cvtsi64_si128((long long)length));
    const __m128i first = _mm_aesenc_si128(keyed, _mm_set_epi64x((long long)BUSTER_INTERN_SECRET1, (long long)BUSTER_INTERN_SECRET0));
    return (uint64_t)_mm_cvtsi128_si64(_mm_aesenc_si128(first, _mm_set_epi64x((long long)BUSTER_INTERN_SECRET3, (long long)BUSTER_INTERN_SECRET2)));
}

// The state after a 64-byte chunk past the first, whose fold is `folded`: XORed into an AES round of the state (the first
// chunk's fold is the state). A plain XOR of the chunks' folds would lose two equal chunks (an identifier `X X` of two
// equal 64-byte halves would hash as its length alone) and couldn't tell `X Y` from `Y X`.
BUSTER_INLINE __m128i intern_chain(__m128i state, __m128i folded)
{
    return _mm_xor_si128(_mm_aesenc_si128(state, _mm_set_epi64x((long long)BUSTER_INTERN_SECRET1, (long long)BUSTER_INTERN_SECRET0)), folded);
}

// The hash of a spelling of any length, a 16-byte lane at a time: the plain definition, which test-refinement checks
// intern_hash_bytes and intern_hash_vector against.
static __attribute__((unused)) uint64_t intern_hash_bytes_scalar(const uint8_t* bytes, uint64_t length)
{
    __m128i state = _mm_setzero_si128();
    uint64_t x0 = 0;
    uint64_t x1 = 0;
    const uint64_t lanes = (length + 15) / 16;
    for (uint64_t lane = 0; lane < lanes; lane += 1)
    {
        uint8_t chunk[16] = { 0 };
        const uint64_t remaining = length - lane * 16;
        memcpy(chunk, bytes + lane * 16, remaining < 16 ? remaining : 16);
        uint64_t q0;
        uint64_t q1;
        memcpy(&q0, chunk, sizeof(q0));
        memcpy(&q1, chunk + 8, sizeof(q1));
        const uint8_t rotation = intern_lane_rotations[lane & 3];
        x0 ^= __builtin_rotateleft64(q0, rotation);
        x1 ^= __builtin_rotateleft64(q1, rotation);
        if ((lane & 3) == 3 || lane + 1 == lanes)
        {
            // The end of a 64-byte chunk.
            const __m128i folded = _mm_set_epi64x((long long)x1, (long long)x0);
            state = lane < 4 ? folded : intern_chain(state, folded);
            x0 = 0;
            x1 = 0;
        }
    }
    return intern_mix(state, length);
}

// The rotation of each qword of a 64-byte chunk: four 16-byte lanes, which take intern_lane_rotations in order.
#define BUSTER_INTERN_CHUNK_ROTATIONS _mm512_setr_epi64(0, 0, 17, 17, 31, 31, 47, 47)

// The fold of a 64-byte chunk whose four lanes are rotated already: their XOR, into the lowest lane with two 128-bit
// lane swaps.
BUSTER_INLINE __m128i intern_fold(__m512i lanes)
{
    __m512i r = _mm512_xor_si512(lanes, _mm512_shuffle_i64x2(lanes, lanes, _MM_SHUFFLE(1, 0, 3, 2)));
    r = _mm512_xor_si512(r, _mm512_shuffle_i64x2(r, r, _MM_SHUFFLE(2, 3, 0, 1)));
    return _mm512_castsi512_si128(r);
}

// The hash of a spelling of up to 64 bytes in `v`, zero past its length: one chunk, its four lanes rotated at once.
BUSTER_INLINE uint64_t intern_hash_vector(__m512i v, uint64_t length)
{
    return intern_mix(intern_fold(_mm512_rolv_epi64(v, BUSTER_INTERN_CHUNK_ROTATIONS)), length);
}

// The state after the chunks past a spelling's first 64 bytes, chained onto the first's (`state`): out of line, since
// nearly every spelling is one chunk.
static __attribute__((noinline)) __m128i intern_chain_chunks(const uint8_t* bytes, uint64_t length, __m128i state)
{
    for (uint64_t offset = BUSTER_INTERN_VECTOR_BYTES; offset < length; offset += BUSTER_INTERN_VECTOR_BYTES)
    {
        const uint64_t remaining = length - offset;
        const __mmask64 mask = remaining >= BUSTER_INTERN_VECTOR_BYTES ? ~0ull : _bzhi_u64(~0ull, (uint32_t)remaining);
        const __m512i chunk = _mm512_maskz_loadu_epi8(mask, bytes + offset);
        state = intern_chain(state, intern_fold(_mm512_rolv_epi64(chunk, BUSTER_INTERN_CHUNK_ROTATIONS)));
    }
    return state;
}

// The hash of a spelling of any length: its first 64 bytes (a masked load, which never reads past it) rotated and folded,
// then any later chunks chained.
static uint64_t intern_hash_bytes(const uint8_t* bytes, uint64_t length)
{
    const __mmask64 mask = length >= BUSTER_INTERN_VECTOR_BYTES ? ~0ull : _bzhi_u64(~0ull, (uint32_t)length);
    __m128i state = intern_fold(_mm512_rolv_epi64(_mm512_maskz_loadu_epi8(mask, bytes), BUSTER_INTERN_CHUNK_ROTATIONS));
    if (length > BUSTER_INTERN_VECTOR_BYTES)
    {
        state = intern_chain_chunks(bytes, length, state);
    }
    return intern_mix(state, length);
}

// A key for intern_bucket_matches: the spelling's first 16 bytes (zero past its end) in each 128-bit lane.
BUSTER_INLINE __m512i intern_bucket_key(__m128i first_bytes)
{
    return _mm512_broadcast_i32x4(first_bytes);
}

// The slots of a bucket whose prefix is the key's: bit 4s + 3 for slot s, which is the index of the dword that holds its
// id. Only the prefixes' dwords are compared, so each slot's nibble of the mask is 7 when the slot matches, which the
// add carries into its bit 3, and less otherwise.
BUSTER_INLINE uint32_t intern_bucket_matches(__m512i bucket, __m512i key)
{
    const uint32_t equal = _mm512_mask_cmpeq_epi32_mask(0x7777, bucket, key);
    return (equal + 0x1111) & 0x8888;
}

// The empty slots of a bucket: bit 4s + 3 for slot s (its id is 0).
BUSTER_INLINE uint32_t intern_bucket_empties(__m512i bucket)
{
    return _mm512_mask_cmpeq_epi32_mask(0x8888, bucket, _mm512_setzero_si512());
}

// The id in the slot that bit `bit` (4s + 3) names, in the bucket at `bucket`: its dword `bit`.
BUSTER_INLINE IdentId intern_slot_id(const uint8_t* bucket, uint32_t bit)
{
    IdentId result;
    memcpy(&result, bucket + bit * sizeof(IdentId), sizeof(result));
    return result;
}

// The spelling's first 16 bytes, zero past its end.
BUSTER_INLINE __m128i intern_first_bytes(const uint8_t* bytes, uint32_t length)
{
    return _mm_maskz_loadu_epi8((__mmask16)_bzhi_u32(0xFFFF, length < 16 ? length : 16), bytes);
}

// The buckets of a table of `buckets` buckets (a power of two), cleared: every slot empty.
static bool intern_allocate_buckets(InternTable* table, uint64_t buckets)
{
    Arena arena = arena_reserve(buckets * BUSTER_INTERN_BUCKET_BYTES + BUSTER_INTERN_BUCKET_BYTES);
    uint8_t* memory = arena_push(&arena, buckets * BUSTER_INTERN_BUCKET_BYTES, BUSTER_INTERN_BUCKET_BYTES);
    bool result = memory != 0;
    if (result)
    {
        // Fresh mappings read as zero, so every slot is empty already.
        table->buckets = memory;
        table->bucket_mask = buckets - 1;
        table->bucket_arena = arena;
    }
    else
    {
        arena_release(&arena);
    }
    return result;
}

// Puts spelling `id` (with `hash`, known to be absent) in the first empty slot of its probe sequence: its bucket, then
// the next ones.
static void intern_place(InternTable* table, IdentId id, uint64_t hash)
{
    const uint8_t* bytes = table->records + table->record_of[id];
    const uint32_t length = table->length_of[id];
    uint8_t slot[BUSTER_INTERN_SLOT_BYTES] = { 0 };
    memcpy(slot, bytes, length < BUSTER_INTERN_PREFIX_BYTES ? length : BUSTER_INTERN_PREFIX_BYTES);
    memcpy(slot + BUSTER_INTERN_PREFIX_BYTES, &id, sizeof(id));
    uint64_t bucket = hash & table->bucket_mask;
    uint32_t empties = 0;
    while (empties == 0)
    {
        uint8_t* bucket_bytes = table->buckets + bucket * BUSTER_INTERN_BUCKET_BYTES;
        empties = intern_bucket_empties(_mm512_load_si512(bucket_bytes));
        if (empties != 0)
        {
            // Bit 4s + 3 is slot s's id.
            memcpy(bucket_bytes + ((uint32_t)__builtin_ctz(empties) - 3) * 4, slot, sizeof(slot));
        }
        bucket = (bucket + 1) & table->bucket_mask;
    }
}

// Doubles the buckets and places every spelling again.
static bool intern_grow(InternTable* table)
{
    InternTable grown = *table;
    bool result = intern_allocate_buckets(&grown, (table->bucket_mask + 1) * 2);
    if (result)
    {
        for (IdentId id = 1; id < table->count; id += 1)
        {
            intern_place(&grown, id, intern_hash_bytes(table->records + table->record_of[id], table->length_of[id]));
        }
        arena_release(&table->bucket_arena);
        *table = grown;
    }
    return result;
}

// Adds a spelling (known to be absent) with `hash`: its record, its id and its slot. Returns 0 if the table is full.
static IdentId intern_insert(InternTable* table, const uint8_t* bytes, uint32_t length, uint64_t hash)
{
    IdentId result = 0;
    bool fits = table->count < BUSTER_INTERN_MAX_IDS;
    const uint64_t slot_count = (table->bucket_mask + 1) * BUSTER_INTERN_BUCKET_SLOTS;
    if (fits && (uint64_t)table->count * BUSTER_INTERN_LOAD_DENOMINATOR >= slot_count * BUSTER_INTERN_LOAD_NUMERATOR)
    {
        fits = intern_grow(table);
    }
    uint8_t* record = fits ? arena_push(&table->record_arena, (uint64_t)length + 1, 1) : 0;
    if (record != 0)
    {
        result = table->count;
        memcpy(record, bytes, length);
        record[length] = 0;
        table->record_of[result] = (uint32_t)(record - table->records);
        table->length_of[result] = length;
        table->count += 1;
        intern_place(table, result, hash);
    }
    return result;
}

// The id of a spelling with `hash`, or 0 if it is absent: each bucket's prefix matches in order (a short spelling is
// found by its prefix alone; a longer one is confirmed against its record), until a bucket with an empty slot.
static IdentId intern_find_hashed(const InternTable* table, const uint8_t* bytes, uint32_t length, uint64_t hash)
{
    const __m512i key = intern_bucket_key(intern_first_bytes(bytes, length));
    uint64_t bucket = hash & table->bucket_mask;
    IdentId result = 0;
    bool done = false;
    while (!done)
    {
        const uint8_t* bucket_bytes = table->buckets + bucket * BUSTER_INTERN_BUCKET_BYTES;
        const __m512i slots = _mm512_load_si512(bucket_bytes);
        for (uint32_t matches = intern_bucket_matches(slots, key); !done && matches != 0; matches &= matches - 1)
        {
            const IdentId id = intern_slot_id(bucket_bytes, (uint32_t)__builtin_ctz(matches));
            if (length <= BUSTER_INTERN_SHORT_LENGTH ||
                (table->length_of[id] == length && memcmp(table->records + table->record_of[id], bytes, length) == 0))
            {
                result = id;
                done = true;
            }
        }
        done |= intern_bucket_empties(slots) != 0;
        bucket = (bucket + 1) & table->bucket_mask;
    }
    return result;
}

// The id of a spelling with `hash` (intern_hash_bytes's), which is added if it is new. Returns 0 if a new spelling
// doesn't fit.
static IdentId intern_find_or_add_hashed(InternTable* table, const uint8_t* bytes, uint32_t length, uint64_t hash)
{
    IdentId result = intern_find_hashed(table, bytes, length, hash);
    if (result == 0)
    {
        result = intern_insert(table, bytes, length, hash);
    }
    return result;
}

// The id of a spelling, which is added if it is new. Returns 0 if a new spelling doesn't fit.
static IdentId intern_find_or_add(InternTable* table, const uint8_t* bytes, uint32_t length)
{
    return intern_find_or_add_hashed(table, bytes, length, intern_hash_bytes(bytes, length));
}

// The id of a spelling, or 0 if it was never interned.
static __attribute__((unused)) IdentId intern_find(const InternTable* table, const uint8_t* bytes, uint32_t length)
{
    return intern_find_hashed(table, bytes, length, intern_hash_bytes(bytes, length));
}

static __attribute__((unused)) const uint8_t* intern_bytes(const InternTable* table, IdentId id)
{
    return table->records + table->record_of[id];
}

static __attribute__((unused)) uint32_t intern_length(const InternTable* table, IdentId id)
{
    return table->length_of[id];
}

// Adds the predefined names, which get ids 1 to C_NAME_COUNT - 1 in order.
static bool intern_add_names(InternTable* table)
{
    bool result = true;
    for (uint32_t name = 1; result && name < C_NAME_COUNT; name += 1)
    {
        const char* spelling = c_name_spellings[name];
        result = intern_find_or_add(table, (const uint8_t*)spelling, (uint32_t)strlen(spelling)) == name;
    }
    return result;
}

// Reserves the table's memory and adds the predefined names. Returns false if the memory can't be reserved.
static __attribute__((unused)) bool intern_initialize(InternTable* table)
{
    *table = (InternTable){ 0 };
    table->record_arena = arena_reserve(BUSTER_INTERN_RECORD_BYTES_MAX);
    table->column_arena = arena_reserve(2 * (uint64_t)BUSTER_INTERN_MAX_IDS * sizeof(uint32_t) + 2 * BUSTER_INTERN_VECTOR_BYTES);
    table->records = table->record_arena.base;
    table->record_of = arena_push_array(&table->column_arena, BUSTER_INTERN_MAX_IDS, sizeof(uint32_t), BUSTER_INTERN_VECTOR_BYTES);
    table->length_of = arena_push_array(&table->column_arena, BUSTER_INTERN_MAX_IDS, sizeof(uint32_t), BUSTER_INTERN_VECTOR_BYTES);
    // Id 0's spelling: an empty one, so that no other spelling is at offset 0.
    uint8_t* none = table->records != 0 ? arena_push(&table->record_arena, 1, 1) : 0;
    bool result = none != 0 && table->record_of != 0 && table->length_of != 0 && intern_allocate_buckets(table, BUSTER_INTERN_INITIAL_BUCKETS);
    if (result)
    {
        none[0] = 0;
        table->count = 1;
        result = intern_add_names(table);
    }
    return result;
}

static __attribute__((unused)) void intern_release(InternTable* table)
{
    arena_release(&table->bucket_arena);
    arena_release(&table->record_arena);
    arena_release(&table->column_arena);
    *table = (InternTable){ 0 };
}

// Back to the predefined names only, keeping the memory (for benchmark passes and tests).
static __attribute__((unused)) bool intern_reset(InternTable* table)
{
    memset(table->buckets, 0, (table->bucket_mask + 1) * BUSTER_INTERN_BUCKET_BYTES);
    table->record_arena.length = 1;
    table->count = 1;
    return intern_add_names(table);
}
