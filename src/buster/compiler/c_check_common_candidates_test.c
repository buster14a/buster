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
            result = op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_X86 || op == CHECK_OP_BUILTIN_MISC;
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

// Set by test-checker-driver --reach-filter-only: the generated nodes' counts, by operator, on stdout.
static bool check_test_verbose_reach;

// The reach filter's property (#1340): a node check_common_reach_step takes out of the scan has no bit of its own reach
// (check_common_own_reach, the one verdict) and is no self-assignment's candidate. Over generated graphs and streams,
// under every set of the options the exact test reads, by the audit's own steps (check_common_reach_audit_kept and
// check_common_reach_audit_node), with every column the filter reads ending at a protected page.
typedef struct CheckTestReachTotals CheckTestReachTotals;
struct CheckTestReachTotals
{
    // Per operator: the candidates with a bit, and those the filter took out.
    uint64_t found[CHECK_OP_COUNT + 1];
    uint64_t taken[CHECK_OP_COUNT + 1];
    uint64_t candidates;
    uint64_t kept;
    uint64_t lost;
    uint32_t applied;
    uint32_t skipped;
};

// The operators a generated node draws from: every family's, the leaves and operators the tests peel or name, the ones
// no family has, and one outside the enumeration.
static const CheckOp check_test_reach_ops[] =
{
    CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE,
    CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE, CHECK_OP_VALUE,
    CHECK_OP_ASSIGN, CHECK_OP_ASSIGN, CHECK_OP_ASSIGN, CHECK_OP_ASSIGN, CHECK_OP_ASSIGN, CHECK_OP_ASSIGN, CHECK_OP_INITIALIZE, CHECK_OP_INITIALIZE, CHECK_OP_INITIALIZE,
    CHECK_OP_INITIALIZE, CHECK_OP_CONVERT, CHECK_OP_CONVERT, CHECK_OP_CONVERT, CHECK_OP_CONVERT, CHECK_OP_CALL, CHECK_OP_CALL, CHECK_OP_CALL, CHECK_OP_CALL,
    CHECK_OP_SUBSCRIPT, CHECK_OP_SUBSCRIPT, CHECK_OP_SUBSCRIPT, CHECK_OP_SUBSCRIPT, CHECK_OP_NOT, CHECK_OP_NOT, CHECK_OP_LOGICAL_AND, CHECK_OP_LOGICAL_AND,
    CHECK_OP_LOGICAL_OR, CHECK_OP_LOGICAL_OR, CHECK_OP_CONDITION, CHECK_OP_CONDITION, CHECK_OP_CONDITION, CHECK_OP_CONDITIONAL, CHECK_OP_CONDITIONAL, CHECK_OP_COMMA,
    CHECK_OP_COMMA, CHECK_OP_SHIFT_LEFT, CHECK_OP_SHIFT_LEFT, CHECK_OP_SHIFT_RIGHT, CHECK_OP_DIVIDE, CHECK_OP_DIVIDE, CHECK_OP_REMAINDER, CHECK_OP_REMAINDER,
    CHECK_OP_DIVIDE_ASSIGN, CHECK_OP_REMAINDER_ASSIGN, CHECK_OP_DEREFERENCE, CHECK_OP_DEREFERENCE, CHECK_OP_DEREFERENCE, CHECK_OP_CAST, CHECK_OP_CAST, CHECK_OP_CAST,
    CHECK_OP_SELECT, CHECK_OP_SELECT, CHECK_OP_ADD, CHECK_OP_ADD, CHECK_OP_SUBTRACT, CHECK_OP_MULTIPLY, CHECK_OP_LESS, CHECK_OP_LESS, CHECK_OP_EQUAL, CHECK_OP_EQUAL,
    CHECK_OP_GREATER_EQUAL, CHECK_OP_POSITIVE, CHECK_OP_NEGATIVE, CHECK_OP_ADD_ASSIGN, CHECK_OP_SUBTRACT_ASSIGN, CHECK_OP_MULTIPLY_ASSIGN, CHECK_OP_SHIFT_LEFT_ASSIGN,
    CHECK_OP_MEMBER, CHECK_OP_ARROW, CHECK_OP_BUILTIN_MISC, CHECK_OP_BUILTIN_FLOAT, CHECK_OP_ADDRESS, CHECK_OP_SIZEOF, CHECK_OP_BIT_AND, CHECK_OP_STATEMENT_VALUE,
    (CheckOp)(CHECK_OP_COUNT + 3), (CheckOp)300,
};

// The constants' values: zero, the small ones, each width's limits and their neighbours, and negative ones.
static const unsigned __int128 check_test_reach_integers[] =
{
    0, 0, 0, 1, 1, 2, 3, 4, 5, 7, 8, 127, 128, 200, 255, 256, 299, 300, 301, 32767, 32768, 65535, 65536, 0x7fffffffu, 0x80000000u, 0xffffffffu,
    (unsigned __int128)1 << 32, (unsigned __int128)1 << 63, ~(unsigned __int128)0, ~(unsigned __int128)1, ~(unsigned __int128)0 << 31,
};

