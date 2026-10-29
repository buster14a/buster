// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Exact fortified libc calls whose ordinary prototypes come from the existing Clang metadata. Recognition alone
// grants no capability to other generated records. No prefix/suffix is sufficient; the full spelling must match.
// The indices identify the size bound, a sufficient write-length bound (NOT strncat's source limit), and printf's
// security flag. UINT8_MAX means absent. Names are bounded slices, with no terminator or readable padding required.
#ifndef BUSTER_C_BUILTIN_FORTIFY_INCLUDED
#define BUSTER_C_BUILTIN_FORTIFY_INCLUDED

#include <stdint.h>
#include <string.h>

typedef enum CBuiltinFortifyId
{
    CBUILTIN_FORTIFY_MEMCPY,
    CBUILTIN_FORTIFY_MEMMOVE,
    CBUILTIN_FORTIFY_MEMPCPY,
    CBUILTIN_FORTIFY_MEMSET,
    CBUILTIN_FORTIFY_STRCPY,
    CBUILTIN_FORTIFY_STPCPY,
    CBUILTIN_FORTIFY_STRNCPY,
    CBUILTIN_FORTIFY_STPNCPY,
    CBUILTIN_FORTIFY_STRCAT,
    CBUILTIN_FORTIFY_STRNCAT,
    CBUILTIN_FORTIFY_SPRINTF,
    CBUILTIN_FORTIFY_SNPRINTF,
    CBUILTIN_FORTIFY_VSPRINTF,
    CBUILTIN_FORTIFY_VSNPRINTF,
    CBUILTIN_FORTIFY_COUNT,
} CBuiltinFortifyId;

typedef struct CBuiltinFortify CBuiltinFortify;
struct CBuiltinFortify
{
    const char* name;
    uint32_t name_bytes;
    uint8_t bound;
    uint8_t length;
    uint8_t flag;
};

static const CBuiltinFortify cbuiltin_fortify_calls[CBUILTIN_FORTIFY_COUNT] =
{
    { "__builtin___memcpy_chk", 22, 3, 2, UINT8_MAX },
    { "__builtin___memmove_chk", 23, 3, 2, UINT8_MAX },
    { "__builtin___mempcpy_chk", 23, 3, 2, UINT8_MAX },
    { "__builtin___memset_chk", 22, 3, 2, UINT8_MAX },
    { "__builtin___strcpy_chk", 22, 2, UINT8_MAX, UINT8_MAX },
    { "__builtin___stpcpy_chk", 22, 2, UINT8_MAX, UINT8_MAX },
    { "__builtin___strncpy_chk", 23, 3, 2, UINT8_MAX },
    { "__builtin___stpncpy_chk", 23, 3, 2, UINT8_MAX },
    { "__builtin___strcat_chk", 22, 2, UINT8_MAX, UINT8_MAX },
    { "__builtin___strncat_chk", 23, 3, UINT8_MAX, UINT8_MAX },
    { "__builtin___sprintf_chk", 23, 2, UINT8_MAX, 1 },
    { "__builtin___snprintf_chk", 24, 3, 1, 2 },
    { "__builtin___vsprintf_chk", 24, 2, UINT8_MAX, 1 },
    { "__builtin___vsnprintf_chk", 25, 3, 1, 2 },
};

static __attribute__((unused)) CBuiltinFortifyId cbuiltin_fortify_id(const char* name, uint32_t bytes)
{
    CBuiltinFortifyId result = CBUILTIN_FORTIFY_COUNT;
    // This is a cold dispatch after the existing batched metadata lookup, not a second general name lookup.
    if (name != NULL && bytes >= 12 && memcmp(name, "__builtin___", 12) == 0)
    {
        for (uint32_t i = 0; i < CBUILTIN_FORTIFY_COUNT && result == CBUILTIN_FORTIFY_COUNT; i += 1)
        {
            const CBuiltinFortify* call = cbuiltin_fortify_calls + i;
            if (bytes == call->name_bytes && memcmp(name, call->name, bytes) == 0)
            {
                result = (CBuiltinFortifyId)i;
            }
        }
    }
    return result;
}

#endif
