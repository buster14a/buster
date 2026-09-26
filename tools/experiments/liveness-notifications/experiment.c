/* Research only: causal scheduling for machine_fast_close_live_ranges.
 * Standalone: cc -std=c11 -O2 -Wall -Wextra -Werror experiment.c -o experiment
 * The disposable instrumentation patch includes this file with
 * BUSTER_LIVENESS_REPLAY defined. No normal compiler includes it.
 * No clocks, PMU access, files, threads, callbacks or persistent state.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#if defined(BUSTER_LIVENESS_REPLAY)
typedef u32 LpU32;
typedef u64 LpU64;
typedef u8 LpU8;
#define LP_LOCAL BUSTER_GLOBAL_LOCAL
#else
typedef uint32_t LpU32;
typedef uint64_t LpU64;
typedef uint8_t LpU8;
#define LP_LOCAL static
#endif

typedef struct LpGraph {
    LpU32 blocks;
    LpU32 words;
    LpU32 const* offsets;
    LpU32 const* predecessors;
    LpU64 const* reads;
    LpU64 const* writes;
} LpGraph;

typedef struct LpEvent {
    LpU32 block;
    LpU32 word;
} LpEvent;

_Static_assert(sizeof(LpEvent) == 8, "event byte accounting");

typedef struct LpCounts {
    LpU64 pops;
    LpU64 transfer_words;
    LpU64 edge_visits;
    LpU64 merge_words;
    LpU64 in_updates;
    LpU64 out_updates;
    LpU64 in_bits;
    LpU64 out_bits;
    LpU64 redundant_pops;
    LpU64 notifications;
    LpU64 duplicates;
    LpU64 pushes;
    LpU64 initial_pushes;
    LpU64 max_pending;
    LpU64 mask_tests;
    LpU64 seed_words;
    LpU64 plane_zero_words;
    LpU64 queue_clear_entries;
    LpU64 sweeps;
    int overflow;
} LpCounts;

typedef struct LpState {
    LpU64* in;
    LpU64* out;
    LpU64* next_in;
    LpU64* next_out;
    LpEvent* queue;
    LpU8* queued;
    size_t capacity;
} LpState;

typedef struct LpEvidence {
    LpGraph graph;
    LpCounts actual;
    LpCounts baseline;
    LpCounts filtered;
    LpCounts words;
    LpCounts oracle;
    LpU64* expected_in;
    LpU64* expected_out;
    LpU64 materialized_bits;
} LpEvidence;

LP_LOCAL void lp_require(int condition, char const* message)
{
    if (!condition)
    {
        fprintf(stderr, "LIVENESS_PROBE_FAILURE %s\n", message);
        exit(EXIT_FAILURE);
    }
}

LP_LOCAL LpU64 lp_popcount(LpU64 value)
{
    LpU64 count = 0;
    while (value)
    {
        value &= value - 1;
        count += 1;
    }
    return count;
}

LP_LOCAL void lp_add(LpCounts* counts, LpU64* destination, LpU64 amount)
{
    if (amount > UINT64_MAX - *destination)
    {
        counts->overflow = 1;
        *destination = UINT64_MAX;
    }
    else
    {
        *destination += amount;
    }
}

LP_LOCAL size_t lp_plane(LpGraph const* graph)
{
    lp_require(graph->words == 0 || graph->blocks <= SIZE_MAX / graph->words, "plane multiplication overflow");
    size_t plane = (size_t)graph->blocks * graph->words;
    lp_require(plane <= SIZE_MAX / sizeof(LpU64), "plane byte overflow");
    lp_require(graph->blocks != UINT32_MAX, "offset sentinel overflow");
    lp_require((!graph->blocks && !graph->offsets) || (graph->offsets && graph->offsets[0] == 0), "invalid predecessor offsets");
    for (LpU32 block = 0; block < graph->blocks; block += 1)
    {
        lp_require(graph->offsets[block] <= graph->offsets[block + 1], "nonmonotone predecessor offsets");
        for (LpU32 edge = graph->offsets[block]; edge < graph->offsets[block + 1]; edge += 1)
        {
            lp_require(graph->predecessors[edge] < graph->blocks, "invalid predecessor ID");
        }
    }
    lp_require(!plane || (graph->reads && graph->writes), "missing input planes");
    return plane;
}

LP_LOCAL void lp_in_update(LpCounts* counts, LpU64 old_value, LpU64 new_value)
{
    lp_require((old_value & ~new_value) == 0, "nonmonotone IN update");
    if (old_value != new_value)
    {
        lp_add(counts, &counts->in_updates, 1);
        lp_add(counts, &counts->in_bits, lp_popcount(new_value & ~old_value));
    }
}

LP_LOCAL void lp_out_update(LpCounts* counts, LpU64 old_value, LpU64 new_value)
{
    lp_require((old_value & ~new_value) == 0, "nonmonotone OUT update");
    if (old_value != new_value)
    {
        lp_add(counts, &counts->out_updates, 1);
        lp_add(counts, &counts->out_bits, lp_popcount(new_value & ~old_value));
    }
}

LP_LOCAL void lp_push(LpState* state, LpCounts* counts, size_t* pending,
                      size_t index, LpU32 block, LpU32 word, int initial)
{
    lp_require(index < state->capacity, "queued index overflow");
    if (!initial)
    {
        lp_add(counts, &counts->notifications, 1);
    }
    if (state->queued[index])
    {
        lp_add(counts, &counts->duplicates, 1);
    }
    else
    {
        lp_require(*pending < state->capacity, "queue capacity exceeded");
        state->queue[*pending].block = block;
        state->queue[*pending].word = word;
        *pending += 1;
        state->queued[index] = 1;
        lp_add(counts, &counts->pushes, 1);
        if (initial)
        {
            lp_add(counts, &counts->initial_pushes, 1);
        }
        if (*pending > counts->max_pending)
        {
            counts->max_pending = (LpU64)*pending;
        }
    }
}

/* Exact block/word loop and LIFO ordering of the pinned production solver.
 * filtered==1 adds only the killed-OUT-notification control. */