// A generated graph's columns, for the shapes check_test_common_reach_shape writes over the drawn nodes.
typedef struct CheckTestReachColumns CheckTestReachColumns;
struct CheckTestReachColumns
{
    CheckOp* ops;
    uint32_t* operand0;
    uint32_t* operand1;
    TypeId* value_types;
    uint32_t* value_flags;
    uint8_t* value_constants;
    uint32_t* payload_rows;
    CheckValue* payloads;
    uint32_t* payload_count;
    CheckNodeConversions* conversions;
    uint32_t* argument_offsets;
    uint32_t* argument_counts;
    uint32_t* argument_nodes;
    CheckConversion* argument_conversions;
    uint32_t count;
};

// `node` rewritten as an operator `op` of type `type` without a constant, or as a leaf's integer constant `integer` of
// that type (`constant`), or as a bit-field's lvalue of `bit_width` bits.
static void check_test_common_reach_node(const CheckTestReachColumns* columns, uint32_t node, CheckOp op, TypeId type, bool constant, unsigned __int128 integer,
                                         uint32_t bit_width)
{
    if (node < columns->count)
    {
        const CheckValue value =
        {
            .type = type,
            .category = bit_width != 0 ? CHECK_LVALUE : CHECK_RVALUE,
            .flags = (constant ? BUSTER_CHECK_ICE : 0u) | (bit_width != 0 ? BUSTER_CHECK_BIT_FIELD : 0u),
            .constant_kind = constant ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE,
            .integer = integer,
            .bit_width = bit_width,
        };
        columns->ops[node] = op;
        columns->value_types[node] = type;
        columns->value_flags[node] = value.flags;
        columns->value_constants[node] = (uint8_t)value.constant_kind;
        columns->payload_rows[node] = 0;
        if (integer != 0 || bit_width != 0)
        {
            columns->payloads[*columns->payload_count] = value;
            *columns->payload_count += 1;
            columns->payload_rows[node] = *columns->payload_count;
        }
    }
}

// The shapes written, by their node's operator, which --reach-filter-only prints.
static uint64_t check_test_reach_shaped[CHECK_OP_COUNT];

