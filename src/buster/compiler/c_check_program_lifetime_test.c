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

// #1355: the blocks a jump enters (check_program_lifetime_entered) and the lifetime ends a jump makes
// (check_control_lifetimes_left), each against the ancestor walks they replaced, kept here as the oracle: random forests
// whose blocks are runs from their opening brace to their closing one and whose heads come before their statements, as
// the parser's trees are (docs/c-parser.md's guarantees), with gotos into and out of blocks, computed gotos, labels whose
// address is taken, cases (their switch's, another token's or none), asm gotos, statement expressions' blocks and
// functions inside blocks; then real sources' programs.
#define BUSTER_CHECK_TEST_LIFETIME_TOKENS 768u
#define BUSTER_CHECK_TEST_LIFETIME_FRAMES 40u
#define BUSTER_CHECK_TEST_LIFETIME_ASMS 12u
#define BUSTER_CHECK_TEST_LIFETIME_REGIONS 6u
// A token whose parent is the block or body of the frame at depth (the low bits), whose closing brace is still to come.
#define BUSTER_CHECK_TEST_LIFETIME_PENDING 0x80000000u

static uint64_t check_test_lifetime_random(uint64_t* state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * UINT64_C(2685821657736338717);
}

typedef enum CheckTestLifetimeFrameKind
{
    CHECK_TEST_LIFETIME_BODY,
    CHECK_TEST_LIFETIME_BLOCK,
    CHECK_TEST_LIFETIME_HEAD,
} CheckTestLifetimeFrameKind;

typedef struct CheckTestLifetimeFrame CheckTestLifetimeFrame;
struct CheckTestLifetimeFrame
{
    CheckTestLifetimeFrameKind kind;
    // A head's token, the parent of its statement; BUSTER_CHECK_NONE for a block or a body until its closing brace.
    uint32_t root;
    uint32_t opening;
    uint32_t remaining;
    // The innermost switch whose statement holds the frame's statements, BUSTER_CHECK_NONE for none.
    uint32_t owner;
    bool alternate;
};

typedef struct CheckTestLifetimeForest CheckTestLifetimeForest;
struct CheckTestLifetimeForest
{
    uint32_t parents[BUSTER_CHECK_TEST_LIFETIME_TOKENS + 1];
    uint32_t label_targets[BUSTER_CHECK_TEST_LIFETIME_TOKENS + 1];
    uint32_t first[BUSTER_CHECK_TEST_LIFETIME_TOKENS + 1];
    uint32_t case_map[BUSTER_CHECK_TEST_LIFETIME_TOKENS + 1];
    uint32_t labels[BUSTER_CHECK_TEST_LIFETIME_TOKENS];
    uint8_t roles[BUSTER_CHECK_TEST_LIFETIME_TOKENS + 1];
    uint8_t kinds[BUSTER_CHECK_TEST_LIFETIME_TOKENS + 1];
    uint8_t addressed[BUSTER_CHECK_TEST_LIFETIME_TOKENS + 1];
    CheckProgramCase cases[BUSTER_CHECK_TEST_LIFETIME_TOKENS];
    CheckInputAsm asms[BUSTER_CHECK_TEST_LIFETIME_ASMS];
    CheckAsmLabel asm_labels[BUSTER_CHECK_TEST_LIFETIME_ASMS * 3];
    CheckInputRegion regions[BUSTER_CHECK_TEST_LIFETIME_REGIONS];
    uint32_t count;
    uint32_t label_count;
    uint32_t case_count;
    uint32_t asm_count;
    uint32_t region_count;
};

static uint32_t check_test_lifetime_emit(CheckTestLifetimeForest* forest, uint8_t role, uint8_t kind, uint32_t parent)
{
    const uint32_t token = forest->count;
    forest->roles[token] = role;
    forest->kinds[token] = kind;
    forest->parents[token] = parent;
    forest->count += 1;
    return token;
}

// The parent a statement of the frame at `depth` takes: its head, or its block or body once that closes.
static uint32_t check_test_lifetime_statement_parent(const CheckTestLifetimeFrame* frames, uint32_t depth)
{
    const CheckTestLifetimeFrame* frame = frames + depth;
    return frame->kind == CHECK_TEST_LIFETIME_HEAD ? frame->root : BUSTER_CHECK_TEST_LIFETIME_PENDING | depth;
}

