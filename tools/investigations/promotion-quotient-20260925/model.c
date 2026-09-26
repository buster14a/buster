// Standalone executable finite model and exact trace census; not a compiler IR.
// Hosted only. The queue and dense solvers share inputs but no update helper.
// All storage is bounded; no recursion, dependencies, callbacks, or warm cache.
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#define MODEL_BLOCKS 3u
#define TRACE_ROWS 262144u
#define TRACE_BYTES (64u * 1024u * 1024u)
#define TRACE_LINE (1024u * 1024u)

typedef struct ModelGraph ModelGraph;
struct ModelGraph
{
    uint8_t edges[MODEL_BLOCKS][MODEL_BLOCKS];
    uint8_t reachable[MODEL_BLOCKS];
    uint32_t entry;
};

typedef struct ModelState ModelState;
struct ModelState
{
    uint8_t effect[MODEL_BLOCKS];
    uint8_t seed[MODEL_BLOCKS];
    bool event_safe;
};

typedef struct ModelResult ModelResult;
struct ModelResult
{
    uint8_t bad_in[MODEL_BLOCKS];
    uint8_t bad_out[MODEL_BLOCKS];
    uint8_t live[MODEL_BLOCKS];
    bool safe;
};

typedef struct EventSummary EventSummary;
struct EventSummary
{
    uint8_t effect;
    bool seed;
    bool invalid;
};

static uint64_t checks;
static uint64_t failures;

static void require(bool condition, char const* message)
{
    checks += 1;
    if (!condition)
    {
        if (failures < 12)
        {
            fprintf(stderr, "MODEL_FAILURE %s\n", message);
        }
        failures += 1;
    }
}

static ModelGraph make_graph(uint32_t bits, uint32_t entry)
{
    ModelGraph graph = {.entry = entry};
    for (uint32_t a = 0; a < MODEL_BLOCKS; a += 1)
    {
        for (uint32_t b = 0; b < MODEL_BLOCKS; b += 1)
        {
            graph.edges[a][b] = (uint8_t)((bits >> (a * MODEL_BLOCKS + b)) & 1u);
        }
    }
    graph.reachable[entry] = 1;
    for (uint32_t round = 0; round < MODEL_BLOCKS; round += 1)
    {
        for (uint32_t a = 0; a < MODEL_BLOCKS; a += 1)
        {
            for (uint32_t b = 0; b < MODEL_BLOCKS; b += 1)
            {
                graph.reachable[b] |= (uint8_t)(graph.reachable[a] && graph.edges[a][b]);
            }
        }
    }
    return graph;
}

// Direct transcription of the two monotone queue kernels, no SSA materializer.
static ModelResult queue_solve(ModelGraph const* graph, ModelState const* state)
{
    ModelResult result = {.safe = state->event_safe};
    uint32_t queue[MODEL_BLOCKS];
    uint32_t read = 0;
    uint32_t write = 0;
    memcpy(result.live, state->seed, sizeof(result.live));
    for (uint32_t b = 0; b < MODEL_BLOCKS; b += 1)
    {
        bool predecessor = false;
        for (uint32_t p = 0; p < MODEL_BLOCKS; p += 1)
        {
            predecessor |= graph->edges[p][b] != 0;
        }
        result.bad_in[b] = (uint8_t)(b == graph->entry || !graph->reachable[b] || !predecessor);
        result.bad_out[b] = (uint8_t)(state->effect[b] == 2 || (!state->effect[b] && result.bad_in[b]));
        if (result.bad_out[b])
        {
            queue[write++] = b;
        }
    }
    while (read < write)
    {
        uint32_t b = queue[read++];
        for (uint32_t s = 0; s < MODEL_BLOCKS; s += 1)
        {
            if (graph->edges[b][s])
            {
                result.bad_in[s] = 1;
                if (!state->effect[s] && !result.bad_out[s])
                {
                    result.bad_out[s] = 1;
                    queue[write++] = s;
                }
            }
        }
    }
    for (uint32_t b = 0; b < MODEL_BLOCKS; b += 1)
    {
        result.safe &= !(state->seed[b] && result.bad_in[b]);
    }
    if (result.safe)
    {
        read = 0;
        write = 0;
        for (uint32_t b = 0; b < MODEL_BLOCKS; b += 1)
        {
            if (result.live[b])
            {
                queue[write++] = b;
            }
        }
        while (read < write)
        {
            uint32_t b = queue[read++];
            for (uint32_t p = 0; p < MODEL_BLOCKS; p += 1)
            {
                if (graph->edges[p][b] && !state->effect[p] && !result.live[p])
                {
                    result.live[p] = 1;
                    queue[write++] = p;
                }
            }
        }
    }
    return result;
}