// The shape one family's test looks for, written over `node`'s drawn operands, which stay the tree's: the generator's
// independent draws make most of them once in ten thousand nodes or never. `draw` picks the shape; `pointer` is a
// prototyped function's pointer, `array` an array of four, `enumeration` an enumeration.
static void check_test_common_reach_shape(const CheckTestReachColumns* columns, uint32_t node, uint32_t draw, TypeId pointer, TypeId array, TypeId enumeration)
{
    const CheckOp op = columns->ops[node];
    const uint32_t count = columns->count;
    const uint32_t left = columns->operand0[node];
    const uint32_t right = columns->operand1[node];
    const uint32_t shape = draw % 4;
    const unsigned __int128 value = check_test_reach_integers[draw / 4 % BUSTER_ARRAY_LENGTH(check_test_reach_integers)];
    check_test_reach_shaped[(uint32_t)op < CHECK_OP_COUNT ? op : CHECK_OP_VALUE] += 1;
    if ((op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER || op == CHECK_OP_DIVIDE_ASSIGN || op == CHECK_OP_REMAINDER_ASSIGN) && right < count)
    {
        // A zero divisor, or one under a cast or a conversion to an integer type.
        const uint32_t under = columns->operand0[right];
        if (shape == 0 || under >= count)
        {
            check_test_common_reach_node(columns, right, CHECK_OP_VALUE, BUSTER_TYPE_INT, true, shape == 3 ? value : 0, 0);
        }
        else
        {
            check_test_common_reach_node(columns, right, shape == 1 ? CHECK_OP_CAST : CHECK_OP_CONVERT, BUSTER_TYPE_LONG, false, 0, 0);
            check_test_common_reach_node(columns, under, CHECK_OP_VALUE, BUSTER_TYPE_INT, true, shape == 3 ? value : 0, 0);
        }
    }
    else if (op == CHECK_OP_DEREFERENCE && left < count)
    {
        // A null pointer constant, under a cast, or nullptr under a conversion; of a type whose value a read takes.
        const uint32_t under = columns->operand0[left];
        columns->value_types[node] = BUSTER_TYPE_INT;
        if (shape == 0 || under >= count)
        {
            check_test_common_reach_node(columns, left, CHECK_OP_VALUE, shape == 0 ? BUSTER_TYPE_NULLPTR : BUSTER_TYPE_INT, shape != 0, 0, 0);
        }
        else if (shape == 1)
        {
            check_test_common_reach_node(columns, left, CHECK_OP_CAST, pointer, false, 0, 0);
            check_test_common_reach_node(columns, under, CHECK_OP_VALUE, BUSTER_TYPE_INT, true, 0, 0);
        }
        else
        {
            check_test_common_reach_node(columns, left, CHECK_OP_CONVERT, pointer, false, 0, 0);
            check_test_common_reach_node(columns, under, CHECK_OP_VALUE, BUSTER_TYPE_NULLPTR, false, 0, 0);
        }
    }
    else if (op == CHECK_OP_SUBSCRIPT && left < count && right < count)
    {
        // A constant index into an array of four.
        columns->value_types[left] = array;
        check_test_common_reach_node(columns, right, CHECK_OP_VALUE, BUSTER_TYPE_INT, true, value, 0);
    }
    else if ((op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT) && (op == CHECK_OP_ASSIGN ? right : left) < count)
    {
        const uint32_t source = op == CHECK_OP_ASSIGN ? right : left;
        const uint32_t under = columns->operand0[source];
        columns->conversions[node].operands[op == CHECK_OP_ASSIGN ? 1 : 0].flags = BUSTER_CHECK_CONVERT_ASSIGNMENT;
        if (shape == 0 && under < count)
        {
            // A `<<` under a selection or a conversion, converted to _Bool.
            columns->value_types[node] = BUSTER_TYPE_BOOL;
            check_test_common_reach_node(columns, source, draw / 256 % 2 != 0 ? CHECK_OP_SELECT : CHECK_OP_CONVERT, BUSTER_TYPE_INT, false, 0, 0);
            check_test_common_reach_node(columns, under, CHECK_OP_SHIFT_LEFT, BUSTER_TYPE_INT, false, 0, 0);
        }
        else if (shape == 1 && op == CHECK_OP_ASSIGN && left < count)
        {
            // A constant stored to a bit-field of a few bits.
            columns->value_types[node] = BUSTER_TYPE_INT;
            check_test_common_reach_node(columns, left, CHECK_OP_MEMBER, BUSTER_TYPE_INT, false, 0, 1 + draw / 256 % 6);
            check_test_common_reach_node(columns, source, CHECK_OP_VALUE, BUSTER_TYPE_INT, true, value, 0);
        }
        else
        {
            // A constant converted to a narrower, a wider or an enumeration's type.
            static const TypeId targets[] = { BUSTER_TYPE_CHAR, BUSTER_TYPE_UCHAR, BUSTER_TYPE_SHORT, BUSTER_TYPE_LONG, BUSTER_TYPE_UINT, 0 };
            const TypeId target = targets[draw / 256 % BUSTER_ARRAY_LENGTH(targets)];
            columns->value_types[node] = target != 0 ? target : enumeration;
            check_test_common_reach_node(columns, source, CHECK_OP_VALUE, target != 0 ? BUSTER_TYPE_INT : enumeration, true, value, 0);
        }
    }
    else if (op == CHECK_OP_CALL && columns->argument_offsets != NULL && left < count)
    {
        // A prototyped callee, whose parameter's type the conversion is to, and one argument, so that it alone decides: a
        // `<<` under a selection or a conversion passed to _Bool, an enumeration's value passed to another's, a double
        // passed to a float, or a constant, narrowed or not.
        const uint32_t slot = columns->argument_offsets[node];
        const uint32_t argument = columns->argument_nodes[slot];
        const uint32_t call_shape = draw % 5;
        columns->value_types[left] = pointer;
        columns->argument_counts[node] = 1;
        if (argument < count)
        {
            const uint32_t under = columns->operand0[argument];
            if (call_shape == 0 && under < count)
            {
                columns->argument_conversions[slot] = (CheckConversion){ .type = BUSTER_TYPE_BOOL, .flags = BUSTER_CHECK_CONVERT_ASSIGNMENT };
                check_test_common_reach_node(columns, argument, draw / 256 % 2 != 0 ? CHECK_OP_SELECT : CHECK_OP_CONVERT, BUSTER_TYPE_INT, false, 0, 0);
                check_test_common_reach_node(columns, under, draw / 512 % 2 != 0 ? CHECK_OP_SHIFT_LEFT : CHECK_OP_CONDITIONAL, BUSTER_TYPE_INT, false, 0, 0);
            }
            else if (call_shape == 1)
            {
                columns->argument_conversions[slot] = (CheckConversion){ .type = enumeration, .flags = BUSTER_CHECK_CONVERT_ASSIGNMENT };
                check_test_common_reach_node(columns, argument, draw / 256 % 2 != 0 ? CHECK_OP_COMMA : CHECK_OP_ADD, enumeration, false, 0, 0);
            }
            else if (call_shape == 2)
            {
                columns->argument_conversions[slot] = (CheckConversion){ .type = BUSTER_TYPE_FLOAT, .flags = BUSTER_CHECK_CONVERT_ASSIGNMENT };
                check_test_common_reach_node(columns, argument, CHECK_OP_ADD, BUSTER_TYPE_DOUBLE, false, 0, 0);
            }
            else
            {
                static const TypeId targets[] = { BUSTER_TYPE_CHAR, BUSTER_TYPE_UCHAR, BUSTER_TYPE_SHORT, BUSTER_TYPE_LONG, BUSTER_TYPE_DOUBLE };
                columns->argument_conversions[slot] = (CheckConversion){ .type = targets[draw / 256 % BUSTER_ARRAY_LENGTH(targets)], .flags = BUSTER_CHECK_CONVERT_ASSIGNMENT };
                check_test_common_reach_node(columns, argument, CHECK_OP_VALUE, BUSTER_TYPE_INT, true, value, 0);
            }
        }
    }
    else if ((op == CHECK_OP_NOT || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITION) && left < count)
    {
        // A truth context's `<<` or conditional under a selection or a conversion, or its floating constant.
        const uint32_t under = columns->operand0[left];
        if (shape == 0 || under >= count)
        {
            check_test_common_reach_node(columns, left, CHECK_OP_VALUE, BUSTER_TYPE_DOUBLE, false, 0, 0);
            columns->value_constants[left] = (uint8_t)CHECK_CONSTANT_FLOATING;
        }
        else
        {
            check_test_common_reach_node(columns, left, shape == 1 ? CHECK_OP_SELECT : CHECK_OP_CONVERT, BUSTER_TYPE_INT, false, 0, 0);
            check_test_common_reach_node(columns, under, shape == 3 ? CHECK_OP_CONDITIONAL : CHECK_OP_SHIFT_LEFT, BUSTER_TYPE_INT, false, 0, 0);
        }
    }
}

