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
static const CBuiltinSourceName cbuiltin_source_leaves[] = {
#include "c_builtin_x86_leaves.inc"
};
#undef BUSTER_CBUILTIN_X86_LEAF
#undef BUSTER_CBUILTIN_SOURCE_LEAF

// The source checker deliberately accepts only this frozen workload's intrinsic declarations. A lowering row
// outside it is not a callable source builtin. Reuse its exact sorted name inventory, without importing its types.
#define BUSTER_BUILTIN_METADATA(n, t, a, f, h, l, s, line, ordinal, custom, evaluable, kind, constraints) { n, sizeof(n) - 1 },
static const CBuiltinSourceName cbuiltin_source_intrinsic_scope[] = {
#include "generated/clang_builtin_own_main_23_1_1.inc"
};
#undef BUSTER_BUILTIN_METADATA

#define BUSTER_CBUILTIN_IMMEDIATE_NAME(n) { n, sizeof(n) - 1 },
static const CBuiltinSourceName cbuiltin_source_immediate_scope[] = {
#include "c_builtin_immediate_scope.inc"
};
#undef BUSTER_CBUILTIN_IMMEDIATE_NAME

static __attribute__((unused)) bool cbuiltin_source_intrinsic(const uint8_t* name, uint32_t length)
{
    bool scoped = false;
    bool result = false;
    uint32_t low = 0;
    uint32_t high = sizeof(cbuiltin_source_intrinsic_scope) / sizeof(cbuiltin_source_intrinsic_scope[0]);
    while (name != NULL && length != 0 && length < 128 && low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const CBuiltinSourceName* entry = cbuiltin_source_intrinsic_scope + middle;
        const uint32_t common = length < entry->length ? length : entry->length;
        const int bytes = memcmp(name, entry->name, common);
        const int compared = bytes != 0 ? bytes : (length > entry->length) - (length < entry->length);
        if (compared == 0)
        {
            scoped = true;
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
    const bool immediate_candidate = !scoped && name != NULL && (length == 21 || length == 24 || length == 25) && memcmp(name, "__builtin_ia32_", 15) == 0;
    for (uint32_t i = 0; immediate_candidate && !scoped && i < sizeof(cbuiltin_source_immediate_scope) / sizeof(cbuiltin_source_immediate_scope[0]); i += 1)
    {
        const CBuiltinSourceName* entry = cbuiltin_source_immediate_scope + i;
        scoped = length == entry->length && memcmp(name, entry->name, length) == 0;
    }
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
