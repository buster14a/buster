// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Identical live literals share one data atom (#1474). check_data_emit calls this after selection, before planning
// payloads: each record maps to the first equal record, which has already been planned when it is reused. Object
// identities and symbols stay intact, so static relocations and both backends address the same bytes without a
// frontend rewrite. Named arrays and predefined function-name arrays never set mergeable_string.
//
// Keys include the array type, requested alignment and every decoded byte, including embedded and final NULs.
// The existing eight-string hash and radix sort have exact scalar twins. Hash collisions are sorted by the full
// key with an iterative merge sort, never searched pairwise; equal-hash batches therefore remain O(n log n).
// Inputs are borrowed and never changed. canonical is scratch-owned; NULL means every record owns its storage.
// Temporary sorting storage is released before payload planning. All reads stay within each literal's size.

static bool check_data_string_candidate(const TypeTable* types, const CheckDataObject* object,
                                        const SymbolId* object_symbols, uint32_t object_count, const Link* link)
{
    bool valid = object->string_pool && object->mergeable_string && object->initializer_plan == NULL &&
                 object->literal_size != 0 && object->literal_bytes != NULL;
    if (valid)
    {
        CheckDataPlan plan;
        valid = check_data_plan_header(types, object, object_symbols, object_count, link, &plan) == CHECK_DATA_OK;
        if (valid)
        {
            const TypeId base = type_strip_aligned(types, object->type);
            valid = type_kind(types, type_conversion_operand(types, base)) == BUSTER_TYPE_KIND_ARRAY && object->literal_size == plan.size &&
                    (link->symbols.flags[plan.symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_OBJECT;
        }
    }
    return valid;
}

static int check_data_string_compare(const CheckDataObject* objects, uint32_t a, uint32_t b)
{
    const CheckDataObject* left = objects + a;
    const CheckDataObject* right = objects + b;
    int result = (left->type > right->type) - (left->type < right->type);
    if (result == 0)
    {
        result = (left->alignment_log2 > right->alignment_log2) - (left->alignment_log2 < right->alignment_log2);
    }
    if (result == 0)
    {
        result = (left->literal_size > right->literal_size) - (left->literal_size < right->literal_size);
    }
    if (result == 0)
    {
        result = memcmp(left->literal_bytes, right->literal_bytes, left->literal_size);
    }
    return result;
}

// Hash up to BUSTER_LINK_HASH_BATCH records. Long literals use the same scalar hash without narrowing their length.
static void check_data_string_hashes(const CheckDataObject* objects, const uint32_t* indices, uint32_t count, bool scalar, uint64_t* keys)
{
    const uint8_t* pointers[BUSTER_LINK_HASH_BATCH];
    uint32_t lengths[BUSTER_LINK_HASH_BATCH];
    uint64_t hashes[BUSTER_LINK_HASH_BATCH];
    for (uint32_t i = 0; i < count; i += 1)
    {
        const CheckDataObject* object = objects + indices[i];
        pointers[i] = object->literal_bytes;
        lengths[i] = object->literal_size <= UINT32_MAX ? (uint32_t)object->literal_size : 0;
    }
    if (!BUSTER_SCALAR_PATH(scalar))
    {
        link_hash_names(pointers, lengths, count, hashes);
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        const CheckDataObject* object = objects + indices[i];
        if (BUSTER_SCALAR_PATH(scalar) || object->literal_size > UINT32_MAX)
        {
            hashes[i] = link_hash(object->literal_bytes, object->literal_size);
        }
        const uint32_t hash = (uint32_t)(hashes[i] ^ (hashes[i] >> 32)) ^ object->type;
        keys[i] = (uint64_t)hash << 32 | indices[i];
    }
}

// One equal-hash run, sorted by its full key and then by record index. The index tie makes the canonical record first.
static void check_data_string_collision_sort(const CheckDataObject* objects, uint64_t* keys, uint64_t* temporary, uint32_t count)
{
    uint64_t* from = keys;
    uint64_t* to = temporary;
    for (uint64_t width = 1; width < count; width *= 2)
    {
        for (uint64_t start = 0; start < count; start += width * 2)
        {
            const uint64_t middle = start + width < count ? start + width : count;
            const uint64_t end = middle + width < count ? middle + width : count;
            uint64_t left = start;
            uint64_t right = middle;
            for (uint64_t out = start; out < end; out += 1)
            {
                bool take_left = right == end;
                if (left < middle && right < end)
                {
                    const uint32_t a = (uint32_t)from[left];
                    const uint32_t b = (uint32_t)from[right];
                    const int order = check_data_string_compare(objects, a, b);
                    take_left = order < 0 || (order == 0 && a < b);
                }
                to[out] = from[take_left ? left++ : right++];
            }
        }
        uint64_t* swap = from;
        from = to;
        to = swap;
    }
    if (from != keys)
    {
        memcpy(keys, from, (uint64_t)count * sizeof(*keys));
    }
}

static bool check_data_string_pool(const TypeTable* types, const CheckDataObject* objects, uint32_t count,
                                   const SymbolId* object_symbols, uint32_t object_count, const Link* link,
                                   bool scalar, Arena* scratch, uint32_t** canonical)
{
    *canonical = NULL;
    uint32_t strings = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        strings += check_data_string_candidate(types, objects + i, object_symbols, object_count, link);
    }
    bool valid = true;
    if (strings > 1)
    {
        const uint64_t mark = scratch->length;
        uint32_t* mapping = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
        const uint64_t retained = scratch->length;
        uint64_t* keys = mapping != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, strings) : NULL;
        uint64_t* temporary = keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, strings) : NULL;
        valid = temporary != NULL;
        uint32_t indices[BUSTER_LINK_HASH_BATCH];
        uint32_t batch = 0;
        uint32_t written = 0;
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            mapping[i] = i;
            if (check_data_string_candidate(types, objects + i, object_symbols, object_count, link))
            {
                indices[batch++] = i;
                if (batch == BUSTER_LINK_HASH_BATCH)
                {
                    check_data_string_hashes(objects, indices, batch, scalar, keys + written);
                    written += batch;
                    batch = 0;
                }
            }
        }
        if (valid && batch != 0)
        {
            check_data_string_hashes(objects, indices, batch, scalar, keys + written);
        }
        uint64_t* sorted = valid ? c_bindings_sort(keys, temporary, strings, !BUSTER_SCALAR_PATH(scalar), UINT32_MAX) : NULL;
        uint64_t* other = sorted == keys ? temporary : keys;
        for (uint32_t first = 0; valid && first < strings;)
        {
            uint32_t end = first + 1;
            while (end < strings && sorted[end] >> 32 == sorted[first] >> 32)
            {
                end += 1;
            }
            // Identical repeats need a linear comparison, without sorting their contents again.
            uint32_t lowest = (uint32_t)sorted[first];
            bool same = true;
            for (uint32_t i = first + 1; same && i < end; i += 1)
            {
                const uint32_t index = (uint32_t)sorted[i];
                same = check_data_string_compare(objects, lowest, index) == 0;
                lowest = index < lowest ? index : lowest;
            }
            if (!same)
            {
                check_data_string_collision_sort(objects, sorted + first, other + first, end - first);
                lowest = (uint32_t)sorted[first];
            }
            for (uint32_t i = first; i < end; i += 1)
            {
                const uint32_t index = (uint32_t)sorted[i];
                if (!same && check_data_string_compare(objects, lowest, index) != 0)
                {
                    lowest = index;
                }
                mapping[index] = lowest;
            }
            first = end;
        }
        *canonical = valid ? mapping : NULL;
        arena_pop_to(scratch, valid ? retained : mark);
    }
    return valid;
}