// Independent synchronous least-fixed-point oracle. Recomputes equations from
// snapshots rather than reusing the queue's discovery/update implementation.
static ModelResult dense_solve(ModelGraph const* graph, ModelState const* state)
{
    ModelResult result = {.safe = state->event_safe};
    bool changed = true;
    while (changed)
    {
        ModelResult next = result;
        changed = false;
        for (uint32_t b = 0; b < MODEL_BLOCKS; b += 1)
        {
            uint32_t predecessor_count = 0;
            bool incoming_bad = false;
            for (uint32_t p = 0; p < MODEL_BLOCKS; p += 1)
            {
                predecessor_count += graph->edges[p][b];
                incoming_bad |= graph->edges[p][b] && result.bad_out[p];
            }
            bool base = b == graph->entry || !graph->reachable[b] || !predecessor_count;
            next.bad_in[b] = (uint8_t)(base || incoming_bad);
            next.bad_out[b] = (uint8_t)(state->effect[b] == 2 || (state->effect[b] == 0 && next.bad_in[b]));
            changed |= next.bad_in[b] != result.bad_in[b] || next.bad_out[b] != result.bad_out[b];
        }
        result = next;
    }
    memcpy(result.live, state->seed, sizeof(result.live));
    for (uint32_t b = 0; b < MODEL_BLOCKS; b += 1)
    {
        result.safe &= !(result.bad_in[b] && state->seed[b]);
    }
    changed = result.safe;
    while (changed)
    {
        ModelResult next = result;
        changed = false;
        for (uint32_t b = 0; b < MODEL_BLOCKS; b += 1)
        {
            bool successor_live = false;
            for (uint32_t s = 0; s < MODEL_BLOCKS; s += 1)
            {
                successor_live |= graph->edges[b][s] && result.live[s];
            }
            next.live[b] = (uint8_t)(state->seed[b] || (state->effect[b] == 0 && successor_live));
            changed |= next.live[b] != result.live[b];
        }
        result = next;
    }
    return result;
}

static bool same_result(ModelResult const* a, ModelResult const* b)
{
    bool same = a->safe == b->safe && !memcmp(a->bad_in, b->bad_in, sizeof(a->bad_in)) &&
                !memcmp(a->bad_out, b->bad_out, sizeof(a->bad_out)) && !memcmp(a->live, b->live, sizeof(a->live));
    return same;
}

static EventSummary append_event(EventSummary state, uint32_t event)
{
    if (event == 0)
    {
        state.invalid |= state.effect == 2;
        state.seed |= state.effect == 0;
    }
    else
    {
        state.effect = (uint8_t)event;
    }
    return state;
}

static bool same_summary(EventSummary a, EventSummary b)
{
    bool same = a.effect == b.effect && a.seed == b.seed && a.invalid == b.invalid;
    return same;
}