// One generated graph of `count` nodes, a tree in node order (check_common_parents), audited under each of the 256 option sets, with its argument columns or, as a
// graph without calls' slices has them, without. Whether every run agreed with its expectations: no node lost, and the filter applied exactly where neither -Wdouble-promotion nor
// -Wimplicit-int-float-conversion may report.
static bool check_test_common_reach_case(uint64_t* state, uint32_t count, bool arguments, const TypeTable* types, const TypeId* pool, uint32_t pool_count,
                                         const TypeId* shapes, Arena* scratch, CheckTestReachTotals* totals)
{
    enum { COLUMNS = 20, TOKENS = 97 };
    CheckTestSelectionColumn columns[COLUMNS] = { 0 };
    static const uint32_t conversion_flags[] =
    {
        0, BUSTER_CHECK_CONVERT_ASSIGNMENT, BUSTER_CHECK_CONVERT_ASSIGNMENT, BUSTER_CHECK_CONVERT_ASSIGNMENT, BUSTER_CHECK_CONVERT_ASSIGNMENT | BUSTER_CHECK_CONVERT_CAST,
        BUSTER_CHECK_CONVERT_ARITHMETIC, BUSTER_CHECK_CONVERT_ASSIGNMENT | BUSTER_CHECK_CONVERT_ATOMIC, BUSTER_CHECK_CONVERT_PROMOTION, BUSTER_CHECK_CONVERT_CAST,
    };
    // The nodes' argument counts first, so that the argument columns end at their protected pages too.
    uint32_t* argument_shape = malloc(((uint64_t)count + 1) * sizeof(uint32_t));
    CheckOp* drawn = malloc(((uint64_t)count + 1) * sizeof(CheckOp));
    // The nodes no node reads yet, and the facts' tree.
    uint32_t* open = malloc(((uint64_t)count + 1) * sizeof(uint32_t));
    uint32_t* parents = malloc(((uint64_t)count + 1) * sizeof(uint32_t));
    uint64_t* roots = malloc((((uint64_t)count + 63) / 64 + 1) * sizeof(uint64_t));
    uint32_t open_count = 0;
    bool same = argument_shape != NULL && drawn != NULL && open != NULL && parents != NULL && roots != NULL;
    uint32_t argument_count = 0;
    uint32_t payload_capacity = 0;
    for (uint32_t node = 0; same && node < count; node += 1)
    {
        // Half the nodes are leaves, as an expression tree's are, so that the others find operands.
        const uint32_t leaf = check_test_common_scan_draw(state);
        drawn[node] = leaf % 5 < 2 ? CHECK_OP_VALUE : check_test_reach_ops[leaf / 5 % BUSTER_ARRAY_LENGTH(check_test_reach_ops)];
        const uint32_t shape = check_test_common_scan_draw(state);
        argument_shape[node] = check_graph_has_arguments(drawn[node]) ? (shape % 64 == 0 ? 17 + shape / 64 % 24 : shape % 6) : 0;
        argument_count += argument_shape[node];
        // (Its own value's row, and the two a shape may write over its operands.)
        payload_capacity += 3;
    }
    CheckOp* ops = check_test_selection_column(columns, (uint64_t)count * sizeof(CheckOp));
    uint32_t* operand0 = check_test_selection_column(columns + 1, (uint64_t)count * sizeof(uint32_t));
    uint32_t* operand1 = check_test_selection_column(columns + 2, (uint64_t)count * sizeof(uint32_t));
    uint32_t* operand2 = check_test_selection_column(columns + 3, (uint64_t)count * sizeof(uint32_t));
    uint32_t* tokens = check_test_selection_column(columns + 4, (uint64_t)count * sizeof(uint32_t));
    uint8_t* contexts = check_test_selection_column(columns + 5, count);
    uint32_t* argument_offsets = check_test_selection_column(columns + 6, (uint64_t)count * sizeof(uint32_t));
    uint32_t* argument_counts = check_test_selection_column(columns + 7, (uint64_t)count * sizeof(uint32_t));
    uint32_t* argument_nodes = check_test_selection_column(columns + 8, (uint64_t)argument_count * sizeof(uint32_t));
    TypeId* value_types = check_test_selection_column(columns + 9, (uint64_t)count * sizeof(TypeId));
    uint32_t* value_flags = check_test_selection_column(columns + 10, (uint64_t)count * sizeof(uint32_t));
    uint32_t* value_objects = check_test_selection_column(columns + 11, (uint64_t)count * sizeof(uint32_t));
    uint8_t* value_categories = check_test_selection_column(columns + 12, count);
    uint8_t* value_constants = check_test_selection_column(columns + 13, count);
    uint32_t* payload_rows = check_test_selection_column(columns + 14, (uint64_t)count * sizeof(uint32_t));
    CheckValue* payloads = check_test_selection_column(columns + 15, (uint64_t)payload_capacity * sizeof(CheckValue));
    CheckNodeConversions* conversions = check_test_selection_column(columns + 16, (uint64_t)count * sizeof(CheckNodeConversions));
    CheckConversion* argument_conversions = check_test_selection_column(columns + 17, (uint64_t)argument_count * sizeof(CheckConversion));
    uint8_t* kinds = check_test_selection_column(columns + 18, TOKENS);
    for (uint32_t i = 0; i < COLUMNS - 1; i += 1)
    {
        same = same && columns[i].mapping != NULL && columns[i].mapping != MAP_FAILED;
    }
    uint32_t payload_count = 0;
    uint32_t argument_next = 0;
    for (uint32_t token = 0; same && token < TOKENS; token += 1)
    {
        kinds[token] = check_test_common_scan_draw(state) % 3 == 0 ? PARSE_KIND_COMMA : PARSE_KIND_IDENTIFIER;
    }
    for (uint32_t node = 0; same && node < count; node += 1)
    {
        ops[node] = drawn[node];
        // A tree in node order, as the facts' walk needs: each operand is a node no other has taken, mostly the latest one
        // (the node before, where it is free), sometimes any; a few are absent or name no node. The second operand is
        // drawn first, so that it is the node before its reader as often as an evaluation order makes it.
        uint32_t operands[3] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        static const uint32_t order[3] = { 1, 0, 2 };
        for (uint32_t turn = 0; turn < 3; turn += 1)
        {
            const uint32_t slot = order[turn];
            const uint32_t draw = check_test_common_scan_draw(state);
            const uint32_t present = drawn[node] == CHECK_OP_VALUE ? 0 : slot == 0 ? 90 : slot == 1 ? 65 : 15;
            static const uint32_t strays[] = { 0, 1, 0x7fffffffu, 0xf0000000u };
            if (draw % 100 < present && draw / 100 % 64 == 0)
            {
                operands[slot] = count + strays[draw / 6400 % BUSTER_ARRAY_LENGTH(strays)];
            }
            else if (draw % 100 < present && open_count != 0)
            {
                const uint32_t pick = draw / 100 % 4 != 0 ? open_count - 1 : draw / 6400 % open_count;
                operands[slot] = open[pick];
                open[pick] = open[open_count - 1];
                open_count -= 1;
            }
        }
        operand0[node] = operands[0];
        operand1[node] = operands[1];
        operand2[node] = operands[2];
        tokens[node] = check_test_common_scan_draw(state) % (TOKENS + 3);
        contexts[node] = check_test_common_scan_draw(state) % 24 == 0 ? BUSTER_CHECK_CONTEXT_ASSERT : 0;
        const uint32_t kind = check_test_common_scan_draw(state) % 100;
        const uint32_t shape = check_test_common_scan_draw(state);
        CheckValue value =
        {
            .type = pool[check_test_common_scan_draw(state) % pool_count] | (shape % 8 == 0 ? BUSTER_TYPE_CONST : 0) | (shape % 32 == 1 ? BUSTER_TYPE_VOLATILE : 0),
            .category = (CheckCategory)(shape / 32 % 3),
            .flags = (shape / 128 % 2 != 0 ? BUSTER_CHECK_ICE : 0) | (shape / 256 % 12 == 0 ? BUSTER_CHECK_BIT_FIELD : 0),
            .constant_kind = kind < 46 ? CHECK_CONSTANT_NONE : kind < 80 ? CHECK_CONSTANT_INTEGER : kind < 88 ? CHECK_CONSTANT_FLOATING : kind < 95 ? CHECK_CONSTANT_ADDRESS :
                             kind < 98 ? CHECK_CONSTANT_COMPLEX : CHECK_CONSTANT_LABEL_DIFFERENCE,
        };
        if (value.constant_kind == CHECK_CONSTANT_INTEGER)
        {
            value.integer = check_test_reach_integers[check_test_common_scan_draw(state) % BUSTER_ARRAY_LENGTH(check_test_reach_integers)];
        }
        if ((value.flags & BUSTER_CHECK_BIT_FIELD) != 0)
        {
            value.bit_width = 1 + shape / 4096 % 9;
        }
        value_types[node] = value.type;
        value_flags[node] = value.flags;
        value_objects[node] = 0;
        value_categories[node] = (uint8_t)value.category;
        // (A constant kind's byte past the kinds, where one in sixty-four nodes has it: no test names it.)
        value_constants[node] = shape / 65536 % 64 == 0 ? (uint8_t)(16 + CHECK_CONSTANT_INTEGER + shape / 4194304 % 3 * 112) : (uint8_t)value.constant_kind;
        const bool payload = value_constants[node] == (uint8_t)value.constant_kind && (value.integer != 0 || value.bit_width != 0);
        payload_rows[node] = payload ? payload_count + 1 : 0;
        if (payload)
        {
            payloads[payload_count] = value;
            payload_count += 1;
        }
        for (uint32_t slot = 0; slot < 4; slot += 1)
        {
            const uint32_t draw = check_test_common_scan_draw(state);
            const CheckConversion conversion = { .type = draw % 4 == 0 ? 0 : pool[draw / 4 % pool_count] | (draw / 4096 % 8 == 0 ? BUSTER_TYPE_CONST : 0),
                                                 .flags = conversion_flags[draw / 65536 % BUSTER_ARRAY_LENGTH(conversion_flags)] };
            if (slot < 3)
            {
                conversions[node].operands[slot] = conversion;
            }
            else
            {
                conversions[node].store = conversion;
            }
        }
        // An operator without arguments has columns no test may read: an offset and a count past the slots.
        argument_offsets[node] = check_graph_has_arguments(ops[node]) ? argument_next : 0xfffffff0u;
        argument_counts[node] = check_graph_has_arguments(ops[node]) ? argument_shape[node] : 0xfffffff0u;
        for (uint32_t i = 0; i < argument_shape[node]; i += 1)
        {
            const uint32_t draw = check_test_common_scan_draw(state);
            uint32_t argument = draw % 40 == 0 ? count + draw / 40 % 3 : BUSTER_CHECK_NONE;
            if (draw % 40 != 0 && open_count != 0)
            {
                const uint32_t pick = draw / 40 % 3 != 0 ? open_count - 1 : draw / 120 % open_count;
                argument = open[pick];
                open[pick] = open[open_count - 1];
                open_count -= 1;
            }
            argument_nodes[argument_next] = argument;
            argument_conversions[argument_next] = (CheckConversion){ .type = draw / 4096 % 5 == 0 ? 0 : pool[draw / 65536 % pool_count],
                                                                    .flags = conversion_flags[draw / 1048576 % BUSTER_ARRAY_LENGTH(conversion_flags)] };
            argument_next += 1;
        }
        open[open_count] = node;
        open_count += 1;
    }
    const CheckTestReachColumns shaped =
    {
        .ops = ops,
        .operand0 = operand0,
        .operand1 = operand1,
        .value_types = value_types,
        .value_flags = value_flags,
        .value_constants = value_constants,
        .payload_rows = payload_rows,
        .payloads = payloads,
        .payload_count = &payload_count,
        .conversions = conversions,
        .argument_offsets = arguments ? argument_offsets : NULL,
        .argument_counts = argument_counts,
        .argument_nodes = argument_nodes,
        .argument_conversions = argument_conversions,
        .count = count,
    };
    // A third of the nodes take their family's shape, from the last node down, so that a node's shape isn't written over
    // by its reader's.
    for (uint32_t node = count; same && node-- > 0;)
    {
        const uint32_t draw = check_test_common_scan_draw(state);
        if (draw % 3 == 0 && (!check_graph_has_arguments(ops[node]) || argument_shape[node] != 0))
        {
            check_test_common_reach_shape(&shaped, node, draw / 3, shapes[0], shapes[1], shapes[2]);
        }
    }
    const CheckGraph graph =
    {
        .count = count,
        .ops = ops,
        .operand0 = operand0,
        .operand1 = operand1,
        .operand2 = operand2,
        .tokens = tokens,
        .contexts = contexts,
        .argument_offsets = arguments ? argument_offsets : NULL,
        .argument_counts = arguments ? argument_counts : NULL,
        .argument_nodes = arguments ? argument_nodes : NULL,
        .argument_count = arguments ? argument_count : 0,
    };
    const CheckStream stream =
    {
        .count = count,
        .value_types = value_types,
        .value_flags = value_flags,
        .value_objects = value_objects,
        .value_categories = value_categories,
        .value_constants = value_constants,
        .payload_rows = payload_rows,
        .payloads = payloads,
        .payload_count = payload_count,
        .conversions = conversions,
        .argument_conversions = arguments ? argument_conversions : NULL,
    };
    Parser parser = { 0 };
    parser.tokens.kinds = kinds;
    parser.tokens.count = TOKENS;
    const uint64_t mark = scratch->length;
    CheckCommonCandidates candidates = { 0 };
    same = same && check_common_candidates_build(&graph, scratch, &candidates) && (count == 0 || check_common_parents(&graph, parents, roots));
    for (uint32_t set = 0; same && set < 256; set += 1)
    {
        const CheckCommonEnabled enabled =
        {
            .sign_compare = (set & 1) != 0,
            .self_assign = (set & 2) != 0,
            .enum_cast = (set & 4) != 0,
            .atomic_access = (set & 8) != 0,
            .float_precision = (set & 16) != 0,
            .promotion = (set & 32) != 0,
            .int_float = (set & 64) != 0,
            .const_int_float = (set & 128) != 0,
        };
        const uint64_t inner = scratch->length;
        CheckCommonReachFilter filter = { 0 };
        same = check_common_reach_filter_build(&graph, &stream, types, &enabled, parents, scratch, &filter) &&
               (filter.traits != NULL) == (count != 0 && !enabled.promotion && !enabled.int_float);
        const uint64_t* kept = same && filter.traits != NULL ? check_common_reach_audit_kept(&graph, &stream, &candidates, &enabled, &filter, scratch) : NULL;
        same = same && (kept != NULL || filter.traits == NULL);
        totals->applied += kept != NULL;
        totals->skipped += kept == NULL;
        if (kept != NULL)
        {
            const bool edges = enabled.promotion || enabled.int_float || enabled.const_int_float;
            CheckCommonReachCounts counts = { 0 };
            CheckCommonScan scan = { 0 };
            for (uint32_t node = check_common_next_reach_op(&graph, &stream, &candidates, enabled.sign_compare, enabled.atomic_access, enabled.float_precision, edges, NULL, &scan);
                 node < count;
                 node = check_common_next_reach_op(&graph, &stream, &candidates, enabled.sign_compare, enabled.atomic_access, enabled.float_precision, edges, NULL, &scan))
            {
                const uint8_t bits = check_common_own_reach(&parser, &graph, &stream, types, node, &enabled);
                const bool self_assign = enabled.self_assign && check_common_self_assign_candidate(&graph, node);
                const uint32_t op = (uint32_t)ops[node] < CHECK_OP_COUNT ? (uint32_t)ops[node] : CHECK_OP_COUNT;
                totals->found[op] += bits != 0 || self_assign;
                totals->taken[op] += (kept[node >> 6] & (UINT64_C(1) << (node & 63))) == 0;
                check_common_reach_audit_node(&graph, kept, node, bits, self_assign, &counts);
            }
            totals->candidates += counts.candidates;
            totals->kept += counts.kept;
            totals->lost += counts.lost;
            same = counts.lost == 0;
        }
        scratch->length = inner;
    }
    scratch->length = mark;
    for (uint32_t i = 0; i < COLUMNS; i += 1)
    {
        check_test_selection_unmap(columns + i);
    }
    free(roots);
    free(parents);
    free(open);
    free(drawn);
    free(argument_shape);
    return same;
}

