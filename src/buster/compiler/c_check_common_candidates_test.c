// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The shared common-warning candidate masks (#1340), against each consumer's operator predicate. Every operator and
// invalid operator IDs cross the 16-node blocks, with input and exact-capacity output ending at protected pages. Two
// builds into separate storage must agree, unused tail bits stay zero and independent readers keep their order. Included after c_check_common_selection_test.c, whose guarded columns these tests borrow.

// Independent of the production lookup table and scalar classifier: membership is the consuming pass's operator set.
static bool check_test_common_candidate(CheckCommonCandidate kind, CheckOp op)
{
    bool result = false;
    switch (kind)
    {
        case CHECK_COMMON_CANDIDATE_REACH:
        {
            switch (op)
            {
                case CHECK_OP_INITIALIZE:
                case CHECK_OP_ASSIGN:
                case CHECK_OP_CONVERT:
                case CHECK_OP_CALL:
                case CHECK_OP_SUBSCRIPT:
                case CHECK_OP_COMMA:
                case CHECK_OP_NOT:
                case CHECK_OP_LOGICAL_AND:
                case CHECK_OP_LOGICAL_OR:
                case CHECK_OP_CONDITION:
                case CHECK_OP_CONDITIONAL:
                case CHECK_OP_DEREFERENCE:
                case CHECK_OP_DIVIDE:
                case CHECK_OP_REMAINDER:
                case CHECK_OP_SHIFT_LEFT:
                case CHECK_OP_SHIFT_RIGHT:
                case CHECK_OP_DIVIDE_ASSIGN:
                case CHECK_OP_REMAINDER_ASSIGN:
                case CHECK_OP_SHIFT_LEFT_ASSIGN:
                case CHECK_OP_SHIFT_RIGHT_ASSIGN:
                {
                    result = true;
                } break;
                default: break;
            }
        } break;
        case CHECK_COMMON_CANDIDATE_COMPARE:
        {
            switch (op)
            {
                case CHECK_OP_LESS:
                case CHECK_OP_LESS_EQUAL:
                case CHECK_OP_GREATER:
                case CHECK_OP_GREATER_EQUAL:
                case CHECK_OP_EQUAL:
                case CHECK_OP_NOT_EQUAL:
                {
                    result = true;
                } break;
                default: break;
            }
        } break;
        case CHECK_COMMON_CANDIDATE_RETURN:
        {
            result = op == CHECK_OP_ADDRESS || op == CHECK_OP_CONVERT;
        } break;
        case CHECK_COMMON_CANDIDATE_CALL:
        {
            result = op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_X86;
        } break;
        case CHECK_COMMON_CANDIDATE_SELECT:
        {
            result = op == CHECK_OP_SELECT;
        } break;
        case CHECK_COMMON_CANDIDATE_MASK:
        {
            result = op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF || op == CHECK_OP_CONDITIONAL ||
                     op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR;
        } break;
        case CHECK_COMMON_CANDIDATE_DISCARD:
        {
            result = op == CHECK_OP_CAST || op == CHECK_OP_COMMA;
        } break;
        case CHECK_COMMON_CANDIDATE_SHIFT:
        {
            result = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT ||
                     op == CHECK_OP_SHIFT_LEFT_ASSIGN || op == CHECK_OP_SHIFT_RIGHT_ASSIGN;
        } break;
        case CHECK_COMMON_CANDIDATE_COUNT: break;
    }
    return result;
}

// The exact masks, including each last block's unused lanes, and two independent scans of every class. A scalar
// consumer must work without reading the index at all, as its own predicates remain the filtering oracle.
static bool check_test_common_candidate_masks(const CheckOp* ops, const CheckCommonCandidates* candidates)
{
    const uint32_t count = candidates->count;
    const uint32_t blocks = candidates->blocks;
    bool same = true;
    for (uint32_t kind = 0; same && kind < CHECK_COMMON_CANDIDATE_COUNT; kind += 1)
    {
        for (uint32_t block = 0; same && block < blocks; block += 1)
        {
            uint16_t expected = 0;
            const uint32_t first = block * 16;
            for (uint32_t lane = 0; lane < 16 && first + lane < count; lane += 1)
            {
                if (check_test_common_candidate((CheckCommonCandidate)kind, ops[first + lane]))
                {
                    expected |= (uint16_t)(1u << lane);
                }
            }
            same = candidates->masks[(uint64_t)kind * blocks + block] == expected;
        }
        CheckCommonScan readers[2] = { 0 };
        for (uint32_t node = 0; same && node < count; node += 1)
        {
            if (check_test_common_candidate((CheckCommonCandidate)kind, ops[node]))
            {
                for (uint32_t reader = 0; same && reader < BUSTER_ARRAY_LENGTH(readers); reader += 1)
                {
                    same = check_common_next_candidate(candidates, (CheckCommonCandidate)kind, readers + reader) == node;
                }
            }
        }
        for (uint32_t reader = 0; same && reader < BUSTER_ARRAY_LENGTH(readers); reader += 1)
        {
            same = check_common_next_candidate(candidates, (CheckCommonCandidate)kind, readers + reader) == count &&
                   check_common_next_candidate(candidates, (CheckCommonCandidate)kind, readers + reader) == count;
        }
    }
    return same;
}