// A random forest of `budget` tokens at most (an explicit stack of frames, no recursion): functions whose statements are
// expressions, gotos, computed gotos, breaks, continues, asm gotos, blocks, heads (if with or without an else, while,
// switch, for, do, a label, a case) with their statement, and functions inside blocks. Then its jumps' targets, labels whose
// address is taken, cases' switches, asm gotos' labels, statement expressions' blocks and the blocks with locals.
static void check_test_lifetime_forest(CheckTestLifetimeForest* forest, uint64_t seed, uint32_t budget)
{
    uint64_t state = seed * 2 + 1;
    CheckTestLifetimeFrame frames[BUSTER_CHECK_TEST_LIFETIME_FRAMES];
    memset(forest, 0, sizeof(*forest));
    memset(forest->case_map, 0xff, sizeof(forest->case_map));
    const uint32_t limit = budget < BUSTER_CHECK_TEST_LIFETIME_TOKENS ? budget : BUSTER_CHECK_TEST_LIFETIME_TOKENS;
    for (uint32_t functions = 0; forest->count + 16 < limit && functions < 64; functions += 1)
    {
        const uint32_t body = check_test_lifetime_emit(forest, PARSE_ROLE_OPEN_BODY, PARSE_KIND_L_BRACE, BUSTER_CHECK_NONE);
        frames[0] = (CheckTestLifetimeFrame){ .kind = CHECK_TEST_LIFETIME_BODY,
                                              .root = BUSTER_CHECK_NONE,
                                              .opening = body,
                                              .remaining = 1 + (uint32_t)(check_test_lifetime_random(&state) % 6),
                                              .owner = BUSTER_CHECK_NONE };
        uint32_t depth = 1;
        for (uint32_t steps = 0; depth != 0 && steps < 4 * BUSTER_CHECK_TEST_LIFETIME_TOKENS; steps += 1)
        {
            CheckTestLifetimeFrame* top = frames + depth - 1;
            const bool full = forest->count + 12 >= limit;
            const uint32_t choice = (uint32_t)(check_test_lifetime_random(&state) % 100);
            const uint32_t parent = check_test_lifetime_statement_parent(frames, depth - 1);
            if (top->remaining == 0 || full)
            {
                if (top->kind == CHECK_TEST_LIFETIME_HEAD && top->alternate && !full)
                {
                    // An if's else: its statement is the else's, the else the if's head's.
                    top->root = check_test_lifetime_emit(forest, PARSE_ROLE_ELSE, PARSE_KIND_ELSE, top->root);
                    top->remaining = 1;
                    top->alternate = false;
                }
                else
                {
                    uint32_t closing = BUSTER_CHECK_NONE;
                    if (top->kind != CHECK_TEST_LIFETIME_HEAD)
                    {
                        const bool block = top->kind == CHECK_TEST_LIFETIME_BLOCK;
                        closing = check_test_lifetime_emit(forest, block ? PARSE_ROLE_CLOSE_BLOCK : PARSE_ROLE_CLOSE_BODY, PARSE_KIND_R_BRACE, BUSTER_CHECK_NONE);
                        forest->parents[top->opening] = closing;
                        for (uint32_t token = top->opening + 1; token < closing; token += 1)
                        {
                            forest->parents[token] = forest->parents[token] == (BUSTER_CHECK_TEST_LIFETIME_PENDING | (depth - 1)) ? closing : forest->parents[token];
                        }
                    }
                    depth -= 1;
                    if (depth != 0)
                    {
                        if (closing != BUSTER_CHECK_NONE)
                        {
                            forest->parents[closing] = check_test_lifetime_statement_parent(frames, depth - 1);
                        }
                        frames[depth - 1].remaining -= frames[depth - 1].remaining != 0;
                    }
                }
            }
            else if (choice < 18 || depth + 1 >= BUSTER_CHECK_TEST_LIFETIME_FRAMES)
            {
                const uint32_t leaf = check_test_lifetime_emit(forest, PARSE_ROLE_OPERAND, PARSE_KIND_IDENTIFIER, BUSTER_CHECK_NONE);
                forest->parents[leaf] = check_test_lifetime_emit(forest, PARSE_ROLE_TERMINATOR, PARSE_KIND_SEMI, parent);
                top->remaining -= 1;
            }
            else if (choice < 34)
            {
                // A goto, or (one in four) a computed one, whose `*` is the goto's child.
                const bool computed = choice < 22;
                const uint32_t jump = check_test_lifetime_emit(forest, PARSE_ROLE_RETURN, PARSE_KIND_GOTO, BUSTER_CHECK_NONE);
                const uint8_t name_role = computed ? PARSE_ROLE_RETURN : PARSE_ROLE_OPERAND;
                const uint8_t name_kind = computed ? PARSE_KIND_STAR : PARSE_KIND_IDENTIFIER;
                const uint32_t name = check_test_lifetime_emit(forest, name_role, name_kind, jump);
                if (computed)
                {
                    check_test_lifetime_emit(forest, PARSE_ROLE_OPERAND, PARSE_KIND_IDENTIFIER, name);
                }
                forest->parents[jump] = check_test_lifetime_emit(forest, PARSE_ROLE_TERMINATOR, PARSE_KIND_SEMI, parent);
                top->remaining -= 1;
            }
            else if (choice < 40)
            {
                const uint32_t jump = check_test_lifetime_emit(forest, PARSE_ROLE_RETURN, choice < 37 ? PARSE_KIND_BREAK : PARSE_KIND_CONTINUE, BUSTER_CHECK_NONE);
                forest->parents[jump] = check_test_lifetime_emit(forest, PARSE_ROLE_TERMINATOR, PARSE_KIND_SEMI, parent);
                top->remaining -= 1;
            }
            else if (choice < 43 && forest->asm_count < BUSTER_CHECK_TEST_LIFETIME_ASMS)
            {
                // An asm goto's statement, whose labels come below.
                const uint32_t statement = check_test_lifetime_emit(forest, PARSE_ROLE_TERMINATOR, PARSE_KIND_SEMI, parent);
                forest->asms[forest->asm_count].statement = choice < 42 ? statement : forest->count + 1000;
                forest->asm_count += 1;
                top->remaining -= 1;
            }
            else if (choice < 60)
            {
                const uint32_t opening = check_test_lifetime_emit(forest, PARSE_ROLE_OPEN_BLOCK, PARSE_KIND_L_BRACE, BUSTER_CHECK_NONE);
                frames[depth] = (CheckTestLifetimeFrame){ .kind = CHECK_TEST_LIFETIME_BLOCK,
                                                          .root = BUSTER_CHECK_NONE,
                                                          .opening = opening,
                                                          .remaining = (uint32_t)(check_test_lifetime_random(&state) % 5),
                                                          .owner = top->owner };
                depth += 1;
            }
            else if (choice < 96 || top->kind != CHECK_TEST_LIFETIME_BLOCK)
            {
                // A head, after a keyword whose parent it is: an if (half with an else, an else-if chain's links), a
                // while, a switch, a for, a do, a label or a case.
                static const uint8_t shape_roles[9] = {
                    PARSE_ROLE_CLOSE_CONDITION,
                    PARSE_ROLE_CLOSE_CONDITION,
                    PARSE_ROLE_CLOSE_CONDITION,
                    PARSE_ROLE_CLOSE_CONDITION,
                    PARSE_ROLE_CLOSE_CONDITION,
                    PARSE_ROLE_CLOSE_FOR,
                    PARSE_ROLE_DO,
                    PARSE_ROLE_LABEL,
                    PARSE_ROLE_CLOSE_CASE,
                };
                static const uint8_t shape_kinds[9] = {
                    PARSE_KIND_IF,
                    PARSE_KIND_IF,
                    PARSE_KIND_IF,
                    PARSE_KIND_WHILE,
                    PARSE_KIND_SWITCH,
                    PARSE_KIND_IDENTIFIER,
                    PARSE_KIND_IDENTIFIER,
                    PARSE_KIND_IDENTIFIER,
                    PARSE_KIND_IDENTIFIER,
                };
                const uint32_t shape = (uint32_t)(check_test_lifetime_random(&state) % 9);
                const uint8_t role = shape_roles[shape];
                const uint8_t kind = shape_kinds[shape];
                const uint32_t keyword = role == PARSE_ROLE_CLOSE_CONDITION ? check_test_lifetime_emit(forest, PARSE_ROLE_LEAF_RIGHT, kind, BUSTER_CHECK_NONE) : BUSTER_CHECK_NONE;
                const uint32_t head = check_test_lifetime_emit(forest, role, kind, parent);
                if (keyword != BUSTER_CHECK_NONE)
                {
                    forest->parents[keyword] = head;
                }
                if (role == PARSE_ROLE_LABEL)
                {
                    forest->labels[forest->label_count] = head;
                    forest->label_count += 1;
                }
                if (role == PARSE_ROLE_CLOSE_CASE)
                {
                    forest->case_map[head] = forest->case_count;
                    forest->cases[forest->case_count] = (CheckProgramCase){ .token = head, .owner = top->owner, .target = BUSTER_CHECK_NONE };
                    forest->case_count += 1;
                }
                frames[depth] = (CheckTestLifetimeFrame){ .kind = CHECK_TEST_LIFETIME_HEAD,
                                                          .root = head,
                                                          .remaining = 1,
                                                          .owner = kind == PARSE_KIND_SWITCH ? head : top->owner,
                                                          .alternate = shape <= 1 };
                depth += 1;
            }
            else
            {
                // A function inside a block (GNU's nested functions): its own body, its parent's statement.
                const uint32_t opening = check_test_lifetime_emit(forest, PARSE_ROLE_OPEN_BODY, PARSE_KIND_L_BRACE, BUSTER_CHECK_NONE);
                frames[depth] = (CheckTestLifetimeFrame){ .kind = CHECK_TEST_LIFETIME_BODY,
                                                          .root = BUSTER_CHECK_NONE,
                                                          .opening = opening,
                                                          .remaining = 1 + (uint32_t)(check_test_lifetime_random(&state) % 3),
                                                          .owner = BUSTER_CHECK_NONE };
                depth += 1;
            }
        }
    }
    const uint32_t count = forest->count;
    // Each goto's label: mostly a label, else no token, or any token at all; a computed goto's isn't read.
    for (uint32_t token = 0; token < count; token += 1)
    {
        const uint32_t pick = (uint32_t)(check_test_lifetime_random(&state) % 20);
        const uint32_t label = forest->label_count != 0 ? forest->labels[check_test_lifetime_random(&state) % forest->label_count] : BUSTER_CHECK_NONE;
        const uint32_t any = (uint32_t)(check_test_lifetime_random(&state) % (count + 1));
        forest->label_targets[token] = forest->kinds[token] != PARSE_KIND_GOTO ? count : pick == 0 ? count : pick == 1 ? any : label < count ? label : count;
        forest->addressed[token] = forest->roles[token] == PARSE_ROLE_LABEL && check_test_lifetime_random(&state) % 7 == 0;
        // The blocks with locals: seven in ten blocks, and a few other tokens, which no pass may read as blocks.
        const uint32_t odds = (uint32_t)(check_test_lifetime_random(&state) % 10);
        const bool block = forest->roles[token] == PARSE_ROLE_CLOSE_BLOCK;
        forest->first[token] = (block && odds < 7) || (!block && odds == 0) ? (token != 0 ? token - 1 : 0) : BUSTER_CHECK_NONE;
    }
    forest->first[count] = BUSTER_CHECK_NONE;
    // A case's switch, else another token, or none.
    for (uint32_t c = 0; c < forest->case_count; c += 1)
    {
        const uint32_t pick = (uint32_t)(check_test_lifetime_random(&state) % 12);
        forest->cases[c].owner = pick == 0 ? BUSTER_CHECK_NONE : pick == 1 ? (uint32_t)(check_test_lifetime_random(&state) % count) : forest->cases[c].owner;
    }
    for (uint32_t a = 0; a < forest->asm_count; a += 1)
    {
        const uint32_t labels = forest->label_count != 0 ? (uint32_t)(check_test_lifetime_random(&state) % 4) : 0;
        forest->asms[a].labels = forest->asm_labels + a * 3;
        forest->asms[a].label_count = labels < 3 ? labels : 3;
        for (uint32_t l = 0; l < forest->asms[a].label_count; l += 1)
        {
            forest->asm_labels[a * 3 + l].target = forest->labels[check_test_lifetime_random(&state) % forest->label_count];
        }
    }
    for (uint32_t token = 0; token < count && forest->region_count < BUSTER_CHECK_TEST_LIFETIME_REGIONS; token += 1)
    {
        if (forest->roles[token] == PARSE_ROLE_CLOSE_BLOCK && check_test_lifetime_random(&state) % 9 == 0)
        {
            forest->regions[forest->region_count].block = token;
            forest->region_count += 1;
        }
    }
}

