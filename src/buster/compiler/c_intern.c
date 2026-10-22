// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The hash's constants (the lanes' whitening and the chain's round key) are wyhash's public-domain constants
// (THIRD_PARTY_NOTICES.md).
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
// tags, which needed a tag group, a slot and a record: three dependent loads.) Each id's span (its record's offset and
// length) is in the same mapping as the buckets, past them, and the first buckets and spans are at the start of the record
// arena, so that a table of up to 16,384 buckets, 40,960 ids and about 700 KB of records touches its first 64 KB, in
// base pages, and one huge page: the kernel clears each fresh huge page, 2 MB, on its first touch.
//
// IDs are opaque: they are given in the order spellings are first interned, and nothing may be emitted in their order,
// so that interning from several threads later can't change the compiler's output. Two exceptions are fixed: id 0 is
// none, and the first IDs are the names the stages after the lexer switch on (the keywords, the directive names and
// the preprocessor's built-in names: BUSTER_C_NAMES), so that classifying a keyword is one compare against
// C_NAME_KEYWORD_COUNT.
//
// The hash (intern_hash_bytes_scalar defines it) folds each 64-byte chunk of the spelling into 128 bits: the XOR of its
// four 16-byte lanes, each XORed with a whitening of its own and put through two AES rounds (the length goes into the
// first lane). The whitening keeps equal lanes from cancelling in the fold, and the rounds keep changes in two lanes
// from cancelling: a fold of the lanes as they are is linear, so flipping a bit in one lane and the same bit in another
// would leave it as it was, and after one round a flipped bit's change still depends on its byte alone. Chunks after
// the first are chained with an AES round (the state's round, XORed with the chunk's fold), so that equal or swapped
// chunks don't cancel either. The hash is the low qword of the state. intern_hash_bytes computes
// it 64 bytes at a time, and intern_hash_vector from a spelling of up to 64 bytes that one masked load read (zero past
// its end), as c_refine.c does: all three must agree on every spelling (test-refinement checks it).
//
// main.c includes this file after c_lexer.c (BUSTER_INLINE) and lane.c (Arena); every function is static. Layout, in
// this order:
//   - The predefined names: BUSTER_C_NAMES (keywords, then directive names, then the preprocessor's names), IdentId,
//     the C_NAME_* ids, CGnuKeywords, CLanguage, c_language_gnu_keywords and c_name_is_keyword (which names are
//     keywords in a language mode).
//   - The table: InternSpan, InternTable, the hash (intern_lane_rounds, intern_chain, intern_hash_bytes_scalar,
//     intern_fold, intern_with_length, intern_hash_vector, intern_hash_zero_lanes, intern_hash_vectors4_short,
//     intern_chain_chunks, intern_hash_bytes), the bucket helpers
//     (intern_bucket_key, intern_bucket_matches, intern_bucket_empties, intern_slot_id, intern_first_bytes),
//     intern_layout_bytes, intern_lay_out, intern_place, intern_slot, intern_grow, intern_reserve_record, intern_insert, intern_find_hashed,
//     intern_find_or_add_hashed, intern_find_or_add, intern_find_or_add_vector, intern_find, intern_bytes,
//     intern_length, intern_add_names, intern_initialize_with, intern_initialize, intern_release and intern_reset.
//   - Spellings added from several lanes: InternBulk, intern_place_shared, intern_bulk_begin, intern_bulk_replace,
//     intern_bulk_add and intern_bulk_end.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------------------
// The predefined names

typedef uint32_t IdentId;

