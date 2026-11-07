// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Lifetime planning against independent ancestor walks. The small forests permute token IDs independently of
// postorder; the deep fixtures use closed-form expectations without a quadratic reference walk.

static uint64_t check_test_lifetime_ancestor_room(const Parser* parser, const CheckInput* input, uint32_t* first, uint32_t* next)
{
    const uint32_t count = input->token_count;
    uint64_t room = 0;
    memset(first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
    for (uint32_t token = count; token-- > 0;)
    {
        uint32_t block = check_program_lifetime_local(input, token) ? parser->tree.parents[token] : BUSTER_CHECK_NONE;
        while (block < count && parser->tree.roles[block] != PARSE_ROLE_CLOSE_BLOCK && parser->tree.roles[block] != PARSE_ROLE_CLOSE_BODY)
        {
            block = parser->tree.parents[block];
        }
        if (block < count && parser->tree.roles[block] == PARSE_ROLE_CLOSE_BLOCK)
        {
            room += first[block] == BUSTER_CHECK_NONE ? 2 : 0;
            next[token] = first[block];
            first[block] = token;
        }
    }
    for (uint32_t token = 0; token < count; token += 1)
    {
        uint32_t ancestor = parser->tree.roles[token] == PARSE_ROLE_RETURN ? parser->tree.parents[token] : count;
        while (ancestor < count && parser->tree.roles[ancestor] != PARSE_ROLE_CLOSE_BODY)
        {
            room += parser->tree.roles[ancestor] == PARSE_ROLE_CLOSE_BLOCK && first[ancestor] != BUSTER_CHECK_NONE;
            ancestor = parser->tree.parents[ancestor];
        }
    }
    return room;
}

static void check_test_program_lifetime_case(CheckTest* test, Arena* arena, uint32_t count, uint32_t shape)
{
    const uint64_t saved = arena->length;
    const uint64_t slots = (uint64_t)count + 1;
    uint32_t* columns = arena_push_array(arena, slots * 9, sizeof(uint32_t), _Alignof(uint32_t));
    uint8_t* bytes = arena_push_array(arena, slots * 3, sizeof(uint8_t), _Alignof(uint8_t));
    bool ready = columns != NULL && bytes != NULL;
    check_test_expect(test, ready, "lifetime planning fixture memory");
    if (ready)
    {
        uint32_t* parents = columns;
        uint32_t* postorder = parents + slots;
        uint32_t* declarations = postorder + slots;
        uint32_t* bodies = declarations + slots;
        uint32_t* first = bodies + slots;
        uint32_t* next = first + slots;
        uint32_t* work = next + slots;
        uint32_t* expected_first = work + slots;
        uint32_t* expected_next = expected_first + slots;
        uint8_t* roles = bytes;
        uint8_t* kinds = roles + slots;
        uint8_t* bits = kinds + slots;
        memset(columns, 0xff, slots * 9 * sizeof(uint32_t));
        memset(bytes, 0, slots * 3);
        const bool deep = shape >= 3;
        const uint32_t depth = deep ? (count - 1) / 3 : 0;
        for (uint32_t p = 0; p < count; p += 1)
        {
            const uint32_t token = shape == 0 ? p : shape == 1 ? count - 1 - p : shape == 2 ? (p + count / 2) % count : p;
            postorder[p] = token;
        }
        for (uint32_t p = 0; p < count; p += 1)
        {
            const uint32_t token = postorder[p];
            const uint32_t parent_position = p + 1 < count ? p + 1 + (p * 17u + shape * 11u) % (count - p - 1) : count;
            parents[token] = parent_position < count ? postorder[parent_position] : BUSTER_CHECK_NONE;
            roles[token] = p % 11 == 0 ? PARSE_ROLE_CLOSE_BODY : p % 5 == 0 ? PARSE_ROLE_CLOSE_BLOCK : p % 3 == 0 ? PARSE_ROLE_RETURN : PARSE_ROLE_LEAF_RIGHT;
            declarations[token] = p % 4 == 0 ? token : BUSTER_CHECK_NONE;
            bodies[token] = p % 7 == 0 ? BUSTER_CHECK_NONE : 0;
            kinds[token] = p % 13 == 0 ? C_BINDING_OBJECT + 1 : C_BINDING_OBJECT;
            bits[token] = p % 17 == 0 ? BUSTER_CHECK_BITS_STATIC : 0;
            if (deep)
            {
                const bool body = p == count - 1;
                const uint32_t block = (p / 3) * 3 + 2;
                parents[p] = body ? BUSTER_CHECK_NONE : p % 3 != 2 ? block : p + 3 < count ? p + 3 : count - 1;
                roles[p] = body ? PARSE_ROLE_CLOSE_BODY : p % 3 == 2 ? PARSE_ROLE_CLOSE_BLOCK : p % 3 == 1 ? PARSE_ROLE_RETURN : PARSE_ROLE_LEAF_RIGHT;
                declarations[p] = shape == 3 && p % 3 == 0 && !body ? p : BUSTER_CHECK_NONE;
                bodies[p] = 0;
                kinds[p] = C_BINDING_OBJECT;
                bits[p] = 0;
            }
        }
        const CheckSparseColumn declaration_bodies = check_sparse_from_values(arena, bodies, count);
        ready = check_sparse_present(declaration_bodies);
        check_test_expect(test, ready, "lifetime fixture declaration bodies");
        if (ready)
        {
            Parser parser = { 0 };
            parser.options.tree_only = true;
            parser.tokens.count = count;
            parser.tree.parents = parents;
            parser.tree.roles = roles;
            // The C driver uses tree-only parsing: no retained postorder is available to the lifetime planner.
            parser.postorder = NULL;
            const CheckInput input =
            {
                .token_count = count,
                .declarations = declarations,
                .declaration_bodies = declaration_bodies,
                .declaration_kinds = kinds,
                .declaration_bits = bits,
            };
            memset(next, 0xa5, slots * sizeof(uint32_t));
            memset(expected_next, 0xa5, slots * sizeof(uint32_t));
            memset(work, 0xa5, slots * sizeof(uint32_t));
            const uint64_t room = check_program_lifetime_blocks(&parser, &input, first, next, work);
            const uint64_t expected = deep ? shape == 3 ? (uint64_t)depth * (depth + 1) / 2 + 2 * depth : 0 : check_test_lifetime_ancestor_room(&parser, &input, expected_first, expected_next);
            check_test_expect(test, room == expected, "lifetime control capacity agrees with ancestor walks or the deep-chain closed form");
            bool equal = true;
            if (deep)
            {
                for (uint32_t token = 0; token <= count; token += 1)
                {
                    const uint32_t expected_local = shape == 3 && token < count - 1 && token % 3 == 2 ? token - 2 : BUSTER_CHECK_NONE;
                    equal = equal && first[token] == expected_local;
                    equal = equal && (shape != 3 || token >= count - 1 || token % 3 != 0 || next[token] == BUSTER_CHECK_NONE);
                }
            }
            else
            {
                equal = memcmp(first, expected_first, slots * sizeof(uint32_t)) == 0 && memcmp(next, expected_next, slots * sizeof(uint32_t)) == 0;
            }
            check_test_expect(test, equal, "lifetime declarations keep their containing block and token order");
        }
    }
    arena_pop_to(arena, saved);
}

static void check_test_program_lifetime_planning(CheckTest* test)
{
    Arena arena = arena_reserve(UINT64_C(1) << 25);
    check_test_expect(test, arena.base != NULL, "lifetime planning test arena");
    if (arena.base != NULL)
    {
        for (uint32_t count = 0; count <= 193; count += 1)
        {
            for (uint32_t shape = 0; shape < 3; shape += 1)
            {
                check_test_program_lifetime_case(test, &arena, count, shape);
            }
        }
        check_test_program_lifetime_case(test, &arena, 3 * 16384 + 1, 3);
        check_test_program_lifetime_case(test, &arena, 3 * 16384 + 1, 4);
    }
    arena_release(&arena);
}
