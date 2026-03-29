#pragma once
#include <buster/compiler/intern_table.h>

#include <buster/arena.h>
#include <buster/assertion.h>

#define XXH_IMPLEMENTATION
#define XXH_STATIC_LINKING_ONLY
#define XXH_INLINE_ALL
#include <xxhash/xxhash.h>

BUSTER_GLOBAL_LOCAL constexpr u64 first_arena_multiplier = sizeof(u64);
BUSTER_GLOBAL_LOCAL constexpr u64 second_arena_multiplier = sizeof(u32);
BUSTER_GLOBAL_LOCAL constexpr u64 divisor = first_arena_multiplier + second_arena_multiplier;

STRUCT(Slot)
{
    String8 name;
    u64 hash;
};

STRUCT(SlotIndex)
{
    u32 v;
};

BUSTER_INLINE u64 get_hash_table_allocation_size(InternTable* table)
{
    let byte_size = table->hash_table_arena->position - arena_minimum_position;
    return byte_size;
}

BUSTER_INLINE u32 get_hash_table_capacity(InternTable* table)
{
    let byte_size = get_hash_table_allocation_size(table);
    BUSTER_CHECK(byte_size % divisor == 0);
    return (u32)(byte_size / divisor);
}

BUSTER_INLINE u32* get_slot_index(InternTable* table, u32 index)
{
    let capacity = get_hash_table_capacity(table);
    BUSTER_CHECK(index < capacity);
    let result = (u32*)((u8*)table->hash_table_arena + arena_minimum_position + first_arena_multiplier * capacity);
    return result;
}

BUSTER_INLINE u32 get_slot_count(InternTable* table)
{
    let byte_size = table->slot_arena->position - arena_minimum_position;
    BUSTER_CHECK(byte_size % sizeof(Slot) == 0);
    return (u32)(byte_size / sizeof(Slot));
}

BUSTER_GLOBAL_LOCAL constexpr u64 intern_table_start_element_count = 1 << 10;

BUSTER_GLOBAL_LOCAL void table_allocate(InternTable* table, u32 count)
{
    BUSTER_CHECK(table->hash_table_arena->position == arena_minimum_position);
    u64 allocation_size = (u64)count * divisor;
    let allocation = arena_allocate_bytes(table->hash_table_arena, allocation_size, alignof(u64));
    memset(allocation, 0, allocation_size);
}
 
BUSTER_GLOBAL_LOCAL void table_grow(InternTable* table)
{
    let current_capacity = get_hash_table_capacity(table);
    BUSTER_CHECK(get_hash_table_allocation_size(table) % divisor == 0);
    BUSTER_CHECK(current_capacity == get_hash_table_allocation_size(table) / 12);
    let target_capacity = current_capacity << 1;

    if (current_capacity)
    {
        let scratch = scratch_begin(0, 0);

        let allocation = arena_allocate_bytes(scratch.arena, get_hash_table_allocation_size(table), alignof(u64));
        memcpy(allocation, (u8*)table->hash_table_arena + arena_minimum_position, table->hash_table_arena->position - arena_minimum_position);
        table_allocate(table, target_capacity);

        scratch_end(scratch);
    }
    else
    {
        table_allocate(table, target_capacity);
    }
    // TODO, this is wrong
    bool a = true;
    if (a) BUSTER_TRAP();
}

BUSTER_GLOBAL_LOCAL u64 hash_string(String8 string)
{
    return XXH3_64bits(string.pointer, string.length);
}

BUSTER_F_IMPL InternIndex table_intern(InternTable* restrict table, String8 string)
{
    let candidate_hash = hash_string(string);
    BUSTER_CHECK(candidate_hash != 0);
    let current_capacity = get_hash_table_capacity(table);

    if ((table->hash_table_item_count + 1) * 100 / current_capacity > 70)
    {
        table_grow(table);
    }

    BUSTER_CHECK(is_power_of_two(current_capacity));
    let mask = current_capacity - 1;
    u64* restrict hash_pointer = arena_get_pointer(table->hash_table_arena, u64, arena_minimum_position);
    Slot* slot_pointer = arena_get_pointer(table->slot_arena, Slot, arena_minimum_position);

    u32 index;
    for (index = (u32)candidate_hash & mask;; index = (index + 1) & mask)
    {
        let hash = hash_pointer[index];

        if ((hash == 0) | (hash == candidate_hash))
        {
            break;
        }
    }

    let hash = hash_pointer[index];

    InternIndex result;

    if (hash == 0)
    {
        table->hash_table_item_count += 1;
        hash_pointer[index] = candidate_hash;
        *get_slot_index(table, index) = get_slot_count(table);
        let interned_string = string8_duplicate_arena(table->string_arena, string, false);
        let slot = arena_allocate(table->slot_arena, Slot, 1);
        *slot = {
            .name = interned_string,
            .hash = candidate_hash,
        };
        result = { .v = index };
    }
    else if (hash == candidate_hash)
    {
        let candidate_slot = slot_pointer[*get_slot_index(table, index)];
        if (string8_equal(string, candidate_slot.name))
        {
            BUSTER_TRAP();
        }
        else
        {
            BUSTER_TRAP();
            // for (u32 collision_index = index;; collision_index = (collision_index + 1) & mask)
            // {
            //     let hash = hash_pointer[index];
            //
            //     if ((hash == 0) | (hash == candidate_hash))
            //     {
            //         if (a) BUSTER_TRAP();
            //     }
            // }
        }
    }
    else
    {
        BUSTER_UNREACHABLE();
    }

    return result;
}

BUSTER_F_IMPL InternTable intern_table_create()
{
    InternTable result = {
        .string_arena = arena_create({}),
        .slot_arena = arena_create({}),
        .hash_table_arena = arena_create({}),
    };

    table_allocate(&result, intern_table_start_element_count);
    return result;
}
