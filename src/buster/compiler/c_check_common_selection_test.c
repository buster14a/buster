// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// check_common_unselected_roots (#1341, #1360) by both paths against check_common_unselected_walk, its definition one
// selection at a time: fixtures of _Generic's, __builtin_choose_expr's and __extension__'s token shapes, nested and side by
// side, whose marks are also written out by hand; then random trees and graphs, with roots that hold no selection's selected
// node (the bits) and roots that do (the walk), into marks of their own and into the roots themselves. Every column ends
// at a PROT_NONE page, so that a read or write past one faults; the scratch arena must be as it was after each call; and
// each of the function's scratch allocations, failed in turn, must return false and leave the marks as they were.
// Included by c_checker_driver_test.c.

// A selection pass's input: the tokens' kinds and parents, the graph's columns and the roots, each ending at a PROT_NONE
// page (check_test_selection_column).
typedef struct CheckTestSelections CheckTestSelections;
struct CheckTestSelections
{
    uint8_t* kinds;
    uint32_t* parents;
    uint32_t token_count;
    CheckOp* ops;
    uint32_t* tokens;
    uint32_t* operands;
    uint8_t* roots;
    uint32_t node_count;
};

typedef struct CheckTestSelectionColumn CheckTestSelectionColumn;
struct CheckTestSelectionColumn
{
    uint8_t* mapping;
    uint64_t size;
};

// `bytes` of memory that end where a PROT_NONE page starts, or null.
static void* check_test_selection_column(CheckTestSelectionColumn* column, uint64_t bytes)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t pages = (bytes + page - 1) / page;
    column->size = (pages + 1) * page;
    column->mapping = mmap(NULL, column->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = column->mapping != MAP_FAILED && mprotect(column->mapping + pages * page, page, PROT_NONE) == 0;
    void* result = guarded ? column->mapping + pages * page - bytes : NULL;
    return result;
}

static void check_test_selection_unmap(CheckTestSelectionColumn* column)
{
    if (column->mapping != NULL && column->mapping != MAP_FAILED)
    {
        munmap(column->mapping, column->size);
    }
    column->mapping = NULL;
}

// Each path's marks against the walk's, from `initial` marks of their own and in the roots themselves, for one value.
// Whether every run agreed and returned true, with the scratch arena as it was; `changed` counts the runs whose
// marks the walk changed.
static bool check_test_selection_compare(const CheckTestSelections* input, uint8_t value, Arena* scratch, uint32_t* changed)
{
    CheckTestSelectionColumn columns[3] = { 0 };
    uint8_t* expected = check_test_selection_column(&columns[0], input->node_count);
    uint8_t* actual = check_test_selection_column(&columns[1], input->node_count);
    uint8_t* roots = check_test_selection_column(&columns[2], input->node_count);
    bool same = expected != NULL && actual != NULL && roots != NULL;
    const CheckGraph graph = {
        .ops = input->ops,
        .tokens = input->tokens,
        .operand0 = input->operands,
        .count = input->node_count,
    };
    for (uint32_t aliased = 0; same && aliased < 2; aliased += 1)
    {
        for (uint32_t scalar = 0; same && scalar < 2; scalar += 1)
        {
            Parser parser = { 0 };
            parser.options.scalar = scalar != 0;
            parser.tokens.kinds = input->kinds;
            parser.tokens.count = input->token_count;
            parser.tree.parents = input->parents;
            for (uint32_t i = 0; i < input->node_count; i += 1)
            {
                expected[i] = aliased ? input->roots[i] : (uint8_t)(0x5a ^ i);
                actual[i] = expected[i];
                roots[i] = input->roots[i];
            }
            check_common_unselected_walk(&graph, &parser, aliased ? expected : input->roots, expected, value);
            const uint64_t length = scratch->length;
            const bool returned = check_common_unselected_roots(&graph, &parser, aliased ? actual : roots, actual, value, scratch);
            same = returned && scratch->length == length && memcmp(expected, actual, input->node_count) == 0 &&
                   (aliased || memcmp(roots, input->roots, input->node_count) == 0);
            bool differs = false;
            for (uint32_t i = 0; i < input->node_count; i += 1)
            {
                differs |= expected[i] != (aliased ? input->roots[i] : (uint8_t)(0x5a ^ i));
            }
            *changed += differs;
        }
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(columns); i += 1)
    {
        check_test_selection_unmap(&columns[i]);
    }
    return same;
}

