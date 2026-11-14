// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The pinned clang release's library builtins (#148): the rows of its builtin table with attribute F, __builtin_X, which clang -O0
// compiles to a call of the C library's function X (__builtin_sin to sin, __builtin_printf to printf), or to an LLVM
// intrinsic that computes what X returns (__builtin_fmax's llvm.maxnum, __builtin_floorl's llvm.floor, __builtin_abs's
// llvm.abs, __builtin_frexp's llvm.frexp). c_builtin_library_calls.inc lists the ones this compiler compiles so, each
// as a call of X: the checker gives the call its row's ordinary prototype and checks it as a call of that prototype,
// folds the constant ones clang folds (c_check_builtin_library.c), and the lowering calls X by name, through the unit's
// own declaration's symbol where the unit declares X with external linkage, as clang's call shares the module's
// function of that name (c_check_program_lower.c's runtime functions). __builtin_powi, powif and powil are llvm.powi's,
// whose code calls libgcc's __powidf2, __powisf2 and __powixf2 (cbuiltin_library_libgcc).
//
// The table's F rows of custom checking that classify a value (__builtin_isnormal and its kin) and __builtin_signbitf and
// signbitl, which clang tests inline, are the floating builtins' (c_check_builtin_float.c), not calls. The __fp16 ones
// (__builtin_sinf16 and its kin, #148) take and return __fp16, which no C function can: their function types take and
// return _Float16, whose values pass alike, and the checker gives their calls' arguments and results __fp16
// (cbuiltin_library_half, c_checker.c's check_x86_action). clang's -O0 code calls their own names (sinf16), but for those
// it computes in float, which the lowering does too (check_lower_library_promoted): a call of ceilf, floorf, truncf,
// rintf, roundevenf, roundf, fmaxf, fminf, fmaximum_numf, fminimum_numf or frexpf of the arguments converted to float, or
// of fma of them converted to double, the result converted back to __fp16, and fabsf16's and copysignf16's bits. Its other
// F rows stay refused, and __has_builtin answers 0 for them: C++'s __builtin_trivially_relocate, whose prototype doesn't
// decode (c_builtin_intrinsic_types.c), but which the catalog's miscellaneous builtins check. The FILE builtins (fprintf and its kin) are calls whose
// prototypes take the unit's FILE (c_check_intrinsic_input.c's check_input_file_type). The alloca variants are the catalogue's miscellaneous builtins (c_check_builtin_misc.c),
// which take them first.
//
// The rows with attribute f are the C library's functions by their own names (sin, strlen, printf), which clang makes
// builtins of the unit unless -fno-builtin, -ffreestanding or -fno-builtin-NAME says otherwise, and ALL_GNU_LANGUAGES'
// (alloca, bzero, strdup) in GNU's modes alone (the preprocessor's pp_library_builtin). Such a name, declared, is the
// library's function, which calls of it reach as of any function; undeclared, its call declares it implicitly with the
// row's prototype, which C99 and later make an error and C89 a warning, each naming the prototype and the header
// (c_check_input.c's implicit library functions). c_builtin_library_plain.inc lists the rows this compiler implements so:
// all of clang's in C's modes, stdarg.h's va_start, va_end and va_copy among them, whose va_list reference (A) a call passes
// as its decayed pointer to a function of the name, as clang's code does. C23's <stdbit.h> functions (stdc_leading_zeros_uc
// and the others) are C23's alone, and a call of the unit's declaration of one is a bit builtin of its own, computed
// inline as clang computes it (c_builtin_catalog.c's cbuiltin_stdc_library).
// Names are bounded slices, which need no terminator or readable padding.
#ifndef BUSTER_C_BUILTIN_LIBRARY_INCLUDED
#define BUSTER_C_BUILTIN_LIBRARY_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef enum CBuiltinLibraryId
{
#define BUSTER_CBUILTIN_LIBRARY_CALL(id, name) CBUILTIN_LIBRARY_##id,
#include "c_builtin_library_calls.inc"
#undef BUSTER_CBUILTIN_LIBRARY_CALL
    CBUILTIN_LIBRARY_COUNT,
} CBuiltinLibraryId;

// Each library function's name, as its call's symbol spells it, and its length, in the list's order.
static const char* const cbuiltin_library_names[CBUILTIN_LIBRARY_COUNT] __attribute__((unused)) =
{
#define BUSTER_CBUILTIN_LIBRARY_CALL(id, name) name,
#include "c_builtin_library_calls.inc"
#undef BUSTER_CBUILTIN_LIBRARY_CALL
};

static const uint8_t cbuiltin_library_lengths[CBUILTIN_LIBRARY_COUNT] __attribute__((unused)) =
{
#define BUSTER_CBUILTIN_LIBRARY_CALL(id, name) sizeof(name) - 1,
#include "c_builtin_library_calls.inc"
#undef BUSTER_CBUILTIN_LIBRARY_CALL
};

#define BUSTER_CBUILTIN_LIBRARY_PREFIX "__builtin_"
#define BUSTER_CBUILTIN_LIBRARY_PREFIX_LENGTH (sizeof(BUSTER_CBUILTIN_LIBRARY_PREFIX) - 1)
// The longest listed builtin's name, __builtin___get_unsafe_stack_bottom, bounds every lookup (test-checker checks it).
#define BUSTER_CBUILTIN_LIBRARY_MAX_NAME 35u

