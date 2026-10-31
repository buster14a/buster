// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang 23.1.1's library builtins (#148): the rows of its builtin table with attribute F, __builtin_X, which clang -O0
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
// signbitl, which clang tests inline, are the floating builtins' (c_check_builtin_float.c), not calls. Its other F rows
// stay refused, and __has_builtin answers 0 for them: the __fp16 ones (their prototypes take and return __fp16, which no
// C function can), the matrix builtins and C++'s __builtin_trivially_relocate, the alloca variants, the signaling NaNs'
// and the _Float128 and _Float16 NaNs' (which clang folds and libc has no function for), and the FILE builtins (fprintf
// and its kin, whose prototypes need the unit's FILE). Those that the checker can type are refused by name where they
// are lowered (cbuiltin_library_refusal); the others' prototypes don't decode (c_builtin_intrinsic_types.c).
//
// The rows with attribute f are the C library's functions by their own names (sin, strlen, printf), which clang makes
// builtins of the unit unless -fno-builtin, -ffreestanding or -fno-builtin-NAME says otherwise, and ALL_GNU_LANGUAGES'
// (alloca, bzero, strdup) in GNU's modes alone (the preprocessor's pp_library_builtin). Such a name, declared, is the
// library's function, which calls of it reach as of any function; undeclared, its call declares it implicitly with the
// row's prototype, which C99 and later make an error and C89 a warning, each naming the prototype and the header
// (c_check_input.c's implicit library functions). c_builtin_library_plain.inc lists the rows this compiler implements so:
// all of clang's in C's modes but those that need the unit's FILE, jmp_buf or ucontext_t or have no prototype to check
// (setjmp and its kin, vfork, umask, pthread_create), and stdarg.h's va_start, va_end and va_copy.
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
        case CBUILTIN_LIBRARY_LLABS: result = CBUILTIN_LIBRARY_FOLD_ABSOLUTE; break;
        case CBUILTIN_LIBRARY_FMAX:
        case CBUILTIN_LIBRARY_FMAXF:
        case CBUILTIN_LIBRARY_FMAXL:
        case CBUILTIN_LIBRARY_FMAXIMUM_NUM:
        case CBUILTIN_LIBRARY_FMAXIMUM_NUMF:
        case CBUILTIN_LIBRARY_FMAXIMUM_NUML: result = CBUILTIN_LIBRARY_FOLD_MAXIMUM; break;
        case CBUILTIN_LIBRARY_FMIN:
        case CBUILTIN_LIBRARY_FMINF:
        case CBUILTIN_LIBRARY_FMINL:
        case CBUILTIN_LIBRARY_FMINIMUM_NUM:
        case CBUILTIN_LIBRARY_FMINIMUM_NUMF:
        case CBUILTIN_LIBRARY_FMINIMUM_NUML: result = CBUILTIN_LIBRARY_FOLD_MINIMUM; break;
        case CBUILTIN_LIBRARY_COPYSIGNL: result = CBUILTIN_LIBRARY_FOLD_COPY_SIGN; break;
        default: break;
    }
    return result;
}

// The plain library builtins whose rows have attribute E and that this compiler folds (c_builtin_library_plain.inc's), each
// with its row's prototype and its fold: a call of the unit's declaration of one, where clang makes that declaration the
// builtin (its name a builtin of the unit, external, of the row's prototype: Sema's BuiltinAttr), folds as __builtin_X's
// does (c_check_builtin_library.c's check_builtin_library_fold_plain). memchr, strchr, wcschr and wmemchr fold to
// pointers, which the code calls instead, and memcpy, memmove, wmemcpy and wmemmove in C++'s constant evaluation alone.
typedef struct CBuiltinLibraryPlainFold CBuiltinLibraryPlainFold;
struct CBuiltinLibraryPlainFold
{
    const char* name;
    const char* prototype;
    CBuiltinLibraryFold fold;
    bool wide;
};

