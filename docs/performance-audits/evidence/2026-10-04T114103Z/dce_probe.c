/*
 * Independent C11 graph experiment for Buster DCE research.
 * No Buster, LLVM, or other vendor implementation is incorporated.
 * This is a reduced dependency-graph model, NOT a Buster compiler patch.
 *
 * A models the pinned DCE retention rule: block parameters cannot be deleted.
 * B extends reference-count deletion to block parameters.
 * C marks the operand closure of mandatory roots, including live parameters.
 * The dense fixed-point oracle is deliberately separate from C's worklist.
 * Node IDs are already resolved definitions: alias resolution, CFG validity,
 * Buster purity classification, arena policy and compaction are not modeled.
 */
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Node Node;
struct Node
{
    uint32_t first;
    uint32_t count;
    bool root;
    bool phi;
};

typedef struct Graph Graph;
struct Graph
{
    uint32_t count;
    uint32_t edge_count;
    Node *nodes;
    uint32_t *edges;
};

typedef struct Scratch Scratch;
struct Scratch
{
    uint32_t *uses;
    uint32_t *queue;
    unsigned char *a;
    unsigned char *b;
    unsigned char *c;
    unsigned char *oracle;
};

typedef struct Stats Stats;
struct Stats
{
    uint64_t edge_visits;
    uint32_t pushes;
    uint32_t kept;
};

static void require(bool condition, char const *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void *array_allocate(size_t count, size_t size)
{
    require(size != 0 && count <= SIZE_MAX / size, "allocation size");
    void *result = calloc(count ? count : 1, size);
    require(result != NULL, "allocation failed");
    return result;
}

static Graph graph_allocate(uint32_t nodes, uint32_t edges)
{
    Graph result = {nodes, edges, array_allocate(nodes, sizeof(Node)),
                    array_allocate(edges, sizeof(uint32_t))};
    return result;
}

static Scratch scratch_allocate(uint32_t count)
{
    Scratch result = {array_allocate(count, sizeof(uint32_t)),
                      array_allocate(count, sizeof(uint32_t)),
                      array_allocate(count, 1), array_allocate(count, 1),
                      array_allocate(count, 1), array_allocate(count, 1)};
    return result;
}

static void graph_free(Graph *graph)
{
    free(graph->edges);
    free(graph->nodes);
    *graph = (Graph){0};
}

static void scratch_free(Scratch *scratch)
{
    free(scratch->oracle);
    free(scratch->c);
    free(scratch->b);
    free(scratch->a);
    free(scratch->queue);
    free(scratch->uses);
    *scratch = (Scratch){0};
}

static bool graph_valid(Graph const *graph)
{
    bool result = graph->count == 0 || graph->nodes != NULL;
    result = result && (graph->edge_count == 0 || graph->edges != NULL);
    for (uint32_t i = 0; result && i < graph->count; ++i)
    {
        Node row = graph->nodes[i];
        result = row.first <= graph->edge_count && row.count <= graph->edge_count - row.first;
        for (uint32_t j = 0; result && j < row.count; ++j)
        {
            result = graph->edges[row.first + j] < graph->count;
        }
    }
    return result;
}

/* Each graph edge is an operand occurrence, including duplicates. */
static Stats reference_count(Graph const *graph, Scratch *scratch,
                             bool delete_parameters, unsigned char *keep)
{
    Stats result = {0};
    memset(scratch->uses, 0, (size_t)graph->count * sizeof(uint32_t));
    memset(keep, 1, graph->count);
    for (uint32_t i = 0; i < graph->count; ++i)
    {
        Node row = graph->nodes[i];
        for (uint32_t j = 0; j < row.count; ++j)
        {
            uint32_t operand = graph->edges[row.first + j];
            require(scratch->uses[operand] != UINT32_MAX, "use-count overflow");
            ++scratch->uses[operand];
            ++result.edge_visits;
        }
    }
    uint32_t head = 0;
    uint32_t tail = 0;
    for (uint32_t i = 0; i < graph->count; ++i)
    {
        Node row = graph->nodes[i];
        if (!row.root && (delete_parameters || !row.phi) && scratch->uses[i] == 0)
        {
            require(tail < graph->count, "seed queue capacity");
            scratch->queue[tail++] = i;
        }
    }
    while (head < tail)
    {
        uint32_t id = scratch->queue[head++];
        require(keep[id] != 0, "duplicate deletion");
        keep[id] = 0;
        Node row = graph->nodes[id];
        for (uint32_t j = 0; j < row.count; ++j)
        {
            uint32_t operand = graph->edges[row.first + j];
            require(scratch->uses[operand] != 0, "use-count underflow");
            --scratch->uses[operand];
            ++result.edge_visits;
            Node definition = graph->nodes[operand];
            if (scratch->uses[operand] == 0 && keep[operand] && !definition.root &&
                (delete_parameters || !definition.phi))
            {
                require(tail < graph->count, "deletion queue capacity");
                scratch->queue[tail++] = operand;
            }
        }
    }
    result.pushes = tail;
    for (uint32_t i = 0; i < graph->count; ++i) result.kept += keep[i] != 0;
    return result;
}

static Stats root_mark(Graph const *graph, Scratch *scratch, unsigned char *keep)
{
    Stats result = {0};
    memset(keep, 0, graph->count);
    uint32_t head = 0;
    uint32_t tail = 0;
    for (uint32_t i = 0; i < graph->count; ++i)
    {
        if (graph->nodes[i].root)
        {
            keep[i] = 1;
            scratch->queue[tail++] = i;
        }
    }
    while (head < tail)
    {
        Node row = graph->nodes[scratch->queue[head++]];
        for (uint32_t j = 0; j < row.count; ++j)
        {
            uint32_t operand = graph->edges[row.first + j];
            ++result.edge_visits;
            if (!keep[operand])
            {
                /* Mark at enqueue, not dequeue: one queue entry per node. */
                keep[operand] = 1;
                require(tail < graph->count, "mark queue capacity");
                scratch->queue[tail++] = operand;
            }
        }
    }
    result.pushes = tail;
    result.kept = tail;
    return result;
}

static void dense_oracle(Graph const *graph, unsigned char *keep)
{
    for (uint32_t i = 0; i < graph->count; ++i) keep[i] = graph->nodes[i].root;
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (uint32_t i = 0; i < graph->count; ++i)
        {
            if (keep[i])
            {
                Node row = graph->nodes[i];
                for (uint32_t j = 0; j < row.count; ++j)
                {
                    uint32_t operand = graph->edges[row.first + j];
                    if (!keep[operand])
                    {
                        keep[operand] = 1;
                        changed = true;
                    }
                }
            }
        }
    }
}