// The library builtin a name spells: __builtin_ followed by one of the list's functions, by a binary search over the
// list's names, which are in byte order; CBUILTIN_LIBRARY_COUNT for any other name.
// Whether a builtin's name may be one of these lists' (the library's, libgcc's and the refused ones): no x86 builtin's
// (__builtin_ia32_) is, and most of the calls the checker and the lowering ask about are of x86 builtins.
static inline __attribute__((unused)) bool cbuiltin_library_candidate(const char* name, uint32_t length)
{
    return name != NULL && !(length > 15 && memcmp(name, "__builtin_ia32_", 15) == 0);
}

static __attribute__((unused)) CBuiltinLibraryId cbuiltin_library_id(const char* name, uint32_t length)
{
    CBuiltinLibraryId result = CBUILTIN_LIBRARY_COUNT;
    const bool prefixed = name != NULL && length > BUSTER_CBUILTIN_LIBRARY_PREFIX_LENGTH && length <= BUSTER_CBUILTIN_LIBRARY_MAX_NAME &&
                          memcmp(name, BUSTER_CBUILTIN_LIBRARY_PREFIX, BUSTER_CBUILTIN_LIBRARY_PREFIX_LENGTH) == 0;
    const char* function = prefixed ? name + BUSTER_CBUILTIN_LIBRARY_PREFIX_LENGTH : NULL;
    const uint32_t function_length = prefixed ? length - (uint32_t)BUSTER_CBUILTIN_LIBRARY_PREFIX_LENGTH : 0;
    uint32_t lower = 0;
    uint32_t upper = prefixed ? CBUILTIN_LIBRARY_COUNT : 0;
    while (lower < upper)
    {
        const uint32_t middle = lower + (upper - lower) / 2;
        const uint32_t entry_length = cbuiltin_library_lengths[middle];
        const uint32_t common = function_length < entry_length ? function_length : entry_length;
        int order = memcmp(function, cbuiltin_library_names[middle], common);
        order = order != 0 ? order : (function_length > entry_length) - (function_length < entry_length);
        if (order == 0)
        {
            result = (CBuiltinLibraryId)middle;
            upper = lower;
        }
        else if (order < 0)
        {
            upper = middle;
        }
        else
        {
            lower = middle + 1;
        }
    }
    return result;
}

// Whether a library builtin's prototype takes or returns __fp16 (its name's f16 forms, __builtin_sinf16's and the others).
static __attribute__((unused)) bool cbuiltin_library_half(CBuiltinLibraryId id)
{
    const uint32_t length = id < CBUILTIN_LIBRARY_COUNT ? cbuiltin_library_lengths[id] : 0;
    return length > 3 && memcmp(cbuiltin_library_names[id] + length - 3, "f16", 3) == 0;
}

// The constant folds of the library builtins that clang folds and this compiler does too (c_check_builtin_library.c,
// which reads each: its header lists them), which __has_constexpr_builtin advertises.
typedef enum CBuiltinLibraryFold
{
    CBUILTIN_LIBRARY_FOLD_NONE,
    // A comparison: of strings (stopping at a NUL), of a bounded prefix of strings (strncmp's), or of arrays (memcmp's).
    CBUILTIN_LIBRARY_FOLD_COMPARE,
    CBUILTIN_LIBRARY_FOLD_COMPARE_BOUNDED,
    CBUILTIN_LIBRARY_FOLD_COMPARE_MEMORY,
    CBUILTIN_LIBRARY_FOLD_LENGTH,
    CBUILTIN_LIBRARY_FOLD_ABSOLUTE,
    CBUILTIN_LIBRARY_FOLD_MAXIMUM,
    CBUILTIN_LIBRARY_FOLD_MINIMUM,
    CBUILTIN_LIBRARY_FOLD_COPY_SIGN,
} CBuiltinLibraryFold;

// A library builtin's fold, NONE for one this doesn't fold, and whether its strings are wide.
static __attribute__((unused)) CBuiltinLibraryFold cbuiltin_library_fold(CBuiltinLibraryId id, bool* wide)
{
    CBuiltinLibraryFold result = CBUILTIN_LIBRARY_FOLD_NONE;
    *wide = id == CBUILTIN_LIBRARY_WCSCMP || id == CBUILTIN_LIBRARY_WCSNCMP || id == CBUILTIN_LIBRARY_WMEMCMP || id == CBUILTIN_LIBRARY_WCSLEN;
    switch (id)
    {
        case CBUILTIN_LIBRARY_STRCMP:
        case CBUILTIN_LIBRARY_WCSCMP: result = CBUILTIN_LIBRARY_FOLD_COMPARE; break;
        case CBUILTIN_LIBRARY_STRNCMP:
        case CBUILTIN_LIBRARY_WCSNCMP: result = CBUILTIN_LIBRARY_FOLD_COMPARE_BOUNDED; break;
        case CBUILTIN_LIBRARY_BCMP:
        case CBUILTIN_LIBRARY_WMEMCMP: result = CBUILTIN_LIBRARY_FOLD_COMPARE_MEMORY; break;
        case CBUILTIN_LIBRARY_WCSLEN: result = CBUILTIN_LIBRARY_FOLD_LENGTH; break;
        case CBUILTIN_LIBRARY_ABS:
        case CBUILTIN_LIBRARY_LABS:
        case CBUILTIN_LIBRARY_LLABS:
        case CBUILTIN_LIBRARY_FABSF128: result = CBUILTIN_LIBRARY_FOLD_ABSOLUTE; break;
        case CBUILTIN_LIBRARY_FMAX:
        case CBUILTIN_LIBRARY_FMAXF:
        case CBUILTIN_LIBRARY_FMAXL:
        case CBUILTIN_LIBRARY_FMAXIMUM_NUM:
        case CBUILTIN_LIBRARY_FMAXIMUM_NUMF:
        case CBUILTIN_LIBRARY_FMAXIMUM_NUML:
        case CBUILTIN_LIBRARY_FMAXF16:
        case CBUILTIN_LIBRARY_FMAXIMUM_NUMF16:
        case CBUILTIN_LIBRARY_FMAXF128:
        case CBUILTIN_LIBRARY_FMAXIMUM_NUMF128: result = CBUILTIN_LIBRARY_FOLD_MAXIMUM; break;
        case CBUILTIN_LIBRARY_FMIN:
        case CBUILTIN_LIBRARY_FMINF:
        case CBUILTIN_LIBRARY_FMINL:
        case CBUILTIN_LIBRARY_FMINIMUM_NUM:
        case CBUILTIN_LIBRARY_FMINIMUM_NUMF:
        case CBUILTIN_LIBRARY_FMINIMUM_NUML:
        case CBUILTIN_LIBRARY_FMINF16:
        case CBUILTIN_LIBRARY_FMINIMUM_NUMF16:
        case CBUILTIN_LIBRARY_FMINF128:
        case CBUILTIN_LIBRARY_FMINIMUM_NUMF128: result = CBUILTIN_LIBRARY_FOLD_MINIMUM; break;
        case CBUILTIN_LIBRARY_COPYSIGNL:
        case CBUILTIN_LIBRARY_COPYSIGNF128: result = CBUILTIN_LIBRARY_FOLD_COPY_SIGN; break;
        default: break;
    }
    return result;
}