static void run_model(void)
{
    uint64_t states = 0;
    for (uint32_t graph_bits = 0; graph_bits < 512; graph_bits += 1)
    {
        for (uint32_t entry = 0; entry < MODEL_BLOCKS; entry += 1)
        {
            ModelGraph graph = make_graph(graph_bits, entry);
            for (uint32_t effects = 0; effects < 27; effects += 1)
            {
                for (uint32_t seeds = 0; seeds < 8; seeds += 1)
                {
                    for (uint32_t event_safe = 0; event_safe < 2; event_safe += 1)
                    {
                        ModelState state = {.event_safe = event_safe != 0};
                        uint32_t digits = effects;
                        for (uint32_t b = 0; b < MODEL_BLOCKS; b += 1)
                        {
                            state.effect[b] = (uint8_t)(digits % 3);
                            digits /= 3;
                            state.seed[b] = (uint8_t)((seeds >> b) & 1u);
                        }
                        ModelResult queue = queue_solve(&graph, &state);
                        ModelResult dense = dense_solve(&graph, &state);
                        require(same_result(&queue, &dense), "queue versus synchronous fixed point");
                        states += 1;
                    }
                }
            }
        }
    }
    EventSummary summaries[121];
    uint32_t summary_count = 0;
    uint32_t sequence_count = 1;
    for (uint32_t length = 0; length <= 4; length += 1)
    {
        for (uint32_t sequence = 0; sequence < sequence_count; sequence += 1)
        {
            EventSummary state = {0};
            uint32_t digits = sequence;
            for (uint32_t index = 0; index < length; index += 1)
            {
                state = append_event(state, digits % 3);
                digits /= 3;
            }
            summaries[summary_count++] = state;
        }
        sequence_count *= 3;
    }
    uint64_t congruence_cases = 0;
    for (uint32_t a = 0; a < summary_count; a += 1)
    {
        for (uint32_t b = 0; b < summary_count; b += 1)
        {
            if (same_summary(summaries[a], summaries[b]))
            {
                for (uint32_t event = 0; event < 3; event += 1)
                {
                    require(same_summary(append_event(summaries[a], event), append_event(summaries[b], event)),
                            "equal summaries remain equal under a common appended event");
                    congruence_cases += 1;
                }
            }
        }
    }
    EventSummary store_load = append_event(append_event((EventSummary){0}, 1), 0);
    EventSummary load_store = append_event(append_event((EventSummary){0}, 0), 1);
    require(store_load.effect == load_store.effect && store_load.seed != load_store.seed, "last-store alone loses read-before-write");
    EventSummary reset_load = append_event(append_event((EventSummary){0}, 2), 0);
    require(reset_load.invalid && !store_load.invalid, "lifetime reset is not a store");
    ModelGraph good = make_graph(1u << 1, 0); // 0 -> 1; block 2 unreachable.
    ModelGraph dead_pred = make_graph((1u << 1) | (1u << 7), 0); // extra 2 -> 1.
    ModelState state = {.effect = {1, 0, 0}, .seed = {0, 1, 0}, .event_safe = true};
    ModelResult good_result = queue_solve(&good, &state);
    ModelResult dead_result = queue_solve(&dead_pred, &state);
    require(good_result.safe && !dead_result.safe, "unreachable predecessor cannot be discarded");
    ModelState reset = state;
    reset.effect[0] = 2;
    ModelResult reset_result = queue_solve(&good, &reset);
    require(good_result.safe && !reset_result.safe, "identical CFG and seeds but reset differs");
    ModelState bad_event = state;
    bad_event.event_safe = false;
    ModelResult bad_event_result = queue_solve(&good, &bad_event);
    require(good_result.safe && !bad_event_result.safe, "event rejection must remain in key");
    uint32_t parameter_count = UINT16_MAX - 1u;
    bool first_fits = parameter_count < UINT16_MAX;
    parameter_count += 1;
    bool second_fits = parameter_count < UINT16_MAX;
    require(first_fits && !second_fits, "final materialization success is not a reusable kernel result");
    printf("MODEL states=%" PRIu64 " graph_contexts=1536 event_sequences=%u congruence_cases=%" PRIu64
           " checks=%" PRIu64 " failures=%" PRIu64 "\n", states, summary_count, congruence_cases, checks, failures);
}

