// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Declaration-only signature buckets. Lexical scopes and translation-unit linkage have separate keys; immutable
// candidates keep their original source chains. Hash collisions always reach the exact signature/compatibility checks.
// Canonical fingerprints strip ALIGNED at every depth, retain nominal tag identity and exclude only the outer
// function's return type and ABI. Types form an append-only DAG, so each type's fingerprint is prepared once.
#ifndef BUSTER_C_CHECK_OVERLOAD_INDEX_INCLUDED
#define BUSTER_C_CHECK_OVERLOAD_INDEX_INCLUDED

#define BUSTER_CHECK_OVERLOAD_INDEX_GROUP 8u

typedef struct CheckOverloadIndex CheckOverloadIndex;
struct CheckOverloadIndex
{
    uint64_t* keys;
    uint32_t* names;
    uint32_t* scopes;
    uint32_t* heads;
    uint32_t capacity;
    uint32_t* lexical_next;
    uint32_t* linked_next;
    uint32_t* unmarked;
    uint8_t* checked; // The snapshot has checked/inherited the complete overload history, including ordinary declarations.
    uint8_t* active;
    void* type_hashes;
    uint64_t type_capacity;
    uint32_t type_count;
};

static bool check_overload_index_initialize(CheckOverloadIndex* index, Arena* arena, uint32_t candidates, uint32_t names)
{
    uint64_t capacity = BUSTER_CHECK_OVERLOAD_INDEX_GROUP;
    while (capacity < (uint64_t)candidates * 4)
    {
        capacity *= 2;
    }
    bool valid = capacity <= UINT32_MAX;
    if (valid)
    {
        index->capacity = (uint32_t)capacity;
        index->keys = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, capacity);
        index->names = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        index->scopes = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        index->heads = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        index->lexical_next = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)candidates + 1);
        index->linked_next = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)candidates + 1);
        index->unmarked = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)candidates + 1);
        index->checked = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)candidates + 1);
        index->active = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, names);
        valid = index->keys != NULL && index->names != NULL && index->scopes != NULL && index->heads != NULL &&
                index->lexical_next != NULL && index->linked_next != NULL && index->unmarked != NULL && index->checked != NULL && index->active != NULL;
        if (valid)
        {
            memset(index->keys, 0, capacity * sizeof(uint64_t));
            memset(index->active, 0, names);
        }
    }
    return valid;
}

static uint64_t check_overload_type_hash(const CheckOverloadIndex* index, TypeId type)
{
    const uint64_t* hashes = index->type_hashes;
    return type_hash_mix(hashes[type_index(type)], type_qualifiers(type));
}

static bool check_overload_signature_hash(CheckOverloadIndex* index, const TypeTable* table, TypeId function, Arena* arena, uint64_t* signature)
{
    const TypeColumns* types = &table->types;
    const bool valid = type_grow_stack(arena, &index->type_hashes, &index->type_capacity, types->count, sizeof(uint64_t));
    if (valid)
    {
        uint64_t* hashes = index->type_hashes;
        for (uint32_t i = index->type_count; i < types->count; i += 1)
        {
            const uint8_t kind = types->kind[i];
            uint64_t hash = type_hash_finish(i);
            if (kind == BUSTER_TYPE_KIND_ALIGNED)
            {
                hash = hashes[type_index(types->base[i])];
            }
            else if (kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_ATOMIC || kind == BUSTER_TYPE_KIND_FUNCTION)
            {
                hash = type_hash_mix(kind | (uint64_t)type_key_flags(kind, types->flags[i]) << 8, types->extent[i]);
                hash = type_hash_mix(hash, check_overload_type_hash(index, types->base[i]));
                if (kind == BUSTER_TYPE_KIND_FUNCTION)
                {
                    for (uint32_t p = 0; p < types->extent[i]; p += 1)
                    {
                        hash = type_hash_mix(hash, check_overload_type_hash(index, table->parameters.type[types->index[i] + p]));
                    }
                }
            }
            hashes[i] = hash;
        }
        index->type_count = types->count;
        const TypeId type = type_strip_aligned(table, function);
        uint32_t count = 0;
        const TypeId* parameters = type_parameters(table, type, &count);
        uint64_t hash = type_hash_mix(count, types->flags[type_index(type)] & BUSTER_TYPE_FLAG_KEY);
        for (uint32_t p = 0; p < count; p += 1)
        {
            hash = type_hash_mix(hash, check_overload_type_hash(index, type_unqualified(parameters[p])));
        }
        *signature = type_hash_finish(hash);
    }
    return valid;
}

