// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Exceptional source facts only: literal-warning identities and records that contain flexible subobjects. Empty
// inputs allocate nothing. A lane may borrow a frozen table for lookups; its first insertion makes a private copy.

typedef struct CheckWarningSet CheckWarningSet;
struct CheckWarningSet
{
    Arena* arena;
    uint64_t* keys;
    uint32_t capacity;
    uint32_t count;
    bool borrowed;
};

static uint32_t check_warning_set_slot(const uint64_t* keys, uint32_t capacity, uint64_t key)
{
    const uint64_t hash = key * UINT64_C(11400714819323198485);
    uint32_t slot = (uint32_t)(hash ^ (hash >> 32)) & (capacity - 1);
    for (uint32_t visited = 0; visited < capacity && keys[slot] != 0 && keys[slot] != key; visited += 1)
    {
        slot = (slot + 1) & (capacity - 1);
    }
    return slot;
}

static bool check_warning_set_contains(const CheckWarningSet* set, uint64_t key)
{
    const bool present = key != 0 && set->count != 0 && set->keys[check_warning_set_slot(set->keys, set->capacity, key)] == key;
    return present;
}

static bool check_warning_set_add(CheckWarningSet* set, uint64_t key)
{
    bool valid = key != 0;
    const bool present = valid && check_warning_set_contains(set, key);
    if (valid && !present && (set->borrowed || set->count >= set->capacity / 2))
    {
        const uint32_t capacity = set->capacity == 0 ? 16 : set->count < set->capacity / 2 ? set->capacity :
                                  set->capacity <= UINT32_MAX / 2 ? set->capacity * 2 : 0;
        uint64_t* keys = capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(set->arena, uint64_t, capacity) : NULL;
        valid = keys != NULL;
        if (valid)
        {
            memset(keys, 0, (uint64_t)capacity * sizeof(*keys));
            for (uint32_t i = 0; i < set->capacity; i += 1)
            {
                const uint64_t old = set->keys[i];
                if (old != 0)
                {
                    keys[check_warning_set_slot(keys, capacity, old)] = old;
                }
            }
            set->keys = keys;
            set->capacity = capacity;
            set->borrowed = false;
        }
    }
    if (valid && !present)
    {
        set->keys[check_warning_set_slot(set->keys, set->capacity, key)] = key;
        set->count += 1;
    }
    return valid;
}
