// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The common warnings' walks over trees (#1340). check_common_parents by both paths against its definition: random graphs
// in node order (operands and arguments before their nodes, each read once) of every size around the vector path's 16
// nodes, and the same with one edge broken (a node read twice, by two nodes or one, or an operand past its node), each
// column ending at a PROT_NONE page. The cached roots agree with that definition even on broken graphs, their masks'
// tails are zero, and cached/uncached scans give the same roots while the scalar scan still gives every node.
// check_common_emission_subtree_start against each token's subtree's first token, on
// random trees whose subtrees are runs of tokens, with parents as deep as their children now and then. Included by
// c_checker_driver_test.c, after c_check_common_selection_test.c, whose guarded columns it takes.

static uint32_t check_test_tree_random(uint64_t* state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (uint32_t)(*state >> 32);
}

// A graph's columns, each ending at a PROT_NONE page.
typedef struct CheckTestTreeGraph CheckTestTreeGraph;
struct CheckTestTreeGraph
{
    CheckTestSelectionColumn columns[8];
    CheckGraph graph;
};

static bool check_test_tree_graph(CheckTestTreeGraph* input, uint32_t count, uint32_t argument_room)
{
    *input = (CheckTestTreeGraph){ 0 };
    input->graph.count = count;
    input->graph.ops = check_test_selection_column(&input->columns[0], (uint64_t)count * sizeof(CheckOp));
    input->graph.operand0 = check_test_selection_column(&input->columns[1], (uint64_t)count * sizeof(uint32_t));
    input->graph.operand1 = check_test_selection_column(&input->columns[2], (uint64_t)count * sizeof(uint32_t));
    input->graph.operand2 = check_test_selection_column(&input->columns[3], (uint64_t)count * sizeof(uint32_t));
    input->graph.argument_offsets = check_test_selection_column(&input->columns[4], (uint64_t)count * sizeof(uint32_t));
    input->graph.argument_counts = check_test_selection_column(&input->columns[5], (uint64_t)count * sizeof(uint32_t));
    input->graph.argument_nodes = check_test_selection_column(&input->columns[6], (uint64_t)argument_room * sizeof(uint32_t));
    return input->graph.ops != NULL && input->graph.operand0 != NULL && input->graph.operand1 != NULL && input->graph.operand2 != NULL &&
           input->graph.argument_offsets != NULL && input->graph.argument_counts != NULL && input->graph.argument_nodes != NULL;
}

static void check_test_tree_release(CheckTestTreeGraph* input)
{
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(input->columns); i += 1)
    {
        check_test_selection_unmap(&input->columns[i]);
    }
}

// A random graph in node order: each node reads up to three operands and, where its operator has them, arguments, each a
// node before it that nothing reads yet (`open` holds them, the latest last), so that every node is read once at most.
static void check_test_tree_generate(CheckGraph* graph, uint32_t argument_room, uint32_t* open, uint64_t* state)
{
    uint32_t open_count = 0;
    uint32_t arguments = 0;
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        const uint32_t r = check_test_tree_random(state);
        const CheckOp op = (r & 7) == 0 ? CHECK_OP_CALL : (r & 7) == 1 ? CHECK_OP_BUILTIN_MISC : (CheckOp)((r >> 3) % CHECK_OP_COUNT);
        graph->ops[node] = op;
        uint32_t* slots[] = { &graph->operand0[node], &graph->operand1[node], &graph->operand2[node] };
        const uint32_t wanted = (r >> 10) % 4;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(slots); i += 1)
        {
            // Take a recent open node now and then, a slot left absent otherwise.
            const bool take = i < wanted && open_count != 0;
            const uint32_t pick = take ? open_count - 1 - check_test_tree_random(state) % (open_count < 4 ? open_count : 4) : 0;
            *slots[i] = take ? open[pick] : BUSTER_CHECK_NONE;
            if (take)
            {
                open[pick] = open[open_count - 1];
                open_count -= 1;
            }
        }
        graph->argument_offsets[node] = arguments;
        graph->argument_counts[node] = 0;
        if (check_graph_has_arguments(op))
        {
            const uint32_t argued = (r >> 14) % 5;
            for (uint32_t a = 0; a < argued && open_count != 0 && arguments < argument_room; a += 1)
            {
                graph->argument_nodes[arguments] = open[open_count - 1];
                open_count -= 1;
                arguments += 1;
                graph->argument_counts[node] += 1;
            }
        }
        open[open_count] = node;
        open_count += 1;
    }
}