// How the code computes a call of the unit's declaration of a plain library builtin in place of calling the library's
// function, as clang -O0's code computes its builtin's intrinsic inline on x86-64 (cbuiltin_library_plain_folds'
// `computed`, c_check_program.c's check_program_call_computed):
// - EXTREME: fmax, fmin, fmaximum_num and fminimum_num of float and double, by llvm.maxnum's, minnum's, maximumnum's and
//   minimumnum's instructions (#967), as __builtin_X's.
// - ABSOLUTE: abs, labs and llabs, llvm.abs's: the argument's sign spread across it, then its exclusive or with the
//   argument less the spread, which is no branch and wraps as neg does (abs(INT_MIN) is INT_MIN, as undefined in C).
// - FLOAT_ABSOLUTE and COPY_SIGN: fabs, fabsf and fabsl, llvm.fabs's, and copysign, copysignf and copysignl,
//   llvm.copysign's: the first argument's bits with its sign bit cleared, or replaced by the second's.
// - COPY, MOVE and SET: memcpy, memmove and memset, llvm.memcpy's, memmove's and memset's, of a constant size of at most
//   BUSTER_CBUILTIN_LIBRARY_INLINE_BYTES: their bytes' loads and stores, as __builtin_memcpy's are (#1373). clang's
//   FastISel copies at most 32 bytes so and calls memcpy past them; its memmove inlines more, and its memset calls the
//   library's whatever the size, which a few stores beat. A size that is no constant, or a larger one, calls the
//   library's function, whose vector loops a large size wants.
typedef enum CBuiltinLibraryComputed
{
    CBUILTIN_LIBRARY_COMPUTED_NONE,
    CBUILTIN_LIBRARY_COMPUTED_EXTREME,
    CBUILTIN_LIBRARY_COMPUTED_ABSOLUTE,
    CBUILTIN_LIBRARY_COMPUTED_FLOAT_ABSOLUTE,
    CBUILTIN_LIBRARY_COMPUTED_COPY_SIGN,
    CBUILTIN_LIBRARY_COMPUTED_COPY,
    CBUILTIN_LIBRARY_COMPUTED_MOVE,
    CBUILTIN_LIBRARY_COMPUTED_SET,
} CBuiltinLibraryComputed;

#define BUSTER_CBUILTIN_LIBRARY_INLINE_BYTES 32u

// The plain library builtins whose rows have attribute E and that this compiler folds (c_builtin_library_plain.inc's), each
// with its row's prototype and its fold: a call of the unit's declaration of one, where clang makes that declaration the
// builtin (its name a builtin of the unit, external, of the row's prototype: Sema's BuiltinAttr), folds as __builtin_X's
// does (c_check_builtin_library.c's check_builtin_library_fold_plain). memchr, strchr, wcschr and wmemchr fold to
// pointers, which the code calls instead, and memcpy, memmove, wmemcpy and wmemmove in C++'s constant evaluation alone.
// Past them, the plain builtins that don't fold (CBUILTIN_LIBRARY_FOLD_NONE: no E, or C++'s alone) but whose call the code
// computes inline (`computed`, CBuiltinLibraryComputed), each with its library builtin where the list has one
// (`library`: __builtin_fmax's, __builtin_abs's, __builtin_copysignl's) and CBUILTIN_LIBRARY_COUNT for the others and
// the folds.
typedef struct CBuiltinLibraryPlainFold CBuiltinLibraryPlainFold;
struct CBuiltinLibraryPlainFold
{
    const char* name;
    const char* prototype;
    CBuiltinLibraryFold fold;
    bool wide;
    CBuiltinLibraryId library;
    CBuiltinLibraryComputed computed;
};