typedef struct TraceRow TraceRow;
struct TraceRow
{
    uint32_t function;
    uint32_t local;
    uint32_t blocks;
    uint32_t initial_safe;
    uint32_t key_offset;
    uint64_t block_visits;
    uint64_t forward_edges;
    uint64_t backward_edges;
    uint32_t safe;
    uint32_t hit;
    uint32_t shared;
    uint64_t comparisons;
    uint64_t compared_bytes;
    uint32_t inserted;
    uint64_t sidecar_bytes;
};

static TraceRow rows[TRACE_ROWS];
static uint32_t order[TRACE_ROWS];
static uint32_t sorted[TRACE_ROWS];
static char keys[TRACE_BYTES];
static char line[TRACE_LINE];

static int compare_rows(uint32_t left, uint32_t right)
{
    TraceRow const* a = rows + left;
    TraceRow const* b = rows + right;
    int result = 0;
    if (a->function != b->function)
    {
        result = a->function < b->function ? -1 : 1;
    }
    else if (a->initial_safe != b->initial_safe)
    {
        result = a->initial_safe < b->initial_safe ? -1 : 1;
    }
    else if (a->blocks != b->blocks)
    {
        result = a->blocks < b->blocks ? -1 : 1;
    }
    else
    {
        result = memcmp(keys + a->key_offset, keys + b->key_offset, a->blocks);
    }
    return result;
}

static void sort_rows(uint32_t count)
{
    for (uint32_t index = 0; index < count; index += 1)
    {
        order[index] = index;
    }
    for (uint32_t width = 1; width < count; width *= 2)
    {
        for (uint32_t start = 0; start < count; start += 2 * width)
        {
            uint32_t middle = start + width < count ? start + width : count;
            uint32_t end = start + 2 * width < count ? start + 2 * width : count;
            uint32_t a = start;
            uint32_t b = middle;
            for (uint32_t out = start; out < end; out += 1)
            {
                bool take_a = a < middle && (b == end || compare_rows(order[a], order[b]) <= 0);
                sorted[out] = take_a ? order[a++] : order[b++];
            }
        }
        memcpy(order, sorted, sizeof(*order) * count);
    }
}

