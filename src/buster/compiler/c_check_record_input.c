// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Anonymous members introduce their named members into the containing record's namespace. Validate that expanded
// namespace with an explicit stack and a bounded hash table before layout; named aggregate fields do not expand.
// The table starts from the direct member count and grows only for introduced names. The token bound limits
// anonymous expansion depth, not clearing: named lexical nesting must not pay for every descendant's tokens.

static bool check_input_record_names_grow(CheckInputReader* reader, uint32_t** identifiers, uint64_t* capacity)
{
    // At most UINT32_MAX nonzero identifier IDs can be distinct. With growth at half occupancy, capacities and
    // allocation products fit in uint64_t; arena_push_array still checks its byte multiplication and available room.
    const uint64_t next_capacity = *capacity * 2;
    uint32_t* next = arena_push_array(reader->scratch, next_capacity, sizeof(uint32_t), 4);
    const bool valid = next != 0;
    reader->output->resource |= !valid;
    if (valid)
    {
        memset(next, 0, next_capacity * sizeof(uint32_t));
        for (uint64_t i = 0; i < *capacity; i += 1)
        {
            const uint32_t name = (*identifiers)[i];
            if (name != 0)
            {
                uint64_t slot = (uint64_t)name * UINT64_C(11400714819323198485) & (next_capacity - 1);
                while (next[slot] != 0)
                {
                    slot = (slot + 1) & (next_capacity - 1);
                }
                next[slot] = name;
            }
        }
        *identifiers = next;
        *capacity = next_capacity;
    }
    return valid;
}

static bool check_input_record_names(CheckInputReader* reader, const TypeMember* members, uint32_t count, uint32_t bound, uint32_t* failed_member)
{
    typedef struct CheckRecordNameFrame CheckRecordNameFrame;
    struct CheckRecordNameFrame
    {
        uint32_t next;
        uint32_t end;
        uint32_t root_member;
        bool root;
    };
    const uint64_t mark = reader->scratch->length;
    uint64_t capacity = 1;
    while (capacity < (uint64_t)count * 2 + 1)
    {
        capacity *= 2;
    }
    uint32_t* identifiers = arena_push_array(reader->scratch, capacity, sizeof(uint32_t), 4);
    CheckRecordNameFrame root = { .end = count, .root = true };
    CheckRecordNameFrame* stack = &root;
    uint64_t stack_capacity = 1;
    bool valid = identifiers != 0;
    reader->output->resource |= !valid;
    *failed_member = BUSTER_CHECK_NONE;
    if (valid)
    {
        memset(identifiers, 0, capacity * sizeof(uint32_t));
        uint64_t depth = 1;
        uint64_t names = 0;
        while (valid && depth != 0)
        {
            CheckRecordNameFrame* frame = &stack[depth - 1];
            if (frame->next == frame->end)
            {
                depth -= 1;
            }
            else
            {
                const uint32_t index = frame->next++;
                const uint32_t root_member = frame->root ? index : frame->root_member;
                const uint32_t name = frame->root ? members[index].name : reader->types->members.name[index];
                const TypeId type = frame->root ? members[index].type : reader->types->members.type[index];
                const bool bit_field = frame->root ? members[index].bit_width != BUSTER_TYPE_NOT_BIT_FIELD :
                                       (reader->types->members.flags[index] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
                if (name != 0)
                {
                    uint64_t slot = (uint64_t)name * UINT64_C(11400714819323198485) & (capacity - 1);
                    while (identifiers[slot] != 0 && identifiers[slot] != name)
                    {
                        slot = (slot + 1) & (capacity - 1);
                    }
                    valid = identifiers[slot] == 0;
                    if (valid && names == capacity / 2)
                    {
                        valid = check_input_record_names_grow(reader, &identifiers, &capacity);
                        slot = (uint64_t)name * UINT64_C(11400714819323198485) & (capacity - 1);
                        while (valid && identifiers[slot] != 0)
                        {
                            slot = (slot + 1) & (capacity - 1);
                        }
                    }
                    if (valid)
                    {
                        identifiers[slot] = name;
                        names += 1;
                    }
                }
                else if (!bit_field)
                {
                    const TypeId record = type_strip_aligned(reader->types, type);
                    const uint8_t kind = type_kind(reader->types, record);
                    valid = (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && type_is_complete(reader->types, record) && depth <= bound;
                    if (valid && depth == stack_capacity)
                    {
                        // Old generations remain scratch-owned until this validation ends; geometric growth bounds
                        // their total storage and copying by actual anonymous depth, even for a huge token bound.
                        const uint64_t next_capacity = stack_capacity * 2 < (uint64_t)bound + 1 ? stack_capacity * 2 : (uint64_t)bound + 1;
                        CheckRecordNameFrame* next = arena_push_array(reader->scratch, next_capacity, sizeof(CheckRecordNameFrame), 4);
                        valid = next != 0;
                        reader->output->resource |= !valid;
                        if (valid)
                        {
                            memcpy(next, stack, depth * sizeof(CheckRecordNameFrame));
                            stack = next;
                            stack_capacity = next_capacity;
                        }
                    }
                    if (valid)
                    {
                        const uint32_t tag = reader->types->types.index[type_index(record)];
                        const uint32_t first = reader->types->tags.member_begin[tag];
                        stack[depth++] = (CheckRecordNameFrame){ .next = first, .end = first + reader->types->tags.member_count[tag], .root_member = root_member };
                    }
                }
                if (!valid)
                {
                    *failed_member = root_member;
                }
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}