// check_common_parents' definition: each node's last reader of an operand or argument below the count, and whether every
// one precedes its reader and each node has one reader at most.
static bool check_test_tree_parents(const CheckGraph* graph, uint32_t* parents)
{
    bool tree = true;
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        parents[node] = BUSTER_CHECK_NONE;
    }
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
        {
            if (operands[i] < graph->count)
            {
                tree &= operands[i] < node && parents[operands[i]] == BUSTER_CHECK_NONE;
                parents[operands[i]] = node;
            }
        }
        for (uint32_t a = 0; check_graph_has_arguments(graph->ops[node]) && a < graph->argument_counts[node]; a += 1)
        {
            const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + a];
            if (argument < graph->count)
            {
                tree &= argument < node && parents[argument] == BUSTER_CHECK_NONE;
                parents[argument] = node;
            }
        }
    }
    return tree;
}

// Root membership comes from the independent edge walk, even where the graph is not a tree. Both vector scans must
// enumerate those roots in node order; either scalar scan still gives every node for its consumer to test.
static bool check_test_tree_roots_case(const uint32_t* expected, const uint32_t* parents, const uint64_t* roots, uint32_t count)
{
    bool same = true;
    for (uint32_t node = 0; same && node < count; node += 1)
    {
        same = ((roots[node >> 6] >> (node & 63)) & 1u) == (expected[node] == BUSTER_CHECK_NONE);
    }
    if (count != 0 && (count & 63) != 0)
    {
        same &= (roots[count >> 6] >> (count & 63)) == 0;
    }
    for (uint32_t cached = 0; same && cached < 2; cached += 1)
    {
        for (uint32_t vector = 0; same && vector < 2; vector += 1)
        {
            CheckCommonScan scan = { 0 };
            for (uint32_t node = 0; same && node < count; node += 1)
            {
                if (vector == 0 || expected[node] == BUSTER_CHECK_NONE)
                {
                    same = check_common_next_root(parents, cached != 0 ? roots : NULL, count, &scan, vector != 0) == node;
                }
            }
            same &= check_common_next_root(parents, cached != 0 ? roots : NULL, count, &scan, vector != 0) == count;
            same &= check_common_next_root(parents, cached != 0 ? roots : NULL, count, &scan, vector != 0) == count;
        }
    }
    return same;
}

// One graph by both paths against the definition: the same answer and root membership, and where it's a tree, the
// same parents. The output roots and parents end at protected pages as the input columns do.
static bool check_test_tree_parents_case(const CheckGraph* graph, uint32_t* expected, uint32_t* actual, uint64_t* roots, bool* tree)
{
    *tree = check_test_tree_parents(graph, expected);
    bool same = true;
    for (uint32_t path = 0; same && path < 2; path += 1)
    {
        const uint64_t words = ((uint64_t)graph->count + 63) / 64;
        memset(roots, 0xa5, words * sizeof(uint64_t));
        const bool answer = check_common_parents(graph, actual, roots, path == 0);
        same = answer == *tree && check_test_tree_roots_case(expected, actual, roots, graph->count);
        for (uint32_t node = 0; same && answer && node < graph->count; node += 1)
        {
            same = actual[node] == expected[node];
        }
        const bool uncached = check_common_parents(graph, actual, NULL, path == 0);
        same &= uncached == *tree;
        for (uint32_t node = 0; same && node < graph->count; node += 1)
        {
            same = (actual[node] == BUSTER_CHECK_NONE) == (expected[node] == BUSTER_CHECK_NONE) &&
                   (!uncached || actual[node] == expected[node]);
        }
    }
    return same;
}

