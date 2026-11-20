// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The common warnings' walks over trees (#1340). check_common_parents against its definition: random graphs in node order
// (operands and arguments before their nodes, each read once) of every size around its 16-node blocks, and the same with
// one edge broken (a node read twice, by two nodes or one, or an operand past its node), each column ending at a
// PROT_NONE page. The cached roots agree with that definition even on broken graphs, their masks' tails are zero, and
// cached and uncached scans give the same roots.
// check_common_emission_subtree_start against each token's subtree's first token, on
// random trees whose subtrees are runs of tokens, with parents as deep as their children now and then. The reach walk's
// per-body lists of statement expressions (check_common_body_regions, #1360) against their definition and its scan, and
// check_common_literal_blocked against check_common_literal_contexts' marks. Included
// by c_checker_driver_test.c, after c_check_common_selection_test.c, whose guarded columns it takes.

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

// Root membership comes from the independent edge walk, even where the graph is not a tree. Both scans, by the root bits
// and by the parents, must enumerate those roots in node order.
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
        CheckCommonScan scan = { 0 };
        for (uint32_t node = 0; same && node < count; node += 1)
        {
            if (expected[node] == BUSTER_CHECK_NONE)
            {
                same = check_common_next_root(parents, cached != 0 ? roots : NULL, count, &scan) == node;
            }
        }
        same &= check_common_next_root(parents, cached != 0 ? roots : NULL, count, &scan) == count;
        same &= check_common_next_root(parents, cached != 0 ? roots : NULL, count, &scan) == count;
    }
    return same;
}