// When a keyword is one, by language mode (CLanguage): in every mode, from C99 (or with GNU's keywords), from C99, from
// C23, from C23 (or with GNU's keywords), or only with GNU's keywords, which GNU modes have (c_language_gnu_keywords).
// Clang's TokenKinds.def has the same rules for these names (KEYGNU is GNU's keywords).
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
//
// The keywords are clang 23's for C: every name in its TokenKinds.def and RegularKeywordAttrInfo.inc that its
// __is_identifier answers 0 for in some mode from c89 to gnu23, with the modes it does (148, in six rules). The first
// 108 are the ones the C sources use; the 40 after `__private_extern__` are clang's others (calling conventions,
// builtins that are keywords, Objective-C and ARM keyword attributes, `_ExtInt`, `_Countof`), which are keywords in C
// all the same. docs/c-refinement.md has the procedure.
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
    X(PRIVATE_EXTERN, "__private_extern__", BUSTER_C_KEYWORD_ALL) \
    X(COUNTOF, "_Countof", BUSTER_C_KEYWORD_ALL) \
    X(EXTINT, "_ExtInt", BUSTER_C_KEYWORD_ALL) \
    X(ARM_AGNOSTIC, "__arm_agnostic", BUSTER_C_KEYWORD_ALL) \
    X(ARM_IN, "__arm_in", BUSTER_C_KEYWORD_ALL) \
    X(ARM_INOUT, "__arm_inout", BUSTER_C_KEYWORD_ALL) \
    X(ARM_LOCALLY_STREAMING, "__arm_locally_streaming", BUSTER_C_KEYWORD_ALL) \
    X(ARM_NEW, "__arm_new", BUSTER_C_KEYWORD_ALL) \
    X(ARM_OUT, "__arm_out", BUSTER_C_KEYWORD_ALL) \
    X(ARM_PRESERVES, "__arm_preserves", BUSTER_C_KEYWORD_ALL) \
    X(ARM_STREAMING, "__arm_streaming", BUSTER_C_KEYWORD_ALL) \
    X(ARM_STREAMING_COMPATIBLE, "__arm_streaming_compatible", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_COLUMN, "__builtin_COLUMN", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_FILE, "__builtin_FILE", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_FILE_NAME, "__builtin_FILE_NAME", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_FUNCTION, "__builtin_FUNCTION", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_LINE, "__builtin_LINE", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_AVAILABLE, "__builtin_available", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_BIT_CAST, "__builtin_bit_cast", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_OMP_REQUIRED_SIMD_ALIGN, "__builtin_omp_required_simd_align", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_PTRAUTH_TYPE_DISCRIMINATOR, "__builtin_ptrauth_type_discriminator", BUSTER_C_KEYWORD_ALL) \
    X(BUILTIN_VECTORELEMENTS, "__builtin_vectorelements", BUSTER_C_KEYWORD_ALL) \
    X(CDECL, "__cdecl", BUSTER_C_KEYWORD_ALL) \
    X(FASTCALL, "__fastcall", BUSTER_C_KEYWORD_ALL) \
    X(FP16, "__fp16", BUSTER_C_KEYWORD_ALL) \
    X(FUNCREF, "__funcref", BUSTER_C_KEYWORD_ALL) \
    X(IS_BITWISE_CLONEABLE, "__is_bitwise_cloneable", BUSTER_C_KEYWORD_ALL) \
    X(IS_DESTRUCTIBLE, "__is_destructible", BUSTER_C_KEYWORD_ALL) \
    X(IS_NOTHROW_DESTRUCTIBLE, "__is_nothrow_destructible", BUSTER_C_KEYWORD_ALL) \
    X(MODULE_PRIVATE, "__module_private__", BUSTER_C_KEYWORD_ALL) \
    X(GNU_NULLPTR, "__nullptr", BUSTER_C_KEYWORD_ALL) \
    X(OB_TRAP, "__ob_trap", BUSTER_C_KEYWORD_ALL) \
    X(OB_WRAP, "__ob_wrap", BUSTER_C_KEYWORD_ALL) \
    X(OBJC_NO, "__objc_no", BUSTER_C_KEYWORD_ALL) \
    X(OBJC_YES, "__objc_yes", BUSTER_C_KEYWORD_ALL) \
    X(PASCAL, "__pascal", BUSTER_C_KEYWORD_ALL) \
    X(PTRAUTH, "__ptrauth", BUSTER_C_KEYWORD_ALL) \
    X(REGCALL, "__regcall", BUSTER_C_KEYWORD_ALL) \
    X(STDCALL, "__stdcall", BUSTER_C_KEYWORD_ALL) \
    X(THISCALL, "__thiscall", BUSTER_C_KEYWORD_ALL) \
    X(VECTORCALL, "__vectorcall", BUSTER_C_KEYWORD_ALL)

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
#define C_NAME_KEYWORD_COUNT C_NAME_VECTORCALL
_Static_assert(C_NAME_VECTORCALL + 1 == C_NAME_DEFINE, "the keywords must come first");

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

// Whether a language has GNU's keywords (asm, typeof, and inline before C99), clang's GNUKeywords: as its mode has them
// (GNU modes do), unless -fgnu-keywords or -fno-gnu-keywords (or GCC's -fasm and -fno-asm) set them.
typedef enum CGnuKeywords
{
    C_GNU_KEYWORDS_MODE,
    C_GNU_KEYWORDS_ON,
    C_GNU_KEYWORDS_OFF,
} CGnuKeywords;

// A language mode: the C standard's year in full (1989, 1999, 2011, 2017 or 2023, so that later standards compare
// greater) and whether GNU extensions are on (gnu17 rather than c17), as clang's -std= gives them, and its GNU keywords
// (a CGnuKeywords, so that a mode written without them has the mode's).
typedef struct CLanguage CLanguage;
struct CLanguage
{
    uint32_t standard;
    bool gnu;
    uint8_t gnu_keywords;
};

// Does `language` have GNU's keywords?
static __attribute__((unused)) bool c_language_gnu_keywords(CLanguage language)
{
    return language.gnu_keywords == C_GNU_KEYWORDS_ON || (language.gnu_keywords == C_GNU_KEYWORDS_MODE && language.gnu);
}