static void check_test_driver_common_parents(CheckTest* test)
{
    static const uint32_t sizes[] = { 0, 1, 2, 15, 16, 17, 31, 32, 33, 47, 63, 64, 65, 100, 255, 256, 257, 1000, 4099 };
    uint64_t state = UINT64_C(1340);
    bool same = true;
    uint32_t trees = 0;
    uint32_t broken = 0;
    for (uint32_t round = 0; same && round < 4 * BUSTER_ARRAY_LENGTH(sizes); round += 1)
    {
        const uint32_t count = sizes[round % BUSTER_ARRAY_LENGTH(sizes)];
        const uint32_t argument_room = count * 4 + 1;
        CheckTestTreeGraph input;
        same = check_test_tree_graph(&input, count, argument_room);
        uint32_t* open = malloc(((uint64_t)count + 1) * sizeof(uint32_t));
        uint32_t* expected = malloc(((uint64_t)count + 1) * sizeof(uint32_t));
        CheckTestSelectionColumn output[2] = { 0 };
        uint32_t* actual = check_test_selection_column(output, ((uint64_t)count + 1) * sizeof(uint32_t));
        uint64_t* roots = check_test_selection_column(output + 1, (((uint64_t)count + 63) / 64) * sizeof(uint64_t));
        same = same && open != NULL && expected != NULL && actual != NULL && roots != NULL;
        if (same)
        {
            check_test_tree_generate(&input.graph, argument_room, open, &state);
            bool tree = false;
            same = check_test_tree_parents_case(&input.graph, expected, actual, roots, &tree);
            trees += tree;
            // One edge broken at a random node: an operand that a node before reads too, the same operand twice, an
            // operand at or past its node, and an argument that an operand reads too.
            for (uint32_t kind = 0; same && count > 2 && kind < 4; kind += 1)
            {
                const uint32_t node = 2 + check_test_tree_random(&state) % (count - 2);
                uint32_t reader = BUSTER_CHECK_NONE;
                for (uint32_t at = node; at-- > 0 && reader == BUSTER_CHECK_NONE;)
                {
                    reader = input.graph.operand0[at] < at ? at : BUSTER_CHECK_NONE;
                }
                if (kind == 0 && reader != BUSTER_CHECK_NONE)
                {
                    input.graph.operand1[node] = input.graph.operand0[reader];
                }
                else if (kind == 1)
                {
                    input.graph.operand0[node] = node - 1;
                    input.graph.operand1[node] = node - 1;
                }
                else if (kind == 2)
                {
                    input.graph.operand0[node] = node;
                }
                else if (reader != BUSTER_CHECK_NONE && input.graph.argument_counts[node] != 0)
                {
                    input.graph.argument_nodes[input.graph.argument_offsets[node]] = input.graph.operand0[reader];
                }
                same = check_test_tree_parents_case(&input.graph, expected, actual, roots, &tree);
                broken += !tree;
                check_test_tree_generate(&input.graph, argument_room, open, &state);
            }
            // Root-density extremes and cross-word tails: every node a root, one chain, no root (self edges), and
            // alternating child/root pairs. The broken all-self graph still has a well-defined empty root bitmap.
            for (uint32_t shape = 0; same && round < BUSTER_ARRAY_LENGTH(sizes) && shape < 4; shape += 1)
            {
                for (uint32_t node = 0; node < count; node += 1)
                {
                    input.graph.ops[node] = CHECK_OP_VALUE;
                    input.graph.operand0[node] = shape == 1 && node != 0 ? node - 1 : shape == 2 ? node :
                                                 shape == 3 && (node & 1) != 0 ? node - 1 : BUSTER_CHECK_NONE;
                    input.graph.operand1[node] = BUSTER_CHECK_NONE;
                    input.graph.operand2[node] = BUSTER_CHECK_NONE;
                    input.graph.argument_counts[node] = 0;
                }
                same = check_test_tree_parents_case(&input.graph, expected, actual, roots, &tree);
            }
        }
        free(open);
        free(expected);
        check_test_selection_unmap(output);
        check_test_selection_unmap(output + 1);
        check_test_tree_release(&input);
    }
    check_test_expect(test, same && trees != 0 && broken != 0,
                      "common parents and cached roots: guarded random trees and broken graphs by both paths against the definition");
}