static const CBuiltinLibraryPlainFold cbuiltin_library_plain_folds[] __attribute__((unused)) =
{
    { "bcmp", "ivC*vC*z", CBUILTIN_LIBRARY_FOLD_COMPARE_MEMORY, false, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_NONE },
    { "memcmp", "ivC*vC*z", CBUILTIN_LIBRARY_FOLD_COMPARE_MEMORY, false, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_NONE },
    { "strcmp", "icC*cC*", CBUILTIN_LIBRARY_FOLD_COMPARE, false, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_NONE },
    { "strlen", "zcC*", CBUILTIN_LIBRARY_FOLD_LENGTH, false, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_NONE },
    { "strncmp", "icC*cC*z", CBUILTIN_LIBRARY_FOLD_COMPARE_BOUNDED, false, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_NONE },
    { "wcscmp", "iwC*wC*", CBUILTIN_LIBRARY_FOLD_COMPARE, true, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_NONE },
    { "wcslen", "zwC*", CBUILTIN_LIBRARY_FOLD_LENGTH, true, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_NONE },
    { "wcsncmp", "iwC*wC*z", CBUILTIN_LIBRARY_FOLD_COMPARE_BOUNDED, true, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_NONE },
    { "wmemcmp", "iwC*wC*z", CBUILTIN_LIBRARY_FOLD_COMPARE_MEMORY, true, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_NONE },
    { "fmax", "ddd", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_FMAX, CBUILTIN_LIBRARY_COMPUTED_EXTREME },
    { "fmaxf", "fff", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_FMAXF, CBUILTIN_LIBRARY_COMPUTED_EXTREME },
    { "fmin", "ddd", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_FMIN, CBUILTIN_LIBRARY_COMPUTED_EXTREME },
    { "fminf", "fff", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_FMINF, CBUILTIN_LIBRARY_COMPUTED_EXTREME },
    { "fmaximum_num", "ddd", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_FMAXIMUM_NUM, CBUILTIN_LIBRARY_COMPUTED_EXTREME },
    { "fmaximum_numf", "fff", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_FMAXIMUM_NUMF, CBUILTIN_LIBRARY_COMPUTED_EXTREME },
    { "fminimum_num", "ddd", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_FMINIMUM_NUM, CBUILTIN_LIBRARY_COMPUTED_EXTREME },
    { "fminimum_numf", "fff", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_FMINIMUM_NUMF, CBUILTIN_LIBRARY_COMPUTED_EXTREME },
    { "abs", "ii", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_ABS, CBUILTIN_LIBRARY_COMPUTED_ABSOLUTE },
    { "labs", "LiLi", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_LABS, CBUILTIN_LIBRARY_COMPUTED_ABSOLUTE },
    { "llabs", "LLiLLi", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_LLABS, CBUILTIN_LIBRARY_COMPUTED_ABSOLUTE },
    { "fabs", "dd", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_FLOAT_ABSOLUTE },
    { "fabsf", "ff", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_FLOAT_ABSOLUTE },
    { "fabsl", "LdLd", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_FLOAT_ABSOLUTE },
    { "copysign", "ddd", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_COPY_SIGN },
    { "copysignf", "fff", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_COPY_SIGN },
    { "copysignl", "LdLdLd", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_COPYSIGNL, CBUILTIN_LIBRARY_COMPUTED_COPY_SIGN },
    { "memcpy", "v*v*vC*z", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_COPY },
    { "memmove", "v*v*vC*z", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_MOVE },
    { "memset", "v*v*iz", CBUILTIN_LIBRARY_FOLD_NONE, false, CBUILTIN_LIBRARY_COUNT, CBUILTIN_LIBRARY_COMPUTED_SET },
};

#define BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS ((uint32_t)(sizeof(cbuiltin_library_plain_folds) / sizeof(cbuiltin_library_plain_folds[0])))

// The index of a plain library builtin's fold (cbuiltin_library_plain_folds), or BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS.
static __attribute__((unused)) uint32_t cbuiltin_library_plain_fold(const char* name, uint32_t length)
{
    uint32_t result = BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS;
    for (uint32_t i = 0; name != NULL && result == BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS && i < BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS; i += 1)
    {
        result = length == strlen(cbuiltin_library_plain_folds[i].name) && memcmp(name, cbuiltin_library_plain_folds[i].name, length) == 0 ? i : result;
    }
    return result;
}

// __builtin_powi, powif and powil: llvm.powi's, whose x86-64 code calls libgcc's __powidf2, __powisf2 and __powixf2
// (x raised to an int's power by repeated squaring, not pow's result). Their index, 0 to 2, or 3 for any other name.
#define BUSTER_CBUILTIN_LIBRARY_LIBGCC 3u

static __attribute__((unused)) uint32_t cbuiltin_library_libgcc(const char* name, uint32_t length)
{
    static const char* const names[BUSTER_CBUILTIN_LIBRARY_LIBGCC] = { "__builtin_powi", "__builtin_powif", "__builtin_powil" };
    uint32_t result = BUSTER_CBUILTIN_LIBRARY_LIBGCC;
    for (uint32_t i = 0; name != NULL && i < BUSTER_CBUILTIN_LIBRARY_LIBGCC && result == BUSTER_CBUILTIN_LIBRARY_LIBGCC; i += 1)
    {
        result = length == strlen(names[i]) && memcmp(name, names[i], length) == 0 ? i : result;
    }
    return result;
}

// The Clang release the plain library builtins are pinned to (#1131): 23.1.4, CI's reference, whose rows are 23.1.1's
// too and whose GNU library builtins include strlcat and strlcpy, as every upstream 23.x release's do. The oracle tests
// compare `clang --version` with this release's official build, report both when they differ, and skip only the plain
// names a named build's table is known to classify differently (cbuiltin_library_table_differences).
#define BUSTER_CLANG_REFERENCE_VERSION "23.1.4"