LP_LOCAL LpCounts lp_blocks(LpGraph const* graph, LpState* state, int filtered)
{
    LpCounts counts = {0};
    size_t plane = (size_t)graph->blocks * graph->words;
    memset(state->in, 0, (plane ? plane : 1) * sizeof(*state->in));
    memset(state->out, 0, (plane ? plane : 1) * sizeof(*state->out));
    memset(state->queued, 0, graph->blocks ? graph->blocks : 1);
    counts.plane_zero_words = (LpU64)(plane ? plane : 1) * 2;
    counts.queue_clear_entries = graph->blocks ? graph->blocks : 1;
    size_t pending = 0;
    for (LpU32 block = 0; block < graph->blocks; block += 1)
    {
        lp_push(state, &counts, &pending, block, block, 0, 1);
    }
    while (pending)
    {
        LpU32 block = state->queue[--pending].block;
        state->queued[block] = 0;
        lp_add(&counts, &counts.pops, 1);
        LpU64 old_updates = counts.in_updates;
        size_t row = (size_t)block * graph->words;
        for (LpU32 word = 0; word < graph->words; word += 1)
        {
            size_t index = row + word;
            LpU64 value = graph->reads[index] | (state->out[index] & ~graph->writes[index]);
            lp_add(&counts, &counts.transfer_words, 1);
            lp_in_update(&counts, state->in[index], value);
            state->in[index] = value;
        }
        if (old_updates == counts.in_updates)
        {
            lp_add(&counts, &counts.redundant_pops, 1);
        }
        for (LpU32 edge = graph->offsets[block]; edge < graph->offsets[block + 1]; edge += 1)
        {
            LpU32 predecessor = graph->predecessors[edge];
            int notify = 0;
            lp_add(&counts, &counts.edge_visits, 1);
            for (LpU32 word = 0; word < graph->words; word += 1)
            {
                size_t index = (size_t)predecessor * graph->words + word;
                LpU64 old_value = state->out[index];
                LpU64 value = old_value | state->in[row + word];
                lp_add(&counts, &counts.merge_words, 1);
                if (value != old_value)
                {
                    state->out[index] = value;
                    lp_out_update(&counts, old_value, value);
                    if (filtered)
                    {
                        lp_add(&counts, &counts.mask_tests, 1);
                        notify |= ((value & ~old_value) & ~graph->writes[index]) != 0;
                    }
                    else
                    {
                        notify = 1;
                    }
                }
            }
            if (notify)
            {
                lp_push(state, &counts, &pending, predecessor, predecessor, 0, 0);
            }
        }
    }
    return counts;
}