// A random tree of `count` tokens whose subtrees are runs: a run's root is any of its tokens, the tokens before it and
// after it split into runs of its children, worked out with an explicit stack. Each token's subtree's first token into
// `starts`; a child's depth is its parent's, or one deeper.
static void check_test_tree_tokens(uint32_t count, uint32_t* parents, uint32_t* depths, uint32_t* starts, uint32_t* stack, uint64_t* state)
{
    // The stack holds runs to make subtrees of: their first token, their end and their parent.
    uint32_t depth = 0;
    if (count != 0)
    {
        stack[0] = 0;
        stack[1] = count;
        stack[2] = BUSTER_CHECK_NONE;
        depth = 1;
    }
    while (depth != 0)
    {
        depth -= 1;
        const uint32_t first = stack[3 * depth];
        const uint32_t end = stack[3 * depth + 1];
        const uint32_t parent = stack[3 * depth + 2];
        const uint32_t root = first + check_test_tree_random(state) % (end - first);
        parents[root] = parent;
        depths[root] = parent == BUSTER_CHECK_NONE ? 0 : depths[parent] + (check_test_tree_random(state) % 3 != 0);
        starts[root] = first;
        // The tokens before the root and after it as runs of children of up to 8 tokens' worth of random cuts.
        for (uint32_t side = 0; side < 2; side += 1)
        {
            uint32_t at = side == 0 ? first : root + 1;
            const uint32_t stop = side == 0 ? root : end;
            while (at < stop)
            {
                const uint32_t span = 1 + check_test_tree_random(state) % (stop - at < 8 ? stop - at : (stop - at) / 2 + 1);
                stack[3 * depth] = at;
                stack[3 * depth + 1] = at + span;
                stack[3 * depth + 2] = root;
                depth += 1;
                at += span;
            }
        }
    }
}

static void check_test_driver_common_subtree_starts(CheckTest* test)
{
    static const uint32_t sizes[] = { 1, 2, 3, 17, 64, 65, 1000, 20000 };
    uint64_t state = UINT64_C(1350);
    bool same = true;
    for (uint32_t round = 0; same && round < 3 * BUSTER_ARRAY_LENGTH(sizes); round += 1)
    {
        const uint32_t count = sizes[round % BUSTER_ARRAY_LENGTH(sizes)];
        uint32_t* parents = malloc((uint64_t)count * sizeof(uint32_t));
        uint32_t* depths = malloc((uint64_t)count * sizeof(uint32_t));
        uint32_t* starts = malloc((uint64_t)count * sizeof(uint32_t));
        uint32_t* stack = malloc(3 * ((uint64_t)count + 1) * sizeof(uint32_t));
        same = parents != NULL && depths != NULL && starts != NULL && stack != NULL;
        if (same)
        {
            check_test_tree_tokens(count, parents, depths, starts, stack, &state);
            Parser parser = { 0 };
            parser.tokens.count = count;
            parser.tree.parents = parents;
            parser.tree.depths = depths;
            CheckCommonEmission emission = { .parser = &parser };
            for (uint32_t token = 0; same && token < count; token += 1)
            {
                same = check_common_emission_subtree_start(&emission, token) == starts[token] &&
                       check_common_subtree_start(&parser, token) == starts[token];
            }
        }
        free(parents);
        free(depths);
        free(starts);
        free(stack);
    }
    check_test_expect(test, same, "common subtree starts: every token of random trees of runs, by the search from the root back");
}

static void check_test_driver_common_trees(CheckTest* test)
{
    check_test_driver_common_parents(test);
    check_test_driver_common_subtree_starts(test);
}