static bool check_test_common_candidate_case(const CheckOp* ops, uint32_t count)
{
    CheckTestSelectionColumn columns[3] = { 0 };
    const uint32_t blocks = (uint32_t)(((uint64_t)count + 15) / 16);
    const uint64_t bytes = (uint64_t)blocks * CHECK_COMMON_CANDIDATE_COUNT * sizeof(uint16_t);
    CheckOp* guarded = check_test_selection_column(columns, (uint64_t)count * sizeof(CheckOp));
    uint8_t* storage[2] =
    {
        check_test_selection_column(columns + 1, bytes + 16),
        check_test_selection_column(columns + 2, bytes + 16),
    };
    CheckCommonCandidates built[2] = { 0 };
    bool same = guarded != NULL && storage[0] != NULL && storage[1] != NULL;
    if (same && count != 0)
    {
        memcpy(guarded, ops, (uint64_t)count * sizeof(CheckOp));
    }
    const CheckGraph graph =
    {
        .ops = guarded,
        .count = count,
    };
    for (uint32_t path = 0; same && path < 2; path += 1)
    {
        memset(storage[path], 0xa5, bytes + 16);
        Arena scratch =
        {
            .base = storage[path],
            .length = 16,
            .capacity = bytes + 16,
        };
        same = check_common_candidates_build(&graph, &scratch, built + path) &&
               built[path].count == count && built[path].blocks == blocks && scratch.length == bytes + 16 &&
               built[path].masks == (count != 0 ? (uint16_t*)(storage[path] + 16) : NULL);
        if (same)
        {
            same = check_test_common_candidate_masks(guarded, built + path);
        }
        for (uint32_t byte = 0; same && byte < 16; byte += 1)
        {
            same = storage[path][byte] == 0xa5;
        }
    }
    if (same && count != 0)
    {
        same = memcmp(built[0].masks, built[1].masks, bytes) == 0 && memcmp(guarded, ops, (uint64_t)count * sizeof(CheckOp)) == 0;
        // The first readers left the index unchanged: a third read must get every candidate again.
        same = same && check_test_common_candidate_masks(guarded, built);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(columns); i += 1)
    {
        check_test_selection_unmap(columns + i);
    }
    return same;
}

// Multiple comparison batches followed by a partial vector, with no other reach candidates.
static void check_test_common_candidate_comparison_tail(CheckTest* test)
{
    enum { COUNT = 399, BLOCKS = (COUNT + 15) / 16 };
    CheckOp ops[COUNT] = { 0 };
    uint32_t left[COUNT] = { 0 };
    uint32_t right[COUNT] = { 0 };
    uint32_t parents[COUNT];
    uint8_t reach[COUNT] = { 0 };
    CheckValue values[COUNT] = { 0 };
    uint16_t masks[BLOCKS * CHECK_COMMON_CANDIDATE_COUNT] = { 0 };
    for (uint32_t node = 0; node < COUNT; node += 1)
    {
        parents[node] = BUSTER_CHECK_NONE;
        if (node % 3 == 0)
        {
            values[node].type = BUSTER_TYPE_UCHAR;
        }
        else if (node % 3 == 1)
        {
            values[node] = (CheckValue){ .type = BUSTER_TYPE_INT, .flags = BUSTER_CHECK_ICE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = node % 2 != 0 ? 300 : 42 };
        }
        else
        {
            ops[node] = CHECK_OP_GREATER;
            left[node] = node - 2;
            right[node] = node - 1;
            masks[CHECK_COMMON_CANDIDATE_COMPARE * BLOCKS + node / 16] |= (uint16_t)(1u << (node % 16));
        }
    }
    const CheckGraph graph = { .count = COUNT, .ops = ops, .operand0 = left, .operand1 = right };
    const CheckStream stream = { .count = COUNT, .dense_values = values };
    const TypeCapacity capacity = { 0 };
    Arena type_storage = arena_reserve(type_capacity_bytes(capacity));
    Arena type_index = arena_reserve(type_index_capacity_bytes(capacity));
    TypeTable types = { 0 };
    const bool ready = type_initialize(&types, &type_storage, &type_index, capacity);
    const CheckCommonCandidates candidates = { .count = COUNT, .blocks = BLOCKS, .masks = masks };
    if (ready)
    {
        check_common_range_candidates(&graph, &stream, &types, parents, reach, &candidates);
    }
    bool same = ready;
    for (uint32_t node = 2; node < COUNT; node += 3)
    {
        same &= ((reach[node] & BUSTER_CHECK_REACH_SIGN) != 0) == (node % 2 == 0);
    }
    check_test_expect(test, same, "common candidates: range warnings across comparison batches and a partial tail");
    arena_release(&type_index);
    arena_release(&type_storage);
}