/* Local causal state is (block, word); dense IN/OUT planes remain unchanged.
 * All READ generators enter IN once. OUT always receives every gained bit.
 * Only a gained IN word notifies consumers. At most one pending event per
 * pair, with explicit IDs and a deterministic LIFO / predecessor order. */
LP_LOCAL LpCounts lp_words(LpGraph const* graph, LpState* state)
{
    LpCounts counts = {0};
    size_t plane = (size_t)graph->blocks * graph->words;
    memset(state->out, 0, plane * sizeof(*state->out));
    memset(state->queued, 0, plane);
    counts.plane_zero_words = plane;
    counts.seed_words = plane;
    counts.queue_clear_entries = plane;
    size_t pending = 0;
    for (LpU32 block = 0; block < graph->blocks; block += 1)
    {
        for (LpU32 word = 0; word < graph->words; word += 1)
        {
            size_t index = (size_t)block * graph->words + word;
            state->in[index] = graph->reads[index];
            lp_in_update(&counts, 0, state->in[index]);
            if (state->in[index])
            {
                lp_push(state, &counts, &pending, index, block, word, 1);
            }
        }
    }
    while (pending)
    {
        LpEvent event = state->queue[--pending];
        size_t source = (size_t)event.block * graph->words + event.word;
        state->queued[source] = 0;
        lp_add(&counts, &counts.pops, 1);
        for (LpU32 edge = graph->offsets[event.block]; edge < graph->offsets[event.block + 1]; edge += 1)
        {
            LpU32 predecessor = graph->predecessors[edge];
            size_t index = (size_t)predecessor * graph->words + event.word;
            LpU64 old_out = state->out[index];
            LpU64 delta_out = state->in[source] & ~old_out;
            lp_add(&counts, &counts.edge_visits, 1);
            lp_add(&counts, &counts.merge_words, 1);
            if (delta_out)
            {
                state->out[index] = old_out | delta_out;
                lp_out_update(&counts, old_out, state->out[index]);
                lp_add(&counts, &counts.mask_tests, 1);
                lp_add(&counts, &counts.transfer_words, 1);
                LpU64 old_in = state->in[index];
                LpU64 value = old_in | (delta_out & ~graph->writes[index]);
                if (value != old_in)
                {
                    state->in[index] = value;
                    lp_in_update(&counts, old_in, value);
                    lp_push(state, &counts, &pending, index, predecessor, event.word, 0);
                }
            }
        }
    }
    return counts;
}

/* Independent synchronous full recomputation: no queue, OR accumulator
 * retained across rounds, or immediate within-round propagation. */
LP_LOCAL LpCounts lp_recompute(LpGraph const* graph, LpState* state)
{
    LpCounts counts = {0};
    size_t plane = (size_t)graph->blocks * graph->words;
    memset(state->in, 0, plane * sizeof(*state->in));
    memset(state->out, 0, plane * sizeof(*state->out));
    counts.plane_zero_words = (LpU64)plane * 2;
    int changed = 1;
    while (changed)
    {
        changed = 0;
        lp_add(&counts, &counts.sweeps, 1);
        lp_require(counts.sweeps <= (LpU64)graph->blocks + 2, "full recomputation did not converge");
        memset(state->next_out, 0, plane * sizeof(*state->next_out));
        lp_add(&counts, &counts.plane_zero_words, plane);
        for (LpU32 block = 0; block < graph->blocks; block += 1)
        {
            lp_add(&counts, &counts.pops, 1);
            for (LpU32 edge = graph->offsets[block]; edge < graph->offsets[block + 1]; edge += 1)
            {
                size_t destination = (size_t)graph->predecessors[edge] * graph->words;
                size_t source = (size_t)block * graph->words;
                lp_add(&counts, &counts.edge_visits, 1);
                for (LpU32 word = 0; word < graph->words; word += 1)
                {
                    state->next_out[destination + word] |= state->in[source + word];
                    lp_add(&counts, &counts.merge_words, 1);
                }
            }
        }
        for (size_t index = 0; index < plane; index += 1)
        {
            state->next_in[index] = graph->reads[index] | (state->next_out[index] & ~graph->writes[index]);
            lp_add(&counts, &counts.transfer_words, 1);
            changed |= state->next_in[index] != state->in[index] || state->next_out[index] != state->out[index];
            lp_in_update(&counts, state->in[index], state->next_in[index]);
            lp_out_update(&counts, state->out[index], state->next_out[index]);
        }
        memcpy(state->in, state->next_in, plane * sizeof(*state->in));
        memcpy(state->out, state->next_out, plane * sizeof(*state->out));
    }
    return counts;
}