static uint64_t check_overload_index_key(uint64_t signature, uint32_t name, uint32_t scope)
{
    return type_hash_finish(type_hash_mix(signature, (uint64_t)name | (uint64_t)scope << 32)) | 1;
}

// Eight contiguous fingerprints per probe; metadata is read only for matching lanes. The scalar twin uses exactly
// the same buckets and empty-slot order, including a completely full table. Neither path reads past the last bucket.
static uint32_t check_overload_index_find(const CheckOverloadIndex* index, uint64_t key, uint32_t name, uint32_t scope, bool scalar)
{
    uint32_t result = UINT32_MAX;
    const uint32_t mask = index->capacity - 1;
    uint32_t bucket = (uint32_t)key & mask & ~(BUSTER_CHECK_OVERLOAD_INDEX_GROUP - 1);
    for (uint32_t visited = 0; result == UINT32_MAX && visited < index->capacity; visited += BUSTER_CHECK_OVERLOAD_INDEX_GROUP)
    {
        uint32_t matches = 0;
        uint32_t empty = 0;
        if (scalar)
        {
            for (uint32_t lane = 0; lane < BUSTER_CHECK_OVERLOAD_INDEX_GROUP; lane += 1)
            {
                matches |= index->keys[bucket + lane] == key ? 1u << lane : 0;
                empty |= index->keys[bucket + lane] == 0 ? 1u << lane : 0;
            }
        }
        else
        {
            const __m512i keys = _mm512_loadu_si512(index->keys + bucket);
            matches = _mm512_cmpeq_epi64_mask(keys, _mm512_set1_epi64((long long)key));
            empty = _mm512_cmpeq_epi64_mask(keys, _mm512_setzero_si512());
        }
        while (result == UINT32_MAX && matches != 0)
        {
            const uint32_t slot = bucket + (uint32_t)__builtin_ctz(matches);
            matches &= matches - 1;
            if (index->names[slot] == name && index->scopes[slot] == scope)
            {
                result = slot;
            }
        }
        if (result == UINT32_MAX && empty != 0)
        {
            result = bucket + (uint32_t)__builtin_ctz(empty);
        }
        bucket = (bucket + BUSTER_CHECK_OVERLOAD_INDEX_GROUP) & mask;
    }
    return result;
}

static uint32_t check_overload_index_head(const CheckOverloadIndex* index, uint32_t slot)
{
    return slot != UINT32_MAX && index->keys[slot] != 0 ? index->heads[slot] : UINT32_MAX;
}

// Replace an identical type's representative with its newly validated snapshot. Its inherited flags and canonical
// identity summarize the older record; source-time uses still follow the untouched candidates[].previous chain.
static uint32_t check_overload_index_previous(const CheckOverloadCandidate* candidates, TypeId type, uint32_t head, const uint32_t* next)
{
    uint32_t previous = head;
    while (previous != UINT32_MAX && candidates[previous].type == type)
    {
        previous = next[previous];
    }
    return previous;
}

static void check_overload_index_publish(CheckOverloadIndex* index, uint32_t slot, uint64_t key, uint32_t name, uint32_t scope, uint32_t candidate)
{
    index->keys[slot] = key;
    index->names[slot] = name;
    index->scopes[slot] = scope;
    index->heads[slot] = candidate;
}

#endif