// The language modes of a plain library builtin's row (c_builtin_library_plain.inc's second column): every C mode's
// (ALL_LANGUAGES, false), GNU's alone (ALL_GNU_LANGUAGES, true) or C23's and later (C23_LANG: <stdbit.h>'s functions).
#define BUSTER_CBUILTIN_LIBRARY_ALL 0u
#define BUSTER_CBUILTIN_LIBRARY_GNU 1u
#define BUSTER_CBUILTIN_LIBRARY_C23 2u

// The plain library builtins (c_builtin_library_plain.inc): each name, its length and its language modes, in the names'
// order.
typedef struct CBuiltinLibraryPlain CBuiltinLibraryPlain;
struct CBuiltinLibraryPlain
{
    const char* name;
    uint32_t length;
    uint8_t modes;
};

static const CBuiltinLibraryPlain cbuiltin_library_plains[] __attribute__((unused)) =
{
#define BUSTER_CBUILTIN_LIBRARY_PLAIN(name, modes) { name, sizeof(name) - 1, modes },
#include "c_builtin_library_plain.inc"
#undef BUSTER_CBUILTIN_LIBRARY_PLAIN
};

// A plain library builtin's language token of clang's table, as its row's modes say.
static __attribute__((unused)) const char* cbuiltin_library_plain_languages(const CBuiltinLibraryPlain* plain)
{
    return plain->modes == BUSTER_CBUILTIN_LIBRARY_GNU ? "ALL_GNU_LANGUAGES" : plain->modes == BUSTER_CBUILTIN_LIBRARY_C23 ? "C23_LANG" : "ALL_LANGUAGES";
}

#define BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT ((uint32_t)(sizeof(cbuiltin_library_plains) / sizeof(cbuiltin_library_plains[0])))
// The longest listed name, stdc_first_trailing_zero_ull's, bounds every lookup (test-checker checks it).
#define BUSTER_CBUILTIN_LIBRARY_PLAIN_MAX_NAME 28u

// The plain library builtin a name spells, by a binary search: NULL for a name not listed.
static __attribute__((unused)) const CBuiltinLibraryPlain* cbuiltin_library_plain(const char* name, uint32_t length)
{
    const CBuiltinLibraryPlain* result = NULL;
    uint32_t lower = 0;
    uint32_t upper = name != NULL && length != 0 && length <= BUSTER_CBUILTIN_LIBRARY_PLAIN_MAX_NAME ? BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT : 0;
    while (lower < upper)
    {
        const uint32_t middle = lower + (upper - lower) / 2;
        const CBuiltinLibraryPlain* entry = cbuiltin_library_plains + middle;
        const uint32_t common = length < entry->length ? length : entry->length;
        int order = memcmp(name, entry->name, common);
        order = order != 0 ? order : (length > entry->length) - (length < entry->length);
        if (order == 0)
        {
            result = entry;
            upper = lower;
        }
        else if (order < 0)
        {
            upper = middle;
        }
        else
        {
            lower = middle + 1;
        }
    }
    return result;
}

// The plain names a vendor's clang build is known to classify differently from the reference's official build, by the
// vendor word its `clang --version` puts before "clang version" (clang_reference_parse's "Ubuntu 23.1.6"): Debian's and
// Ubuntu's packages, apt.llvm.org's included, comment strlcpy's and strlcat's rows out of Builtins.td with
// debian/patches/23-strlcpy_strlcat_warning_removed.diff (carried since LLVM 3.4), in every release; upstream's
// llvmorg-23.1.4 and llvmorg-23.1.5 tags and release/23.x keep them. The oracle tests skip a mismatched answer only when
// the found build is a row's vendor's and the name that row's: a difference they can't attribute to a known build's
// table stays a failure, since the same probe can't tell an unlisted build's table from a buster bug.
static const struct
{
    const char* vendor;
    const char* name;
} cbuiltin_library_table_differences[] = {
    { "Debian", "strlcat" },
    { "Debian", "strlcpy" },
    { "Ubuntu", "strlcat" },
    { "Ubuntu", "strlcpy" },
};

// Whether the build `found` names (clang_reference_parse's: a vendor word, a space, then the release, or the release
// alone for an upstream build) has a library-builtin table known to classify `name` differently from the reference's.
static __attribute__((unused)) bool cbuiltin_library_table_differs(const char* found, const char* name, uint32_t length)
{
    bool result = false;
    const char* space = found != NULL ? strchr(found, ' ') : NULL;
    const size_t vendor = space != NULL ? (size_t)(space - found) : 0;
    for (uint32_t i = 0; vendor != 0 && !result && i < sizeof(cbuiltin_library_table_differences) / sizeof(cbuiltin_library_table_differences[0]); i += 1)
    {
        const size_t spelling = strlen(cbuiltin_library_table_differences[i].name);
        result = strlen(cbuiltin_library_table_differences[i].vendor) == vendor && memcmp(found, cbuiltin_library_table_differences[i].vendor, vendor) == 0 &&
                 spelling == length && memcmp(name, cbuiltin_library_table_differences[i].name, length) == 0;
    }
    return result;
}

// The unit's types that a prototype names by its typedefs, as clang's TypePrinter spells them (#148): FILE's (P) and
// ucontext_t's (K), their typedefs' types, which a sugared spelling names by the typedef instead, and jmp_buf's (J) and
// sigjmp_buf's (SJ) as a parameter decays them ("long *"), which keeps no typedef; NULL where the unit has none.
typedef struct CBuiltinLibraryNamed CBuiltinLibraryNamed;
struct CBuiltinLibraryNamed
{
    const char* file;
    const char* jmp_buf;
    const char* sigjmp_buf;
    const char* ucontext;
};