// The columns of `input` copied to guarded memory (columns[0..6]), then compared for the values 0, 1 and 0x80.
static bool check_test_selection_case(const CheckTestSelections* input, Arena* scratch, uint32_t* changed)
{
    CheckTestSelectionColumn columns[6] = { 0 };
    CheckTestSelections guarded = {
        .kinds = check_test_selection_column(&columns[0], input->token_count),
        .parents = check_test_selection_column(&columns[1], (uint64_t)input->token_count * sizeof(uint32_t)),
        .token_count = input->token_count,
        .ops = check_test_selection_column(&columns[2], (uint64_t)input->node_count * sizeof(CheckOp)),
        .tokens = check_test_selection_column(&columns[3], (uint64_t)input->node_count * sizeof(uint32_t)),
        .operands = check_test_selection_column(&columns[4], (uint64_t)input->node_count * sizeof(uint32_t)),
        .roots = check_test_selection_column(&columns[5], input->node_count),
        .node_count = input->node_count,
    };
    bool same = guarded.kinds != NULL && guarded.parents != NULL && guarded.ops != NULL && guarded.tokens != NULL && guarded.operands != NULL &&
                guarded.roots != NULL;
    if (same)
    {
        memcpy(guarded.kinds, input->kinds, input->token_count);
        memcpy(guarded.parents, input->parents, (uint64_t)input->token_count * sizeof(uint32_t));
        memcpy(guarded.ops, input->ops, (uint64_t)input->node_count * sizeof(CheckOp));
        memcpy(guarded.tokens, input->tokens, (uint64_t)input->node_count * sizeof(uint32_t));
        memcpy(guarded.operands, input->operands, (uint64_t)input->node_count * sizeof(uint32_t));
        memcpy(guarded.roots, input->roots, input->node_count);
    }
    static const uint8_t values[] = { 0, 1, 0x80 };
    for (uint32_t v = 0; same && v < BUSTER_ARRAY_LENGTH(values); v += 1)
    {
        same = check_test_selection_compare(&guarded, values[v], scratch, changed);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(columns); i += 1)
    {
        check_test_selection_unmap(&columns[i]);
    }
    return same;
}

static uint32_t check_test_selection_random(uint64_t* state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (uint32_t)(*state >> 32);
}

// A random tree of `token_count` tokens and graph of `node_count` nodes: keyword kinds among other kinds, parents mostly
// a little after their children (where a selection's keyword is), selections at those parents and elsewhere, and roots;
// with `fast`, no selection's selected node among the roots.
static void check_test_selection_generate(CheckTestSelections* input, uint64_t* state, bool fast)
{
    const uint32_t count = input->token_count;
    for (uint32_t at = 0; at < count; at += 1)
    {
        const uint32_t r = check_test_selection_random(state);
        input->kinds[at] = (r & 7) == 0 ? PARSE_KIND_GENERIC : (r & 7) == 1 ? PARSE_KIND_TYPE_ARGUMENT : (r & 15) == 2 ? PARSE_KIND_EXTENSION :
                           (uint8_t)(r >> 8);
        const uint32_t shape = (r >> 16) & 3;
        input->parents[at] = shape <= 1 ? at + 1 + (r >> 18) % 40 : shape == 2 ? BUSTER_CHECK_NONE : (r >> 18) % (count + 3);
    }
    for (uint32_t node = 0; node < input->node_count; node += 1)
    {
        const uint32_t r = check_test_selection_random(state);
        const bool select = r % 3 == 0;
        input->ops[node] = select ? CHECK_OP_SELECT : (CheckOp)((r >> 2) % CHECK_OP_COUNT);
        input->ops[node] = !select && input->ops[node] == CHECK_OP_SELECT ? CHECK_OP_VALUE : input->ops[node];
        const uint32_t keyword = count != 0 ? (r >> 4) % count : 0;
        const uint32_t place = (r >> 12) & 7;
        input->tokens[node] = place == 0 ? BUSTER_CHECK_NONE : select && place <= 4 && count != 0 ? input->parents[keyword] :
                              check_test_selection_random(state) % (count + 2);
        input->operands[node] = (r >> 20) % (input->node_count + 1);
        input->operands[node] = input->operands[node] == input->node_count ? BUSTER_CHECK_NONE : input->operands[node];
        input->roots[node] = (r >> 28) == 0 ? 0x80 : (uint8_t)((r >> 29) & 1);
    }
    for (uint32_t node = 0; fast && node < input->node_count; node += 1)
    {
        if (input->ops[node] == CHECK_OP_SELECT && input->operands[node] < input->node_count)
        {
            input->roots[input->operands[node]] = 0;
        }
    }
}

