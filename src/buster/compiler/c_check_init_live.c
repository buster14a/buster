// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Include in c_check_initializer.c after check_initializer_part_bits. Scalar writes use compressed interval
// coverage. An aggregate VALUE also owns an evaluation: covering all its bytes with child writes does not
// discard that evaluation. Only a later whole write of its exact type/offset or a typed containing ancestor
// does so. The source walker represents a union member switch with a whole-union CLEAR.
// No storage scales with the destination's extent. Failure rolls scratch back and publishes no selection;
// success retains only the selected bytes. Ambiguous zero-sized aggregate evaluations remain live because
// type/offset cannot distinguish, for example, two empty members at the same address without a logical path.
#ifndef BUSTER_C_CHECK_INIT_LIVE_INCLUDED
#define BUSTER_C_CHECK_INIT_LIVE_INCLUDED

typedef struct CheckInitLiveEntry CheckInitLiveEntry;
struct CheckInitLiveEntry
{
    uint64_t offset;
    TypeId type;
    uint8_t flags;
};

typedef struct CheckInitLiveMap CheckInitLiveMap;
struct CheckInitLiveMap
{
    CheckInitLiveEntry* entries;
    uint32_t count;
    uint32_t capacity;
};

typedef struct CheckInitLiveFrame CheckInitLiveFrame;
struct CheckInitLiveFrame
{
    TypeId type;
    uint64_t offset;
    uint32_t next;
    bool covered;
    bool entered;
};

typedef struct CheckInitLiveSearch CheckInitLiveSearch;
struct CheckInitLiveSearch
{
    CheckInitLiveMap seen;
    CheckInitLiveFrame* frames;
    uint32_t capacity;
};

static TypeId check_init_live_type(const TypeTable* types, TypeId type)
{
    return type_unqualified(type_strip_aligned(types, type));
}

static uint32_t check_init_live_slot(const CheckInitLiveEntry* entries, uint32_t capacity, TypeId type, uint64_t offset)
{
    uint64_t hash = offset ^ ((uint64_t)type * UINT64_C(0x9e3779b97f4a7c15));
    hash ^= hash >> 32;
    uint32_t slot = (uint32_t)hash & (capacity - 1);
    while (entries[slot].type != 0 && (entries[slot].type != type || entries[slot].offset != offset))
    {
        slot = (slot + 1) & (capacity - 1);
    }
    return slot;
}

static uint8_t check_init_live_lookup(const CheckInitLiveMap* map, TypeId type, uint64_t offset)
{
    uint8_t flags = 0;
    if (map->capacity != 0)
    {
        const uint32_t slot = check_init_live_slot(map->entries, map->capacity, type, offset);
        flags = map->entries[slot].flags;
    }
    return flags;
}

static bool check_init_live_insert(CheckInitLiveMap* map, Arena* scratch, TypeId type, uint64_t offset, uint8_t flags)
{
    bool valid = true;
    const bool present = map->capacity != 0 && map->entries[check_init_live_slot(map->entries, map->capacity, type, offset)].type != 0;
    if (!present && map->count == map->capacity / 2)
    {
        const uint32_t capacity = map->capacity == 0 ? 16 : map->capacity <= UINT32_MAX / 2 ? map->capacity * 2 : 0;
        CheckInitLiveEntry* entries = capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckInitLiveEntry, capacity) : NULL;
        valid = entries != NULL;
        if (valid)
        {
            memset(entries, 0, (uint64_t)capacity * sizeof(*entries));
            for (uint32_t i = 0; i < map->capacity; i += 1)
            {
                const CheckInitLiveEntry entry = map->entries[i];
                if (entry.type != 0)
                {
                    entries[check_init_live_slot(entries, capacity, entry.type, entry.offset)] = entry;
                }
            }
            map->entries = entries;
            map->capacity = capacity;
        }
    }
    if (valid)
    {
        const uint32_t slot = check_init_live_slot(map->entries, map->capacity, type, offset);
        if (map->entries[slot].type == 0)
        {
            map->entries[slot].type = type;
            map->entries[slot].offset = offset;
            map->count += 1;
        }
        map->entries[slot].flags |= flags;
    }
    return valid;
}