static void check_test_common_candidate_exhaustion(CheckTest* test)
{
    CheckOp ops[17];
    for (uint32_t node = 0; node < BUSTER_ARRAY_LENGTH(ops); node += 1)
    {
        ops[node] = CHECK_OP_SHIFT_LEFT;
    }
    const CheckGraph graph =
    {
        .ops = ops,
        .count = BUSTER_ARRAY_LENGTH(ops),
    };
    const uint32_t blocks = 2;
    const uint64_t bytes = (uint64_t)blocks * CHECK_COMMON_CANDIDATE_COUNT * sizeof(uint16_t);
    _Alignas(16) uint8_t storage[16 + 2 * CHECK_COMMON_CANDIDATE_COUNT * sizeof(uint16_t)];
    bool same = true;
    for (uint32_t path = 0; same && path < 2; path += 1)
    {
        for (uint32_t failure = 0; same && failure < 3; failure += 1)
        {
            memset(storage, 0xa5, sizeof(storage));
            Arena scratch =
            {
                .base = storage,
                .length = 16,
                .capacity = sizeof(storage) - (failure == 0),
            };
            check_common_resources = (CheckCommonResources){ .fail_at = failure == 1 ? 0u : UINT32_MAX };
            CheckCommonCandidates candidates = { 0 };
            const bool result = check_common_candidates_build(&graph, &scratch, &candidates);
            same = result == (failure == 2) && candidates.count == graph.count && candidates.blocks == blocks &&
                   scratch.length == (failure == 2 ? 16 + bytes : 16) &&
                   candidates.masks == (failure == 2 ? (uint16_t*)(storage + 16) : NULL);
            for (uint32_t byte = 0; same && failure != 2 && byte < sizeof(storage); byte += 1)
            {
                same = storage[byte] == 0xa5;
            }
            if (same && result)
            {
                same = check_test_common_candidate_masks(ops, &candidates);
            }
        }
        // Even the largest count must fail before reading a graph column if its masks do not fit.
        Arena none = { 0 };
        const CheckGraph huge = { .count = UINT32_MAX };
        CheckCommonCandidates candidates = { 0 };
        check_common_resources = (CheckCommonResources){ .fail_at = UINT32_MAX };
        same &= !check_common_candidates_build(&huge, &none, &candidates) && candidates.masks == NULL &&
                candidates.count == UINT32_MAX && candidates.blocks == (UINT32_C(1) << 28) && none.length == 0;
        const CheckGraph empty = { 0 };
        check_common_resources = (CheckCommonResources){ .fail_at = 0 };
        same &= check_common_candidates_build(&empty, &none, &candidates) && candidates.count == 0 && candidates.blocks == 0 &&
                candidates.masks == NULL && none.length == 0 && check_common_resources.pushes == 0;
    }
    check_test_expect(test, same, "common candidates: exact capacity, injected and real exhaustion, largest count and empty input");
}

static uint32_t check_test_common_scan_draw(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return (uint32_t)(*state >> 32);
}