static void check_closed(Graph const *graph, unsigned char const *keep)
{
    for (uint32_t i = 0; i < graph->count; ++i)
    {
        require(!graph->nodes[i].root || keep[i], "deleted mandatory root");
        if (keep[i])
        {
            Node row = graph->nodes[i];
            for (uint32_t j = 0; j < row.count; ++j)
            {
                require(keep[graph->edges[row.first + j]] != 0, "dangling retained use");
            }
        }
    }
}

static void compare_graph(Graph const *graph, Scratch *scratch)
{
    require(graph_valid(graph), "invalid graph");
    (void)reference_count(graph, scratch, false, scratch->a);
    (void)reference_count(graph, scratch, true, scratch->b);
    (void)root_mark(graph, scratch, scratch->c);
    dense_oracle(graph, scratch->oracle);
    require(memcmp(scratch->c, scratch->oracle, graph->count) == 0, "mark/oracle disagreement");
    for (uint32_t i = 0; i < graph->count; ++i)
    {
        require(!scratch->c[i] || scratch->b[i], "C not subset of B");
        require(!scratch->b[i] || scratch->a[i], "B not subset of A");
    }
    check_closed(graph, scratch->a);
    check_closed(graph, scratch->b);
    check_closed(graph, scratch->c);
}

static uint64_t exhaustive_small(void)
{
    Graph graph = graph_allocate(3, 9);
    Scratch scratch = scratch_allocate(3);
    uint64_t cases = 0;
    for (uint32_t adjacency = 0; adjacency < 512; ++adjacency)
    {
        uint32_t edge = 0;
        for (uint32_t i = 0; i < 3; ++i)
        {
            graph.nodes[i].first = edge;
            for (uint32_t j = 0; j < 3; ++j)
            {
                if ((adjacency >> (3 * i + j)) & 1u) graph.edges[edge++] = j;
            }
            graph.nodes[i].count = edge - graph.nodes[i].first;
        }
        graph.edge_count = edge;
        for (uint32_t roots = 0; roots < 8; ++roots)
        {
            for (uint32_t phis = 0; phis < 8; ++phis)
            {
                for (uint32_t i = 0; i < 3; ++i)
                {
                    graph.nodes[i].root = ((roots >> i) & 1u) != 0;
                    graph.nodes[i].phi = ((phis >> i) & 1u) != 0;
                }
                compare_graph(&graph, &scratch);
                ++cases;
            }
        }
    }
    scratch_free(&scratch);
    graph_free(&graph);
    return cases;
}