// One graph against the definition: the same answer and root membership, and where it's a tree, the same parents. The
// output roots and parents end at protected pages as the input columns do.
static bool check_test_tree_parents_case(const CheckGraph* graph, uint32_t* expected, uint32_t* actual, uint64_t* roots, bool* tree)
{
    *tree = check_test_tree_parents(graph, expected);
    const uint64_t words = ((uint64_t)graph->count + 63) / 64;
    memset(roots, 0xa5, words * sizeof(uint64_t));
    const bool answer = check_common_parents(graph, actual, roots);
    bool same = answer == *tree && check_test_tree_roots_case(expected, actual, roots, graph->count);
    for (uint32_t node = 0; same && answer && node < graph->count; node += 1)
    {
        same = actual[node] == expected[node];
    }
    const bool uncached = check_common_parents(graph, actual, NULL);
    same &= uncached == *tree;
    for (uint32_t node = 0; same && node < graph->count; node += 1)
    {
        same = (actual[node] == BUSTER_CHECK_NONE) == (expected[node] == BUSTER_CHECK_NONE) &&
               (!uncached || actual[node] == expected[node]);
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
                      "common parents and cached roots: guarded random trees and broken graphs against the definition");
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

// check_common_body_regions against its definition, each body's regions in region order, and each body's
// check_common_body_reached with the lists against its scan of all the unit's regions (#1360): random programs of up to
// 40 bodies, whose entries are evaluations, labels, returns, ends, jumps and now and then an indirect jump, and whose
// statement expressions belong to random bodies, some to none (a body index past the last), with entries of their own
// past the body's and now and then past its end. Then each scratch allocation failed in turn, and the arena's capacity
// one byte short: false, and the scratch arena as it was.
static void check_test_driver_body_regions(CheckTest* test)
{
    static const CheckControlKind kinds[] =
    {
        CHECK_CONTROL_EVALUATE, CHECK_CONTROL_EVALUATE, CHECK_CONTROL_LABEL, CHECK_CONTROL_RETURN, CHECK_CONTROL_END, CHECK_CONTROL_JUMP,
        CHECK_CONTROL_JUMP, CHECK_CONTROL_EVALUATE,
    };
    const CheckCommonResources saved = check_common_resources;
    check_common_resources = (CheckCommonResources){ .fail_at = UINT32_MAX };
    Arena scratch = arena_reserve(UINT64_C(1) << 24);
    uint64_t state = UINT64_C(1360);
    bool same = scratch.base != NULL;
    uint32_t listed = 0;
    for (uint32_t round = 0; same && round < 400; round += 1)
    {
        const uint32_t body_count = round % 5 == 0 ? 0 : 1 + check_test_tree_random(&state) % 40;
        const uint32_t region_count = round % 7 == 1 ? 0 : check_test_tree_random(&state) % 120;
        CheckProgramBody* bodies = calloc((uint64_t)body_count + 1, sizeof(CheckProgramBody));
        CheckProgramRegion* regions = calloc((uint64_t)region_count + 1, sizeof(CheckProgramRegion));
        CheckControl* controls = calloc((uint64_t)body_count * 64 + 1, sizeof(CheckControl));
        uint8_t* seen = malloc(64);
        uint8_t* scanned = malloc(64);
        uint32_t* queue = malloc(64 * sizeof(uint32_t));
        same = bodies != NULL && regions != NULL && controls != NULL && seen != NULL && scanned != NULL && queue != NULL;
        uint32_t first = 0;
        for (uint32_t b = 0; same && b < body_count; b += 1)
        {
            const uint32_t count = 1 + check_test_tree_random(&state) % 64;
            const uint32_t entry_count = check_test_tree_random(&state) % 3 == 0 ? count : 1 + check_test_tree_random(&state) % count;
            bodies[b] = (CheckProgramBody){ .first = first, .count = count, .entry_count = entry_count };
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t r = check_test_tree_random(&state);
                const CheckControlKind kind = r % 97 == 0 ? CHECK_CONTROL_JUMP_INDIRECT : kinds[r % BUSTER_ARRAY_LENGTH(kinds)];
                controls[first + i] = (CheckControl){ .kind = kind, .target = (r >> 8) % (count + 2), .node = BUSTER_CHECK_NONE };
            }
            first += count;
        }
        for (uint32_t r = 0; same && r < region_count; r += 1)
        {
            const uint32_t random = check_test_tree_random(&state);
            const uint32_t body = (random & 15) == 0 ? body_count + (random >> 4) % 3 : body_count != 0 ? (random >> 4) % body_count : 0;
            const uint32_t count = body < body_count ? bodies[body].count : 64;
            const uint32_t entry_count = body < body_count ? bodies[body].entry_count : 0;
            const uint32_t extra = count - entry_count;
            const uint32_t place = (random >> 20) % 9 == 0 ? count + (random >> 24) % 3 : entry_count + (extra != 0 ? (random >> 12) % extra : 0);
            regions[r] = (CheckProgramRegion){ .body = body, .first = place };
        }
        const CheckInput input = { .region_count = region_count };
        const CheckProgram program =
        {
            .input = &input,
            .controls = controls,
            .bodies = bodies,
            .body_count = body_count,
            .regions = regions,
            .region_count = region_count,
        };
        const uint64_t length = scratch.length;
        CheckCommonBodyRegions lists = { 0 };
        same = same && check_common_body_regions(&program, &lists, &scratch) && (lists.order == NULL) == (region_count == 0) &&
               (lists.offsets == NULL) == (region_count == 0);
        // Each body's list: its regions, in order; and every region of a body in one.
        uint32_t total = 0;
        for (uint32_t b = 0; same && lists.order != NULL && b < body_count; b += 1)
        {
            uint32_t k = lists.offsets[b];
            for (uint32_t r = 0; same && r < region_count; r += 1)
            {
                if (regions[r].body == b)
                {
                    same = k < lists.offsets[b + 1] && lists.order[k] == r;
                    k += 1;
                }
            }
            same = same && k == lists.offsets[b + 1];
            total += lists.offsets[b + 1] - lists.offsets[b];
        }
        same = same && (lists.order == NULL || (lists.offsets[0] == 0 && total == lists.offsets[body_count]));
        for (uint32_t b = 0; same && b < body_count; b += 1)
        {
            check_common_body_reached(&program, bodies + b, lists, seen, queue);
            check_common_body_reached(&program, bodies + b, (CheckCommonBodyRegions){ 0 }, scanned, queue);
            same = memcmp(seen, scanned, bodies[b].count) == 0;
            listed += lists.order != NULL && bodies[b].entry_count < bodies[b].count && lists.offsets[b + 1] != lists.offsets[b];
        }
        arena_pop_to(&scratch, length);
        // Each allocation failed in turn, then too little room: false, nothing kept.
        for (uint32_t failure = 0; same && region_count != 0 && failure < 3; failure += 1)
        {
            check_common_resources = (CheckCommonResources){ .fail_at = failure < 2 ? failure : UINT32_MAX };
            Arena small = scratch;
            small.capacity = failure < 2 ? scratch.capacity : scratch.length + ((uint64_t)body_count + 2) * sizeof(uint32_t) + 3;
            CheckCommonBodyRegions none = { 0 };
            same = !check_common_body_regions(&program, &none, &small) && none.order == NULL && none.offsets == NULL;
            check_common_resources = (CheckCommonResources){ .fail_at = UINT32_MAX };
        }
        free(bodies);
        free(regions);
        free(controls);
        free(seen);
        free(scanned);
        free(queue);
    }
    arena_release(&scratch);
    check_common_resources = saved;
    check_test_expect(test, same && listed > 500, "common body regions: each body's statement expressions listed, and its reach with them as with the scan");
}