// The token, operator, reach and unevaluated scans against their definitions (#2924): every item in order
// whose role is one of the set's (and kind, where one is asked), whose operator is in the set, whose reach shares a bit
// with the asked bits (each of the eight, and pairs), whose evaluated byte is zero; then the count, twice. Every length
// to 300, so that a scan's 16- or 64-item blocks end at every lane and its list of 64 items refills, at four densities,
// each column ending at a protected page. The inputs come from their own generator, so that the tests after them draw
// what they drew before.
static void check_test_common_scan_definitions(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* wide = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* second = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool ready = mapping != MAP_FAILED && wide != MAP_FAILED && second != MAP_FAILED && page >= 300 * sizeof(uint32_t);
    ready = ready && mprotect(mapping + page, page, PROT_NONE) == 0 && mprotect(wide + page, page, PROT_NONE) == 0 &&
            mprotect(second + page, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "common scans: guarded columns");
    uint64_t state = UINT64_C(0x2545f4914f6cdd1d);
    static const uint8_t roles[] = { PARSE_ROLE_OPERAND, PARSE_ROLE_TERMINATOR, PARSE_ROLE_PREFIX };
    static const CheckOp listed_ops[] = { CHECK_OP_CALL, CHECK_OP_ASSIGN, CHECK_OP_SHIFT_LEFT };
    const CheckCommonOps set = check_common_ops(listed_ops, BUSTER_ARRAY_LENGTH(listed_ops));
    bool tokens = true;
    bool ops = true;
    bool reaches = true;
    bool unevaluated = true;
    uint32_t found = 0;
    for (uint32_t count = 0; ready && count <= 300; count += 1)
    {
        for (uint32_t density = 0; density < 4; density += 1)
        {
            // One chance in 1, 2, 16 or never that an item is of the asked sort.
            const uint32_t odds = density == 0 ? 1 : density == 1 ? 2 : density == 2 ? 16 : 0;
            uint8_t* role_column = mapping + page - count;
            uint8_t* kind_column = second + page - count;
            CheckOp* op_column = (CheckOp*)(wide + page - (uint64_t)count * sizeof(CheckOp));
            for (uint32_t i = 0; i < count; i += 1)
            {
                const bool asked = odds != 0 && check_test_common_scan_draw(&state) % odds == 0;
                role_column[i] = asked ? roles[check_test_common_scan_draw(&state) % BUSTER_ARRAY_LENGTH(roles)] : PARSE_ROLE_MEMBER;
                kind_column[i] = check_test_common_scan_draw(&state) % 2 == 0 ? PARSE_KIND_SEMI : PARSE_KIND_IDENTIFIER;
                op_column[i] = asked ? listed_ops[check_test_common_scan_draw(&state) % BUSTER_ARRAY_LENGTH(listed_ops)] : CHECK_OP_VALUE;
            }
            const Parser parser = { .tokens = { .kinds = kind_column, .count = count }, .tree = { .roles = role_column } };
            for (uint32_t pass = 0; pass < 2; pass += 1)
            {
                const uint32_t kind = pass == 0 ? BUSTER_CHECK_NONE : PARSE_KIND_SEMI;
                CheckCommonScan scan = { 0 };
                uint32_t expected = 0;
                for (uint32_t token = check_common_next_token(&parser, count, kind, roles, BUSTER_ARRAY_LENGTH(roles), &scan); tokens;
                     token = check_common_next_token(&parser, count, kind, roles, BUSTER_ARRAY_LENGTH(roles), &scan))
                {
                    while (expected < count && !(role_column[expected] != PARSE_ROLE_MEMBER && (kind == BUSTER_CHECK_NONE || kind_column[expected] == kind)))
                    {
                        expected += 1;
                    }
                    tokens = token == expected;
                    found += token < count;
                    if (token >= count)
                    {
                        break;
                    }
                    expected += 1;
                }
                tokens = tokens && check_common_next_token(&parser, count, kind, roles, BUSTER_ARRAY_LENGTH(roles), &scan) == count;
            }
            const CheckGraph graph = { .count = count, .ops = op_column };
            CheckCommonScan scan = { 0 };
            uint32_t expected = 0;
            for (uint32_t node = check_common_next_op(&graph, &set, &scan); ops; node = check_common_next_op(&graph, &set, &scan))
            {
                while (expected < count && op_column[expected] == CHECK_OP_VALUE)
                {
                    expected += 1;
                }
                ops = node == expected;
                if (node >= count)
                {
                    break;
                }
                expected += 1;
            }
            ops = ops && check_common_next_op(&graph, &set, &scan) == count;
            // The reach bytes reuse the role column's page: the asked bits one of the eight, or two, by the length; a
            // random byte without them, but one time in `odds` with the lowest of them or all.
            uint8_t* bytes = role_column;
            const uint8_t asked = (uint8_t)((1u << (count % 8)) | ((count / 8) % 2 != 0 ? 1u << ((count / 16) % 8) : 0u));
            for (uint32_t i = 0; i < count; i += 1)
            {
                const bool shares = odds != 0 && check_test_common_scan_draw(&state) % odds == 0;
                const uint32_t draw = check_test_common_scan_draw(&state);
                const uint8_t some = draw % 2 == 0 ? (uint8_t)(asked & (0u - asked)) : asked;
                bytes[i] = (uint8_t)((draw >> 8) & ~asked) | (shares ? some : 0u);
            }
            CheckCommonScan reach = { 0 };
            expected = 0;
            for (uint32_t node = check_common_next_reach(bytes, count, asked, &reach); reaches;
                 node = check_common_next_reach(bytes, count, asked, &reach))
            {
                while (expected < count && (bytes[expected] & asked) == 0)
                {
                    expected += 1;
                }
                reaches = node == expected;
                if (node >= count)
                {
                    break;
                }
                expected += 1;
            }
            reaches = reaches && check_common_next_reach(bytes, count, asked, &reach) == count;
            // The evaluated bytes, on the same page: zero one time in `odds`, otherwise not.
            for (uint32_t i = 0; i < count; i += 1)
            {
                const bool zero = odds != 0 && check_test_common_scan_draw(&state) % odds == 0;
                bytes[i] = zero ? 0 : (uint8_t)(1 + check_test_common_scan_draw(&state) % 255);
            }
            CheckCommonScan zeros = { 0 };
            expected = 0;
            for (uint32_t node = check_common_next_unevaluated(bytes, count, &zeros); unevaluated;
                 node = check_common_next_unevaluated(bytes, count, &zeros))
            {
                while (expected < count && bytes[expected] != 0)
                {
                    expected += 1;
                }
                unevaluated = node == expected;
                if (node >= count)
                {
                    break;
                }
                expected += 1;
            }
            unevaluated = unevaluated && check_common_next_unevaluated(bytes, count, &zeros) == count;
        }
    }
    check_test_expect(test, tokens && found != 0, "common scans: the token scan lists every token of the asked roles and kind, in order, at every length");
    check_test_expect(test, ops, "common scans: the operator scan lists every node of the set, in order, at every length");
    check_test_expect(test, reaches, "common scans: the reach scan lists every node sharing the asked bits, in order, at every length");
    check_test_expect(test, unevaluated, "common scans: the unevaluated scan lists every node not evaluated, in order, at every length");
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
    if (wide != MAP_FAILED)
    {
        munmap(wide, page * 2);
    }
    if (second != MAP_FAILED)
    {
        munmap(second, page * 2);
    }
}

