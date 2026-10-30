// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The actual x86 rows/leaves, not Clang's recognition catalogue. These queries attest a source implementation,
// not permission to execute instructions: target/operand/privilege checks remain the lowering's responsibility.
// Shared with source-level drift tests; all names are bounded slices and need no terminator or readable padding.
#ifndef BUSTER_C_BUILTIN_SOURCE_INTRINSICS_INCLUDED
#define BUSTER_C_BUILTIN_SOURCE_INTRINSICS_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct CBuiltinSourceName CBuiltinSourceName;
struct CBuiltinSourceName
{
    const char* name;
    uint32_t length;
};

#define BUSTER_CBUILTIN_X86_ROW(name, ...) { name, sizeof(name) - 1 },
static const CBuiltinSourceName cbuiltin_source_rows[] = {
#include "c_builtin_x86_rows.inc"
};
#undef BUSTER_CBUILTIN_X86_ROW

#define BUSTER_CBUILTIN_SOURCE_LEAF(name, ...) { name, sizeof(name) - 1 },
#define BUSTER_CBUILTIN_X86_LEAF(id, ...) BUSTER_CBUILTIN_SOURCE_LEAF(__VA_ARGS__)
#define BUSTER_CBUILTIN_AMX(id, builtin, ...) { "__builtin_ia32_" builtin "_internal", sizeof("__builtin_ia32_" builtin "_internal") - 1 },
static const CBuiltinSourceName cbuiltin_source_leaves[] = {
#include "c_builtin_x86_leaves.inc"
#include "c_builtin_amx_leaves.inc"
};
#undef BUSTER_CBUILTIN_AMX
#undef BUSTER_CBUILTIN_X86_LEAF
#undef BUSTER_CBUILTIN_SOURCE_LEAF

// A name of the source scopes below, with what clang 23.1.1's metadata requires of a unit for the name to be a builtin
// there (#148): its language modes' token (ALL_LANGUAGES, C23_LANG, ...) and its target features' expression
// (BuiltinTargetFeatures.h's grammar: ',' and, '|' or, parentheses), which the preprocessor's __has_builtin evaluates
// against the unit's target, as clang's does.
typedef struct CBuiltinSourceScope CBuiltinSourceScope;
struct CBuiltinSourceScope
{
    const char* name;
    uint32_t length;
    const char* features;
    const char* languages;
};

// The source checker accepts this frozen workload's intrinsic declarations and separately attested additions below.
// A lowering row outside those scopes is not a callable source builtin. Reuse the sorted inventory, without its types.
#define BUSTER_BUILTIN_METADATA(n, t, a, f, h, l, s, line, ordinal, custom, evaluable, kind, constraints) { n, sizeof(n) - 1, f, l },
static const CBuiltinSourceScope cbuiltin_source_intrinsic_scope[] = {
#include "generated/clang_builtin_own_main_23_1_1.inc"
};
#undef BUSTER_BUILTIN_METADATA

#define BUSTER_CBUILTIN_IMMEDIATE_NAME(n, f) { n, sizeof(n) - 1, f, "ALL_LANGUAGES" },
static const CBuiltinSourceScope cbuiltin_source_immediate_scope[] = {
#include "c_builtin_immediate_scope.inc"
};
#undef BUSTER_CBUILTIN_IMMEDIATE_NAME

// The scopes' row of a name, or null: the sorted inventory's by a binary search, then the five immediate names'.
static __attribute__((unused)) const CBuiltinSourceScope* cbuiltin_source_scope(const uint8_t* name, uint32_t length)
{
    const CBuiltinSourceScope* result = NULL;
    uint32_t low = 0;
    uint32_t high = sizeof(cbuiltin_source_intrinsic_scope) / sizeof(cbuiltin_source_intrinsic_scope[0]);
    while (name != NULL && length != 0 && length < 128 && low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const CBuiltinSourceScope* entry = cbuiltin_source_intrinsic_scope + middle;
        const uint32_t common = length < entry->length ? length : entry->length;
        const int bytes = memcmp(name, entry->name, common);
        const int compared = bytes != 0 ? bytes : (length > entry->length) - (length < entry->length);
        if (compared == 0)
        {
            result = entry;
            low = high;
        }
        else if (compared < 0)
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }
    const bool immediate_candidate = result == NULL && name != NULL && (length == 21 || length == 24 || length == 25) && memcmp(name, "__builtin_ia32_", 15) == 0;
    for (uint32_t i = 0; immediate_candidate && result == NULL && i < sizeof(cbuiltin_source_immediate_scope) / sizeof(cbuiltin_source_immediate_scope[0]); i += 1)
    {
        const CBuiltinSourceScope* entry = cbuiltin_source_immediate_scope + i;
        result = length == entry->length && memcmp(name, entry->name, length) == 0 ? entry : NULL;
    }
    return result;
}

static __attribute__((unused)) bool cbuiltin_source_intrinsic(const uint8_t* name, uint32_t length)
{
    const bool scoped = cbuiltin_source_scope(name, length) != NULL;
    bool result = false;
    for (uint32_t i = 0; scoped && !result && i < sizeof(cbuiltin_source_rows) / sizeof(cbuiltin_source_rows[0]); i += 1)
    {
        const CBuiltinSourceName* entry = cbuiltin_source_rows + i;
        result = length == entry->length && memcmp(name, entry->name, length) == 0;
    }
    for (uint32_t i = 0; scoped && !result && i < sizeof(cbuiltin_source_leaves) / sizeof(cbuiltin_source_leaves[0]); i += 1)
    {
        const CBuiltinSourceName* entry = cbuiltin_source_leaves + i;
        result = length == entry->length && memcmp(name, entry->name, length) == 0;
    }
    // These two prototype-checked generic intrinsics have dedicated source lowering, not an x86 row or leaf.
    result |= scoped && ((length == sizeof("__builtin_assume") - 1 && memcmp(name, "__builtin_assume", length) == 0) ||
                         (length == sizeof("__builtin_unpredictable") - 1 && memcmp(name, "__builtin_unpredictable", length) == 0));
    return result;
}

#endif