// The blocks a jump enters, as check_program_lifetime_entered found them before #1355: each jump's source and its
// ancestors stamped, then a walk from its target's parent clearing blocks up to the first stamped ancestor or a function's
// body (every one up to the body for a label whose address is taken, `epoch` 0). Each walk adds its steps to `work`, the
// old passes' cost, which the tests print beside the new ones'.
static void check_test_lifetime_bypass(const Parser* parser, const uint32_t* stamps, uint32_t epoch, uint32_t token, uint32_t* first, uint64_t* work)
{
    const uint32_t count = parser->tokens.count;
    uint32_t ancestor = token < count ? parser->tree.parents[token] : count;
    uint32_t steps = 0;
    while (ancestor < count && steps < count && (epoch == 0 || stamps[ancestor] != epoch) && parser->tree.roles[ancestor] != PARSE_ROLE_CLOSE_BODY)
    {
        if (parser->tree.roles[ancestor] == PARSE_ROLE_CLOSE_BLOCK)
        {
            first[ancestor] = BUSTER_CHECK_NONE;
        }
        ancestor = parser->tree.parents[ancestor];
        steps += 1;
    }
    *work += steps;
}

static void check_test_lifetime_stamp(const Parser* parser, uint32_t* stamps, uint32_t epoch, uint32_t token, uint64_t* work)
{
    const uint32_t count = parser->tokens.count;
    uint32_t ancestor = token;
    uint32_t steps = 0;
    while (ancestor < count && steps <= count)
    {
        stamps[ancestor] = epoch;
        ancestor = parser->tree.parents[ancestor];
        steps += 1;
    }
    *work += steps;
}

