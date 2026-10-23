// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Function-parameter transparent-union relations. Include after the ordinary merge helpers and pair memo. Each
// distinct (left,right,parameter-context) pair is expanded once into an AND/OR graph. Ordinary derived-type children
// must all match; a mismatched transparent parameter may match any field through an ordinary comparison. Reverse
// edges propagate false answers once. Starting remaining pairs true computes the greatest fixed point even when a
// union field's callback signature refers back to a nominal tag. No input-dependent C recursion is used.
//
// The graph can answer additional queries: old answers are settled, and only newly discovered pairs/edges need work.
// This lets one composite share every nested candidate comparison. A successful OR retains its first compatible
// member/other pair for the ordinary composite builder. Nominal unions outside a function parameter never expand.
// Optional activation positions preserve source order; NULL deliberately requests the final TypeTable relation.

typedef struct TypeTransparentPair TypeTransparentPair;
struct TypeTransparentPair
{
    TypePair pair;
    TypePair selected;
    uint32_t first_parent;
    uint32_t first_child;
    uint32_t children;
    uint32_t remaining;
    bool parameter;
    bool any;
    bool answer;
};

typedef struct TypeTransparentEdge TypeTransparentEdge;
struct TypeTransparentEdge
{
    uint32_t parent;
    uint32_t child;
    uint32_t next;
};

typedef struct TypeTransparentSearch TypeTransparentSearch;
struct TypeTransparentSearch
{
    Arena* arena;
    TypePairMemo ordinary;
    TypePairMemo parameters;
    void* pairs;
    void* edges;
    void* queue;
    uint64_t pair_capacity;
    uint64_t edge_capacity;
    uint64_t queue_capacity;
    uint32_t pair_count;
    uint32_t edge_count;
    const uint32_t* activations;
    uint32_t activation_count;
    uint32_t source_position;
    bool full;
};

static bool type_transparent_active(const TypeTransparentSearch* search, const TypeTable* table, TypeId type)
{
    bool active = type_is_transparent_union(table, type);
    if (active && search->activations != 0)
    {
        const uint32_t tag = table->types.index[type_index(type_strip_aligned(table, type))];
        active = tag < search->activation_count && search->activations[tag] != 0 && search->activations[tag] != UINT32_MAX &&
                 search->activations[tag] <= search->source_position;
    }
    return active;
}

static uint32_t type_transparent_pair(TypeTransparentSearch* search, TypeId left, TypeId right, bool parameter)
{
    TypePairMemo* memo = parameter ? &search->parameters : &search->ordinary;
    memo->arena = search->arena;
    const uint64_t key = type_pair_key(left, right);
    TypeId known = 0;
    if (!type_pair_find(memo, key, &known) && !search->full)
    {
        search->full = search->pair_count == UINT32_MAX ||
                       !type_grow_stack(search->arena, &search->pairs, &search->pair_capacity, (uint64_t)search->pair_count + 1, sizeof(TypeTransparentPair));
        if (!search->full)
        {
            known = search->pair_count + 1;
            search->full = !type_pair_add(memo, key, known);
            if (!search->full)
            {
                const TypePair pair = { .left = left, .right = right };
                ((TypeTransparentPair*)search->pairs)[search->pair_count] = (TypeTransparentPair)
                {
                    .pair = pair,
                    .selected = pair,
                    .first_parent = UINT32_MAX,
                    .parameter = parameter,
                    .answer = true,
                };
                search->pair_count += 1;
            }
        }
    }
    return !search->full ? known - 1 : UINT32_MAX;
}

static void type_transparent_edge(TypeTransparentSearch* search, uint32_t old_count, uint32_t parent, TypeId left, TypeId right, bool parameter)
{
    const uint32_t child = type_transparent_pair(search, left, right, parameter);
    if (!search->full)
    {
        search->full = search->edge_count == UINT32_MAX ||
                       !type_grow_stack(search->arena, &search->edges, &search->edge_capacity, (uint64_t)search->edge_count + 1, sizeof(TypeTransparentEdge));
    }
    if (!search->full)
    {
        TypeTransparentPair* pairs = search->pairs;
        ((TypeTransparentEdge*)search->edges)[search->edge_count] = (TypeTransparentEdge)
        {
            .parent = parent,
            .child = child,
            .next = pairs[child].first_parent,
        };
        if (child >= old_count)
        {
            pairs[child].first_parent = search->edge_count;
        }
        const bool possible = child >= old_count || pairs[child].answer;
        pairs[parent].remaining += possible;
        pairs[parent].answer &= pairs[parent].any || possible;
        pairs[parent].children += 1;
        search->edge_count += 1;
    }
}