LP_LOCAL void lp_equal(LpGraph const* graph, LpU64 const* left_in, LpU64 const* left_out,
                       LpU64 const* right_in, LpU64 const* right_out)
{
    size_t bytes = (size_t)graph->blocks * graph->words * sizeof(LpU64);
    lp_require(memcmp(left_in, right_in, bytes) == 0, "IN planes differ");
    lp_require(memcmp(left_out, right_out, bytes) == 0, "OUT planes differ (killed bits must be retained)");
}

LP_LOCAL void lp_print(char const* family, char const* solver, LpGraph const* graph,
                       LpCounts const* counts, LpU64 live_bits)
{
    lp_require(!counts->overflow, "counter overflow; result is not usable evidence");
    printf("LIVENESS_WORK_V1 family=%s solver=%s blocks=%u words=%u edges=%u "
           "pops=%llu transfer_words=%llu edge_visits=%llu merge_words=%llu "
           "in_updates=%llu out_updates=%llu in_bits=%llu out_bits=%llu "
           "redundant_pops=%llu notifications=%llu duplicates=%llu pushes=%llu "
           "initial_pushes=%llu max_pending=%llu mask_tests=%llu seed_words=%llu "
           "plane_zero_words=%llu queue_clear_entries=%llu sweeps=%llu materialized_bits=%llu\n",
           family, solver, (unsigned)graph->blocks, (unsigned)graph->words,
           (unsigned)(graph->offsets ? graph->offsets[graph->blocks] : 0),
           (unsigned long long)counts->pops, (unsigned long long)counts->transfer_words,
           (unsigned long long)counts->edge_visits, (unsigned long long)counts->merge_words,
           (unsigned long long)counts->in_updates, (unsigned long long)counts->out_updates,
           (unsigned long long)counts->in_bits, (unsigned long long)counts->out_bits,
           (unsigned long long)counts->redundant_pops, (unsigned long long)counts->notifications,
           (unsigned long long)counts->duplicates, (unsigned long long)counts->pushes,
           (unsigned long long)counts->initial_pushes, (unsigned long long)counts->max_pending,
           (unsigned long long)counts->mask_tests, (unsigned long long)counts->seed_words,
           (unsigned long long)counts->plane_zero_words, (unsigned long long)counts->queue_clear_entries,
           (unsigned long long)counts->sweeps, (unsigned long long)live_bits);
}

LP_LOCAL void lp_models(LpEvidence* evidence, LpState* state)
{
    LpGraph const* graph = &evidence->graph;
    size_t plane = lp_plane(graph);
    evidence->oracle = lp_recompute(graph, state);
    memcpy(evidence->expected_in, state->in, plane * sizeof(*state->in));
    memcpy(evidence->expected_out, state->out, plane * sizeof(*state->out));
    lp_require(evidence->oracle.in_bits <= UINT64_MAX - evidence->oracle.out_bits, "materialized bit counter overflow");
    evidence->materialized_bits = evidence->oracle.in_bits + evidence->oracle.out_bits;
    evidence->baseline = lp_blocks(graph, state, 0);
    lp_equal(graph, state->in, state->out, evidence->expected_in, evidence->expected_out);
    evidence->filtered = lp_blocks(graph, state, 1);
    lp_equal(graph, state->in, state->out, evidence->expected_in, evidence->expected_out);
    evidence->words = lp_words(graph, state);
    lp_equal(graph, state->in, state->out, evidence->expected_in, evidence->expected_out);
    lp_require(!evidence->baseline.overflow && !evidence->filtered.overflow &&
               !evidence->words.overflow && !evidence->oracle.overflow, "counter overflow");
    lp_require(evidence->baseline.max_pending <= graph->blocks &&
               evidence->filtered.max_pending <= graph->blocks &&
               evidence->words.max_pending <= plane, "pending bound");
    lp_require(evidence->words.pushes <= evidence->words.in_bits, "word event without a new IN bit");
    lp_require(evidence->words.in_bits == evidence->baseline.in_bits &&
               evidence->words.out_bits == evidence->baseline.out_bits, "fact population changed");
}