// A bounded text that a spelling appends to: its bytes so far, `length` of `capacity`, whether it ran out of room,
// restrict's spelling (C89's __restrict, where restrict is no keyword), and the unit's named types.
typedef struct CBuiltinLibraryText CBuiltinLibraryText;
struct CBuiltinLibraryText
{
    char* bytes;
    uint32_t length;
    uint32_t capacity;
    bool full;
    const char* restrict_spelling;
    const CBuiltinLibraryNamed* named;
};

static void cbuiltin_library_append(CBuiltinLibraryText* text, const char* bytes)
{
    const uint32_t length = (uint32_t)strlen(bytes);
    text->full |= text->capacity - text->length <= length;
    if (!text->full)
    {
        memcpy(text->bytes + text->length, bytes, length);
        text->length += length;
        text->bytes[text->length] = 0;
    }
}

// One type of a plain library builtin's encoding, from *position, as clang's TypePrinter spells it in a function type:
// "const char *restrict", "unsigned short", "bool", "_Complex double", a va_list parameter's "struct __va_list_tag *", wchar_t's int, size_t's
// __size_t where `sugared` says (clang's builtins' sugar for it, *sized set) and otherwise unsigned long, FILE and
// ucontext_t likewise by their typedefs or their types (CBuiltinLibraryNamed), jmp_buf and sigjmp_buf decayed, and
// pid_t as int, clang's process ID type on x86-64 Linux. False for a code the plain rows don't use, or a named type the
// unit hasn't.
static bool cbuiltin_library_spell_type(const char* encoding, uint32_t length, uint32_t* position, bool sugared, bool* sized, CBuiltinLibraryText* text)
{
    uint32_t at = *position;
    // A vector's lanes (`V8s`: eight shorts), spelled as clang's TypePrinter spells a vector_size type.
    uint32_t lanes = 0;
    const bool vector = at < length && encoding[at] == 'V';
    at += vector;
    while (vector && at < length && encoding[at] >= '0' && encoding[at] <= '9' && lanes < 100000)
    {
        lanes = lanes * 10u + (uint32_t)(encoding[at] - '0');
        at += 1;
    }
    uint32_t longs = 0;
    bool is_unsigned = false;
    bool complex = false;
    bool is_signed = false;
    while (at < length && (encoding[at] == 'L' || encoding[at] == 'U' || encoding[at] == 'X' || encoding[at] == 'S'))
    {
        longs += encoding[at] == 'L';
        is_unsigned |= encoding[at] == 'U';
        complex |= encoding[at] == 'X';
        is_signed |= encoding[at] == 'S';
        at += 1;
    }
    const CBuiltinLibraryNamed* named = text->named;
    const char code = at < length ? encoding[at] : 0;
    at += at < length;
    const char* base = NULL;
    switch (code)
    {
        case 'v': base = "void"; break;
        case 'c': base = is_unsigned ? "unsigned char" : is_signed ? "signed char" : "char"; break;
        case 's': base = is_unsigned ? "unsigned short" : "short"; break;
        // Only C23's rows (<stdbit.h>'s stdc_has_single_bit_*) take or return bool, which C23 spells as its keyword.
        case 'b': base = "bool"; break;
        case 'i': base = longs == 2 ? (is_unsigned ? "unsigned long long" : "long long") : longs == 1 ? (is_unsigned ? "unsigned long" : "long") :
                         (is_unsigned ? "unsigned int" : "int"); break;
        case 'f': base = "float"; break;
        case 'd': base = longs == 2 ? "__float128" : longs == 1 ? "long double" : "double"; break;
        case 'z': base = sugared ? "__size_t" : "unsigned long"; break;
        case 'w': base = "int"; break;
        // A va_list (a) and a reference to one (A, va_start's and its kin's), each its decayed array's pointer on x86-64.
        case 'a':
        case 'A': base = "struct __va_list_tag *"; break;
        case 'P': base = named == NULL || named->file == NULL ? NULL : sugared ? "FILE" : named->file; break;
        case 'K': base = named == NULL || named->ucontext == NULL ? NULL : sugared ? "ucontext_t" : named->ucontext; break;
        case 'J': base = named == NULL ? NULL : is_signed ? named->sigjmp_buf : named->jmp_buf; break;
        case 'p': base = "int"; break;
        default: break;
    }
    *sized |= code == 'z' || code == 'P' || code == 'K';
    // The base's qualifiers come first ("const char"), then each pointer's star and its own ("char *const *").
    const bool constant = at < length && encoding[at] == 'C';
    at += constant;
    if (constant)
    {
        cbuiltin_library_append(text, "const ");
    }
    if (complex)
    {
        cbuiltin_library_append(text, "_Complex ");
    }
    if (vector && base != NULL)
    {
        char size[32];
        snprintf(size, sizeof(size), "%u", lanes);
        cbuiltin_library_append(text, "__attribute__((__vector_size__(");
        cbuiltin_library_append(text, size);
        cbuiltin_library_append(text, " * sizeof(");
        cbuiltin_library_append(text, base);
        cbuiltin_library_append(text, ")))) ");
    }
    cbuiltin_library_append(text, base != NULL ? base : "");
    while (at < length && (encoding[at] == '*' || encoding[at] == 'C' || encoding[at] == 'R'))
    {
        const bool star = encoding[at] == '*';
        const bool after_star = text->length != 0 && text->bytes[text->length - 1] == '*';
        cbuiltin_library_append(text, star ? (after_star ? "*" : " *") : encoding[at] == 'C' ? "const" : text->restrict_spelling);
        at += 1;
    }
    *position = at;
    return base != NULL && (!vector || (lanes != 0 && !constant && !complex));
}