static void check_test_lifetime_entered_oracle(const Parser* parser, const CheckInput* input, const CheckProgramCase* cases, uint32_t case_count, const uint8_t* addressed,
                                               uint32_t* stamps, uint32_t* first, uint64_t* work)
{
    const uint32_t count = parser->tokens.count;
    memset(stamps, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
    uint32_t epoch = 0;
    for (uint32_t token = 0; token < count; token += 1)
    {
        const bool jump = parser->tree.roles[token] == PARSE_ROLE_RETURN && parser->tokens.kinds[token] == PARSE_KIND_GOTO && !check_input_indirect_goto(parser, token);
        const uint32_t label = jump ? check_sparse_get(input->label_targets, token) : BUSTER_CHECK_NONE;
        if (label < count)
        {
            epoch += 1;
            check_test_lifetime_stamp(parser, stamps, epoch, token, work);
            check_test_lifetime_bypass(parser, stamps, epoch, label, first, work);
        }
        if (addressed != NULL && addressed[token] != 0)
        {
            check_test_lifetime_bypass(parser, stamps, 0, token, first, work);
        }
    }
    for (uint32_t c = 0; c < case_count; c += 1)
    {
        epoch += 1;
        check_test_lifetime_stamp(parser, stamps, epoch, cases[c].owner, work);
        check_test_lifetime_bypass(parser, stamps, epoch, cases[c].token, first, work);
    }
    for (uint32_t a = 0; a < input->asm_count; a += 1)
    {
        const CheckInputAsm* assembly = input->asm_statements + a;
        if (assembly->label_count != 0 && assembly->statement < count)
        {
            epoch += 1;
            check_test_lifetime_stamp(parser, stamps, epoch, assembly->statement, work);
            for (uint32_t l = 0; l < assembly->label_count; l += 1)
            {
                check_test_lifetime_bypass(parser, stamps, epoch, assembly->labels[l].target, first, work);
            }
        }
    }
    for (uint32_t r = 0; r < input->region_count; r += 1)
    {
        if (input->regions[r].block < count)
        {
            first[input->regions[r].block] = BUSTER_CHECK_NONE;
        }
    }
}

// Each block's opening brace, by the block (a token's room and one more): its first child of role OPEN_BLOCK,
// BUSTER_CHECK_NONE for none.
static void check_test_lifetime_openings(const Parser* parser, uint32_t* openings)
{
    const uint32_t count = parser->tokens.count;
    memset(openings, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
    for (uint32_t token = 0; token < count; token += 1)
    {
        const uint32_t parent = parser->tree.parents[token];
        if (parser->tree.roles[token] == PARSE_ROLE_OPEN_BLOCK && parent < count && openings[parent] == BUSTER_CHECK_NONE)
        {
            openings[parent] = token;
        }
    }
}

// The lifetime ends of a jump at `jump`, innermost first, as check_control_lifetimes_left made them before #1355: a walk
// of every ancestor to `closing`, or to `stop` (a break's or continue's loop or switch), or for a goto (`goto_label` a
// token) to the first block with lifetimes that holds its label (`openings`, by check_test_lifetime_openings). Returns how
// many it wrote to `blocks`, and adds its steps to `work`.
static uint32_t check_test_lifetime_left_oracle(const Parser* parser, const uint32_t* first, const uint32_t* openings, uint32_t closing, uint32_t jump, uint32_t stop,
                                                bool jumps_to_label, uint32_t goto_label, uint32_t* blocks, uint64_t* work)
{
    const uint32_t count = parser->tokens.count;
    uint32_t written = 0;
    bool past = false;
    uint32_t steps = 0;
    for (uint32_t ancestor = parser->tree.parents[jump]; !past && ancestor < count && steps < count; ancestor = parser->tree.parents[ancestor])
    {
        past = ancestor == stop || ancestor == closing;
        if (!past && parser->tree.roles[ancestor] == PARSE_ROLE_CLOSE_BLOCK && first[ancestor] != BUSTER_CHECK_NONE)
        {
            const uint32_t opening = openings[ancestor];
            past = jumps_to_label && opening != BUSTER_CHECK_NONE && goto_label > opening && goto_label < ancestor;
            if (!past)
            {
                blocks[written] = ancestor;
                written += 1;
            }
        }
        steps += 1;
    }
    *work += steps;
    return written;
}

// The jump's CHECK_CONTROL_LIFETIME ends among the writer's tasks, in their order, against the oracle's blocks.
static bool check_test_lifetime_left_tasks(const CheckControlWriter* writer, uint32_t jump, const uint32_t* blocks, uint32_t expected)
{
    bool equal = writer->depth == expected;
    for (uint32_t t = 0; equal && t < expected; t += 1)
    {
        const CheckControl entry = writer->tasks[t].entry;
        equal = writer->tasks[t].kind == CHECK_CONTROL_TASK_ENTRY && entry.kind == CHECK_CONTROL_LIFETIME && entry.token == jump && entry.node == 1 && entry.target == blocks[t];
    }
    return equal;
}

// Counts of what a forest round compared, for its report: and the steps of the new walks (`work`, the parent paths' and
// the writer's) and of the old ones (`old_work`, the oracles').
typedef struct CheckTestLifetimeTally CheckTestLifetimeTally;
struct CheckTestLifetimeTally
{
    uint64_t blocks;
    uint64_t entered;
    uint64_t jumps;
    uint64_t entries;
    uint64_t kept;
    uint64_t work;
    uint64_t old_work;
};

// One forest: check_program_lifetime_entered's blocks against the oracle's and its chain against a walk to each token's
// innermost block with lifetimes; then each body written as the writer writes it (check_control_children, its labels from
// 0 by check_control_label, a label of its own for each loop or switch, kept in the head's stops and repeats), and each of
// its jumps, in a random order so that any of them can be the first to a label, against the oracle's walk: a goto to its
// label, a break or continue to each loop around it and to a label no loop has. The parent paths take two steps a token
// at most in each of their two passes, and a jump to a label whose stop is kept walks only the blocks it ends and the one
// where it stops (CheckControlWriter.lifetime_steps), with no walk to its loop.
static bool check_test_lifetime_forest_case(CheckTestLifetimeForest* forest, Arena* arena, uint64_t seed, uint32_t budget, CheckTestLifetimeTally* tally)
{
    check_test_lifetime_forest(forest, seed, budget);
    const uint32_t count = forest->count;
    const uint64_t slots = (uint64_t)count + 1;
    const uint64_t saved = arena->length;
    const uint32_t capacity = count * 4 + 16;
    uint32_t* columns = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots * 12 + (uint64_t)capacity * 2);
    CheckControlTask* tasks = BUSTER_ARENA_PUSH_ARRAY(arena, CheckControlTask, capacity);
    const CheckSparseColumn label_targets = columns != NULL ? check_sparse_from_values(arena, forest->label_targets, count) : (CheckSparseColumn){ 0 };
    bool equal = columns != NULL && tasks != NULL && check_sparse_present(label_targets);
    if (equal)
    {
        uint32_t* nearest = columns;
        uint32_t* sources = nearest + slots;
        uint32_t* first = sources + slots;
        uint32_t* expected_first = first + slots;
        uint32_t* stamps = expected_first + slots;
        uint32_t* children_first = stamps + slots;
        uint32_t* children_last = children_first + slots;
        uint32_t* children_next = children_last + slots;
        uint32_t* children_previous = children_next + slots;
        uint32_t* stops = children_previous + slots;
        uint32_t* repeats = stops + slots;
        uint32_t* openings = repeats + slots;
        uint32_t* labels = openings + slots;
        uint32_t* lifetime_stops = labels + capacity;
        Parser parser = { 0 };
        parser.tokens.count = count;
        parser.tokens.kinds = forest->kinds;
        parser.tree.parents = forest->parents;
        parser.tree.roles = forest->roles;
        const CheckInput input = {
            .token_count = count,
            .label_targets = label_targets,
            .asm_statements = forest->asms,
            .asm_count = forest->asm_count,
            .regions = forest->regions,
            .region_count = forest->region_count,
        };
        CheckProgram program = {
            .input = &input,
            .cases = forest->cases,
            .case_count = forest->case_count,
            .status = CHECK_PROGRAM_OK,
            .token = BUSTER_CHECK_NONE,
        };
        memcpy(first, forest->first, slots * sizeof(uint32_t));
        memcpy(expected_first, forest->first, slots * sizeof(uint32_t));
        memset(nearest, 0xa5, slots * sizeof(uint32_t));
        memset(sources, 0xa5, slots * sizeof(uint32_t));
        const uint64_t entered_work = check_program_lifetime_entered(&program, &parser, forest->addressed, nearest, sources, first);
        check_test_lifetime_entered_oracle(&parser, &input, forest->cases, forest->case_count, forest->addressed, stamps, expected_first, &tally->old_work);
        check_test_lifetime_openings(&parser, openings);
        equal = memcmp(first, expected_first, slots * sizeof(uint32_t)) == 0 && entered_work <= (uint64_t)count * 4;
        tally->work += entered_work;
        for (uint32_t token = 0; token < count; token += 1)
        {
            const bool block = forest->roles[token] == PARSE_ROLE_CLOSE_BLOCK;
            tally->blocks += block && forest->first[token] != BUSTER_CHECK_NONE;
            tally->entered += block && forest->first[token] != BUSTER_CHECK_NONE && first[token] == BUSTER_CHECK_NONE;
            // The chain the writer follows: the innermost block with lifetimes above, past function bodies.
            uint32_t ancestor = forest->parents[token];
            for (uint32_t steps = 0; ancestor < count && steps < count && !(forest->roles[ancestor] == PARSE_ROLE_CLOSE_BLOCK && first[ancestor] != BUSTER_CHECK_NONE); steps += 1)
            {
                ancestor = forest->parents[ancestor];
            }
            equal = equal && nearest[token] == (ancestor < count ? ancestor : count);
        }
        program.lifetime_first = first;
        CheckControlWriter writer = {
            .program = &program,
            .parser = &parser,
            .first = children_first,
            .last = children_last,
            .next = children_next,
            .previous = children_previous,
            .case_map = forest->case_map,
            .source_labels = sources,
            .stops = stops,
            .repeats = repeats,
            .labels = labels,
            .tasks = tasks,
            .capacity = capacity,
            .lifetime_nearest = nearest,
            .lifetime_stops = lifetime_stops,
        };
        memset(lifetime_stops, 0xa5, (uint64_t)capacity * sizeof(uint32_t));
        uint64_t state = seed * 4 + 3;
        uint32_t order[BUSTER_CHECK_TEST_LIFETIME_TOKENS];
        uint32_t loops[BUSTER_CHECK_TEST_LIFETIME_FRAMES];
        uint32_t blocks[BUSTER_CHECK_TEST_LIFETIME_TOKENS];
        for (uint32_t opening = 0; equal && opening < count; opening += 1)
        {
            const uint32_t closing = forest->parents[opening];
            if (forest->roles[opening] == PARSE_ROLE_OPEN_BODY && closing < count)
            {
                writer.label_count = 0;
                writer.opening = opening;
                writer.closing = closing;
                check_control_children(&writer, opening, closing);
                uint32_t jump_count = 0;
                for (uint32_t token = opening + 1; token < closing; token += 1)
                {
                    // The body's own tokens: none past a function inside it.
                    uint32_t ancestor = forest->parents[token];
                    for (uint32_t steps = 0; ancestor < count && steps < count && forest->roles[ancestor] != PARSE_ROLE_CLOSE_BODY; steps += 1)
                    {
                        ancestor = forest->parents[ancestor];
                    }
                    const uint8_t kind = forest->kinds[token];
                    const bool loop = forest->roles[token] == PARSE_ROLE_CLOSE_FOR || forest->roles[token] == PARSE_ROLE_DO ||
                                      (forest->roles[token] == PARSE_ROLE_CLOSE_CONDITION && (kind == PARSE_KIND_WHILE || kind == PARSE_KIND_SWITCH)) ||
                                      (forest->roles[token] == PARSE_ROLE_CLOSE_BLOCK && token % 5 == 0);
                    if (ancestor == closing && loop)
                    {
                        writer.stops[token] = check_control_label(&writer);
                        writer.repeats[token] = check_control_label(&writer);
                    }
                    if (ancestor == closing && forest->roles[token] == PARSE_ROLE_RETURN && (kind == PARSE_KIND_GOTO || kind == PARSE_KIND_BREAK || kind == PARSE_KIND_CONTINUE))
                    {
                        order[jump_count] = token;
                        jump_count += 1;
                    }
                }
                const uint32_t orphan = check_control_label(&writer);
                for (uint32_t j = jump_count; j > 1; j -= 1)
                {
                    const uint32_t other = (uint32_t)(check_test_lifetime_random(&state) % j);
                    const uint32_t held = order[j - 1];
                    order[j - 1] = order[other];
                    order[other] = held;
                }
                for (uint32_t j = 0; equal && j < jump_count; j += 1)
                {
                    const uint32_t jump = order[j];
                    uint32_t loop_count = 0;
                    for (uint32_t ancestor = forest->parents[jump]; ancestor < closing && loop_count < BUSTER_CHECK_TEST_LIFETIME_FRAMES; ancestor = forest->parents[ancestor])
                    {
                        loops[loop_count] = ancestor;
                        loop_count += writer.stops[ancestor] != BUSTER_CHECK_NONE;
                    }
                    const bool to_label = forest->kinds[jump] == PARSE_KIND_GOTO;
                    for (uint32_t l = 0; equal && l <= (to_label ? 0 : loop_count); l += 1)
                    {
                        const bool stopping = l % 2 == 0;
                        const uint32_t* targets = to_label ? NULL : stopping ? writer.stops : writer.repeats;
                        const uint32_t stop = !to_label && l < loop_count ? loops[l] : BUSTER_CHECK_NONE;
                        const uint32_t target = to_label ? BUSTER_CHECK_NONE : stop != BUSTER_CHECK_NONE ? targets[stop] : orphan;
                        const uint32_t label = to_label ? forest->label_targets[jump] : BUSTER_CHECK_NONE;
                        const bool known = !to_label && writer.lifetime_stops[target] != BUSTER_CHECK_NONE;
                        writer.depth = 0;
                        const uint64_t before = writer.lifetime_steps;
                        check_control_lifetimes_left(&writer, jump, label, targets, target);
                        const uint64_t steps = writer.lifetime_steps - before;
                        const uint32_t expected = check_test_lifetime_left_oracle(&parser, first, openings, closing, jump, stop, to_label, label, blocks, &tally->old_work);
                        equal = check_test_lifetime_left_tasks(&writer, jump, blocks, expected) && program.status == CHECK_PROGRAM_OK && (!known || steps <= (uint64_t)expected + 1);
                        tally->work += steps;
                        tally->jumps += 1;
                        tally->entries += expected;
                        tally->kept += known;
                    }
                }
            }
        }
    }
    arena_pop_to(arena, saved);
    return equal;
}

// Appends to a generated source, failing (`*fits` false) where it would pass `size`.
static void check_test_lifetime_append(char* text, size_t size, size_t* length, bool* fits, const char* piece)
{
    const size_t added = strlen(piece);
    *fits = *fits && *length + added < size;
    if (*fits)
    {
        memcpy(text + *length, piece, added + 1);
        *length += added;
    }
}

// A loop or switch a break (`stops`) or a continue at `jump` leaves to, in a source of for, while, do and switch statements:
// its first ancestor that is a for, a do, or a condition whose keyword (`keywords`, by the condition's closer) is a while,
// or for a break a switch; BUSTER_CHECK_NONE for none. A for's, a while's or a switch's head counts only where the jump
// follows it, in its statement: a jump in a statement expression of its controlling expressions leaves the loop around
// it, as check_control_build gives a region its stop and repeat (GNU's break and continue).
static uint32_t check_test_lifetime_loop(const Parser* parser, const uint32_t* keywords, uint32_t jump, bool stops)
{
    const uint32_t count = parser->tokens.count;
    uint32_t ancestor = parser->tree.parents[jump];
    bool found = false;
    for (uint32_t steps = 0; ancestor < count && steps < count && !found; steps += 1)
    {
        const uint8_t role = parser->tree.roles[ancestor];
        const uint8_t kind = role == PARSE_ROLE_CLOSE_CONDITION && keywords[ancestor] < count ? parser->tokens.kinds[keywords[ancestor]] : PARSE_KIND_IDENTIFIER;
        const bool loop = role == PARSE_ROLE_CLOSE_FOR || role == PARSE_ROLE_DO || kind == PARSE_KIND_WHILE || (stops && kind == PARSE_KIND_SWITCH);
        found = loop && (jump > ancestor || role == PARSE_ROLE_DO);
        ancestor = found ? ancestor : parser->tree.parents[ancestor];
    }
    return found ? ancestor : BUSTER_CHECK_NONE;
}

// A real source's program with the blocks' lifetimes, as the driver's --tier1 asks for them: its lifetime_first against
// the oracle's blocks (the locals by check_test_lifetime_ancestor_room, the blocks jumps enter by the walks), and each
// goto's, break's and continue's lifetime ends among its controls (outermost first, as the writer's worklist writes them)
// against the old walk's, a break's and continue's loop found by its statements. `bypassed` is how many blocks with locals
// the jumps must enter at least, `ends` how many of the jumps' ends the program must make at least. The walks' steps
// (CheckProgramOptions.lifetime_work) are at most eight a token, one a jump and one an end: the parent paths take four a
// token at most and the walks to the loops two, while the old walks' are quadratic in an else-if chain's depth.
static void check_test_program_lifetime_source(CheckTest* test, const char* source, const char* name, uint32_t bypassed, uint32_t ends, CheckTestLifetimeTally* tally)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = {
        .language = { .standard = 2017, .gnu = true },
        .no_standard_includes = true,
    };
    const ParseOptions parse_options = { 0 };
    const CheckOptions options = { 0 };
    CheckTestTypes types = { 0 };
    Arena input_arena = arena_reserve(UINT64_C(1) << 30);
    Arena scratch = arena_reserve(UINT64_C(1) << 30);
    bool ready = input_arena.base != NULL && scratch.base != NULL && check_test_types_open(&types) && intern_initialize(&identifiers) &&
                 pp_initialize(&pp, &pp_options, &identifiers) && pp_run_source(&pp, name, (const uint8_t*)source, (uint32_t)strlen(source)) &&
                 parse_initialize(&parser, &parse_options, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0;
    CheckInput input = { 0 };
    CheckStream stream = { 0 };
    CheckInitializers initializers = { 0 };
    CheckAsms asms = { 0 };
    CheckDataPrepared data = { 0 };
    CheckProgram program = { 0 };
    ready = ready && check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
            check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &options, &stream) && stream.report_count == 0 &&
            check_initializers_run(&initializers, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
            check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch) &&
            check_data_prepare(&data, &input, &stream, &initializers, &types.table, parser.options.scalar, &input_arena, &scratch);
    uint64_t work = 0;
    const CheckProgramOptions program_options = {
        .initializers = &initializers,
        .asms = &asms,
        .prepared_data = &data,
        .identifiers = &identifiers,
        .initializer_references = data.references,
        .initializer_reference_count = data.reference_count,
        .lifetimes = true,
        .lifetime_work = &work,
    };
    ready = ready && check_program(&program, &parser, &input, &stream, &types.table, program_options, &input_arena, &scratch) && program.status == CHECK_PROGRAM_OK &&
            program.lifetime_first != NULL;
    char description[256];
    snprintf(description, sizeof(description), "%s: the source's program has its blocks' lifetimes", name);
    check_test_expect(test, ready, description);
    const uint32_t count = ready ? input.token_count : 0;
    const uint64_t slots = (uint64_t)count + 1;
    uint32_t* columns = ready ? BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, slots * 8) : NULL;
    uint8_t* addressed = ready ? BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, slots) : NULL;
    ready = ready && columns != NULL && addressed != NULL;
    bool equal = ready;
    uint32_t entered = 0;
    uint32_t made = 0;
    uint32_t jumps = 0;
    uint64_t old_work = 0;
    if (ready)
    {
        uint32_t* expected_first = columns;
        uint32_t* expected_next = expected_first + slots;
        uint32_t* located = expected_next + slots;
        uint32_t* stamps = located + slots;
        uint32_t* keywords = stamps + slots;
        uint32_t* blocks = keywords + slots;
        uint32_t* actual = blocks + slots;
        uint32_t* openings = actual + slots;
        check_test_lifetime_ancestor_room(&parser, &input, expected_first, expected_next);
        check_test_lifetime_openings(&parser, openings);
        memcpy(located, expected_first, slots * sizeof(uint32_t));
        // The labels whose address is taken: a prefix &&'s label (check_program_addressed's own test's definition).
        memset(addressed, 0, slots);
        memset(keywords, 0xff, slots * sizeof(uint32_t));
        for (uint32_t token = 0; token < count; token += 1)
        {
            const bool address = parser.tokens.kinds[token] == PARSE_KIND_AMPAMP && parser.tree.roles[token] == PARSE_ROLE_PREFIX;
            const uint32_t label = address ? check_sparse_get(input.label_targets, token) : BUSTER_CHECK_NONE;
            addressed[label < count ? label : count] = label < count;
            // A condition's keyword: its last child before it of role LEAF_RIGHT, as check_control_expand finds it.
            const uint32_t parent = parser.tree.parents[token];
            const uint32_t slot = parent < count ? parent : count;
            const bool keyword = parent < count && token < parent && parser.tree.roles[token] == PARSE_ROLE_LEAF_RIGHT;
            keywords[slot] = keyword ? token : keywords[slot];
        }
        addressed[count] = 0;
        check_test_lifetime_entered_oracle(&parser, &input, program.cases, program.case_count, addressed, stamps, expected_first, &old_work);
        for (uint32_t token = 0; equal && token < count; token += 1)
        {
            equal = program.lifetime_first[token] == expected_first[token];
            entered += parser.tree.roles[token] == PARSE_ROLE_CLOSE_BLOCK && located[token] != BUSTER_CHECK_NONE && expected_first[token] == BUSTER_CHECK_NONE;
        }
        snprintf(description, sizeof(description), "%s: the blocks jumps enter are the stamped walks', %u of them (at least %u)", name, entered, bypassed);
        check_test_expect(test, equal && entered >= bypassed, description);
        for (uint32_t jump = 0; equal && jump < count; jump += 1)
        {
            const uint8_t kind = parser.tokens.kinds[jump];
            const bool to_label = kind == PARSE_KIND_GOTO && !check_input_indirect_goto(&parser, jump);
            if (parser.tree.roles[jump] == PARSE_ROLE_RETURN && (to_label || kind == PARSE_KIND_BREAK || kind == PARSE_KIND_CONTINUE))
            {
                uint32_t closing = parser.tree.parents[jump];
                for (uint32_t steps = 0; closing < count && steps < count && parser.tree.roles[closing] != PARSE_ROLE_CLOSE_BODY; steps += 1)
                {
                    closing = parser.tree.parents[closing];
                }
                const uint32_t stop = to_label ? BUSTER_CHECK_NONE : check_test_lifetime_loop(&parser, keywords, jump, kind == PARSE_KIND_BREAK);
                const uint32_t label = to_label ? check_sparse_get(input.label_targets, jump) : BUSTER_CHECK_NONE;
                const uint32_t expected = check_test_lifetime_left_oracle(&parser, program.lifetime_first, openings, closing, jump, stop, to_label, label, blocks, &old_work);
                uint32_t found = 0;
                for (uint32_t c = 0; c < program.control_count && found <= expected; c += 1)
                {
                    const CheckControl entry = program.controls[c];
                    if (entry.kind == CHECK_CONTROL_LIFETIME && entry.node == 1 && entry.token == jump)
                    {
                        actual[found < expected ? found : 0] = entry.target;
                        found += 1;
                    }
                }
                equal = found == expected;
                for (uint32_t b = 0; equal && b < expected; b += 1)
                {
                    equal = actual[b] == blocks[expected - 1 - b];
                }
                tally->jumps += 1;
                tally->entries += expected;
                made += expected;
                jumps += 1;
            }
        }
        snprintf(description, sizeof(description), "%s: each jump's lifetime ends are the ancestor walk's, %u of them (at least %u)", name, made, ends);
        check_test_expect(test, equal && made >= ends, description);
        const uint64_t bound = (uint64_t)count * 8 + jumps + made;
        snprintf(description, sizeof(description), "%s: the lifetime walks take %llu steps, at most %llu (8 a token, 1 a jump and 1 an end)", name,
                 (unsigned long long)work, (unsigned long long)bound);
        check_test_expect(test, work <= bound, description);
        tally->work += work;
        tally->old_work += old_work;
    }
    printf("  %s: %u tokens, %u blocks with locals entered by jumps, %u lifetime ends at %u jumps; %llu steps of the walks, the old walks' %llu\n", name, count,
           entered, made, jumps, (unsigned long long)work, (unsigned long long)old_work);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    check_test_types_close(&types);
    arena_release(&scratch);
    arena_release(&input_arena);
}