#if defined(BUSTER_LIVENESS_REPLAY)
LP_LOCAL LpEvidence lp_begin(Arena* arena, MachineFunction const* function, MachineFastPrepass const* prepass,
                             u64 const* reads, u64 const* writes, u32 words)
{
    LpEvidence evidence = {0};
    evidence.graph = (LpGraph){function->block_count, words, prepass->predecessor_offsets,
                               prepass->predecessor_list, reads, writes};
    size_t plane = lp_plane(&evidence.graph);
    size_t count = plane ? plane : 1;
    size_t capacity = plane > function->block_count ? plane : function->block_count;
    capacity = capacity ? capacity : 1;
    lp_require(capacity <= SIZE_MAX / sizeof(LpEvent), "event array overflow");
    LpState state = {0};
    state.in = arena_allocate(arena, LpU64, count);
    state.out = arena_allocate(arena, LpU64, count);
    state.next_in = arena_allocate(arena, LpU64, count);
    state.next_out = arena_allocate(arena, LpU64, count);
    state.queue = arena_allocate(arena, LpEvent, capacity);
    state.queued = arena_allocate(arena, LpU8, capacity);
    state.capacity = capacity;
    evidence.expected_in = arena_allocate(arena, LpU64, count);
    evidence.expected_out = arena_allocate(arena, LpU64, count);
    lp_require(state.in && state.out && state.next_in && state.next_out && state.queue && state.queued &&
               evidence.expected_in && evidence.expected_out, "diagnostic arena exhausted");
    lp_models(&evidence, &state);
    evidence.actual.plane_zero_words = (LpU64)(plane ? plane : 1) * 2;
    evidence.actual.queue_clear_entries = function->block_count ? function->block_count : 1;
    evidence.actual.initial_pushes = function->block_count;
    evidence.actual.pushes = function->block_count;
    evidence.actual.max_pending = function->block_count;
    return evidence;
}

LP_LOCAL void lp_finish(LpEvidence* evidence, u64 const* live_in, u64 const* live_out)
{
    lp_equal(&evidence->graph, live_in, live_out, evidence->expected_in, evidence->expected_out);
    /* Both structs are fully zero-initialized before any field is written.
     * Compare fields rather than padding for portability. */
#define LP_SAME(field) lp_require(evidence->actual.field == evidence->baseline.field, "actual/replay counter mismatch: " #field)
    LP_SAME(pops); LP_SAME(transfer_words); LP_SAME(edge_visits); LP_SAME(merge_words);
    LP_SAME(in_updates); LP_SAME(out_updates); LP_SAME(in_bits); LP_SAME(out_bits);
    LP_SAME(redundant_pops); LP_SAME(notifications); LP_SAME(duplicates); LP_SAME(pushes);
    LP_SAME(initial_pushes); LP_SAME(max_pending); LP_SAME(overflow);
#undef LP_SAME
    if (getenv("BUSTER_LIVENESS_REPORT"))
    {
        lp_print("production", "actual", &evidence->graph, &evidence->actual, evidence->materialized_bits);
        lp_print("production", "filtered", &evidence->graph, &evidence->filtered, evidence->materialized_bits);
        lp_print("production", "word", &evidence->graph, &evidence->words, evidence->materialized_bits);
        lp_print("production", "oracle", &evidence->graph, &evidence->oracle, evidence->materialized_bits);
    }
}
#else
LP_LOCAL void* lp_allocate(size_t count, size_t size)
{
    lp_require(size && count <= SIZE_MAX / size, "allocation size overflow");
    void* result = calloc(count ? count : 1, size);
    lp_require(result != NULL, "allocation failed");
    return result;
}