static bool check_init_live_push(CheckInitLiveSearch* search, Arena* scratch, uint32_t depth, uint32_t limit, CheckInitLiveFrame frame)
{
    bool valid = depth < limit;
    if (valid && depth == search->capacity)
    {
        const uint32_t grown = search->capacity == 0 ? 16 : search->capacity <= UINT32_MAX / 2 ? search->capacity * 2 : limit;
        const uint32_t capacity = grown < limit ? grown : limit;
        CheckInitLiveFrame* frames = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckInitLiveFrame, capacity);
        valid = frames != NULL;
        if (valid)
        {
            if (depth != 0)
            {
                memcpy(frames, search->frames, (uint64_t)depth * sizeof(*frames));
            }
            search->frames = frames;
            search->capacity = capacity;
        }
    }
    if (valid)
    {
        search->frames[depth] = frame;
    }
    return valid;
}

// Enumerate only typed paths that can contain this positive-sized target. Arrays contribute one computed
// element, physical record members retain anonymous/union identity, and repeated DAG states are memoized.
// A covered path cannot hide an uncovered union alternative: the inherited coverage is part of the key.
static CheckInitStatus check_init_live_aggregate(const TypeTable* types, const CheckInitializer* initialized, const CheckInitPart* part,
                                                const CheckInitLiveMap* whole, Arena* scratch, CheckInitLiveSearch* search, bool* live)
{
    const TypeId target = check_init_live_type(types, part->type);
    const uint64_t size = type_size(types, target);
    *live = true;
    CheckInitStatus status = CHECK_INIT_OK;
    if (size != 0 && check_init_live_lookup(whole, target, part->bit_offset) != 0)
    {
        *live = false;
    }
    else if (size != 0 && whole->count != 0)
    {
        if (search->seen.capacity != 0)
        {
            memset(search->seen.entries, 0, (uint64_t)search->seen.capacity * sizeof(*search->seen.entries));
        }
        search->seen.count = 0;
        uint32_t depth = 0;
        const CheckInitLiveFrame root = {
            .type = check_init_live_type(types, initialized->type),
        };
        bool ready = check_init_live_push(search, scratch, depth, types->types.count, root);
        status = ready ? CHECK_INIT_OK : CHECK_INIT_RESOURCE;
        depth += ready;
        bool found = false;
        bool uncovered = false;
        const uint64_t end = part->bit_offset + size * 8;
        while (status == CHECK_INIT_OK && depth != 0 && !uncovered)
        {
            CheckInitLiveFrame* frame = search->frames + depth - 1;
            if (!frame->entered)
            {
                frame->covered = frame->covered || check_init_live_lookup(whole, frame->type, frame->offset) != 0;
                const uint8_t flag = frame->covered ? 2 : 1;
                if ((check_init_live_lookup(&search->seen, frame->type, frame->offset) & flag) != 0)
                {
                    depth -= 1;
                }
                else if (!check_init_live_insert(&search->seen, scratch, frame->type, frame->offset, flag))
                {
                    status = CHECK_INIT_RESOURCE;
                }
                else
                {
                    frame->entered = true;
                    if (frame->type == target && frame->offset == part->bit_offset)
                    {
                        found = true;
                        uncovered = !frame->covered;
                        depth -= 1;
                    }
                }
            }
            else
            {
                const uint8_t kind = type_kind(types, frame->type);
                TypeId child = 0;
                uint64_t offset = 0;
                if (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ||
                    kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
                {
                    if (frame->next == 0)
                    {
                        frame->next = 1;
                        child = check_init_live_type(types, type_element(types, frame->type));
                        const uint64_t bytes = type_size(types, child);
                        const bool packed = kind == BUSTER_TYPE_KIND_EXT_VECTOR && child == BUSTER_TYPE_BOOL;
                        const uint64_t unit = packed ? 1 : bytes <= UINT64_MAX / 8 ? bytes * 8 : 0;
                        const uint64_t delta = part->bit_offset - frame->offset;
                        const uint64_t index = unit != 0 ? delta / unit : 0;
                        const uint64_t count = types->types.extent[type_index(frame->type)];
                        const bool in_array = kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || index < count;
                        if (unit == 0 || !in_array || end - part->bit_offset > unit - delta % unit)
                        {
                            child = 0;
                        }
                        else
                        {
                            offset = part->bit_offset - delta % unit;
                        }
                    }
                }
                else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
                {
                    const uint32_t tag = types->types.index[type_index(frame->type)];
                    const uint32_t begin = types->tags.member_begin[tag];
                    const uint32_t count = types->tags.member_count[tag];
                    const bool indexed = kind == BUSTER_TYPE_KIND_STRUCT && type_is_complete(types, frame->type);
                    uint32_t limit = count;
                    if (indexed && frame->next == 0)
                    {
                        // Completed structs place physical members in order without overlapping positive-sized
                        // members. Only the last start at or before this target can contain its positive width.
                        // Empty ties before a positive member are skipped; unions still inspect every alternative.
                        const uint64_t relative = part->bit_offset - frame->offset;
                        uint32_t low = 0;
                        uint32_t high = count;
                        while (low < high)
                        {
                            const uint32_t middle = low + (high - low) / 2;
                            const bool before = types->members.offset[begin + middle] <= relative;
                            low = before ? middle + 1 : low;
                            high = before ? high : middle;
                        }
                        frame->next = low != 0 ? low - 1 : count;
                        limit = low;
                    }
                    while (frame->next < limit && child == 0)
                    {
                        const uint32_t member = begin + frame->next;
                        frame->next += 1;
                        const TypeId member_type = check_init_live_type(types, types->members.type[member]);
                        const uint64_t relative = types->members.offset[member];
                        const bool aggregate = check_init_aggregate(types, member_type);
                        if (aggregate && (types->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0 &&
                            relative <= UINT64_MAX - frame->offset)
                        {
                            const uint64_t start = frame->offset + relative;
                            const uint64_t bytes = type_size(types, member_type);
                            const bool flexible = type_kind(types, member_type) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY;
                            if (start <= part->bit_offset && (flexible || (bytes <= UINT64_MAX / 8 && end - start <= bytes * 8)))
                            {
                                child = member_type;
                                offset = start;
                            }
                        }
                    }
                    if (indexed)
                    {
                        frame->next = count;
                    }
                }
                if (child != 0)
                {
                    const CheckInitLiveFrame selected = {
                        .type = child,
                        .offset = offset,
                        .covered = frame->covered,
                    };
                    ready = check_init_live_push(search, scratch, depth, types->types.count, selected);
                    status = ready ? CHECK_INIT_OK : CHECK_INIT_RESOURCE;
                    depth += ready;
                }
                else
                {
                    depth -= 1;
                }
            }
        }
        *live = !found || uncovered;
    }
    return status;
}

static CheckInitStatus check_initializer_live_writes(const TypeTable* types, const CheckInitializer* initialized, Arena* scratch, uint8_t** selected)
{
    const uint64_t mark = scratch != NULL ? scratch->length : 0;
    CheckInitStatus status = types != NULL && initialized != NULL && scratch != NULL && selected != NULL &&
                             initialized->status == CHECK_INIT_OK && type_is_valid(types, initialized->type) &&
                             (initialized->count == 0 || initialized->parts != NULL) ? CHECK_INIT_OK : CHECK_INIT_INVALID;
    const uint32_t count = status == CHECK_INIT_OK ? initialized->count : 0;
    const uint32_t edges = count <= UINT32_MAX / 2 ? count * 2 : 0;
    if (status == CHECK_INIT_OK && count != 0 && edges == 0)
    {
        status = CHECK_INIT_RESOURCE;
    }
    uint8_t* live = status == CHECK_INIT_OK && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    const uint64_t retained = scratch != NULL ? scratch->length : 0;
    CheckInitReference* points = status == CHECK_INIT_OK && edges != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckInitReference, edges) : NULL;
    CheckInitReference* temporary = status == CHECK_INIT_OK && edges != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckInitReference, edges) : NULL;
    uint32_t* positions = status == CHECK_INIT_OK && edges != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, edges) : NULL;
    uint32_t* next = status == CHECK_INIT_OK && edges != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)edges + 1) : NULL;
    if (status == CHECK_INIT_OK && count != 0 && (live == NULL || points == NULL || temporary == NULL || positions == NULL || next == NULL))
    {
        status = CHECK_INIT_RESOURCE;
    }
    bool aggregates = false;
    for (uint32_t p = 0; status == CHECK_INIT_OK && p < count; p += 1)
    {
        const CheckInitPart* part = initialized->parts + p;
        uint64_t bits = 0;
        status = check_initializer_part_bits(types, part, &bits);
        if (status == CHECK_INIT_OK)
        {
            const bool aggregate = check_init_aggregate(types, part->type);
            if (aggregate && part->bit_width != 0)
            {
                status = CHECK_INIT_INVALID;
            }
            points[p * 2] = (CheckInitReference){
                .offset = part->bit_offset,
                .part = p * 2,
            };
            points[p * 2 + 1] = (CheckInitReference){
                .offset = part->bit_offset + bits,
                .part = p * 2 + 1,
            };
            aggregates = aggregates || (part->kind == CHECK_INIT_VALUE && aggregate);
        }
    }
    // A stable pass over a digit that every point shares keeps their order, and so does every pass when the points are
    // in order already, as a single write's two ends are. So only the digits in which some offset differs from the
    // first point's are sorted, and none for points in order: the offsets in one object usually differ in their low two
    // bytes alone, and most initializers are one write. Each pass zeroes its 1 KiB of counts with wide vector stores and
    // reads them back with 4-byte loads, which can't take those stores' data and wait for them to reach the cache (#873),
    // so a pass skipped saves that wait as well as its 256 counts' prefix sum.
    uint64_t varying = 0;
    bool unordered = false;
    for (uint32_t i = 1; status == CHECK_INIT_OK && i < edges; i += 1)
    {
        varying |= points[i].offset ^ points[0].offset;
        unordered |= points[i].offset < points[i - 1].offset;
    }
    const uint64_t sorted_digits = unordered ? varying : 0;
    for (uint32_t shift = 0; status == CHECK_INIT_OK && edges > 1 && shift < 64; shift += 8)
    {
        if (((sorted_digits >> shift) & 255) != 0)
        {
            uint32_t sizes[256] = { 0 };
            for (uint32_t i = 0; i < edges; i += 1)
            {
                sizes[(points[i].offset >> shift) & 255] += 1;
            }
            uint32_t total = 0;
            for (uint32_t b = 0; b < 256; b += 1)
            {
                const uint32_t size = sizes[b];
                sizes[b] = total;
                total += size;
            }
            for (uint32_t i = 0; i < edges; i += 1)
            {
                const uint32_t bucket = (points[i].offset >> shift) & 255;
                temporary[sizes[bucket]++] = points[i];
            }
            CheckInitReference* swap = points;
            points = temporary;
            temporary = swap;
        }
    }
    uint32_t unique = 0;
    for (uint32_t i = 0; status == CHECK_INIT_OK && i < edges; i += 1)
    {
        const CheckInitReference point = points[i];
        if (unique == 0 || points[unique - 1].offset != point.offset)
        {
            points[unique++] = point;
        }
        positions[point.part] = unique - 1;
    }
    for (uint32_t i = 0; status == CHECK_INIT_OK && count != 0 && i <= unique; i += 1)
    {
        next[i] = i;
    }
    CheckInitLiveMap whole = { 0 };
    CheckInitLiveSearch search = { 0 };
    for (uint32_t p = status == CHECK_INIT_OK ? count : 0; status == CHECK_INIT_OK && p-- > 0;)
    {
        const CheckInitPart* part = initialized->parts + p;
        const uint32_t high = positions[p * 2 + 1];
        uint32_t at = check_init_uncovered(next, positions[p * 2]);
        const bool aggregate = check_init_aggregate(types, part->type);
        bool evaluated = at < high;
        if (aggregate && part->kind == CHECK_INIT_VALUE && !evaluated)
        {
            status = check_init_live_aggregate(types, initialized, part, &whole, scratch, &search, &evaluated);
        }
        live[p] = evaluated;
        while (status == CHECK_INIT_OK && at < high)
        {
            next[at] = check_init_uncovered(next, at + 1);
            at = next[at];
        }
        const uint64_t size = aggregate ? type_size(types, part->type) : 0;
        if (status == CHECK_INIT_OK && aggregates && aggregate && (part->kind == CHECK_INIT_CLEAR || part->kind == CHECK_INIT_VALUE) &&
            part->bit_width == 0 && size <= UINT64_MAX / 8 && points[high].offset - part->bit_offset == size * 8)
        {
            const bool ready = check_init_live_insert(&whole, scratch, check_init_live_type(types, part->type), part->bit_offset, 1);
            status = ready ? CHECK_INIT_OK : CHECK_INIT_RESOURCE;
        }
    }
    if (selected != NULL)
    {
        *selected = status == CHECK_INIT_OK ? live : NULL;
    }
    if (scratch != NULL)
    {
        arena_pop_to(scratch, status == CHECK_INIT_OK ? retained : mark);
    }
    return status;
}
#endif