// A plain library builtin's prototype, from its row's encoding and attributes (`r` noreturn), as clang's diagnostics
// spell it: "double (double)", "void *(__size_t)", "int (const char *, ...)", "void (void) __attribute__((noreturn))",
// with __size_t for size_t where `sugared` says and restrict as `c99` says (C89 spells it __restrict), into `buffer` of
// `capacity` bytes, NUL-terminated; *sized says whether it has one (and clang adds the spelling without the sugar:
// "(aka '...')", as it does for FILE and ucontext_t, of the unit's types `named`, which may be NULL). False where it
// doesn't fit, or the encoding has a code the plain rows don't use.
static bool cbuiltin_library_spell_as(const char* encoding, uint32_t length, const char* attributes, uint32_t attributes_length, bool sugared, bool c99,
                                      const CBuiltinLibraryNamed* named, bool pointer_to, char* buffer, uint32_t capacity, bool* sized);
static __attribute__((unused)) bool cbuiltin_library_spell(const char* encoding, uint32_t length, const char* attributes, uint32_t attributes_length,
                                                           bool sugared, bool c99, const CBuiltinLibraryNamed* named, char* buffer, uint32_t capacity,
                                                           bool* sized)
{
    return cbuiltin_library_spell_as(encoding, length, attributes, attributes_length, sugared, c99, named, false, buffer, capacity, sized);
}

// cbuiltin_library_spell's prototype, or with `pointer_to` a pointer to it (#1447: `&malloc`'s "void *(*)(__size_t)",
// "__size_t (*)(const char *)").
static bool cbuiltin_library_spell_as(const char* encoding, uint32_t length, const char* attributes, uint32_t attributes_length, bool sugared, bool c99,
                                      const CBuiltinLibraryNamed* named, bool pointer_to, char* buffer, uint32_t capacity, bool* sized)
{
    CBuiltinLibraryText text = {
        .bytes = buffer,
        .capacity = capacity,
        .full = capacity == 0,
        .restrict_spelling = c99 ? "restrict" : "__restrict",
        .named = named,
    };
    uint32_t position = 0;
    *sized = false;
    bool valid = cbuiltin_library_spell_type(encoding, length, &position, sugared, sized, &text);
    const bool pointer = text.length != 0 && text.bytes[text.length - 1] == '*';
    cbuiltin_library_append(&text, pointer_to ? (pointer ? "(*)(" : " (*)(") : pointer ? "(" : " (");
    uint32_t parameters = 0;
    while (valid && position < length && encoding[position] != '.')
    {
        cbuiltin_library_append(&text, parameters != 0 ? ", " : "");
        valid = cbuiltin_library_spell_type(encoding, length, &position, sugared, sized, &text);
        parameters += 1;
    }
    const bool variadic = position < length && encoding[position] == '.';
    cbuiltin_library_append(&text, variadic ? (parameters != 0 ? ", ...)" : "...)") : parameters != 0 ? ")" : "void)");
    if (memchr(attributes, 'r', attributes_length) != NULL)
    {
        cbuiltin_library_append(&text, " __attribute__((noreturn))");
    }
    return valid && !text.full;
}

// Whether a builtin row's attributes make its parameter `index` (from 0) nonnull: its N attribute's list ("N:0,2:"), which
// clang's GetBuiltinType gives the parameter's type as _Nonnull (#1447).
static __attribute__((unused)) bool cbuiltin_library_nonnull(const char* attributes, uint32_t length, uint32_t index)
{
    bool result = false;
    uint32_t i = 0;
    while (i + 1 < length)
    {
        const bool list = attributes[i] == 'N' && attributes[i + 1] == ':';
        uint32_t at = list ? i + 2 : i + 1;
        uint32_t value = 0;
        bool digits = false;
        while (list && at < length && attributes[at] != ':')
        {
            const char c = attributes[at];
            const bool digit = c >= '0' && c <= '9';
            result |= !digit && digits && value == index;
            value = digit ? value * 10u + (uint32_t)(c - '0') : 0;
            digits = digit;
            at += 1;
        }
        result |= list && digits && value == index;
        i = list ? at + 1 : at;
    }
    return result;
}

// One parameter's type of a builtin's prototype (#1447, diagnose_as_builtin's error of mismatched types), as clang's
// TypePrinter spells the builtin's declaration's parameter: the `index`th (from 0) of `encoding`'s parameters, as
// cbuiltin_library_spell_type spells it (size_t's __size_t where `sugared` says, which *sized then says; restrict as `c99`
// says), with " _Nonnull" where its row's N attribute lists it and `sugared` says. Into `buffer` of `capacity` bytes,
// NUL-terminated. False where it and the parameters before it aren't spelled so, or it doesn't fit.
static __attribute__((unused)) bool cbuiltin_library_spell_parameter(const char* encoding, uint32_t length, const char* attributes, uint32_t attributes_length,
                                                                     uint32_t index, bool sugared, bool c99, const CBuiltinLibraryNamed* named, char* buffer,
                                                                     uint32_t capacity, bool* sized)
{
    char skipped[256];
    CBuiltinLibraryText skip = {
        .bytes = skipped,
        .capacity = sizeof(skipped),
        .restrict_spelling = "restrict",
        .named = named,
    };
    CBuiltinLibraryText text = {
        .bytes = buffer,
        .capacity = capacity,
        .full = capacity == 0,
        .restrict_spelling = c99 ? "restrict" : "__restrict",
        .named = named,
    };
    uint32_t position = 0;
    bool ignored = false;
    bool valid = cbuiltin_library_spell_type(encoding, length, &position, sugared, &ignored, &skip);
    for (uint32_t i = 0; valid && i < index; i += 1)
    {
        skip.length = 0;
        skip.full = false;
        valid = position < length && encoding[position] != '.' && cbuiltin_library_spell_type(encoding, length, &position, sugared, &ignored, &skip);
    }
    *sized = false;
    valid = valid && position < length && encoding[position] != '.' && cbuiltin_library_spell_type(encoding, length, &position, sugared, sized, &text);
    if (valid && sugared && cbuiltin_library_nonnull(attributes, attributes_length, index))
    {
        cbuiltin_library_append(&text, " _Nonnull");
    }
    return valid && !text.full;
}