// Generated sources of `branches` arms each: else-if chains whose blocks have locals and leave by goto, gotos into
// other arms' blocks, switches whose cases are in nested blocks with locals, labels whose address is taken, asm gotos
// into blocks, and breaks and continues of for, while, do and switch statements at depth.
static void check_test_program_lifetime_sources(CheckTest* test, uint32_t branches, CheckTestLifetimeTally* tally)
{
    const size_t size = (size_t)branches * 640 + 4096;
    char* text = malloc(size);
    char piece[512];
    check_test_expect(test, text != NULL, "lifetime source fixtures' memory");
    for (uint32_t shape = 0; text != NULL && shape < 6; shape += 1)
    {
        size_t length = 0;
        bool fits = true;
        text[0] = 0;
        const char* const names[6] = { "lifetime-goto-out-chain", "lifetime-goto-into-blocks", "lifetime-nested-cases", "lifetime-addressed-labels", "lifetime-asm-goto",
                                       "lifetime-break-continue-chain" };
        const char* const heads[6] = { "int f(int x){int r=0;", "int f(int x){int r=0;if(x<0)goto in3;",
                                       "int f(int x){int r=0;", "int f(int x){static void*const t[]={&&l0,&&l1,&&l2,&&l3};int r=0;if(x<0)goto*t[(-x)&3];",
                                       "int f(int x){int r=0;", "int f(int x){int r=0;for(;;){" };
        check_test_lifetime_append(text, size, &length, &fits, heads[shape]);
        for (uint32_t k = 0; k < branches; k += 1)
        {
            const char* const otherwise = k == 0 ? "" : "else ";
            if (shape == 0)
            {
                snprintf(piece, sizeof(piece), "%sif(x==%u){int v%u=x;r+=v%u;{int w%u=r;if(w%u>%u)goto out;}goto out;}", otherwise, k, k, k, k, k, k);
            }
            else if (shape == 1)
            {
                snprintf(piece, sizeof(piece), "%sif(x==%u){int v%u=x;in%u:r+=v%u;if(r>%u)goto in%u;{int w%u=r;if(w%u<0)goto in%u;}}", otherwise, k, k, k, k, k * 3 + 7,
                         (k * 7 + 3) % branches, k, k, (k + branches - 1) % branches);
            }
            else if (shape == 2)
            {
                snprintf(piece, sizeof(piece), "%sif(x==%u){int v%u=x;switch(v%u&3){case 0:{int a%u=r;r+=a%u;case 1:{int b%u=r;r+=b%u;case 2:r++;break;}}default:r--;}}",
                         otherwise, k, k, k, k, k, k, k);
            }
            else if (shape == 3 && k < 4)
            {
                snprintf(piece, sizeof(piece), "%sif(x==%u){int v%u=x;l%u:r+=v%u;}", otherwise, k, k, k, k);
            }
            else if (shape == 3)
            {
                snprintf(piece, sizeof(piece), "%sif(x==%u){int v%u=x;r+=v%u;}", otherwise, k, k, k);
            }
            else if (shape == 4)
            {
                snprintf(piece, sizeof(piece), "%sif(x==%u){int v%u=x;o%u:r+=v%u;if(r>%u)asm goto(\"\"::::o%u,o%u);}", otherwise, k, k, k, k, k * 5 + 1, (k * 3 + 1) % branches,
                         (k + 2) % branches);
            }
            else
            {
                snprintf(piece, sizeof(piece),
                         "%sif(x==%u){int v%u=x;r+=v%u;if(r&1){int w%u=r;if(w%u>3)break;continue;}"
                         "switch(v%u&3){case 0:{int z%u=r;r+=z%u;break;}default:{int q%u=r;while(q%u>0){int u%u=q%u;if(u%u==3){int s%u=u%u;r+=s%u;break;}q%u--;continue;}"
                         "do{int d%u=q%u;if(d%u)break;q%u++;continue;}while(q%u<2);}}break;}",
                         otherwise, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k);
            }
            check_test_lifetime_append(text, size, &length, &fits, piece);
        }
        const char* const tails[6] = { "out:return r;}", "return r;}", "return r;}", "return r;}", "return r;}", "else break;}return r;}" };
        check_test_lifetime_append(text, size, &length, &fits, tails[shape]);
        check_test_expect(test, fits, "lifetime source fixtures fit their buffer");
        // Each shape's arms bypass at least one block with locals each, but the chain of breaks', whose jumps leave them;
        // the gotos and breaks leave one or two each.
        const uint32_t bypassed[6] = { 0, branches, branches * 2, 4, branches, 0 };
        const uint32_t ends[6] = { branches * 2, branches, 0, 0, 0, branches * 3 };
        if (fits)
        {
            check_test_program_lifetime_source(test, text, names[shape], bypassed[shape], ends[shape], tally);
        }
    }
    free(text);
}