static void check_test_common_reach_filter(CheckTest* test)
{
    const CheckCommonResources saved = check_common_resources;
    check_common_resources = (CheckCommonResources){ .fail_at = UINT32_MAX };
    const TypeCapacity capacity =
    {
        .types = 256,
        .parameters = 16,
        .tags = 16,
        .members = 16,
        .lookups = 16,
    };
    Arena type_storage = arena_reserve(type_capacity_bytes(capacity));
    Arena type_index_storage = arena_reserve(type_index_capacity_bytes(capacity));
    Arena scratch = arena_reserve(UINT64_C(1) << 26);
    TypeTable types = { 0 };
    bool same = type_initialize(&types, &type_storage, &type_index_storage, capacity);
    TypeId pool[40];
    TypeId shapes[3] = { 0 };
    uint32_t pool_count = 0;
    if (same)
    {
        static const TypeId builtins[] =
        {
            BUSTER_TYPE_INT, BUSTER_TYPE_INT, BUSTER_TYPE_INT, BUSTER_TYPE_UINT, BUSTER_TYPE_CHAR, BUSTER_TYPE_SCHAR, BUSTER_TYPE_UCHAR, BUSTER_TYPE_SHORT, BUSTER_TYPE_USHORT,
            BUSTER_TYPE_LONG, BUSTER_TYPE_ULONG, BUSTER_TYPE_LLONG, BUSTER_TYPE_INT128, BUSTER_TYPE_BOOL, BUSTER_TYPE_BOOL, BUSTER_TYPE_FLOAT, BUSTER_TYPE_FLOAT,
            BUSTER_TYPE_DOUBLE, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_LONG_DOUBLE, BUSTER_TYPE_HALF, BUSTER_TYPE_NULLPTR, BUSTER_TYPE_VOID,
        };
        memcpy(pool, builtins, sizeof(builtins));
        pool_count = BUSTER_ARRAY_LENGTH(builtins);
        const TypeId parameters[] = { BUSTER_TYPE_CHAR, BUSTER_TYPE_DOUBLE };
        const TypeId function = type_function(&types, BUSTER_TYPE_INT, parameters, BUSTER_ARRAY_LENGTH(parameters), BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId derived[] =
        {
            type_pointer(&types, BUSTER_TYPE_INT), type_pointer(&types, function), function, type_array(&types, BUSTER_TYPE_INT, 4), type_array(&types, BUSTER_TYPE_CHAR, 300),
            type_array(&types, BUSTER_TYPE_INT, 0), type_tag(&types, BUSTER_TYPE_KIND_ENUM, 0), type_tag(&types, BUSTER_TYPE_KIND_ENUM, 0),
            type_aligned(&types, BUSTER_TYPE_INT, 4), type_aligned(&types, BUSTER_TYPE_DOUBLE, 4), type_aligned(&types, type_array(&types, BUSTER_TYPE_INT, 2), 4),
            type_complex(&types, BUSTER_TYPE_DOUBLE), type_complex(&types, BUSTER_TYPE_FLOAT), type_atomic(&types, BUSTER_TYPE_INT), type_bit_int(&types, 7, false),
            type_bit_int(&types, 40, true), type_tag(&types, BUSTER_TYPE_KIND_STRUCT, 0),
        };
        // A prototyped function's pointer, an array of four and an enumeration, for check_test_common_reach_shape.
        shapes[0] = derived[1];
        shapes[1] = derived[3];
        shapes[2] = derived[6];
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(derived); i += 1)
        {
            same = same && derived[i] != 0;
            pool[pool_count] = derived[i];
            pool_count += 1;
        }
    }
    static const uint32_t sizes[] = { 0, 1, 2, 15, 16, 17, 31, 33, 63, 64, 65, 127, 129, 255, 257, 700, 2049 };
    CheckTestReachTotals* totals = calloc(1, sizeof(CheckTestReachTotals));
    same = same && totals != NULL && scratch.base != NULL;
    uint64_t state = UINT64_C(0x1340a11ce5eed);
    for (uint32_t round = 0; same && round < 24; round += 1)
    {
        for (uint32_t size = 0; same && size < BUSTER_ARRAY_LENGTH(sizes); size += 1)
        {
            same = check_test_common_reach_case(&state, sizes[size], round % 6 != 5, &types, pool, pool_count, shapes, &scratch, totals);
        }
    }
    check_test_expect(test, same && totals->lost == 0, "reach filter: no generated node it takes out has a bit of its own reach, under every option set");
    // The generator reaches every family both ways: nodes with a bit, which a filter too narrow would lose, and nodes
    // taken out, without which the property would hold of a filter that keeps everything.
    static const CheckOp families[] =
    {
        CHECK_OP_ASSIGN, CHECK_OP_INITIALIZE, CHECK_OP_CONVERT, CHECK_OP_CALL, CHECK_OP_SUBSCRIPT, CHECK_OP_NOT, CHECK_OP_LOGICAL_AND, CHECK_OP_LOGICAL_OR, CHECK_OP_CONDITION,
        CHECK_OP_DIVIDE, CHECK_OP_REMAINDER, CHECK_OP_DIVIDE_ASSIGN, CHECK_OP_REMAINDER_ASSIGN, CHECK_OP_DEREFERENCE, CHECK_OP_ADD, CHECK_OP_SUBTRACT, CHECK_OP_MULTIPLY,
        CHECK_OP_LESS, CHECK_OP_EQUAL, CHECK_OP_GREATER_EQUAL, CHECK_OP_POSITIVE, CHECK_OP_NEGATIVE, CHECK_OP_ADD_ASSIGN, CHECK_OP_SUBTRACT_ASSIGN, CHECK_OP_MULTIPLY_ASSIGN,
    };
    bool reached = same;
    for (uint32_t i = 0; reached && i < BUSTER_ARRAY_LENGTH(families); i += 1)
    {
        reached = totals->found[families[i]] != 0 && totals->taken[families[i]] != 0;
    }
    check_test_expect(test, reached && totals->applied != 0 && totals->skipped != 0 && totals->kept < totals->candidates,
                      "reach filter: every family's operators have generated nodes with a bit and nodes taken out");
    if (totals != NULL && check_test_verbose_reach)
    {
        printf("reach filter: %llu candidates, %llu kept, %llu lost, %u runs with the filter, %u without\n", (unsigned long long)totals->candidates,
               (unsigned long long)totals->kept, (unsigned long long)totals->lost, totals->applied, totals->skipped);
        for (uint32_t op = 0; op <= CHECK_OP_COUNT; op += 1)
        {
            if (totals->found[op] != 0 || totals->taken[op] != 0)
            {
                printf("reach filter: operator %u: %llu with a bit, %llu taken out, %llu nodes offered a shape\n", op, (unsigned long long)totals->found[op],
                       (unsigned long long)totals->taken[op], (unsigned long long)(op < CHECK_OP_COUNT ? check_test_reach_shaped[op] : 0));
            }
        }
    }
    free(totals);
    arena_release(&scratch);
    arena_release(&type_index_storage);
    arena_release(&type_storage);
    check_common_resources = saved;
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
    check_test_common_reach_filter(test);
}