static TypeAnswer type_transparent_query(TypeTransparentSearch* search, const TypeTable* table, TypeId left, TypeId right, bool parameter, TypePair* selected)
{
    const uint32_t old_count = search->pair_count;
    const uint32_t root = type_transparent_pair(search, left, right, parameter);
    for (uint32_t index = old_count; !search->full && index < search->pair_count; index += 1)
    {
        const TypeTransparentPair current = ((TypeTransparentPair*)search->pairs)[index];
        const TypeId l = type_strip_aligned(table, current.pair.left);
        const TypeId r = type_strip_aligned(table, current.pair.right);
        bool leaf = true;
        const bool shallow = type_merge_shallow(table, current.pair.left, current.pair.right, &leaf);
        const bool left_union = !shallow && current.parameter && type_transparent_active(search, table, l);
        const bool right_union = !shallow && current.parameter && type_transparent_active(search, table, r);
        ((TypeTransparentPair*)search->pairs)[index].first_child = search->edge_count;
        if (left_union || right_union)
        {
            ((TypeTransparentPair*)search->pairs)[index].any = true;
            for (uint32_t side = 0; !search->full && side < 2; side += 1)
            {
                if ((side == 0 && left_union) || (side == 1 && right_union))
                {
                    const uint32_t tag = table->types.index[type_index(side == 0 ? l : r)];
                    const uint32_t begin = table->tags.member_begin[tag];
                    const uint32_t count = table->tags.member_count[tag];
                    for (uint32_t member = 0; !search->full && member < count; member += 1)
                    {
                        // Function-parameter matching discards the field's top qualifiers, but keeps atomic and
                        // array identity. In particular an array field does not acquire parameter decay here.
                        const TypeId type = type_unqualified(table->members.type[begin + member]);
                        type_transparent_edge(search, old_count, index, side == 0 ? type : current.pair.left,
                                              side == 0 ? current.pair.right : type, false);
                    }
                }
            }
            if (!search->full && ((TypeTransparentPair*)search->pairs)[index].remaining == 0)
            {
                ((TypeTransparentPair*)search->pairs)[index].answer = false;
            }
        }
        else if (!shallow)
        {
            ((TypeTransparentPair*)search->pairs)[index].answer = false;
        }
        else if (!leaf)
        {
            const uint8_t kind = type_kind(table, l);
            const TypeId left_base = type_kind_is_array(kind) ? type_element(table, l) : table->types.base[type_index(l)];
            const TypeId right_base = type_kind_is_array(kind) ? type_element(table, r) : table->types.base[type_index(r)];
            type_transparent_edge(search, old_count, index, left_base, right_base, false);
            if (!search->full && kind == BUSTER_TYPE_KIND_FUNCTION &&
                (table->types.flags[type_index(l)] & table->types.flags[type_index(r)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0)
            {
                uint32_t count = 0;
                const TypeId* left_parameters = type_parameters(table, l, &count);
                const TypeId* right_parameters = type_parameters(table, r, &count);
                for (uint32_t i = 0; !search->full && i < count; i += 1)
                {
                    type_transparent_edge(search, old_count, index, left_parameters[i], right_parameters[i], true);
                }
            }
        }
    }
    if (!search->full)
    {
        search->full = !type_grow_stack(search->arena, &search->queue, &search->queue_capacity,
                                       (uint64_t)search->pair_count - old_count, sizeof(uint32_t));
    }
    if (!search->full)
    {
        TypeTransparentPair* pairs = search->pairs;
        const TypeTransparentEdge* edges = search->edges;
        uint32_t* queue = search->queue;
        uint32_t count = 0;
        for (uint32_t i = old_count; i < search->pair_count; i += 1)
        {
            if (!pairs[i].answer)
            {
                queue[count++] = i;
            }
        }
        for (uint32_t item = 0; item < count; item += 1)
        {
            for (uint32_t edge = pairs[queue[item]].first_parent; edge != UINT32_MAX; edge = edges[edge].next)
            {
                TypeTransparentPair* parent = &pairs[edges[edge].parent];
                if (parent->answer)
                {
                    parent->remaining -= 1;
                    if (!parent->any || parent->remaining == 0)
                    {
                        parent->answer = false;
                        queue[count++] = edges[edge].parent;
                    }
                }
            }
        }
        for (uint32_t i = old_count; i < search->pair_count; i += 1)
        {
            if (pairs[i].any && pairs[i].answer)
            {
                bool found = false;
                for (uint32_t child = 0; !found && child < pairs[i].children; child += 1)
                {
                    const TypeTransparentPair* candidate = &pairs[edges[pairs[i].first_child + child].child];
                    if (candidate->answer)
                    {
                        pairs[i].selected = candidate->pair;
                        found = true;
                    }
                }
            }
        }
        if (selected != 0 && pairs[root].answer)
        {
            *selected = pairs[root].selected;
        }
    }
    const TypeAnswer result = search->full ? TYPE_ANSWER_FULL : ((TypeTransparentPair*)search->pairs)[root].answer ? TYPE_ANSWER_YES : TYPE_ANSWER_NO;
    return result;
}

static bool type_has_transparent_parameters(const TypeTable* table, TypeId left, TypeId right)
{
    left = type_strip_aligned(table, left);
    right = type_strip_aligned(table, right);
    bool present = false;
    if (type_kind(table, left) == BUSTER_TYPE_KIND_FUNCTION && type_kind(table, right) == BUSTER_TYPE_KIND_FUNCTION &&
        (table->types.flags[type_index(left)] & table->types.flags[type_index(right)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0)
    {
        uint32_t count = 0;
        const TypeId* left_parameters = type_parameters(table, left, &count);
        const TypeId* right_parameters = type_parameters(table, right, &count);
        for (uint32_t i = 0; !present && i < count; i += 1)
        {
            present = left_parameters[i] != right_parameters[i] &&
                      (type_is_transparent_union(table, left_parameters[i]) || type_is_transparent_union(table, right_parameters[i]));
        }
    }
    return present;
}