// Functions whose loops' labels have the same numbers (a body numbers them from 0), the second's loop inside a block with
// locals, which a stop kept from the first would end too; breaks, continues and gotos in statement expressions, in nested
// ones, in a loop with no other break, and in a for's condition and increment and a do's condition, which leave the loop
// around it; cases in nested blocks with locals; and labels whose address is taken, with computed gotos (#1355).
static const char check_test_lifetime_functions[] =
    "int f0(int x){int r=0;for(int i=0;i<x;i++){int a=i;r+=({int t=a;if(t>5)break;if(t<0)continue;t;});"
    "if(a&1){int b=a;if(b>9)break;continue;}r+=({int u=r;if(u>7)break;u;});}return r;}\n"
    "int f1(int x){int r=0;{int c=x;for(int i=0;i<x;i++){int a=i;if(a>3)break;r+=({int t=a;if(t>5)break;t;});}r+=c;}return r;}\n"
    "int f2(int x){int r=0;{int c=x;for(int i=0;i<x;i++){int a=i;r+=({int t=a;if(t==2)goto out;if(t>5)break;t;});}out:r+=c;}return r;}\n"
    "int f3(int x){int r=0;for(int i=0;i<x;i++){int a=i;for(int j=0;({int t=j;if(t>3)break;t;})<a;j+=({int s=j;if(s>8)continue;1;})){int b=j;r+=b;}"
    "do{int d=r;r+=d;}while(({int e=r;if(e>100)break;e<50;}));}return r;}\n"
    "int f4(int x){int r=0;while(x--){int w=x;r+=({int s=w;if(s>3)break;s;});r+=({int p=w;({int q=p;if(q>6)break;if(q>7)continue;q;})+p;});}return r;}\n"
    "int f5(int x){int r=0;switch(x){case 0:{int a=r;r+=a;case 1:{int b=r;r+=b;case 2:r++;break;}}default:r--;}"
    "while(x--){int w=x;switch(w&3){case 0:{int q=w;if(q)continue;break;}default:r+=({int z=w;if(z>4)break;z;});}}return r;}\n"
    "int f6(int x){static void*const t[]={&&l0,&&l1};int r=0;if(x<0)goto*t[(-x)&1];{int a=x;l0:r+=a;}{int b=x;l1:r+=b;}"
    "for(;;){int c=r;if(c>5)goto*t[c&1];break;}return r;}\n";