// clang's description after a vector parameter's spelling (#1447, diagnose_as_builtin's error of mismatched types): the
// `index`th (from 0) of `encoding`'s parameters, where it is a vector and no pointer, as "(vector of 8 'short' values)",
// into `buffer` of `capacity` bytes. False where it isn't one, or it doesn't fit.
static __attribute__((unused)) bool cbuiltin_library_vector_description(const char* encoding, uint32_t length, uint32_t index, char* buffer, uint32_t capacity)
{
    char skipped[256];
    CBuiltinLibraryText skip = {
        .bytes = skipped,
        .capacity = sizeof(skipped),
        .restrict_spelling = "restrict",
    };
    uint32_t position = 0;
    bool ignored = false;
    bool valid = cbuiltin_library_spell_type(encoding, length, &position, true, &ignored, &skip);
    for (uint32_t i = 0; valid && i < index; i += 1)
    {
        skip.length = 0;
        skip.full = false;
        valid = position < length && encoding[position] != '.' && cbuiltin_library_spell_type(encoding, length, &position, true, &ignored, &skip);
    }
    const uint32_t start = position;
    skip.length = 0;
    skip.full = false;
    valid = valid && position < length && encoding[position] == 'V' && cbuiltin_library_spell_type(encoding, length, &position, true, &ignored, &skip);
    // The element's spelling follows the attribute's, and no pointer follows it.
    const char* close = valid ? memchr(skipped, ')', skip.length) : NULL;
    const char* element = close != NULL && skip.length > 5 ? strstr(skipped, ")))) ") : NULL;
    valid = valid && !skip.full && element != NULL && skipped[skip.length - 1] != '*';
    uint32_t lanes = 0;
    for (uint32_t at = start + 1; valid && at < length && encoding[at] >= '0' && encoding[at] <= '9'; at += 1)
    {
        lanes = lanes * 10u + (uint32_t)(encoding[at] - '0');
    }
    const int written = valid ? snprintf(buffer, capacity, "(vector of %u '%.*s' values)", lanes, (int)(skip.length - (uint32_t)(element + 5 - skipped)), element + 5) : -1;
    return valid && written > 0 && (uint32_t)written < capacity;
}

// The header that clang's note names for a row's header token (Builtins::getHeaderName), NULL for NO_HEADER or a token
// the plain rows don't use.
static __attribute__((unused)) const char* cbuiltin_library_header(const char* token)
{
    static const struct
    {
        const char* token;
        const char* header;
    } headers[] =
    {
        { "BLOCKS_H", "Blocks.h" },
        { "COMPLEX_H", "complex.h" },
        { "CTYPE_H", "ctype.h" },
        { "MALLOC_H", "malloc.h" },
        { "MATH_H", "math.h" },
        { "SETJMP_H", "setjmp.h" },
        { "STDARG_H", "stdarg.h" },
        { "STDBIT_H", "stdbit.h" },
        { "STDIO_H", "stdio.h" },
        { "STDLIB_H", "stdlib.h" },
        { "STRINGS_H", "strings.h" },
        { "STRING_H", "string.h" },
        { "UNISTD_H", "unistd.h" },
        { "WCHAR_H", "wchar.h" },
    };
    const char* result = NULL;
    for (uint32_t i = 0; token != NULL && result == NULL && i < sizeof(headers) / sizeof(headers[0]); i += 1)
    {
        result = strcmp(token, headers[i].token) == 0 ? headers[i].header : NULL;
    }
    return result;
}

// Whether __has_builtin advertises a name as a library builtin this compiler implements: the list's and powi's. A
// refused one isn't. Overlong query slices fail before any byte past the bound is read.
// The library function a builtin of no F row calls under its own prototype, as clang's code calls it (#148):
// __builtin_char_memchr's memchr (CGBuiltin.cpp takes it as __builtin_memchr's), which returns char * rather than void *;
// CBUILTIN_LIBRARY_COUNT for any other name.
static __attribute__((unused)) CBuiltinLibraryId cbuiltin_library_alias(const char* name, uint32_t length)
{
    const bool memchr = name != NULL && length == 21 && memcmp(name, "__builtin_char_memchr", 21) == 0;
    return memchr ? CBUILTIN_LIBRARY_MEMCHR : CBUILTIN_LIBRARY_COUNT;
}

static __attribute__((unused)) bool cbuiltin_library_source(const uint8_t* name, uint32_t length)
{
    const bool bounded = name != NULL && length <= BUSTER_CBUILTIN_LIBRARY_MAX_NAME;
    const bool result = bounded && (cbuiltin_library_id((const char*)name, length) != CBUILTIN_LIBRARY_COUNT ||
                                    cbuiltin_library_libgcc((const char*)name, length) != BUSTER_CBUILTIN_LIBRARY_LIBGCC ||
                                    cbuiltin_library_alias((const char*)name, length) != CBUILTIN_LIBRARY_COUNT);
    return result;
}

#endif