static uint32_t random_word(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint64_t randomized(void)
{
    Graph graph = graph_allocate(64, 64 * 8);
    Scratch scratch = scratch_allocate(64);
    uint32_t random = UINT32_C(0x4f392b17);
    uint64_t cases = 0;
    for (uint32_t trial = 0; trial < 20000; ++trial)
    {
        graph.count = 1 + random_word(&random) % 64;
        uint32_t edge = 0;
        for (uint32_t i = 0; i < graph.count; ++i)
        {
            Node *row = graph.nodes + i;
            row->first = edge;
            row->count = random_word(&random) % 9;
            row->root = random_word(&random) % 8 == 0;
            row->phi = random_word(&random) % 4 == 0;
            for (uint32_t j = 0; j < row->count; ++j)
            {
                graph.edges[edge++] = random_word(&random) % graph.count;
            }
        }
        graph.edge_count = edge;
        compare_graph(&graph, &scratch);
        ++cases;
    }
    scratch_free(&scratch);
    graph_free(&graph);
    return cases;
}

/* Dependency graph of an unsigned counted loop with an accumulator. */
static Graph loop_graph(bool result_uses_accumulator)
{
    Graph graph = graph_allocate(14, 18);
    Node rows[14] = {
        {0, 0, true, false},   /* 0: n, ABI parameter */
        {0, 0, true, false},   /* 1: seed, ABI parameter */
        {0, 0, false, false},  /* 2: zero */
        {0, 0, false, false},  /* 3: one */
        {0, 0, false, false},  /* 4: thirty-three */
        {0, 2, false, true},   /* 5: i = phi(zero, increment) */
        {2, 2, false, false},  /* 6: i < n */
        {4, 1, true, false},   /* 7: conditional branch */
        {5, 2, false, true},   /* 8: accumulator = phi(seed, add) */
        {7, 2, false, false},  /* 9: increment i */
        {9, 2, false, false},  /* 10: multiply accumulator by 33 */
        {11, 2, false, false}, /* 11: add 1 */
        {13, 1, true, false},  /* 12: return */
        {14, 0, true, false},  /* 13: backedge terminator */
    };
    uint32_t operands[14] = {2, 9, 5, 0, 6, 1, 11, 5, 3, 8, 4, 10, 3, 0};
    if (result_uses_accumulator) operands[13] = 8;
    memcpy(graph.nodes, rows, sizeof(rows));
    memcpy(graph.edges, operands, sizeof(operands));
    graph.edge_count = 14;
    return graph;
}

/* Bounded semantic control for the hand-built loop, not a general IR VM. */
static uint32_t loop_execute(unsigned char const *keep, uint32_t n,
                             uint32_t seed, bool result_uses_accumulator)
{
    uint32_t accumulator = seed;
    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t product = 0;
        uint32_t next = 0;
        if (keep[10]) product = accumulator * UINT32_C(33);
        if (keep[11]) next = product + UINT32_C(1);
        if (keep[8]) accumulator = next;
    }
    uint32_t result = result_uses_accumulator ? accumulator : n;
    return result;
}

static uint64_t loop_controls(void)
{
    uint64_t comparisons = 0;
    for (uint32_t live = 0; live < 2; ++live)
    {
        Graph graph = loop_graph(live != 0);
        Scratch scratch = scratch_allocate(graph.count);
        compare_graph(&graph, &scratch);
        Stats a = reference_count(&graph, &scratch, false, scratch.a);
        Stats b = reference_count(&graph, &scratch, true, scratch.b);
        Stats c = root_mark(&graph, &scratch, scratch.c);
        printf("loop_%s: nodes=14 retained_A=%u retained_B=%u retained_C=%u edges_A=%" PRIu64
               " edges_B=%" PRIu64 " edges_C=%" PRIu64 "\n",
               live ? "live" : "dead", a.kept, b.kept, c.kept,
               a.edge_visits, b.edge_visits, c.edge_visits);
        require(a.kept == 14 && b.kept == 14 && c.kept == (live ? 14u : 10u), "loop retention");
        unsigned char original[14];
        memset(original, 1, sizeof(original));
        for (uint32_t n = 0; n <= 256; ++n)
        {
            for (uint32_t seed = 0; seed < 256; ++seed)
            {
                uint32_t expected = loop_execute(original, n, seed, live != 0);
                require(loop_execute(scratch.a, n, seed, live != 0) == expected, "A loop result");
                require(loop_execute(scratch.b, n, seed, live != 0) == expected, "B loop result");
                require(loop_execute(scratch.c, n, seed, live != 0) == expected, "C loop result");
                comparisons += 3;
            }
        }
        scratch_free(&scratch);
        graph_free(&graph);
    }
    return comparisons;
}