static void check_test_driver_unselected_roots(CheckTest* test)
{
    Arena scratch = arena_reserve(UINT64_C(1) << 26);
    // _Generic(c, int: a, default: b) = n; __extension__ (e); __builtin_choose_expr(1, f, _Generic(g, long: h)): the
    // tokens, the tree's parents (each selection's keyword a child of its closing parenthesis) and the graph, with the
    // marks the definition gives written out: the control c and the unselected b; f and h are selected and so not roots,
    // g is the inner _Generic's control, inside both ranges; e, n and the semicolons' roots are in none.
    enum
    {
        G_KEYWORD = 0,
        G_OPEN,
        G_CONTROL,
        G_COMMA,
        G_INT,
        G_COLON,
        G_A,
        G_COMMA2,
        G_DEFAULT,
        G_COLON2,
        G_B,
        G_CLOSE,
        G_ASSIGN,
        G_N,
        G_SEMI,
        E_KEYWORD,
        E_OPEN,
        E_E,
        E_CLOSE,
        E_SEMI,
        C_KEYWORD,
        C_OPEN,
        C_ONE,
        C_COMMA,
        C_F,
        C_COMMA2,
        I_KEYWORD,
        I_OPEN,
        I_G,
        I_COMMA,
        I_LONG,
        I_COLON,
        I_H,
        I_CLOSE,
        C_CLOSE,
        C_SEMI,
        FIXTURE_TOKENS,
    };
    uint8_t kinds[FIXTURE_TOKENS];
    uint32_t parents[FIXTURE_TOKENS];
    for (uint32_t at = 0; at < FIXTURE_TOKENS; at += 1)
    {
        kinds[at] = PARSE_KIND_IDENTIFIER;
        parents[at] = BUSTER_CHECK_NONE;
    }
    kinds[G_KEYWORD] = PARSE_KIND_GENERIC;
    kinds[E_KEYWORD] = PARSE_KIND_EXTENSION;
    kinds[C_KEYWORD] = PARSE_KIND_TYPE_ARGUMENT;
    kinds[I_KEYWORD] = PARSE_KIND_GENERIC;
    for (uint32_t at = G_KEYWORD; at < G_CLOSE; at += 1)
    {
        parents[at] = G_CLOSE;
    }
    parents[G_CLOSE] = G_ASSIGN;
    parents[G_N] = G_ASSIGN;
    parents[G_ASSIGN] = G_SEMI;
    parents[E_OPEN] = E_CLOSE;
    parents[E_E] = E_CLOSE;
    parents[E_CLOSE] = E_KEYWORD;
    parents[E_KEYWORD] = E_SEMI;
    for (uint32_t at = C_KEYWORD; at < C_CLOSE; at += 1)
    {
        parents[at] = at >= I_KEYWORD && at < I_CLOSE ? I_CLOSE : C_CLOSE;
    }
    parents[C_CLOSE] = C_SEMI;
    enum
    {
        N_CONTROL,
        N_A,
        N_B,
        N_GENERIC,
        N_N,
        N_ASSIGN,
        N_E,
        N_EXTENSION,
        N_ONE,
        N_F,
        N_G,
        N_H,
        N_INNER,
        N_CHOOSE,
        FIXTURE_NODES,
    };
    CheckOp ops[FIXTURE_NODES];
    for (uint32_t node = 0; node < FIXTURE_NODES; node += 1)
    {
        ops[node] = CHECK_OP_VALUE;
    }
    ops[N_GENERIC] = CHECK_OP_SELECT;
    ops[N_ASSIGN] = CHECK_OP_ASSIGN;
    ops[N_EXTENSION] = CHECK_OP_SELECT;
    ops[N_INNER] = CHECK_OP_SELECT;
    ops[N_CHOOSE] = CHECK_OP_SELECT;
    uint32_t tokens[FIXTURE_NODES] = {
        [N_CONTROL] = G_CONTROL,
        [N_A] = G_A,
        [N_B] = G_B,
        [N_GENERIC] = G_CLOSE,
        [N_N] = G_N,
        [N_ASSIGN] = G_ASSIGN,
        [N_E] = E_E,
        [N_EXTENSION] = E_KEYWORD,
        [N_ONE] = C_ONE,
        [N_F] = C_F,
        [N_G] = I_G,
        [N_H] = I_H,
        [N_INNER] = I_CLOSE,
        [N_CHOOSE] = C_CLOSE,
    };
    uint32_t operands[FIXTURE_NODES];
    for (uint32_t node = 0; node < FIXTURE_NODES; node += 1)
    {
        operands[node] = BUSTER_CHECK_NONE;
    }
    operands[N_GENERIC] = N_A;
    operands[N_ASSIGN] = N_N;
    operands[N_EXTENSION] = N_E;
    operands[N_INNER] = N_H;
    operands[N_CHOOSE] = N_F;
    // Parentless nodes, as the callers' roots are: what no operand names (the assignment's right side is the selection,
    // which the fixture's graph leaves as a root of its own).
    uint8_t roots[FIXTURE_NODES] = { 0 };
    roots[N_CONTROL] = 1;
    roots[N_B] = 1;
    roots[N_GENERIC] = 1;
    roots[N_ASSIGN] = 1;
    roots[N_EXTENSION] = 1;
    roots[N_ONE] = 1;
    roots[N_G] = 1;
    roots[N_INNER] = 1;
    roots[N_CHOOSE] = 1;
    CheckTestSelections fixture = {
        .kinds = kinds,
        .parents = parents,
        .token_count = FIXTURE_TOKENS,
        .ops = ops,
        .tokens = tokens,
        .operands = operands,
        .roots = roots,
        .node_count = FIXTURE_NODES,
    };
    const CheckGraph graph = { .ops = ops, .tokens = tokens, .operand0 = operands, .count = FIXTURE_NODES };
    bool marked = true;
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        Parser parser = { 0 };
        parser.options.scalar = scalar != 0;
        parser.tokens.kinds = kinds;
        parser.tokens.count = FIXTURE_TOKENS;
        parser.tree.parents = parents;
        uint8_t marks[FIXTURE_NODES] = { 0 };
        uint8_t walked[FIXTURE_NODES] = { 0 };
        marked &= check_common_unselected_roots(&graph, &parser, roots, marks, 1, &scratch);
        check_common_unselected_walk(&graph, &parser, roots, walked, 1);
        for (uint32_t node = 0; node < FIXTURE_NODES; node += 1)
        {
            // The control and b of the first _Generic, the 1 and the inner selection inside __builtin_choose_expr's, and
            // g inside both of the last two.
            const bool expected = node == N_CONTROL || node == N_B || node == N_ONE || node == N_G || node == N_INNER;
            marked &= marks[node] == expected && walked[node] == expected;
        }
    }
    check_test_expect(test, marked, "unselected roots: fixture's controls and unselected arms, by both paths and the walk");
    uint32_t changed = 0;
    check_test_expect(test, check_test_selection_case(&fixture, &scratch, &changed), "unselected roots: fixture against the walk");
    // A selected node among the roots takes the walk: f is __builtin_choose_expr's selected arm, which its range excludes.
    roots[N_F] = 1;
    check_test_expect(test, check_test_selection_case(&fixture, &scratch, &changed), "unselected roots: a selected root takes the walk");
    roots[N_F] = 0;
    // Each scratch allocation failed in turn: the bits, then the ranges. The third count succeeds.
    bool exhausted = true;
    for (uint32_t fail = 0; fail < 3; fail += 1)
    {
        Parser parser = { 0 };
        parser.tokens.kinds = kinds;
        parser.tokens.count = FIXTURE_TOKENS;
        parser.tree.parents = parents;
        uint8_t marks[FIXTURE_NODES];
        memset(marks, 0x33, sizeof(marks));
        check_common_resources.fail_at = fail;
        check_common_resources.pushes = 0;
        const bool returned = check_common_unselected_roots(&graph, &parser, roots, marks, 1, &scratch);
        check_common_resources.fail_at = UINT32_MAX;
        bool kept = true;
        for (uint32_t node = 0; node < FIXTURE_NODES; node += 1)
        {
            kept &= marks[node] == 0x33 || (fail == 2 && marks[node] == 1);
        }
        exhausted &= kept && returned == (fail == 2);
    }
    check_test_expect(test, exhausted, "unselected roots: each failed scratch allocation returns false and leaves the marks");
    // Random trees and graphs, each size across the 64-token and 16-node blocks, with and without selected roots.
    uint64_t state = 1341;
    bool same = true;
    uint32_t cases = 0;
    uint32_t fast_changed = 0;
    static const uint32_t sizes[] = { 0, 1, 15, 16, 17, 63, 64, 65, 130, 400, 2100 };
    for (uint32_t round = 0; same && round < 220; round += 1)
    {
        const uint32_t token_count = sizes[round % BUSTER_ARRAY_LENGTH(sizes)];
        const uint32_t node_count = sizes[(round / BUSTER_ARRAY_LENGTH(sizes) + round) % BUSTER_ARRAY_LENGTH(sizes)];
        uint8_t* random_kinds = malloc(token_count + 1);
        uint32_t* random_parents = malloc(((uint64_t)token_count + 1) * sizeof(uint32_t));
        CheckOp* random_ops = malloc(((uint64_t)node_count + 1) * sizeof(CheckOp));
        uint32_t* random_tokens = malloc(((uint64_t)node_count + 1) * sizeof(uint32_t));
        uint32_t* random_operands = malloc(((uint64_t)node_count + 1) * sizeof(uint32_t));
        uint8_t* random_roots = malloc(node_count + 1);
        same = random_kinds != NULL && random_parents != NULL && random_ops != NULL && random_tokens != NULL && random_operands != NULL &&
               random_roots != NULL;
        CheckTestSelections input = {
            .kinds = random_kinds,
            .parents = random_parents,
            .token_count = token_count,
            .ops = random_ops,
            .tokens = random_tokens,
            .operands = random_operands,
            .roots = random_roots,
            .node_count = node_count,
        };
        const bool fast = (round & 1) == 0;
        if (same)
        {
            check_test_selection_generate(&input, &state, fast);
            uint32_t round_changed = 0;
            same = check_test_selection_case(&input, &scratch, &round_changed);
            fast_changed += fast ? round_changed : 0;
            cases += 1;
        }
        free(random_kinds);
        free(random_parents);
        free(random_ops);
        free(random_tokens);
        free(random_operands);
        free(random_roots);
    }
    check_test_expect(test, same && cases == 220, "unselected roots: random trees and graphs against the walk, by both paths");
    check_test_expect(test, fast_changed != 0, "unselected roots: the random cases without selected roots mark some");
    arena_release(&scratch);
}