/* Construct predecessor CSR independently from source/destination edge pairs.
 * Input construction costs: clear B+1 counters, E counts, B prefix sums,
 * B cursor copies, E writes; caller retains the edge pairs only in tests. */
LP_LOCAL LpEvidence lp_case(char const* name, LpU32 blocks, LpU32 slots, LpU32 edges,
                            LpU32 const* sources, LpU32 const* targets,
                            LpU64 const* reads, LpU64 const* writes, int report)
{
    LpU32 words = slots / 64 + (slots % 64 != 0);
    size_t plane = (size_t)blocks * words;
    LpU32* offsets = lp_allocate((size_t)blocks + 1, sizeof(*offsets));
    LpU32* cursors = lp_allocate(blocks, sizeof(*cursors));
    LpU32* predecessors = lp_allocate(edges, sizeof(*predecessors));
    for (LpU32 edge = 0; edge < edges; edge += 1)
    {
        lp_require(sources[edge] < blocks && targets[edge] < blocks, "test edge out of range");
        offsets[targets[edge] + 1] += 1;
    }
    for (LpU32 block = 0; block < blocks; block += 1)
    {
        offsets[block + 1] += offsets[block];
        cursors[block] = offsets[block];
    }
    for (LpU32 edge = 0; edge < edges; edge += 1)
    {
        predecessors[cursors[targets[edge]]++] = sources[edge];
    }
    LpEvidence evidence = {0};
    evidence.graph = (LpGraph){blocks, words, offsets, predecessors, reads, writes};
    size_t capacity = plane > blocks ? plane : blocks;
    LpState state = {0};
    state.in = lp_allocate(plane, sizeof(*state.in));
    state.out = lp_allocate(plane, sizeof(*state.out));
    state.next_in = lp_allocate(plane, sizeof(*state.next_in));
    state.next_out = lp_allocate(plane, sizeof(*state.next_out));
    state.queue = lp_allocate(capacity, sizeof(*state.queue));
    state.queued = lp_allocate(capacity, sizeof(*state.queued));
    state.capacity = capacity ? capacity : 1;
    evidence.expected_in = lp_allocate(plane, sizeof(*evidence.expected_in));
    evidence.expected_out = lp_allocate(plane, sizeof(*evidence.expected_out));
    lp_models(&evidence, &state);
    /* Partial-word and ordinary range materialization are checked at bit
     * granularity, independently of the solvers' word comparisons. */
    for (LpU32 word = 0; word < words; word += 1)
    {
        for (LpU32 bit = 0; bit < 64; bit += 1)
        {
            LpU64 mask = (LpU64)1 << bit;
            LpU32 base_start = UINT32_MAX, base_end = 0;
            LpU32 word_start = UINT32_MAX, word_end = 0;
            for (LpU32 block = 0; block < blocks; block += 1)
            {
                size_t index = (size_t)block * words + word;
                if (evidence.expected_in[index] & mask)
                {
                    if (block < base_start) base_start = block;
                }
                if (evidence.expected_out[index] & mask) base_end = block;
                if (state.in[index] & mask)
                {
                    if (block < word_start) word_start = block;
                }
                if (state.out[index] & mask) word_end = block;
                if ((LpU64)word * 64 + bit >= slots)
                {
                    lp_require(((state.in[index] | state.out[index]) & mask) == 0, "padding became live");
                }
            }
            lp_require(base_start == word_start && base_end == word_end, "materialized ranges differ");
        }
    }
    if (report)
    {
        printf("LIVENESS_SETUP_V1 family=%s blocks=%u slots=%u edges=%u csr_records=%llu "
               "block_queue_bytes=%llu word_queue_bytes=%llu planes_bytes=%llu\n", name,
               (unsigned)blocks, (unsigned)slots, (unsigned)edges,
               (unsigned long long)((LpU64)3 * blocks + 1 + (LpU64)2 * edges),
               (unsigned long long)((LpU64)5 * blocks),
               (unsigned long long)((LpU64)9 * plane),
               (unsigned long long)((LpU64)16 * plane));
        lp_print(name, "block", &evidence.graph, &evidence.baseline, evidence.materialized_bits);
        lp_print(name, "filtered", &evidence.graph, &evidence.filtered, evidence.materialized_bits);
        lp_print(name, "word", &evidence.graph, &evidence.words, evidence.materialized_bits);
        lp_print(name, "oracle", &evidence.graph, &evidence.oracle, evidence.materialized_bits);
    }
    free(state.in); free(state.out); free(state.next_in); free(state.next_out);
    free(state.queue); free(state.queued);
    free(evidence.expected_in); free(evidence.expected_out);
    free(offsets); free(cursors); free(predecessors);
    /* Returned counts are values; no pointer into freed test storage escapes. */
    evidence.graph = (LpGraph){0};
    evidence.expected_in = NULL;
    evidence.expected_out = NULL;
    return evidence;
}

