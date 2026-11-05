// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU noderef's source type sugar (#1447). A null shadow means no annotation anywhere in the type. A nonnull one
// follows the canonical type's pointer/array element, atomic value or function result in `base`; its own `marked`
// bit annotates this level. Thus `int __attribute__((noderef)) *` has a marked base, not a marked pointer. Function
// parameters retain only annotated entries. These immutable arena records contain no TypeIds or member indexes:
// typedef sharing and the deferred reader's replay cannot change their meaning or canonical type identity.

#ifndef BUSTER_CHECK_NODEREF_TYPES
#define BUSTER_CHECK_NODEREF_TYPES 1

typedef struct CheckNoderefType CheckNoderefType;
typedef struct CheckNoderefParameter CheckNoderefParameter;
struct CheckNoderefType
{
    const CheckNoderefType* base;
    const CheckNoderefParameter* parameters;
    bool marked;
};

struct CheckNoderefParameter
{
    const CheckNoderefType* type;
    const CheckNoderefParameter* next;
    uint32_t index;
};

typedef struct CheckNoderefMember CheckNoderefMember;
struct CheckNoderefMember
{
    const CheckNoderefType* type;
    uint32_t member;
    uint32_t token;
};

// Only annotated K&R parameters need a source spelling apart from their binding's identifier-list token.
typedef struct CheckNoderefDeclaration CheckNoderefDeclaration;
struct CheckNoderefDeclaration
{
    const CheckNoderefDeclaration* next;
    uint32_t declaration;
    uint32_t token;
};

// Initializer warnings need only one bit per source graph value, plus the original destination's source shape.
// No expression-only shadow pointer survives the graph warning pass.
typedef struct CheckNoderefInitializer CheckNoderefInitializer;
struct CheckNoderefInitializer
{
    const uint64_t* values;
    const CheckNoderefType* root;
    const CheckNoderefMember* members;
    uint32_t member_count;
};

static const CheckNoderefType* check_noderef_member(const CheckNoderefMember* members, uint32_t count, uint32_t member)
{
    uint32_t first = 0;
    uint32_t end = count;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        if (members[middle].member < member)
        {
            first = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    return first < count && members[first].member == member ? members[first].type : NULL;
}

#endif