static const CBuiltinLibraryPlainFold cbuiltin_library_plain_folds[] __attribute__((unused)) =
{
    { "bcmp", "ivC*vC*z", CBUILTIN_LIBRARY_FOLD_COMPARE_MEMORY, false },
    { "memcmp", "ivC*vC*z", CBUILTIN_LIBRARY_FOLD_COMPARE_MEMORY, false },
    { "strcmp", "icC*cC*", CBUILTIN_LIBRARY_FOLD_COMPARE, false },
    { "strlen", "zcC*", CBUILTIN_LIBRARY_FOLD_LENGTH, false },
    { "strncmp", "icC*cC*z", CBUILTIN_LIBRARY_FOLD_COMPARE_BOUNDED, false },
    { "wcscmp", "iwC*wC*", CBUILTIN_LIBRARY_FOLD_COMPARE, true },
    { "wcslen", "zwC*", CBUILTIN_LIBRARY_FOLD_LENGTH, true },
    { "wcsncmp", "iwC*wC*z", CBUILTIN_LIBRARY_FOLD_COMPARE_BOUNDED, true },
    { "wmemcmp", "iwC*wC*z", CBUILTIN_LIBRARY_FOLD_COMPARE_MEMORY, true },
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

// The library builtins whose prototypes the checker gives but which the lowering refuses, each with the reason its
// refusal names: NULL for any other name.
static __attribute__((unused)) const char* cbuiltin_library_refusal(const char* name, uint32_t length)
{
    static const struct
    {
        const char* name;
        const char* reason;
    } refusals[] =
    {
        { "__builtin_alloca_uninitialized", "__builtin_alloca_uninitialized" },
        { "__builtin_alloca_with_align", "__builtin_alloca_with_align, whose alignment Tier 0's allocation doesn't take" },
        { "__builtin_alloca_with_align_uninitialized", "__builtin_alloca_with_align_uninitialized, whose alignment Tier 0's allocation doesn't take" },
        { "__builtin_nans", "__builtin_nans, a signaling NaN that clang folds and no library function makes" },
        { "__builtin_nansf", "__builtin_nansf, a signaling NaN that clang folds and no library function makes" },
        { "__builtin_nansl", "__builtin_nansl, a signaling NaN that clang folds and no library function makes" },
        { "__builtin_nansf128", "__builtin_nansf128, a signaling NaN that clang folds and no library function makes" },
        { "__builtin_nanf128", "__builtin_nanf128, a _Float128 NaN that clang folds" },
        { "__builtin_nanf16", "__builtin_nanf16, a _Float16 NaN that clang folds and no library function makes" },
        { "__builtin_nansf16", "__builtin_nansf16, a signaling NaN that clang folds and no library function makes" },
    };
    const char* result = NULL;
    for (uint32_t i = 0; name != NULL && result == NULL && i < sizeof(refusals) / sizeof(refusals[0]); i += 1)
    {
        result = length == strlen(refusals[i].name) && memcmp(name, refusals[i].name, length) == 0 ? refusals[i].reason : NULL;
    }
    return result;
}

// The plain library builtins (c_builtin_library_plain.inc): each name, its length and whether it is a builtin of GNU's
// modes alone, in the names' order.
typedef struct CBuiltinLibraryPlain CBuiltinLibraryPlain;
struct CBuiltinLibraryPlain
{
    const char* name;
    uint32_t length;
    bool gnu;
};

static const CBuiltinLibraryPlain cbuiltin_library_plains[] __attribute__((unused)) =
{
#define BUSTER_CBUILTIN_LIBRARY_PLAIN(name, gnu) { name, sizeof(name) - 1, gnu },
#include "c_builtin_library_plain.inc"
#undef BUSTER_CBUILTIN_LIBRARY_PLAIN
};

#define BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT ((uint32_t)(sizeof(cbuiltin_library_plains) / sizeof(cbuiltin_library_plains[0])))
// The longest listed name, _Block_object_dispose's, bounds every lookup (test-checker checks it).
#define BUSTER_CBUILTIN_LIBRARY_PLAIN_MAX_NAME 21u

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

// A bounded text that a spelling appends to: its bytes so far, `length` of `capacity`, whether it ran out of room, and
// restrict's spelling (C89's __restrict, where restrict is no keyword).
typedef struct CBuiltinLibraryText CBuiltinLibraryText;
struct CBuiltinLibraryText
{
    char* bytes;
    uint32_t length;
    uint32_t capacity;
    bool full;
    const char* restrict_spelling;
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
// "const char *restrict", "_Complex double", a va_list parameter's "struct __va_list_tag *", wchar_t's int, size_t's
// __size_t where `sugared` says (clang's builtins' sugar for it, *sized set) and otherwise unsigned long. False for a
// code the plain rows don't use.
static bool cbuiltin_library_spell_type(const char* encoding, uint32_t length, uint32_t* position, bool sugared, bool* sized, CBuiltinLibraryText* text)
{
    uint32_t at = *position;
    uint32_t longs = 0;
    bool is_unsigned = false;
    bool complex = false;
    while (at < length && (encoding[at] == 'L' || encoding[at] == 'U' || encoding[at] == 'X'))
    {
        longs += encoding[at] == 'L';
        is_unsigned |= encoding[at] == 'U';
        complex |= encoding[at] == 'X';
        at += 1;
    }
    const char code = at < length ? encoding[at] : 0;
    at += at < length;
    const char* base = NULL;
    switch (code)
    {
        case 'v': base = "void"; break;
        case 'c': base = is_unsigned ? "unsigned char" : "char"; break;
        case 'i': base = longs == 2 ? (is_unsigned ? "unsigned long long" : "long long") : longs == 1 ? (is_unsigned ? "unsigned long" : "long") :
                         (is_unsigned ? "unsigned int" : "int"); break;
        case 'f': base = "float"; break;
        case 'd': base = longs == 2 ? "__float128" : longs == 1 ? "long double" : "double"; break;
        case 'z': base = sugared ? "__size_t" : "unsigned long"; break;
        case 'w': base = "int"; break;
        case 'a': base = "struct __va_list_tag *"; break;
        default: break;
    }
    *sized |= code == 'z';
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
    cbuiltin_library_append(text, base != NULL ? base : "");
    while (at < length && (encoding[at] == '*' || encoding[at] == 'C' || encoding[at] == 'R'))
    {
        const bool star = encoding[at] == '*';
        const bool after_star = text->length != 0 && text->bytes[text->length - 1] == '*';
        cbuiltin_library_append(text, star ? (after_star ? "*" : " *") : encoding[at] == 'C' ? "const" : text->restrict_spelling);
        at += 1;
    }
    *position = at;
    return base != NULL;
}

// A plain library builtin's prototype, from its row's encoding and attributes (`r` noreturn), as clang's diagnostics
// spell it: "double (double)", "void *(__size_t)", "int (const char *, ...)", "void (void) __attribute__((noreturn))",
// with __size_t for size_t where `sugared` says and restrict as `c99` says (C89 spells it __restrict), into `buffer` of
// `capacity` bytes, NUL-terminated; *sized says whether it has one (and clang adds the spelling without the sugar:
// "(aka '...')"). False where it doesn't fit, or the encoding has a code the plain rows don't use.
static __attribute__((unused)) bool cbuiltin_library_spell(const char* encoding, uint32_t length, const char* attributes, uint32_t attributes_length,
                                                           bool sugared, bool c99, char* buffer, uint32_t capacity, bool* sized)
{
    CBuiltinLibraryText text = { .bytes = buffer, .capacity = capacity, .full = capacity == 0, .restrict_spelling = c99 ? "restrict" : "__restrict" };
    uint32_t position = 0;
    *sized = false;
    bool valid = cbuiltin_library_spell_type(encoding, length, &position, sugared, sized, &text);
    const bool pointer = text.length != 0 && text.bytes[text.length - 1] == '*';
    cbuiltin_library_append(&text, pointer ? "(" : " (");
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
static __attribute__((unused)) bool cbuiltin_library_source(const uint8_t* name, uint32_t length)
{
    const bool bounded = name != NULL && length <= BUSTER_CBUILTIN_LIBRARY_MAX_NAME;
    const bool result = bounded && (cbuiltin_library_id((const char*)name, length) != CBUILTIN_LIBRARY_COUNT ||
                                    cbuiltin_library_libgcc((const char*)name, length) != BUSTER_CBUILTIN_LIBRARY_LIBGCC);
    return result;
}

#endif
