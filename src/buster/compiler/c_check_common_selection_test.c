// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// check_common_unselected_roots (#1341, #1360) by both paths against check_common_unselected_walk, its definition one
// selection at a time: fixtures of _Generic's, __builtin_choose_expr's and __extension__'s token shapes, nested and side by
// side, whose marks are also written out by hand, with roots in a chosen operand (a statement expression's statements,
// #2023) that only an enclosing selection's other operand marks; then random trees and graphs, with roots that hold no
// selection's selected node (the bits) and roots that do (the walk), into marks of their own and into the roots themselves. Every column ends
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
    // The parser's depths, which find each selection's chosen operand (check_common_chosen_span), or null for none.
    uint32_t* depths;
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
            parser.tree.depths = input->depths;
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

// The columns of `input` copied to guarded memory (columns[0..7]), then compared for the values 0, 1 and 0x80.
static bool check_test_selection_case(const CheckTestSelections* input, Arena* scratch, uint32_t* changed)
{
    CheckTestSelectionColumn columns[7] = { 0 };
    CheckTestSelections guarded = {
        .kinds = check_test_selection_column(&columns[0], input->token_count),
        .parents = check_test_selection_column(&columns[1], (uint64_t)input->token_count * sizeof(uint32_t)),
        .depths = input->depths != NULL ? check_test_selection_column(&columns[6], (uint64_t)input->token_count * sizeof(uint32_t)) : NULL,
        .token_count = input->token_count,
        .ops = check_test_selection_column(&columns[2], (uint64_t)input->node_count * sizeof(CheckOp)),
        .tokens = check_test_selection_column(&columns[3], (uint64_t)input->node_count * sizeof(uint32_t)),
        .operands = check_test_selection_column(&columns[4], (uint64_t)input->node_count * sizeof(uint32_t)),
        .roots = check_test_selection_column(&columns[5], input->node_count),
        .node_count = input->node_count,
    };
    bool same = guarded.kinds != NULL && guarded.parents != NULL && guarded.ops != NULL && guarded.tokens != NULL && guarded.operands != NULL &&
                guarded.roots != NULL && (input->depths == NULL || guarded.depths != NULL);
    if (same)
    {
        if (input->depths != NULL)
        {
            memcpy(guarded.depths, input->depths, (uint64_t)input->token_count * sizeof(uint32_t));
        }
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
// with `fast`, no selection's selected node among the roots; with depths, commas among the kinds and depths that rise and
// fall by a step, which make chosen operands of every shape, nested in others or not.
static void check_test_selection_generate(CheckTestSelections* input, uint64_t* state, bool fast)
{
    const uint32_t count = input->token_count;
    uint32_t depth = 0;
    for (uint32_t at = 0; at < count; at += 1)
    {
        const uint32_t r = check_test_selection_random(state);
        input->kinds[at] = (r & 7) == 0 ? PARSE_KIND_GENERIC : (r & 7) == 1 ? PARSE_KIND_TYPE_ARGUMENT : (r & 15) == 2 ? PARSE_KIND_EXTENSION :
                           (r & 7) == 3 ? PARSE_KIND_COMMA : (uint8_t)(r >> 8);
        const uint32_t step = (r >> 24) & 3;
        depth = step == 0 ? depth + 1 : step == 1 && depth > 0 ? depth - 1 : depth;
        if (input->depths != NULL)
        {
            input->depths[at] = depth;
        }
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

// Discover this input's allocation count, then fail every allocation on both paths. A new scratch index must join the
// failure contract too: false, unchanged marks, and the scratch arena at its original length.
static bool check_test_selection_exhaustion(const CheckTestSelections* input, Arena* scratch)
{
    CheckTestSelectionColumn columns[2] = { 0 };
    uint8_t* expected = check_test_selection_column(&columns[0], input->node_count);
    uint8_t* actual = check_test_selection_column(&columns[1], input->node_count);
    bool same = expected != NULL && actual != NULL;
    const CheckGraph graph = {
        .ops = input->ops,
        .tokens = input->tokens,
        .operand0 = input->operands,
        .count = input->node_count,
    };
    const CheckCommonResources saved = check_common_resources;
    const uint64_t length = scratch->length;
    for (uint32_t scalar = 0; same && scalar < 2; scalar += 1)
    {
        Parser parser = { 0 };
        parser.options.scalar = scalar != 0;
        parser.tokens.kinds = input->kinds;
        parser.tokens.count = input->token_count;
        parser.tree.parents = input->parents;
        parser.tree.depths = input->depths;
        memset(expected, 0x33, input->node_count);
        check_common_resources = (CheckCommonResources){ .fail_at = UINT32_MAX };
        same = check_common_unselected_roots(&graph, &parser, input->roots, expected, 1, scratch) && scratch->length == length;
        const uint32_t allocations = check_common_resources.pushes;
        same &= allocations != 0;
        for (uint32_t fail = 0; same && fail <= allocations; fail += 1)
        {
            memset(actual, 0x33, input->node_count);
            check_common_resources = (CheckCommonResources){ .fail_at = fail };
            const bool returned = check_common_unselected_roots(&graph, &parser, input->roots, actual, 1, scratch);
            check_common_resources.fail_at = UINT32_MAX;
            same = returned == (fail == allocations) && scratch->length == length;
            for (uint32_t node = 0; node < input->node_count; node += 1)
            {
                same &= actual[node] == (fail == allocations ? expected[node] : 0x33);
            }
        }
    }
    check_common_resources = saved;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(columns); i += 1)
    {
        check_test_selection_unmap(&columns[i]);
    }
    return same;
}

// Nested selections put a complete inner selection in either the chosen or the unchosen arm. Each arm also has an
// independent statement-expression root at its value's token. The expected marks come from those choices alone:
// controls and unchosen arms are marked; an outer unchosen arm overrides every inner choice (#2023, #1341).
static void check_test_selection_nested(CheckTest* test, Arena* scratch)
{
    enum
    {
        NESTED_LIMIT = 257,
        NESTED_TOKENS = NESTED_LIMIT * 11 + 1,
        NESTED_NODES = NESTED_LIMIT * 7 + 2,
    };
    uint8_t kinds[NESTED_TOKENS];
    uint32_t parents[NESTED_TOKENS];
    uint32_t depths[NESTED_TOKENS];
    CheckOp ops[NESTED_NODES];
    uint32_t tokens[NESTED_NODES];
    uint32_t operands[NESTED_NODES];
    uint8_t roots[NESTED_NODES];
    uint8_t expected[NESTED_NODES];
    uint8_t actual[NESTED_NODES];
    CheckCommonSelection records[NESTED_LIMIT];
    static const uint32_t sizes[] = {
        1,
        65,
        NESTED_LIMIT,
    };
    uint32_t changed = 0;
    for (uint32_t size = 0; size < BUSTER_ARRAY_LENGTH(sizes); size += 1)
    {
        const uint32_t selections = sizes[size];
        const uint32_t token_count = (selections + 1) / 2 * 11 + selections / 2 * 7 + 1;
        const uint32_t node_count = selections * 7 + 2;
        CheckTestSelections input = {
            .kinds = kinds,
            .parents = parents,
            .depths = depths,
            .token_count = token_count,
            .ops = ops,
            .tokens = tokens,
            .operands = operands,
            .roots = roots,
            .node_count = node_count,
        };
        for (uint32_t shape = 0; shape < 3; shape += 1)
        {
            for (uint32_t at = 0; at < token_count; at += 1)
            {
                kinds[at] = PARSE_KIND_IDENTIFIER;
                parents[at] = BUSTER_CHECK_NONE;
                depths[at] = 0;
            }
            for (uint32_t node = 0; node < node_count; node += 1)
            {
                ops[node] = CHECK_OP_VALUE;
                tokens[node] = BUSTER_CHECK_NONE;
                operands[node] = BUSTER_CHECK_NONE;
                roots[node] = 0;
                expected[node] = 0;
            }
            uint32_t first = 0;
            uint32_t end = token_count;
            uint32_t outer_close = BUSTER_CHECK_NONE;
            bool outer_chosen = true;
            bool unselected = false;
            for (uint32_t level = 0; level < selections; level += 1)
            {
                const bool generic = (level & 1) == 0;
                const bool chosen = shape == 0 || (shape == 2 && (level & 1) == 0);
                const uint32_t prefix = generic ? (chosen ? 6 : 10) : (chosen ? 4 : 6);
                const uint32_t suffix = chosen ? (generic ? 5 : 3) : 1;
                const uint32_t close = end - 1;
                const uint32_t simple = chosen ? close - 1 : first + (generic ? 6 : 4);
                for (uint32_t at = first; at < first + prefix; at += 1)
                {
                    parents[at] = close;
                    depths[at] = level + 1;
                }
                for (uint32_t at = end - suffix; at < end; at += 1)
                {
                    parents[at] = close;
                    depths[at] = level + 1;
                }
                kinds[first] = generic ? PARSE_KIND_GENERIC : PARSE_KIND_TYPE_ARGUMENT;
                kinds[first + 1] = PARSE_KIND_L_PAREN;
                kinds[first + 3] = PARSE_KIND_COMMA;
                kinds[close] = PARSE_KIND_R_PAREN;
                depths[first] = level;
                depths[first + 1] = level;
                depths[close] = level;
                parents[close] = outer_close;
                if (generic)
                {
                    kinds[first + 5] = PARSE_KIND_COLON;
                    const uint32_t comma = chosen ? close - 4 : first + 7;
                    kinds[comma] = PARSE_KIND_COMMA;
                    kinds[comma + 1] = PARSE_KIND_DEFAULT;
                    kinds[comma + 2] = PARSE_KIND_COLON;
                }
                else
                {
                    kinds[chosen ? close - 2 : first + 5] = PARSE_KIND_COMMA;
                }
                // Postorder: the innermost value is node 0 and its statement-expression probe node 1; every level
                // adds its control, simple arm, both probes, two conversions at its `)`, and the selection itself.
                const uint32_t node = 2 + (selections - 1 - level) * 7;
                const uint32_t child = level + 1 < selections ? node - 1 : 0;
                tokens[node] = first + 2;
                roots[node] = 1;
                expected[node] = 1;
                tokens[node + 1] = simple;
                roots[node + 1] = chosen;
                expected[node + 1] = chosen;
                tokens[node + 2] = simple;
                roots[node + 2] = 1;
                expected[node + 2] = chosen || unselected;
                tokens[node + 3] = end - suffix - 1;
                roots[node + 3] = 1;
                expected[node + 3] = !chosen || unselected;
                for (uint32_t convert = 4; convert < 6; convert += 1)
                {
                    ops[node + convert] = CHECK_OP_CONVERT;
                    tokens[node + convert] = close;
                    operands[node + convert] = convert == 4 ? (chosen ? child : node + 1) : node + 4;
                }
                ops[node + 6] = CHECK_OP_SELECT;
                tokens[node + 6] = close;
                operands[node + 6] = node + 5;
                roots[node + 6] = level == 0 || !outer_chosen;
                expected[node + 6] = roots[node + 6] != 0 && unselected;
                first += prefix;
                end -= suffix;
                outer_close = close;
                outer_chosen = chosen;
                unselected |= !chosen;
            }
            parents[first] = outer_close;
            depths[first] = selections;
            tokens[0] = first;
            tokens[1] = first;
            roots[0] = !outer_chosen;
            roots[1] = 1;
            expected[0] = roots[0] != 0 && unselected;
            expected[1] = unselected;
            for (uint32_t reverse = 0; reverse < 2; reverse += 1)
            {
                const CheckGraph graph = {
                    .ops = ops,
                    .tokens = tokens,
                    .operand0 = operands,
                    .count = node_count,
                };
                bool same = first + 1 == end;
                for (uint32_t scalar = 0; same && scalar < 2; scalar += 1)
                {
                    Parser parser = { 0 };
                    parser.options.scalar = scalar != 0;
                    parser.tokens.kinds = kinds;
                    parser.tokens.count = token_count;
                    parser.tree.parents = parents;
                    parser.tree.depths = depths;
                    memset(actual, 0, node_count);
                    const uint64_t length = scratch->length;
                    same = check_common_unselected_roots(&graph, &parser, roots, actual, 1, scratch) && scratch->length == length &&
                           memcmp(actual, expected, node_count) == 0;
                    uint32_t record_count = 0;
                    for (uint32_t node = 0; node < node_count; node += 1)
                    {
                        if (ops[node] == CHECK_OP_SELECT)
                        {
                            records[record_count++] = (CheckCommonSelection){
                                .close = tokens[node],
                                .node = node,
                            };
                        }
                    }
                    // Count actual token work, not elapsed time. Reversing the graph also reverses these queries; the
                    // indexed continuation must stay bounded and return the walk's spans in either order.
                    CheckCommonSelectionWork work = { 0 };
                    const bool vector = !BUSTER_SCALAR_PATH(parser.options.scalar);
                    same &= check_common_chosen_spans(&graph, &parser, records, record_count, vector, scratch, &work) && scratch->length == length;
                    same &= record_count == selections && work.walked <= (uint64_t)token_count * 2 && work.indexed <= (uint64_t)token_count * 6 + 2 &&
                            work.queries <= selections && (shape != 0 || selections == 1 || work.queries != 0);
                    for (uint32_t record = 0; record < record_count; record += 1)
                    {
                        const CheckCommonRange span = check_common_chosen_span(&graph, &parser, records[record].node);
                        same &= records[record].chosen.first == span.first && records[record].chosen.end == span.end;
                    }
                }
                same &= check_test_selection_case(&input, scratch, &changed);
                char label[128];
                snprintf(label, sizeof(label), "unselected roots: %u nested selections, shape %u, reversed %u", selections, shape, reverse);
                check_test_expect(test, same, label);
                if (reverse == 0)
                {
                    for (uint32_t node = 0; node < node_count / 2; node += 1)
                    {
                        const uint32_t other = node_count - 1 - node;
                        const CheckOp op = ops[node];
                        ops[node] = ops[other];
                        ops[other] = op;
                        const uint32_t token = tokens[node];
                        tokens[node] = tokens[other];
                        tokens[other] = token;
                        const uint32_t operand = operands[node];
                        operands[node] = operands[other];
                        operands[other] = operand;
                        const uint8_t root = roots[node];
                        roots[node] = roots[other];
                        roots[other] = root;
                        const uint8_t mark = expected[node];
                        expected[node] = expected[other];
                        expected[other] = mark;
                    }
                    for (uint32_t node = 0; node < node_count; node += 1)
                    {
                        const uint32_t operand = operands[node];
                        operands[node] = operand < node_count ? node_count - 1 - operand : operand;
                    }
                }
            }
            if (selections == NESTED_LIMIT && shape == 0)
            {
                check_test_expect(test, check_test_selection_exhaustion(&input, scratch), "unselected roots: nested selection scratch exhaustion");
                // A parser-shaped chain becomes an arbitrary depth column: jumps, a very large depth, and a closing
                // token whose +1 depth wraps. The independent walk defines these synthetic graphs too.
                for (uint32_t at = 0; at < token_count; at += 1)
                {
                    depths[at] *= 3;
                }
                depths[0] = UINT32_MAX - 1;
                depths[token_count - 1] = UINT32_MAX;
                check_test_expect(test, check_test_selection_case(&input, scratch, &changed), "unselected roots: nested selections with arbitrary depth jumps");
            }
        }
    }
}

// Two selections at one closing token, which only a synthetic graph has. A root is unselected if either selection
// excludes its arm: equal choices preserve that arm, different choices preserve neither. Conversions at the same
// closer may hide the choice, or form a cycle whose bounded walk has no chosen span.
static void check_test_selection_shared_close(CheckTest* test, Arena* scratch)
{
    enum
    {
        N_CONTROL,
        N_A,
        N_B,
        N_A_CONVERT,
        N_B_CONVERT,
        N_A_SELECT,
        N_B_SELECT,
        N_A_ROOT,
        N_B_ROOT,
        NODES,
    };
    // __builtin_choose_expr(c, a, b).
    uint8_t kinds[] = {
        PARSE_KIND_TYPE_ARGUMENT,
        PARSE_KIND_L_PAREN,
        PARSE_KIND_IDENTIFIER,
        PARSE_KIND_COMMA,
        PARSE_KIND_IDENTIFIER,
        PARSE_KIND_COMMA,
        PARSE_KIND_IDENTIFIER,
        PARSE_KIND_R_PAREN,
    };
    uint32_t parents[BUSTER_ARRAY_LENGTH(kinds)];
    uint32_t depths[BUSTER_ARRAY_LENGTH(kinds)];
    for (uint32_t at = 0; at < BUSTER_ARRAY_LENGTH(kinds); at += 1)
    {
        parents[at] = at < 7 ? 7 : BUSTER_CHECK_NONE;
        depths[at] = at > 1 && at < 7 ? 1 : 0;
    }
    CheckOp ops[NODES];
    uint32_t tokens[NODES];
    uint32_t operands[NODES];
    uint8_t roots[NODES];
    for (uint32_t node = 0; node < NODES; node += 1)
    {
        ops[node] = node == N_A_CONVERT || node == N_B_CONVERT ? CHECK_OP_CONVERT :
                    node == N_A_SELECT || node == N_B_SELECT ? CHECK_OP_SELECT : CHECK_OP_VALUE;
        tokens[node] = node == N_CONTROL ? 2 : node == N_A || node == N_A_ROOT ? 4 : node == N_B || node == N_B_ROOT ? 6 : 7;
        operands[node] = BUSTER_CHECK_NONE;
        roots[node] = node == N_CONTROL || node == N_A_SELECT || node == N_B_SELECT || node == N_A_ROOT || node == N_B_ROOT;
    }
    operands[N_A_SELECT] = N_A_CONVERT;
    operands[N_B_SELECT] = N_B_CONVERT;
    CheckTestSelections input = {
        .kinds = kinds,
        .parents = parents,
        .depths = depths,
        .token_count = BUSTER_ARRAY_LENGTH(kinds),
        .ops = ops,
        .tokens = tokens,
        .operands = operands,
        .roots = roots,
        .node_count = NODES,
    };
    const CheckGraph graph = {
        .ops = ops,
        .tokens = tokens,
        .operand0 = operands,
        .count = NODES,
    };
    uint32_t changed = 0;
    for (uint32_t shape = 0; shape < 4; shape += 1)
    {
        operands[N_A_CONVERT] = shape == 3 ? N_B_CONVERT : N_A;
        operands[N_B_CONVERT] = shape == 3 ? N_A_CONVERT : shape == 0 ? N_A : N_B;
        tokens[N_B] = shape == 2 ? 7 : 6;
        bool same = true;
        for (uint32_t scalar = 0; same && scalar < 2; scalar += 1)
        {
            Parser parser = { 0 };
            parser.options.scalar = scalar != 0;
            parser.tokens.kinds = kinds;
            parser.tokens.count = BUSTER_ARRAY_LENGTH(kinds);
            parser.tree.parents = parents;
            parser.tree.depths = depths;
            uint8_t marks[NODES] = { 0 };
            const uint64_t length = scratch->length;
            same = check_common_unselected_roots(&graph, &parser, roots, marks, 1, scratch) && scratch->length == length;
            for (uint32_t node = 0; node < NODES; node += 1)
            {
                const bool expected = node == N_CONTROL || node == N_B_ROOT || (shape != 0 && node == N_A_ROOT);
                same &= marks[node] == expected;
            }
        }
        same &= check_test_selection_case(&input, scratch, &changed);
        check_test_expect(test, same, "unselected roots: shared closing token, converted, folded or cyclic choices");
    }
}

// Nested intervals, separated clusters, and short ranges across word boundaries. The oracle tests each token against
// every interval; the production pass may touch each covered word only once plus one boundary word per interval.
static void check_test_selection_ranges(CheckTest* test)
{
    enum
    {
        RANGES = 257,
        TOKENS = 4099,
        WORDS = (TOKENS + 63) / 64,
    };
    uint64_t ranges[RANGES];
    CheckTestSelectionColumn column = { 0 };
    uint64_t* bits = check_test_selection_column(&column, WORDS * sizeof(uint64_t));
    bool same = bits != NULL;
    for (uint32_t shape = 0; same && shape < 4; shape += 1)
    {
        const uint32_t count = shape == 0 ? 0 : RANGES;
        uint32_t end = 0;
        memset(bits, 0, WORDS * sizeof(uint64_t));
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t first = shape == 1 ? i : shape == 2 ? i / 65 * 1024 + i % 65 : i + 1 == count ? TOKENS - 2 : i * 15;
            const uint32_t last = shape == 1 ? TOKENS - i : shape == 2 ? i / 65 * 1024 + 257 - i % 65 :
                                  i + 1 == count ? TOKENS : first + 1 + (i & 3);
            ranges[i] = (uint64_t)first << 32 | last;
            end = last > end ? last : end;
        }
        uint32_t covered = 0;
        const uint64_t touched = check_common_merge_ranges(bits, count != 0 ? ranges : NULL, count, &covered);
        same &= covered == end && touched <= ((uint64_t)end + 63) / 64 + count;
        for (uint32_t at = 0; at < WORDS * 64; at += 1)
        {
            bool expected = false;
            for (uint32_t i = 0; i < count; i += 1)
            {
                expected |= at >= (uint32_t)(ranges[i] >> 32) && at < (uint32_t)ranges[i];
            }
            same &= ((bits[at >> 6] >> (at & 63)) & 1) == expected;
        }
    }
    check_test_expect(test, same, "unselected roots: merged interval bits and bounded word writes, including gaps and tails");
    check_test_selection_unmap(&column);
}

static void check_test_driver_unselected_roots(CheckTest* test)
{
    Arena scratch = arena_reserve(UINT64_C(1) << 26);
    // _Generic(c, int: a, default: b) = n; __extension__ (e); __builtin_choose_expr(1, f, _Generic(g, long: h)): the
    // tokens, the tree's parents (each selection's keyword a child of its closing parenthesis) and the graph, with the
    // marks the definition gives written out: the control c and the unselected b; f and h are selected and so not roots,
    // g is the inner _Generic's control, inside both ranges; e, n and the semicolons' roots are in none. A statement
    // expression's statement in a (s) and in f (t) is a root in a chosen operand, which the parser's depths find, so
    // unmarked, but one in h (u) lies in __builtin_choose_expr's other operand too, so marked (#2023).
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
    uint32_t depths[FIXTURE_TOKENS];
    for (uint32_t at = 0; at < FIXTURE_TOKENS; at += 1)
    {
        kinds[at] = PARSE_KIND_IDENTIFIER;
        parents[at] = BUSTER_CHECK_NONE;
        // (A group's contents are one deeper than its parentheses: _Generic's, the extension's and the outer selection's
        // at 1, the inner _Generic's at 2.)
        depths[at] = (at > G_OPEN && at < G_CLOSE) || at == E_E || (at > C_OPEN && at < C_CLOSE) ? 1 : 0;
        depths[at] += at > I_OPEN && at < I_CLOSE ? 1 : 0;
    }
    kinds[G_COMMA] = PARSE_KIND_COMMA;
    kinds[G_COMMA2] = PARSE_KIND_COMMA;
    kinds[C_COMMA] = PARSE_KIND_COMMA;
    kinds[C_COMMA2] = PARSE_KIND_COMMA;
    kinds[I_COMMA] = PARSE_KIND_COMMA;
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
        N_S,
        N_T,
        N_U,
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
        [N_S] = G_A,
        [N_T] = C_F,
        [N_U] = I_H,
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
    roots[N_S] = 1;
    roots[N_T] = 1;
    roots[N_U] = 1;
    CheckTestSelections fixture = {
        .kinds = kinds,
        .parents = parents,
        .depths = depths,
        .token_count = FIXTURE_TOKENS,
        .ops = ops,
        .tokens = tokens,
        .operands = operands,
        .roots = roots,
        .node_count = FIXTURE_NODES,
    };
    const CheckGraph graph = { .ops = ops, .tokens = tokens, .operand0 = operands, .count = FIXTURE_NODES };
    bool marked = true;
    for (uint32_t scalar = 0; scalar < 4; scalar += 1)
    {
        // (Without the depths, which a test's parser may lack, no operand is known to be chosen.)
        const bool deep = scalar < 2;
        Parser parser = { 0 };
        parser.options.scalar = (scalar & 1) != 0;
        parser.tokens.kinds = kinds;
        parser.tokens.count = FIXTURE_TOKENS;
        parser.tree.parents = parents;
        parser.tree.depths = deep ? depths : NULL;
        uint8_t marks[FIXTURE_NODES] = { 0 };
        uint8_t walked[FIXTURE_NODES] = { 0 };
        marked &= check_common_unselected_roots(&graph, &parser, roots, marks, 1, &scratch);
        check_common_unselected_walk(&graph, &parser, roots, walked, 1);
        for (uint32_t node = 0; node < FIXTURE_NODES; node += 1)
        {
            // The control and b of the first _Generic, the 1 and the inner selection inside __builtin_choose_expr's, g
            // and u inside both of the last two; s and t only where the chosen operands aren't known.
            const bool expected = node == N_CONTROL || node == N_B || node == N_ONE || node == N_G || node == N_INNER || node == N_U ||
                                  (!deep && (node == N_S || node == N_T));
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
    check_test_expect(test, check_test_selection_exhaustion(&fixture, &scratch), "unselected roots: each failed scratch allocation returns false and leaves the marks");
    check_test_selection_ranges(test);
    check_test_selection_nested(test, &scratch);
    check_test_selection_shared_close(test, &scratch);
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
        // (Every other pair of rounds has depths, and so chosen operands.)
        uint32_t* random_depths = (round & 2) != 0 ? malloc(((uint64_t)token_count + 1) * sizeof(uint32_t)) : NULL;
        same = random_kinds != NULL && random_parents != NULL && random_ops != NULL && random_tokens != NULL && random_operands != NULL &&
               random_roots != NULL && ((round & 2) == 0 || random_depths != NULL);
        CheckTestSelections input = {
            .kinds = random_kinds,
            .parents = random_parents,
            .depths = random_depths,
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
        free(random_depths);
    }
    check_test_expect(test, same && cases == 220, "unselected roots: random trees and graphs against the walk, by both paths");
    check_test_expect(test, fast_changed != 0, "unselected roots: the random cases without selected roots mark some");
    arena_release(&scratch);
}
