// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Independent signature classes, forced bucket collisions and comparison with the original full-chain oracle.
#ifndef BUSTER_C_CHECK_OVERLOAD_INDEX_TEST_INCLUDED
#define BUSTER_C_CHECK_OVERLOAD_INDEX_TEST_INCLUDED

static void check_test_overload_index(CheckTest* test, TypeTable* table)
{
    Arena arena = arena_reserve(1u << 22);
    CheckOverloadIndex index = { 0 };
    bool ready = check_overload_index_initialize(&index, &arena, 1024, 8);
    check_test_expect(test, ready, "overload declaration index storage");
    if (ready)
    {
        const TypeId integer = BUSTER_TYPE_INT;
        const TypeId aligned = type_aligned(table, integer, 4);
        TypeId plain = type_pointer(table, type_pointer(table, integer));
        TypeId alias = type_pointer(table, type_pointer(table, aligned));
        // A shared type DAG has exponentially many expanded leaves. Preparing each type once bounds the hash work.
        for (uint32_t depth = 0; depth < 32; depth += 1)
        {
            const TypeId p[2] = { plain, plain };
            const TypeId a[2] = { alias, alias };
            plain = type_pointer(table, type_function(table, integer, p, 2, BUSTER_TYPE_FLAG_PROTOTYPE));
            alias = type_pointer(table, type_function(table, integer, a, 2, BUSTER_TYPE_FLAG_PROTOTYPE));
        }
        const TypeId functions[] = {
            type_function(table, integer, &plain, 1, BUSTER_TYPE_FLAG_PROTOTYPE),
            type_function_abi(table, BUSTER_TYPE_DOUBLE, &alias, 1, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_MICROSOFT),
            type_function(table, integer, &alias, 1, BUSTER_TYPE_FLAG_PROTOTYPE | BUSTER_TYPE_FLAG_VARIADIC),
        };
        uint64_t hashes[3] = { 0 };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(functions); i += 1)
        {
            ready &= check_overload_signature_hash(&index, table, functions[i], &arena, hashes + i);
        }
        check_test_expect(test, ready && hashes[0] == hashes[1] && hashes[0] != hashes[2] && index.type_count == table->types.count,
                          "signature hashes ignore nested alignment and outer return/ABI, retain variadic state and memoize the type DAG");
        const uint32_t prepared = index.type_count;
        ready &= check_overload_signature_hash(&index, table, functions[0], &arena, hashes);
        check_test_expect(test, ready && index.type_count == prepared, "unchanged type DAG needs no second preparation");

        CheckOverloadCandidate candidates[1024];
        TypeId functions_by_tag[129];
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(functions_by_tag); i += 1)
        {
            const TypeId tag = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 160000 + i);
            const TypeId pointer = type_pointer(table, tag);
            functions_by_tag[i] = type_function(table, integer, &pointer, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        }
        const CheckOverloadContext context = { .source_position = UINT32_MAX };
        uint32_t longest = 0;
        for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(candidates); i += 1)
        {
            CheckOverloadCandidate candidate = {
                .type = functions_by_tag[i % BUSTER_ARRAY_LENGTH(functions_by_tag)],
                .declaration = i,
                .canonical = i,
                .object = i + 1,
                .previous = i != 0 ? i - 1 : UINT32_MAX,
                .scope = 1,
                .position = i,
                .flags = BUSTER_CHECK_OVERLOADABLE | (i < BUSTER_ARRAY_LENGTH(functions_by_tag) && i % 7 == 0 ? BUSTER_CHECK_OVERLOAD_UNAVAILABLE : 0),
            };
            uint64_t signature = 0;
            ready &= check_overload_signature_hash(&index, table, candidate.type, &arena, &signature);
            const uint64_t key = check_overload_index_key(signature, 1, 1);
            const uint32_t slot = check_overload_index_find(&index, key, 1, 1, false);
            const uint32_t scalar_slot = check_overload_index_find(&index, key, 1, 1, true);
            const uint32_t head = check_overload_index_head(&index, slot);
            const CheckOverloadResult oracle = check_overload_declare(table, candidates, i, candidate.previous, &candidate, &context);
            const CheckOverloadResult result = check_overload_declare_chain(table, candidates, i, head, &candidate, &context, index.lexical_next, index.checked, UINT32_MAX);
            ready &= slot != UINT32_MAX && slot == scalar_slot && result.status == oracle.status && result.candidate == oracle.candidate &&
                     result.canonical == oracle.canonical && result.object == oracle.object && result.flags == oracle.flags;
            if (ready)
            {
                candidate.canonical = result.canonical;
                candidate.object = result.object;
                candidate.flags = result.flags;
                candidates[i] = candidate;
                index.lexical_next[i] = check_overload_index_previous(candidates, candidate.type, head, index.lexical_next);
                index.checked[i] = 1;
                check_overload_index_publish(&index, slot, key, 1, 1, i);
                uint32_t length = 0;
                for (uint32_t c = i; c != UINT32_MAX; c = index.lexical_next[c])
                {
                    length += 1;
                }
                longest = length > longest ? length : longest;
            }
        }
        check_test_expect(test, ready && longest == 1, "distinct signature buckets and repeated declarations agree with full history and retain one type representative");

        // A fingerprint collision still compares exact signatures; an unmarked declaration outside this bucket
        // still rejects a second unmarked signature.
        const CheckOverloadCandidate collision = {
            .type = functions_by_tag[1],
            .declaration = 1024,
            .canonical = 1024,
            .object = 1025,
            .previous = 0,
            .scope = 1,
            .flags = BUSTER_CHECK_OVERLOADABLE,
        };
        index.lexical_next[0] = UINT32_MAX;
        const CheckOverloadResult collided = check_overload_declare_chain(table, candidates, 1024, 0, &collision, &context, index.lexical_next, index.checked, UINT32_MAX);
        check_test_expect(test, collided.status == CHECK_OVERLOAD_OK && collided.candidate == 1024, "colliding fingerprint cannot merge distinct signatures");
        CheckOverloadCandidate unmarked = collision;
        unmarked.flags = 0;
        const CheckOverloadResult duplicate = check_overload_declare_chain(table, candidates, 1024, UINT32_MAX, &unmarked, &context, index.lexical_next, index.checked, 0);
        check_test_expect(test, duplicate.status == CHECK_OVERLOAD_ATTRIBUTE, "unmarked signature summary survives other signature buckets");

        CheckOverloadCandidate ordinary[2] = { candidates[0], candidates[0] };
        ordinary[0].flags = BUSTER_CHECK_OVERLOAD_DEFINED;
        ordinary[0].previous = UINT32_MAX;
        ordinary[1].flags = 0;
        ordinary[1].previous = 0;
        CheckOverloadCandidate definition = ordinary[1];
        definition.flags = BUSTER_CHECK_OVERLOAD_DEFINED;
        definition.declaration = 1024;
        const uint32_t ordinary_next[2] = { UINT32_MAX, 0 };
        const uint8_t unchecked[2] = { 0, 0 };
        const CheckOverloadResult defined = check_overload_declare_chain(table, ordinary, 2, 1, &definition, &context, ordinary_next, unchecked, 0);
        check_test_expect(test, defined.status == CHECK_OVERLOAD_REDEFINITION,
                          "an ordinary snapshot cannot stand for definition state it has not inherited");

        // Two exact-size vector buckets against a guard page, all keys deliberately colliding. Include wraparound
        // and full-table refusal, with independent expected slots rather than only agreement between the twins.
        index.capacity = 2 * BUSTER_CHECK_OVERLOAD_INDEX_GROUP;
        uint64_t* saved_keys = index.keys;
        uint8_t* pages = mmap(NULL, 2 * BUSTER_ARENA_PAGE_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        const bool guarded = pages != MAP_FAILED && mprotect(pages + BUSTER_ARENA_PAGE_BYTES, BUSTER_ARENA_PAGE_BYTES, PROT_NONE) == 0;
        check_test_expect(test, guarded, "signature fingerprint buckets have an exact guarded tail");
        if (guarded)
        {
            index.keys = (uint64_t*)(pages + BUSTER_ARENA_PAGE_BYTES - index.capacity * sizeof(uint64_t));
        }
        memset(index.keys, 0, index.capacity * sizeof(uint64_t));
        bool probes = true;
        for (uint32_t i = 0; i < index.capacity; i += 1)
        {
            const uint64_t key = (uint64_t)(i + 1) * index.capacity | 1;
            const uint32_t vector = check_overload_index_find(&index, key, 2, i, false);
            const uint32_t scalar = check_overload_index_find(&index, key, 2, i, true);
            probes &= vector == i && scalar == vector;
            check_overload_index_publish(&index, vector, key, 2, i, i);
        }
        probes &= check_overload_index_find(&index, 1, 3, 99, false) == UINT32_MAX &&
                  check_overload_index_find(&index, 1, 3, 99, true) == UINT32_MAX;
        for (uint32_t i = 0; i < index.capacity; i += 1)
        {
            const uint64_t key = (uint64_t)(i + 1) * index.capacity | 1;
            probes &= check_overload_index_find(&index, key, 2, i, false) == i && check_overload_index_find(&index, key, 2, i, true) == i;
        }
        check_test_expect(test, probes, "scalar/vector signature probes agree on collisions, distinct scopes and a full table");
        index.keys = saved_keys;
        if (pages != MAP_FAILED)
        {
            munmap(pages, 2 * BUSTER_ARENA_PAGE_BYTES);
        }
    }
    arena_release(&arena);
    Arena empty = { 0 };
    CheckOverloadIndex exhausted = { 0 };
    uint64_t hash = 123;
    check_test_expect(test, !check_overload_index_initialize(&exhausted, &empty, 1, 1) &&
                           !check_overload_signature_hash(&exhausted, table, 0, &empty, &hash) && hash == 123,
                      "signature index and fingerprint allocation failures report exhaustion");
}

#endif