static void acyclic_phi_control(void)
{
    Graph graph = graph_allocate(4, 2);
    Scratch scratch = scratch_allocate(4);
    graph.nodes[0].root = true;
    graph.nodes[3] = (Node){0, 2, false, true};
    graph.edges[0] = 1;
    graph.edges[1] = 2;
    compare_graph(&graph, &scratch);
    Stats a = reference_count(&graph, &scratch, false, scratch.a);
    Stats b = reference_count(&graph, &scratch, true, scratch.b);
    Stats c = root_mark(&graph, &scratch, scratch.c);
    require(a.kept == 4 && b.kept == 1 && c.kept == 1, "acyclic phi control");
    printf("unused_acyclic_phi: nodes=4 retained_A=%u retained_B=%u retained_C=%u\n", a.kept, b.kept, c.kept);
    scratch_free(&scratch);
    graph_free(&graph);
}

static void large_controls(void)
{
    uint32_t count = 262144;
    Graph graph = graph_allocate(count, count);
    Scratch scratch = scratch_allocate(count);
    for (uint32_t kind = 0; kind < 3; ++kind)
    {
        memset(graph.nodes, 0, (size_t)count * sizeof(Node));
        graph.edge_count = count - 1;
        graph.nodes[kind == 0 ? count - 1 : 0].root = true;
        for (uint32_t i = 1; i < count; ++i)
        {
            graph.nodes[i].first = i - 1;
            graph.nodes[i].count = 1;
            graph.nodes[i].phi = kind == 2 && i % 64 == 1;
            graph.edges[i - 1] = kind == 2 ? (i == count - 1 ? 1 : i + 1) : i - 1;
        }
        require(graph_valid(&graph), "invalid stress graph");
        Stats a = reference_count(&graph, &scratch, false, scratch.a);
        Stats b = reference_count(&graph, &scratch, true, scratch.b);
        Stats c = root_mark(&graph, &scratch, scratch.c);
        uint32_t expected_c = kind == 0 ? count : 1;
        uint32_t expected_ab = kind == 1 ? 1 : count;
        require(a.kept == expected_ab && b.kept == expected_ab && c.kept == expected_c, "stress retention");
        check_closed(&graph, scratch.a);
        check_closed(&graph, scratch.b);
        check_closed(&graph, scratch.c);
        printf("stress_%u: nodes=%u retained_A=%u retained_B=%u retained_C=%u edges_A=%" PRIu64
               " edges_B=%" PRIu64 " edges_C=%" PRIu64 " pushes_C=%u\n",
               kind, count, a.kept, b.kept, c.kept,
               a.edge_visits, b.edge_visits, c.edge_visits, c.pushes);
    }
    scratch_free(&scratch);
    graph_free(&graph);
}

static void input_controls(void)
{
    Graph graph = graph_allocate(1, 1);
    graph.nodes[0] = (Node){0, 1, true, false};
    graph.edges[0] = 1;
    require(!graph_valid(&graph), "invalid operand accepted");
    graph.edges[0] = 0;
    graph.nodes[0].count = UINT32_MAX;
    require(!graph_valid(&graph), "invalid span accepted");
    graph.nodes[0].count = 1;
    require(graph_valid(&graph), "valid self edge rejected");
    graph_free(&graph);
    Graph empty = graph_allocate(0, 0);
    Scratch scratch = scratch_allocate(0);
    compare_graph(&empty, &scratch);
    scratch_free(&scratch);
    graph_free(&empty);
}

int main(void)
{
    input_controls();
    printf("exhaustive_three_node_graphs=%" PRIu64 "\n", exhaustive_small());
    printf("random_multigraphs=%" PRIu64 " seed=0x4f392b17\n", randomized());
    acyclic_phi_control();
    printf("bounded_loop_result_comparisons=%" PRIu64 "\n", loop_controls());
    large_controls();
    puts("PASS: graph oracle, closure, retention subsets, input controls, loop controls");
    puts("NOT MEASURED: Buster execution, production purity, alias/remap/debug correctness, compilation latency, RSS, native code quality");
    return 0;
}