static void census(char const* path)
{
    FILE* input = fopen(path, "rb");
    require(input != NULL, "open census input");
    uint32_t count = 0;
    uint32_t used = 0;
    bool valid = input != NULL;
    while (valid && fgets(line, (int)sizeof(line), input))
    {
        if (!strncmp(line, "PQ ", 3))
        {
            TraceRow row = {0};
            int at = 0;
            int fields = sscanf(line, "PQ %u %u %u %u %n", &row.function, &row.local, &row.blocks, &row.initial_safe, &at);
            size_t length = strlen(line);
            valid = fields == 4 && at > 0 && row.blocks < length && (uint64_t)at + row.blocks < length &&
                    count < TRACE_ROWS && (uint64_t)used + row.blocks <= TRACE_BYTES;
            if (valid)
            {
                char const* key = line + at;
                for (uint32_t b = 0; b < row.blocks; b += 1)
                {
                    valid &= key[b] >= '0' && key[b] <= '6' && key[b] != '3';
                }
                valid &= key[row.blocks] == ' ';
                fields = sscanf(key + row.blocks, " %" SCNu64 " %" SCNu64 " %" SCNu64 " %u %u %u %" SCNu64
                                " %" SCNu64 " %u %" SCNu64, &row.block_visits, &row.forward_edges, &row.backward_edges,
                                &row.safe, &row.hit, &row.shared, &row.comparisons, &row.compared_bytes, &row.inserted,
                                &row.sidecar_bytes);
                valid &= fields == 10;
                if (valid)
                {
                    row.key_offset = used;
                    memcpy(keys + used, key, row.blocks);
                    used += row.blocks;
                    rows[count++] = row;
                }
            }
        }
    }
    if (input)
    {
        valid &= !ferror(input);
        valid &= fclose(input) == 0;
    }
    require(valid, "complete bounded census parse");
    sort_rows(count);
    uint64_t functions = 0;
    uint64_t unique = 0;
    uint64_t hits = 0;
    uint64_t shared = 0;
    uint64_t kernel_blocks = 0;
    uint64_t kernel_edges = 0;
    uint64_t potential_blocks = 0;
    uint64_t potential_edges = 0;
    uint64_t cap_blocks = 0;
    uint64_t cap_edges = 0;
    uint64_t constructed = 0;
    uint64_t comparisons = 0;
    uint64_t compared_bytes = 0;
    uint64_t stored_bytes = 0;
    uint64_t copied_hit_bytes = 0;
    uint64_t maximum_sidecar = 0;
    for (uint32_t index = 0; index < count; index += 1)
    {
        TraceRow const* row = rows + order[index];
        bool first_function = !index || row->function != rows[order[index - 1]].function;
        bool first = !index || compare_rows(order[index - 1], order[index]) != 0;
        functions += first_function;
        unique += first;
        hits += row->hit;
        shared += row->shared;
        kernel_blocks += row->block_visits;
        kernel_edges += row->forward_edges + row->backward_edges;
        constructed += row->blocks;
        comparisons += row->comparisons;
        compared_bytes += row->compared_bytes;
        stored_bytes += (uint64_t)row->inserted * 2u * row->blocks;
        copied_hit_bytes += (uint64_t)row->hit * row->blocks;
        if (row->sidecar_bytes > maximum_sidecar)
        {
            maximum_sidecar = row->sidecar_bytes;
        }
        if (!first)
        {
            TraceRow const* previous = rows + order[index - 1];
            require(row->safe == previous->safe && row->block_visits == previous->block_visits &&
                    row->forward_edges == previous->forward_edges && row->backward_edges == previous->backward_edges,
                    "exact equal-key census results and work agree");
            require(row->local != previous->local, "duplicate local requires a distinct invocation context");
            potential_blocks += row->block_visits;
            potential_edges += row->forward_edges + row->backward_edges;
        }
        if (row->hit)
        {
            cap_blocks += row->block_visits;
            cap_edges += row->forward_edges + row->backward_edges;
        }
    }
    printf("CENSUS file=%s calls=%u functions=%" PRIu64 " unique=%" PRIu64 " potential_reused=%" PRIu64
           " cap8_hits=%" PRIu64 " shared_calls=%" PRIu64 " kernel_block_visits=%" PRIu64 " kernel_edge_visits=%" PRIu64
           " potential_saved_blocks=%" PRIu64 " potential_saved_edges=%" PRIu64
           " cap8_saved_blocks=%" PRIu64 " cap8_saved_edges=%" PRIu64
           " key_cells_constructed=%" PRIu64 " key_comparisons=%" PRIu64 " key_bytes_compared=%" PRIu64
           " insertion_copy_bytes=%" PRIu64 " hit_copy_bytes_if_shared=%" PRIu64 " max_function_sidecar_bytes=%" PRIu64
           " failures=%" PRIu64 "\n", path, count, functions, unique, (uint64_t)count - unique, hits, shared, kernel_blocks,
           kernel_edges, potential_blocks, potential_edges, cap_blocks, cap_edges, constructed, comparisons, compared_bytes,
           stored_bytes, copied_hit_bytes, maximum_sidecar, failures);
}

int main(int argc, char** argv)
{
    if (argc == 1)
    {
        run_model();
    }
    else if (argc == 3 && !strcmp(argv[1], "--census"))
    {
        census(argv[2]);
    }
    else
    {
        require(false, "usage: model [--census trace.log]");
    }
    return failures ? 1 : 0;
}