LP_LOCAL LpU32 lp_random(LpU32* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

int main(void)
{
    LpU64 cases = 0;
    /* Every directed graph on three nodes, including self loops and
     * disconnected SCCs, crossed with every single-bit READ/WRITE state. */
    for (LpU32 graph_mask = 0; graph_mask < 512; graph_mask += 1)
    {
        LpU32 sources[9], targets[9], edges = 0;
        for (LpU32 source = 0; source < 3; source += 1)
        {
            for (LpU32 target = 0; target < 3; target += 1)
            {
                if (graph_mask & (1u << (source * 3 + target)))
                {
                    sources[edges] = source;
                    targets[edges++] = target;
                }
            }
        }
        for (LpU32 facts = 0; facts < 64; facts += 1)
        {
            LpU64 reads[3], writes[3];
            for (LpU32 block = 0; block < 3; block += 1)
            {
                reads[block] = (facts >> block) & 1u;
                writes[block] = (facts >> (block + 3)) & 1u;
            }
            lp_case("exhaustive", 3, 1, edges, sources, targets, reads, writes, 0);
            cases += 1;
        }
    }
    LpU32 seed = 0x8b7219e5u;
    LpU32 slot_sizes[] = {0, 1, 63, 64, 65, 127, 128, 129, 1025};
    for (LpU32 test = 0; test < 512; test += 1)
    {
        LpU32 blocks = lp_random(&seed) % 24;
        LpU32 slots = slot_sizes[test % (sizeof(slot_sizes) / sizeof(slot_sizes[0]))];
        LpU32 words = slots / 64 + (slots % 64 != 0);
        LpU32 edges = blocks ? lp_random(&seed) % (4 * blocks + 1) : 0;
        LpU32 sources[96], targets[96];
        LpU64 reads[24 * 17] = {0}, writes[24 * 17] = {0};
        for (LpU32 edge = 0; edge < edges; edge += 1)
        {
            sources[edge] = lp_random(&seed) % blocks;
            targets[edge] = lp_random(&seed) % blocks;
        }
        for (LpU32 block = 0; block < blocks; block += 1)
        {
            for (LpU32 slot = 0; slot < slots; slot += 1)
            {
                size_t index = (size_t)block * words + slot / 64;
                LpU64 bit = (LpU64)1 << (slot % 64);
                if (lp_random(&seed) % 19 == 0) reads[index] |= bit;
                if (lp_random(&seed) % 5 == 0) writes[index] |= bit;
            }
        }
        lp_case("random", blocks, slots, edges, sources, targets, reads, writes, 0);
        cases += 1;
    }
    /* N (B*ceil(S/64)) and the supplied READ frontier vary independently.
     * Report realized causal IN/OUT bit populations, not just seed counts. */
    LpU32 populations[] = {32, 128, 512};
    LpU32 widths[] = {1, 65, 1025};
    LpU32 frontiers[] = {0, 1, 8, 64};
    for (LpU32 b_index = 0; b_index < 3; b_index += 1)
    {
        LpU32 blocks = populations[b_index];
        for (LpU32 s_index = 0; s_index < 3; s_index += 1)
        {
            LpU32 slots = widths[s_index];
            LpU32 words = slots / 64 + (slots % 64 != 0);
            for (LpU32 d_index = 0; d_index < 4; d_index += 1)
            {
                LpU32 frontier = frontiers[d_index];
                if (frontier > slots) continue;
                for (LpU32 shape = 0; shape < 6; shape += 1)
                {
                    if (shape == 5 && d_index != 1) continue;
                    LpU32 sources[1024], targets[1024], edges = 0;
                    LpU64* reads = lp_allocate((size_t)blocks * words, sizeof(*reads));
                    LpU64* writes = lp_allocate((size_t)blocks * words, sizeof(*writes));
                    char name[96];
                    snprintf(name, sizeof(name), "shape%u-B%u-S%u-D%u", (unsigned)shape,
                             (unsigned)blocks, (unsigned)slots, (unsigned)frontier);
                    if (shape == 5)
                    {
                        snprintf(name, sizeof(name), "dense-B%u-S%u", (unsigned)blocks, (unsigned)slots);
                    }
                    for (LpU32 block = 1; block < blocks; block += 1)
                    {
                        /* 0: favorable chain; 1: adverse chain; 2: cycle;
                         * 3: repeated updates of an already visited hub;
                         * 4: repeated updates wholly killed by that hub;
                         * 5: dense facts, every word active, no kills. */
                        sources[edges] = shape == 1 ? block : shape == 3 || shape == 4 ? blocks - 1 : block - 1;
                        targets[edges++] = shape == 1 ? block - 1 : shape == 3 || shape == 4 ? block - 1 : block;
                    }
                    if (shape == 2)
                    {
                        sources[edges] = blocks - 1;
                        targets[edges++] = 0;
                    }
                    if (shape == 5)
                    {
                        for (LpU32 block = 0; block < blocks; block += 1)
                        {
                            for (LpU32 slot = 0; slot < slots; slot += 1)
                            {
                                reads[(size_t)block * words + slot / 64] |= (LpU64)1 << (slot % 64);
                            }
                        }
                    }
                    else
                    {
                        for (LpU32 slot = 0; slot < frontier; slot += 1)
                        {
                            LpU32 block = shape == 1 ? 0 : shape == 3 || shape == 4 ? slot % (blocks - 1) : blocks - 1;
                            reads[(size_t)block * words + slot / 64] |= (LpU64)1 << (slot % 64);
                        }
                    }
                    if (shape == 4)
                    {
                        for (LpU32 slot = 0; slot < slots; slot += 1)
                        {
                            writes[(size_t)(blocks - 1) * words + slot / 64] |= (LpU64)1 << (slot % 64);
                        }
                    }
                    LpEvidence evidence = lp_case(name, blocks, slots, edges, sources, targets, reads, writes, 1);
                    if (shape < 3)
                    {
                        lp_require(evidence.words.pops == (frontier ? blocks : 0), "chain/cycle event count must be width-independent");
                        lp_require(evidence.words.seed_words == (LpU64)blocks * words, "full seed scan must be charged");
                    }
                    if (shape == 1 && frontier)
                    {
                        lp_require(evidence.baseline.pops == (LpU64)2 * blocks - 1, "adverse-chain baseline count changed");
                    }
                    if (shape == 4)
                    {
                        lp_require(evidence.filtered.pops == blocks, "killed hub must not be requeued");
                        lp_require(evidence.words.out_bits == frontier && evidence.words.in_bits == frontier,
                                   "killed OUT lost, or spuriously propagated");
                    }
                    if (shape == 5)
                    {
                        lp_require(evidence.words.mask_tests > 0, "dense overhead control is vacuous");
                        lp_require(evidence.baseline.pops == blocks && evidence.words.pops == (LpU64)blocks * words &&
                                   evidence.words.merge_words == evidence.baseline.merge_words,
                                   "dense control must expose per-word queue overhead without less edge-word work");
                        lp_require(evidence.filtered.pops == evidence.baseline.pops &&
                                   evidence.filtered.merge_words == evidence.baseline.merge_words,
                                   "dense control must not flatter notification filtering");
                    }
                    free(reads); free(writes);
                    cases += 1;
                }
            }
        }
    }
    printf("LIVENESS_PROBE_RESULT cases=%llu exhaustive=32768 random=512 grid=159 failures=0\n", (unsigned long long)cases);
    return 0;
}
#endif

#undef LP_LOCAL