// Is the predefined name `id` a keyword in `language`?
static __attribute__((unused)) bool c_name_is_keyword(IdentId id, CLanguage language)
{
    bool result = false;
    const bool gnu = c_language_gnu_keywords(language);
    if (id != 0 && id <= C_NAME_KEYWORD_COUNT)
    {
        switch (c_name_keyword_rules[id])
        {
            case BUSTER_C_KEYWORD_ALL: result = true; break;
            case BUSTER_C_KEYWORD_C99_OR_GNU: result = language.standard >= 1999 || gnu; break;
            case BUSTER_C_KEYWORD_C99: result = language.standard >= 1999; break;
            case BUSTER_C_KEYWORD_C23: result = language.standard >= 2023; break;
            case BUSTER_C_KEYWORD_C23_OR_GNU: result = language.standard >= 2023 || gnu; break;
            case BUSTER_C_KEYWORD_GNU: result = gnu; break;
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
// 16,384 buckets: 65,536 slots in 1 MB, room for 40,960 spellings before the first growth. sqlite3.c and its headers
// intern 24,000, which outgrew 8,192 buckets: the growth placed every spelling again and cleared a new huge page, and the
// load before it deferred a fifth more identifiers. 1 MB fits the same huge page (past the arena's first 64 KB) as
// 512 KB, so it costs no more to clear.
#define BUSTER_INTERN_INITIAL_BUCKETS 16384
// The record arena (offsets are u32), reserved up front and committed as it is written, and the most ids.
#define BUSTER_INTERN_RECORD_BYTES_MAX (1ull << 32)
#define BUSTER_INTERN_MAX_IDS (1u << 28)
// The longest spelling the vectorized lookup handles: its masked load and the compare of its terminating zero fit one
// 64-byte vector.
#define BUSTER_INTERN_VECTOR_BYTES 64
#define BUSTER_INTERN_FAST_LENGTH (BUSTER_INTERN_VECTOR_BYTES - 1)

// Where a spelling's record is: its offset in the record arena, and its length (the NUL after it isn't counted).
typedef struct InternSpan InternSpan;
struct InternSpan
{
    uint32_t offset;
    uint32_t length;
};

typedef struct InternTable InternTable;
struct InternTable
{
    // (bucket_mask + 1) buckets of BUSTER_INTERN_BUCKET_BYTES, then the spans of up to `capacity` ids (by IdentId; id
    // 0's is zero), in one mapping, so that they share its pages. The table grows both at once (intern_grow), when
    // `count` reaches `capacity`, the load of 5/8 of the slots. The first ones are at the start of `record_arena`, before
    // the records, so that with the first records they take its first 64 KB, in base pages, and one huge page; after a
    // growth they have a mapping of their own, `arena`.
    uint8_t* buckets;
    uint64_t bucket_mask;
    InternSpan* spans;
    uint32_t capacity;
    Arena arena;
    // The spellings, each followed by a NUL, from offset 0, which is past the first buckets and spans in
    // `record_arena`. Records never move, so a pointer to one stays valid as the table grows.
    uint8_t* records;
    Arena record_arena;
    // Ids in use, counting id 0.
    uint32_t count;
};

// The hash's constants: wyhash's (wyhash is in the public domain).
#define BUSTER_INTERN_SECRET0 0xa0761d6478bd642full
#define BUSTER_INTERN_SECRET1 0xe7037ed1a0b428dbull
#define BUSTER_INTERN_SECRET2 0x8ebc6af09c88c6e3ull
#define BUSTER_INTERN_SECRET3 0x589965cc75374cc3ull

// The whitening of a 64-byte chunk's four 16-byte lanes before their AES rounds, a different pair of wyhash's constants
// per lane (lane k's qwords are 2k and 2k + 1), so that equal lanes don't give equal results, which the fold would
// cancel.
static const uint64_t intern_lane_whitening[8] = {
    BUSTER_INTERN_SECRET0, BUSTER_INTERN_SECRET1, BUSTER_INTERN_SECRET2, BUSTER_INTERN_SECRET3,
    BUSTER_INTERN_SECRET1, BUSTER_INTERN_SECRET2, BUSTER_INTERN_SECRET3, BUSTER_INTERN_SECRET0,
};

// One lane's part of a chunk's fold: two AES rounds of the lane XORed with its whitening (`lane` 0 to 3). Two rounds,
// because after one, the change a flipped bit makes depends on its byte alone, so a bit flipped in two lanes over equal
// bytes would change both alike and cancel; after two, every bit of the result depends on every bit of the lane.
BUSTER_INLINE __m128i intern_lane_rounds(__m128i bytes, uint32_t lane)
{
    const __m128i whitened = _mm_xor_si128(bytes, _mm_set_epi64x((long long)intern_lane_whitening[2 * lane + 1], (long long)intern_lane_whitening[2 * lane]));
    return _mm_aesenc_si128(_mm_aesenc_si128(whitened, _mm_setzero_si128()), _mm_setzero_si128());
}

// The state after a 64-byte chunk past the first, whose fold is `folded`: XORed into an AES round of the state (the first
// chunk's fold is the state). A plain XOR of the chunks' folds would lose two equal chunks (an identifier `X X` of two
// equal 64-byte halves would hash as its length alone) and couldn't tell `X Y` from `Y X`.
BUSTER_INLINE __m128i intern_chain(__m128i state, __m128i folded)
{
    return _mm_xor_si128(_mm_aesenc_si128(state, _mm_set_epi64x((long long)BUSTER_INTERN_SECRET1, (long long)BUSTER_INTERN_SECRET0)), folded);
}

// The hash of a spelling of any length, a 16-byte lane at a time: the plain definition, which test-refinement checks
// intern_hash_bytes and intern_hash_vector against. Each chunk has four lanes, zero past the spelling's end (a spelling
// of no bytes is one chunk of zeros, as the masked loads give them), and the length is XORed into the first lane's low
// qword. The hash is the low qword of the state.
static __attribute__((unused)) uint64_t intern_hash_bytes_scalar(const uint8_t* bytes, uint64_t length)
{
    __m128i state = _mm_setzero_si128();
    const uint64_t chunks = length == 0 ? 1 : (length + BUSTER_INTERN_VECTOR_BYTES - 1) / BUSTER_INTERN_VECTOR_BYTES;
    for (uint64_t chunk = 0; chunk < chunks; chunk += 1)
    {
        __m128i folded = _mm_setzero_si128();
        for (uint32_t lane = 0; lane < 4; lane += 1)
        {
            uint8_t lane_bytes[16] = { 0 };
            const uint64_t at = chunk * BUSTER_INTERN_VECTOR_BYTES + lane * 16;
            if (at < length)
            {
                memcpy(lane_bytes, bytes + at, length - at < 16 ? length - at : 16);
            }
            uint64_t q0;
            uint64_t q1;
            memcpy(&q0, lane_bytes, sizeof(q0));
            memcpy(&q1, lane_bytes + 8, sizeof(q1));
            q0 ^= chunk == 0 && lane == 0 ? length : 0;
            folded = _mm_xor_si128(folded, intern_lane_rounds(_mm_set_epi64x((long long)q1, (long long)q0), lane));
        }
        state = chunk == 0 ? folded : intern_chain(state, folded);
    }
    return (uint64_t)_mm_cvtsi128_si64(state);
}

// The fold of a 64-byte chunk: its four lanes whitened and through two AES rounds at once (a vpxorq and two vaesenc of
// the whole vector), then XORed into the lowest lane with two 128-bit lane swaps.
BUSTER_INLINE __m128i intern_fold(__m512i chunk)
{
    const __m512i whitened = _mm512_xor_si512(chunk, _mm512_loadu_si512(intern_lane_whitening));
    const __m512i lanes = _mm512_aesenc_epi128(_mm512_aesenc_epi128(whitened, _mm512_setzero_si512()), _mm512_setzero_si512());
    __m512i r = _mm512_xor_si512(lanes, _mm512_shuffle_i64x2(lanes, lanes, _MM_SHUFFLE(1, 0, 3, 2)));
    r = _mm512_xor_si512(r, _mm512_shuffle_i64x2(r, r, _MM_SHUFFLE(2, 3, 0, 1)));
    return _mm512_castsi512_si128(r);
}

// A spelling's first chunk with its length XORed into the first lane's low qword.
BUSTER_INLINE __m512i intern_with_length(__m512i chunk, uint64_t length)
{
    return _mm512_xor_si512(chunk, _mm512_zextsi128_si512(_mm_cvtsi64_si128((long long)length)));
}

// The hash of a spelling of up to 64 bytes in `v`, zero past its length: one chunk.
BUSTER_INLINE uint64_t intern_hash_vector(__m512i v, uint64_t length)
{
    return (uint64_t)_mm_cvtsi128_si64(intern_fold(intern_with_length(v, length)));
}

// What a spelling's lanes 2 and 3 add to its chunk's fold when they are zero (it has at most 32 bytes): their rounds of
// their whitening alone, in each 128-bit lane. intern_hash_vectors4_short adds it in place of the two lanes.
BUSTER_INLINE __m512i intern_hash_zero_lanes(void)
{
    return _mm512_broadcast_i32x4(_mm_xor_si128(intern_lane_rounds(_mm_setzero_si128(), 2), intern_lane_rounds(_mm_setzero_si128(), 3)));
}

// The hashes of four spellings of up to 32 bytes each, at once: the 32 bytes of the first two in `ab` (the first's in the
// low half) and of the other two in `cd`, each zero past its length; hash i in qword 2i, intern_hash_vector's of each.
// `lengths` has spelling i's length in dword 4i and zeros elsewhere. The spellings' two 16-byte lanes are transposed (two
// lane shuffles), so that one vector holds the four first lanes and another the four second ones: each is whitened with
// its lane's constants (the first with the lengths too) and goes through its two AES rounds, and the four folds are one
// three-way XOR, with `zero_lanes` (intern_hash_zero_lanes) for the zero lanes 2 and 3. That replaces a fold's four
// shuffles and XORs per spelling, and half the rounds. `*firsts` gets the first lanes: spelling i's first 16 bytes in
// lane i.
BUSTER_INLINE __m512i intern_hash_vectors4_short(__m512i ab, __m512i cd, __m512i lengths, __m512i zero_lanes, __m512i* firsts)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i first_lanes = _mm512_shuffle_i64x2(ab, cd, _MM_SHUFFLE(2, 0, 2, 0));
    const __m512i second_lanes = _mm512_shuffle_i64x2(ab, cd, _MM_SHUFFLE(3, 1, 3, 1));
    const __m512i whitening0 = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i*)intern_lane_whitening));
    const __m512i whitening1 = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i*)(intern_lane_whitening + 2)));
    *firsts = first_lanes;
    const __m512i lane0 = _mm512_aesenc_epi128(_mm512_aesenc_epi128(_mm512_ternarylogic_epi64(first_lanes, lengths, whitening0, 0x96), zero), zero);
    const __m512i lane1 = _mm512_aesenc_epi128(_mm512_aesenc_epi128(_mm512_xor_si512(second_lanes, whitening1), zero), zero);
    return _mm512_ternarylogic_epi64(lane0, lane1, zero_lanes, 0x96);
}