static void check_test_driver_common_candidates(CheckTest* test)
{
    check_test_common_scan_definitions(test);
    const CheckCommonResources saved = check_common_resources;
    check_common_resources = (CheckCommonResources){ .fail_at = UINT32_MAX };
    static const uint32_t sizes[] = { 0, 1, 2, 15, 16, 17, 31, 32, 33, 47, 48, 49, 63, 64, 65, 127, 128, 129, 257, 4099 };
    static const uint32_t invalid[] = { CHECK_OP_COUNT, 127, 128, 255, 256, UINT32_MAX };
    bool same = true;
    for (uint32_t size = 0; same && size < BUSTER_ARRAY_LENGTH(sizes); size += 1)
    {
        const uint32_t count = sizes[size];
        CheckOp* ops = malloc(((uint64_t)count + 1) * sizeof(CheckOp));
        same = ops != NULL;
        for (uint32_t rotation = 0; same && rotation < 16; rotation += 1)
        {
            for (uint32_t node = 0; node < count; node += 1)
            {
                const uint32_t code = (node + rotation) % (CHECK_OP_COUNT + BUSTER_ARRAY_LENGTH(invalid));
                ops[node] = code < CHECK_OP_COUNT ? (CheckOp)code : (CheckOp)invalid[code - CHECK_OP_COUNT];
            }
            same = check_test_common_candidate_case(ops, count);
        }
        free(ops);
    }
    check_test_expect(test, same, "common candidates: all operators and invalid IDs across guarded blocks, exact masks and independent scans");
    static const CheckOp dense[] =
    {
        CHECK_OP_VALUE,
        CHECK_OP_INITIALIZE,
        CHECK_OP_LESS,
        CHECK_OP_ADDRESS,
        CHECK_OP_CALL,
        CHECK_OP_SELECT,
        CHECK_OP_SIZEOF,
        CHECK_OP_CAST,
        CHECK_OP_SHIFT_LEFT,
    };
    bool extremes = true;
    CheckOp repeated[65];
    for (uint32_t kind = 0; extremes && kind < BUSTER_ARRAY_LENGTH(dense); kind += 1)
    {
        for (uint32_t node = 0; node < BUSTER_ARRAY_LENGTH(repeated); node += 1)
        {
            repeated[node] = dense[kind];
        }
        extremes = check_test_common_candidate_case(repeated, BUSTER_ARRAY_LENGTH(repeated));
    }
    check_test_expect(test, extremes, "common candidates: empty classes and dense classes retain the last live lane");
    check_test_common_candidate_comparison_tail(test);
    check_test_common_candidate_exhaustion(test);
    check_common_resources = saved;
}