// check_common_literal_blocked against check_common_literal_contexts' marks (#1360): on random tree graphs
// (check_test_tree_generate) whose nodes without arguments are now and then a sizeof, an alignof or a variable length
// array's bound or extent, with every root eligible (no node suppressed, no selection), a node is blocked exactly where
// the pass leaves it without a literal context.
static void check_test_driver_literal_blocked(CheckTest* test)
{
    static const uint32_t sizes[] = { 1, 2, 3, 16, 17, 64, 65, 257, 1000, 5000 };
    static const CheckOp queries[] = { CHECK_OP_SIZEOF, CHECK_OP_ALIGNOF, CHECK_OP_VLA_BOUND, CHECK_OP_VLA_EXTENT };
    uint64_t state = UINT64_C(13600);
    Arena scratch = arena_reserve(UINT64_C(1) << 24);
    bool same = scratch.base != NULL;
    uint32_t blocked = 0;
    uint32_t reached = 0;
    for (uint32_t round = 0; same && round < 6 * BUSTER_ARRAY_LENGTH(sizes); round += 1)
    {
        const uint32_t count = sizes[round % BUSTER_ARRAY_LENGTH(sizes)];
        const uint32_t argument_room = count * 4 + 1;
        CheckTestTreeGraph input;
        same = check_test_tree_graph(&input, count, argument_room);
        uint32_t* open = malloc(((uint64_t)count + 1) * sizeof(uint32_t));
        uint32_t* parents = malloc(((uint64_t)count + 1) * sizeof(uint32_t));
        uint32_t* tokens = malloc(((uint64_t)count + 1) * sizeof(uint32_t));
        uint8_t* marks = malloc((uint64_t)count + 1);
        same = same && open != NULL && parents != NULL && tokens != NULL && marks != NULL;
        if (same)
        {
            check_test_tree_generate(&input.graph, argument_room, open, &state);
            for (uint32_t node = 0; node < count; node += 1)
            {
                const uint32_t r = check_test_tree_random(&state);
                // (One node in 32, so that most chains to a root hold no query and some hold one or more.)
                const bool plain = !check_graph_has_arguments(input.graph.ops[node]) && (r & 31) == 0;
                input.graph.ops[node] = plain ? queries[(r >> 2) % BUSTER_ARRAY_LENGTH(queries)] : input.graph.ops[node];
                // (No token: no selection is a static one.)
                tokens[node] = BUSTER_CHECK_NONE;
            }
            input.graph.tokens = tokens;
            same = check_test_tree_parents(&input.graph, parents);
            Parser parser = { 0 };
            const uint64_t length = scratch.length;
            same = same && check_common_literal_contexts(&input.graph, &parser, parents, marks, &scratch) && scratch.length == length;
            for (uint32_t node = 0; same && node < count; node += 1)
            {
                const bool marked = (marks[node] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0;
                same = check_common_literal_blocked(&input.graph, parents, node) == !marked;
                blocked += !marked;
                reached += marked && parents[node] != BUSTER_CHECK_NONE;
            }
        }
        free(open);
        free(parents);
        free(tokens);
        free(marks);
        check_test_tree_release(&input);
    }
    arena_release(&scratch);
    check_test_expect(test, same && blocked > 500 && reached > 500,
                      "common literal contexts: a node is blocked exactly where no literal context reaches it, on random trees with queries");
}

static void check_test_driver_common_trees(CheckTest* test)
{
    check_test_driver_common_parents(test);
    check_test_driver_common_subtree_starts(test);
    check_test_driver_body_regions(test);
    check_test_driver_literal_blocked(test);
}