// The state after the chunks past a spelling's first 64 bytes, chained onto the first's (`state`): out of line, since
// nearly every spelling is one chunk.
static __attribute__((noinline)) __m128i intern_chain_chunks(const uint8_t* bytes, uint64_t length, __m128i state)
{
    for (uint64_t offset = BUSTER_INTERN_VECTOR_BYTES; offset < length; offset += BUSTER_INTERN_VECTOR_BYTES)
    {
        const uint64_t remaining = length - offset;
        const __mmask64 mask = remaining >= BUSTER_INTERN_VECTOR_BYTES ? ~0ull : _bzhi_u64(~0ull, (uint32_t)remaining);
        state = intern_chain(state, intern_fold(_mm512_maskz_loadu_epi8(mask, bytes + offset)));
    }
    return state;
}

// The hash of a spelling of any length: its first 64 bytes (a masked load, which never reads past it) folded, then any
// later chunks chained.
static uint64_t intern_hash_bytes(const uint8_t* bytes, uint64_t length)
{
    const __mmask64 mask = length >= BUSTER_INTERN_VECTOR_BYTES ? ~0ull : _bzhi_u64(~0ull, (uint32_t)length);
    __m128i state = intern_fold(intern_with_length(_mm512_maskz_loadu_epi8(mask, bytes), length));
    if (length > BUSTER_INTERN_VECTOR_BYTES)
    {
        state = intern_chain_chunks(bytes, length, state);
    }
    return (uint64_t)_mm_cvtsi128_si64(state);
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

// The bytes of the buckets and spans of a table of `buckets` buckets, from the start of a mapping (so aligned), and the
// ids it has room for before it grows. The records follow them in the record arena: exactly
// BUSTER_INTERN_RECORD_BYTES_MAX of room, so that their offsets fit 32 bits.
static uint64_t intern_layout_bytes(uint64_t buckets, uint32_t* capacity)
{
    const uint64_t load = buckets * BUSTER_INTERN_BUCKET_SLOTS * BUSTER_INTERN_LOAD_NUMERATOR / BUSTER_INTERN_LOAD_DENOMINATOR;
    *capacity = load < BUSTER_INTERN_MAX_IDS ? (uint32_t)load : BUSTER_INTERN_MAX_IDS;
    return buckets * BUSTER_INTERN_BUCKET_BYTES + (uint64_t)*capacity * sizeof(InternSpan);
}

// The buckets of a table of `buckets` buckets (a power of two), cleared (every slot empty), and room for the spans of the
// ids it holds before it grows, from `arena`, whose memory past its length must be fresh (zero). The spans are left for
// the caller to fill. Returns false if they don't fit.
static bool intern_lay_out(InternTable* table, Arena* arena, uint64_t buckets)
{
    uint32_t capacity;
    intern_layout_bytes(buckets, &capacity);
    uint8_t* memory = arena_push(arena, buckets * BUSTER_INTERN_BUCKET_BYTES, BUSTER_INTERN_BUCKET_BYTES);
    InternSpan* spans = arena_push_array(arena, capacity, sizeof(InternSpan), BUSTER_INTERN_BUCKET_BYTES);
    bool result = memory != 0 && spans != 0;
    if (result)
    {
        // Fresh mappings read as zero, so every slot is empty already.
        table->buckets = memory;
        table->bucket_mask = buckets - 1;
        table->spans = spans;
        table->capacity = capacity;
    }
    return result;
}

// Puts `slot` (a spelling's prefix and id; the spelling has `hash` and is known to be absent) in the first empty slot of
// its probe sequence: its bucket, then the next ones.
static void intern_place(InternTable* table, __m128i slot, uint64_t hash)
{
    uint64_t bucket = hash & table->bucket_mask;
    uint32_t empties = 0;
    while (empties == 0)
    {
        uint8_t* bucket_bytes = table->buckets + bucket * BUSTER_INTERN_BUCKET_BYTES;
        empties = intern_bucket_empties(_mm512_load_si512(bucket_bytes));
        if (empties != 0)
        {
            // Bit 4s + 3 is slot s's id.
            _mm_storeu_si128((__m128i*)(bucket_bytes + ((uint32_t)__builtin_ctz(empties) - 3) * 4), slot);
        }
        bucket = (bucket + 1) & table->bucket_mask;
    }
}

// The slot of spelling `id` from its first 16 bytes (`first`, zero past its end): the first BUSTER_INTERN_PREFIX_BYTES,
// then the id in place of the rest.
BUSTER_INLINE __m128i intern_slot(__m128i first, IdentId id)
{
    return _mm_insert_epi32(first, (int)id, BUSTER_INTERN_PREFIX_BYTES / sizeof(IdentId));
}

// Doubles the buckets (and the spans' room) and places every spelling again.
static bool intern_grow(InternTable* table)
{
    const uint64_t buckets = (table->bucket_mask + 1) * 2;
    uint32_t capacity;
    InternTable grown = *table;
    grown.arena = arena_reserve(intern_layout_bytes(buckets, &capacity));
    bool result = intern_lay_out(&grown, &grown.arena, buckets);
    if (!result)
    {
        arena_release(&grown.arena);
    }
    else
    {
        memcpy(grown.spans, table->spans, (uint64_t)table->count * sizeof(InternSpan));
        for (IdentId id = 1; id < table->count; id += 1)
        {
            const uint8_t* bytes = table->records + table->spans[id].offset;
            const uint32_t length = table->spans[id].length;
            intern_place(&grown, intern_slot(intern_first_bytes(bytes, length), id), intern_hash_bytes(bytes, length));
        }
        // The first buckets are in the record arena, which keeps them (`arena` has no capacity then).
        arena_release(&table->arena);
        *table = grown;
    }
    return result;
}

// Room for one more spelling of `length` bytes: an id (the table grows first if it is at its load) and its record, or 0
// if the table is full.
BUSTER_INLINE uint8_t* intern_reserve_record(InternTable* table, uint32_t length)
{
    bool fits = table->count < BUSTER_INTERN_MAX_IDS;
    if (fits && table->count >= table->capacity)
    {
        fits = intern_grow(table);
    }
    return fits ? arena_push(&table->record_arena, (uint64_t)length + 1, 1) : 0;
}

// Adds a spelling (known to be absent) with `hash`: its record, its id and its slot. Returns 0 if the table is full.
static IdentId intern_insert(InternTable* table, const uint8_t* bytes, uint32_t length, uint64_t hash)
{
    IdentId result = 0;
    uint8_t* record = intern_reserve_record(table, length);
    if (record != 0)
    {
        result = table->count;
        memcpy(record, bytes, length);
        record[length] = 0;
        table->spans[result] = (InternSpan){ .offset = (uint32_t)(record - table->records), .length = length };
        table->count += 1;
        intern_place(table, intern_slot(intern_first_bytes(bytes, length), result), hash);
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
                (table->spans[id].length == length && memcmp(table->records + table->spans[id].offset, bytes, length) == 0))
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

// intern_find_or_add for a spelling of 1 to BUSTER_INTERN_FAST_LENGTH bytes, from one masked load of it: its key, the
// compare of a longer spelling's bytes and terminating zero with a candidate's record, its record's bytes and NUL, and
// its slot, all from that vector (zero past the spelling). `home` is its hash's bucket in a table of `bucket_mask` (the
// caller hashed it); if the table has grown since, the function hashes it again. It finds and adds exactly what
// intern_find_or_add does; the vectorized refinement interns the identifiers it defers with it.
static IdentId intern_find_or_add_vector(InternTable* table, const uint8_t* bytes, uint32_t length, uint64_t bucket_mask, uint64_t home)
{
    const __m512i spelling = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, length), bytes);
    const __m512i key = intern_bucket_key(_mm512_castsi512_si128(spelling));
    const __mmask64 compare_mask = _bzhi_u64(~0ull, length + 1);
    if (table->bucket_mask != bucket_mask)
    {
        home = intern_hash_vector(spelling, length) & table->bucket_mask;
    }
    uint64_t bucket = home;
    IdentId result = 0;
    bool done = false;
    while (!done)
    {
        const uint8_t* bucket_bytes = table->buckets + bucket * BUSTER_INTERN_BUCKET_BYTES;
        const __m512i slots = _mm512_load_si512(bucket_bytes);
        for (uint32_t matches = intern_bucket_matches(slots, key); !done && matches != 0; matches &= matches - 1)
        {
            const IdentId id = intern_slot_id(bucket_bytes, (uint32_t)__builtin_ctz(matches));
            bool same = length <= BUSTER_INTERN_SHORT_LENGTH;
            if (!same)
            {
                const __m512i record = _mm512_maskz_loadu_epi8(compare_mask, table->records + table->spans[id].offset);
                same = _mm512_mask_cmpneq_epi8_mask(compare_mask, spelling, record) == 0;
            }
            if (same)
            {
                result = id;
                done = true;
            }
        }
        done |= intern_bucket_empties(slots) != 0;
        bucket = (bucket + 1) & table->bucket_mask;
    }
    const uint64_t probed_mask = table->bucket_mask;
    uint8_t* record = result == 0 ? intern_reserve_record(table, length) : 0;
    if (record != 0)
    {
        result = table->count;
        // The spelling and its NUL: the vector is zero past the spelling.
        _mm512_mask_storeu_epi8(record, compare_mask, spelling);
        table->spans[result] = (InternSpan){ .offset = (uint32_t)(record - table->records), .length = length };
        table->count += 1;
        // intern_place takes the bucket's index as the hash, but in a table that has just grown.
        const uint64_t place = table->bucket_mask == probed_mask ? home : intern_hash_vector(spelling, length);
        intern_place(table, intern_slot(_mm512_castsi512_si128(spelling), result), place);
    }
    return result;
}