// #1355's checks: forests of every size up to a few hundred tokens and their seeds, then the generated sources, at 40 arms
// and at 1,024, where the old walks' steps are quadratic and the work bound would fail them, and the functions' fixture.
static void check_test_program_lifetime_jumps(CheckTest* test)
{
    Arena arena = arena_reserve(UINT64_C(1) << 26);
    CheckTestLifetimeForest* forest = arena.base != NULL ? BUSTER_ARENA_PUSH_ARRAY(&arena, CheckTestLifetimeForest, 1) : NULL;
    check_test_expect(test, forest != NULL, "lifetime jump test arena");
    CheckTestLifetimeTally tally = { 0 };
    bool equal = forest != NULL;
    uint32_t forests = 0;
    for (uint32_t seed = 1; equal && seed <= 600; seed += 1)
    {
        const uint32_t budget = 24 + (seed * 37) % (BUSTER_CHECK_TEST_LIFETIME_TOKENS - 24);
        equal = check_test_lifetime_forest_case(forest, &arena, seed, budget, &tally);
        forests += 1;
        if (!equal)
        {
            printf("  lifetime forest %u (budget %u, %u tokens) differs from the ancestor walks\n", seed, budget, forest->count);
        }
    }
    printf("  lifetime forests: %u, blocks with locals %llu, of which jumps enter %llu; %llu jumps' walks, %llu ends, %llu from a kept stop; %llu steps of the "
           "walks, the old walks' %llu\n",
           forests, (unsigned long long)tally.blocks, (unsigned long long)tally.entered, (unsigned long long)tally.jumps, (unsigned long long)tally.entries,
           (unsigned long long)tally.kept, (unsigned long long)tally.work, (unsigned long long)tally.old_work);
    check_test_expect(test, equal && tally.entered != 0 && tally.entered < tally.blocks && tally.kept != 0 && tally.entries != 0,
                      "the blocks jumps enter and the jumps' lifetime ends are the ancestor walks' on random forests");
    check_test_program_lifetime_sources(test, 40, &tally);
    check_test_program_lifetime_sources(test, 1024, &tally);
    check_test_program_lifetime_source(test, check_test_lifetime_functions, "lifetime-functions-regions", 15, 21, &tally);
    arena_release(&arena);
}
