// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU noderef's source type sugar (#1447). A null shadow means no annotation anywhere in the type. A nonnull one
// follows the canonical type's pointer/array element, atomic value or function result in `base`; its own `marked`
// bit annotates this level. Thus `int __attribute__((noderef)) *` has a marked base, not a marked pointer. Function
// parameters retain only annotated entries. These immutable arena records contain no TypeIds or member indexes:
// typedef sharing and the deferred reader's replay cannot change their meaning or canonical type identity.

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