// The id of a spelling, or 0 if it was never interned.
static __attribute__((unused)) IdentId intern_find(const InternTable* table, const uint8_t* bytes, uint32_t length)
{
    return intern_find_hashed(table, bytes, length, intern_hash_bytes(bytes, length));
}

static __attribute__((unused)) const uint8_t* intern_bytes(const InternTable* table, IdentId id)
{
    return table->records + table->spans[id].offset;
}

static __attribute__((unused)) uint32_t intern_length(const InternTable* table, IdentId id)
{
    return table->spans[id].length;
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

// Reserves the table's memory, with `buckets` buckets (a power of two) to start with, and adds the predefined names.
// Returns false if the memory can't be reserved. c_files.c starts each lane's table at the size of the lane's share of
// the files, so that it grows little or not at all.
static __attribute__((unused)) bool intern_initialize_with(InternTable* table, uint64_t buckets)
{
    uint32_t capacity;
    *table = (InternTable){ 0 };
    table->record_arena = arena_reserve(intern_layout_bytes(buckets, &capacity) + BUSTER_INTERN_RECORD_BYTES_MAX);
    bool result = intern_lay_out(table, &table->record_arena, buckets);
    table->records = result ? table->record_arena.base + table->record_arena.length : 0;
    // Id 0's spelling: an empty one, so that no other spelling is at offset 0.
    uint8_t* none = result ? arena_push(&table->record_arena, 1, 1) : 0;
    result = none != 0;
    if (result)
    {
        none[0] = 0;
        table->spans[0] = (InternSpan){ 0 };
        table->count = 1;
        result = intern_add_names(table);
    }
    return result;
}

// intern_initialize_with the table's usual first size.
static __attribute__((unused)) bool intern_initialize(InternTable* table)
{
    return intern_initialize_with(table, BUSTER_INTERN_INITIAL_BUCKETS);
}

static __attribute__((unused)) void intern_release(InternTable* table)
{
    arena_release(&table->arena);
    arena_release(&table->record_arena);
    *table = (InternTable){ 0 };
}

// Back to the predefined names only, keeping the memory (for benchmark passes and tests).
static __attribute__((unused)) bool intern_reset(InternTable* table)
{
    memset(table->buckets, 0, (table->bucket_mask + 1) * BUSTER_INTERN_BUCKET_BYTES);
    table->record_arena.length = (uint64_t)(table->records - table->record_arena.base) + 1;
    table->count = 1;
    return intern_add_names(table);
}

// ---------------------------------------------------------------------------------------------------------------------
// Spellings added from several lanes
//
// c_files.c refines a round of files on several lanes, each into a table of its own, and merges what they found into
// one shared table: the merge gives each new spelling its id, in the order of the spellings' first occurrences in the
// files, which doesn't depend on the lanes (refining the files one after another into one table gives the same ids).
// These add the merged spellings to the shared table with those ids, on every lane at once:
//   - intern_bulk_begin, on one lane: room for `count` more ids and `bytes` more bytes of records (each spelling and its
//     NUL). If the table would pass its load, it lays out buckets and spans large enough at once, and every spelling it
//     has must then be placed in them again.
//   - intern_bulk_replace, on every lane for a range of the ids the table had, and intern_bulk_add, on every lane for
//     the new spellings it adds: spans and slots, and a new spelling's record. A lane takes an empty slot by exchanging
//     its id, 0, for the spelling's (intern_place_shared), so that lanes placing spellings in one bucket each take a slot
//     of their own, and the slots a probe passes stay full.
//   - intern_bulk_end, on one lane once the lanes have synced: the new count, and the old layout released.
// Nothing reads the table from intern_bulk_begin to intern_bulk_end.

typedef struct InternBulk InternBulk;
struct InternBulk
{
    // The first new id, how many there are, and the offset of the first one's record (the others follow in id order) and
    // their bytes.
    IdentId first;
    uint32_t count;
    uint32_t records;
    uint64_t bytes;
    // If the table grew: the spans it had, whose spellings intern_bulk_replace places again, and the arena of their
    // layout, which intern_bulk_end releases (none for the first layout, which is in the record arena).
    bool grew;
    const InternSpan* old_spans;
    Arena old_arena;
};

// intern_place while other lanes place other spellings in the table: a lane takes an empty slot by exchanging its id, 0,
// for the spelling's (lock cmpxchg), and a lane that finds the slot taken first tries the next empty one. The slot's
// prefix is written after it is taken: nothing reads a prefix until every lane has placed its spellings, and a lane looks
// only at the ids to find empty slots. The probe sequence is intern_place's, so a probe that passes a bucket finds it
// full, as a lookup expects.
static void intern_place_shared(InternTable* table, __m128i slot, uint64_t hash)
{
    const IdentId id = (IdentId)_mm_extract_epi32(slot, BUSTER_INTERN_PREFIX_BYTES / sizeof(IdentId));
    uint8_t prefix[BUSTER_INTERN_SLOT_BYTES];
    _mm_storeu_si128((__m128i*)prefix, slot);
    uint64_t bucket = hash & table->bucket_mask;
    bool placed = false;
    while (!placed)
    {
        uint8_t* bucket_bytes = table->buckets + bucket * BUSTER_INTERN_BUCKET_BYTES;
        for (uint32_t s = 0; !placed && s < BUSTER_INTERN_BUCKET_SLOTS; s += 1)
        {
            uint8_t* slot_bytes = bucket_bytes + s * BUSTER_INTERN_SLOT_BYTES;
            IdentId* word = (IdentId*)(slot_bytes + BUSTER_INTERN_PREFIX_BYTES);
            IdentId expected = 0;
            if (__atomic_load_n(word, __ATOMIC_RELAXED) == 0 && __atomic_compare_exchange_n(word, &expected, id, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            {
                memcpy(slot_bytes, prefix, BUSTER_INTERN_PREFIX_BYTES);
                placed = true;
            }
        }
        bucket = (bucket + 1) & table->bucket_mask;
    }
}

// Room in `table` for `count` more spellings of `bytes` bytes of records in all (each spelling's length + 1): their ids
// from table->count on, and their records from bulk->records on. If the table would pass its load, it gets buckets and
// spans large enough for all of them at once (bulk->grew), and its spellings must be placed in them again
// (intern_bulk_replace). One lane. Returns false, leaving the table as it was, if the ids or the memory don't fit.
static __attribute__((unused)) bool intern_bulk_begin(InternTable* table, uint32_t count, uint64_t bytes, InternBulk* bulk)
{
    *bulk = (InternBulk){ .first = table->count, .count = count, .bytes = bytes };
    bool result = (uint64_t)table->count + count <= BUSTER_INTERN_MAX_IDS;
    uint64_t buckets = table->bucket_mask + 1;
    uint32_t capacity = table->capacity;
    while (result && (uint64_t)table->count + count > capacity)
    {
        buckets *= 2;
        intern_layout_bytes(buckets, &capacity);
    }
    const uint64_t mark = table->record_arena.length;
    uint8_t* records = result ? arena_push(&table->record_arena, bytes, 1) : 0;
    result = records != 0;
    if (result && buckets != table->bucket_mask + 1)
    {
        InternTable grown = *table;
        grown.arena = arena_reserve(intern_layout_bytes(buckets, &capacity));
        result = intern_lay_out(&grown, &grown.arena, buckets);
        if (result)
        {
            bulk->grew = true;
            bulk->old_spans = table->spans;
            bulk->old_arena = table->arena;
            *table = grown;
        }
        else
        {
            arena_release(&grown.arena);
            arena_pop_to(&table->record_arena, mark);
        }
    }
    bulk->records = result ? (uint32_t)(records - table->records) : 0;
    return result;
}

// The spans of ids [begin, end) of those the table had before intern_bulk_begin, and their slots in the buckets it grew
// to (nothing if it didn't grow). Every lane, for ranges that cover the old ids.
static __attribute__((unused)) void intern_bulk_replace(InternTable* table, const InternBulk* bulk, uint32_t begin, uint32_t end)
{
    for (uint32_t id = begin; bulk->grew && id < end && id < bulk->first; id += 1)
    {
        const InternSpan span = bulk->old_spans[id];
        table->spans[id] = span;
        if (id != 0)
        {
            const uint8_t* bytes = table->records + span.offset;
            intern_place_shared(table, intern_slot(intern_first_bytes(bytes, span.length), id), intern_hash_bytes(bytes, span.length));
        }
    }
}

// Adds spelling `id` (one of intern_bulk_begin's new ids) with its record at offset `offset` and `hash`
// (intern_hash_bytes's). Every lane, each for the spellings it adds.
static __attribute__((unused)) void intern_bulk_add(InternTable* table, IdentId id, const uint8_t* bytes, uint32_t length, uint32_t offset, uint64_t hash)
{
    uint8_t* record = table->records + offset;
    memcpy(record, bytes, length);
    record[length] = 0;
    table->spans[id] = (InternSpan){ .offset = offset, .length = length };
    intern_place_shared(table, intern_slot(intern_first_bytes(bytes, length), id), hash);
}

// The table with the spellings intern_bulk_begin made room for, once every lane has added them.
static __attribute__((unused)) void intern_bulk_end(InternTable* table, InternBulk* bulk)
{
    table->count = bulk->first + bulk->count;
    if (bulk->grew)
    {
        arena_release(&bulk->old_arena);
    }
    *bulk = (InternBulk){ 0 };
}
