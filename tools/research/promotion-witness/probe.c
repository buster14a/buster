// Research-only bounded state model, not a compiler module or acceptance suite.
// prepare.py extracts the exact pinned cleanup and supplies four generated includes.
// The remap seam checks roots/list tails, NOT dense canonical publication or codegen.
// Ownership: this research directory only. Entry: main. Oracle: probe_decision.
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUSTER_GLOBAL_LOCAL static
#define IR_PROMOTE_NONE UINT32_MAX
#define MAX_V 128u
#define MAX_P 64u
#define MAX_B 64u
#define MAX_E 16384u
#define OLD 8u

typedef uint32_t u32;
typedef uint64_t u64;
typedef uint8_t u8;
typedef struct IrValueId { u32 value; } IrValueId;
typedef struct IrIncoming IrIncoming;
struct IrIncoming { IrValueId value; IrIncoming* next; };
typedef struct IrBlockParameter IrBlockParameter;
struct IrBlockParameter { IrValueId value; IrIncoming* first_incoming; IrBlockParameter* next; };
typedef struct IrBlock { IrBlockParameter* first_parameter; IrBlockParameter* last_parameter; u32 parameter_count; } IrBlock;
typedef struct IrFunction { u32 value_count; u32 block_count; IrBlock* blocks; } IrFunction;
typedef struct IrProgram { u32 unused; } IrProgram;
typedef struct IrPromoteWitness { u32 first; u32 second; } IrPromoteWitness;
typedef struct IrLocalPromotionStatistics
{
    u64 parameter_sweeps, parameter_block_visits, parameter_visits, parameter_incoming_visits, removed_parameters;
} IrLocalPromotionStatistics;
typedef union Scratch { uint64_t align; u8 bytes[8192]; } Scratch;
typedef struct Arena
{
    Scratch scratch;
    size_t used, payload;
    u64 roots, checks, hits, stores, oracle_failures;
    u32 count, final[MAX_V], trace_count, trace[MAX_P][3];
} Arena;
typedef struct InputParameter { u32 value, block, first, count; } InputParameter;
typedef struct Input
{
    u32 values, blocks, parameters, edges;
    u32 aliases[MAX_V], incoming[MAX_E];
    InputParameter p[MAX_P];
} Input;
typedef struct State
{
    Arena arena;
    IrFunction function;
    IrBlock blocks[MAX_B];
    IrBlockParameter parameters[MAX_P];
    IrIncoming incoming[MAX_E];
    IrLocalPromotionStatistics stats;
    u32 aliases[MAX_V];
} State;

static void require(bool ok, char const* message)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static void* probe_allocate(Arena* arena, size_t size, u32 count)
{
    size_t aligned = (arena->used + 7u) & ~(size_t)7u;
    require(size && count <= sizeof(arena->scratch.bytes) / size, "scratch size");
    size_t bytes = size * count;
    require(aligned <= sizeof(arena->scratch.bytes) - bytes, "scratch capacity");
    void* result = arena->scratch.bytes + aligned;
    arena->used = aligned + bytes;
    arena->payload += bytes;
    return result;
}
#define arena_allocate(a, t, n) ((t*)probe_allocate((a), sizeof(t), (n)))
#include "source_root.inc"

static u32 probe_root(Arena* arena, u32* replacements, u32 value)
{
    arena->roots += 1;
    return source_root(replacements, value);
}

// Independent classifier: no path compression, no early conflicting-input exit.
static u32 oracle_root(Arena const* arena, u32 const* replacements, u32 value)
{
    u32 steps = 0;
    require(value < arena->count, "oracle value bound");
    while (replacements[value] != value)
    {
        value = replacements[value];
        steps += 1;
        require(value < arena->count && steps < arena->count, "replacement forest");
    }
    return value;
}

static void probe_decision(Arena* arena, u32 const* replacements, IrBlockParameter const* p,
                           u32 old, bool removed, u32 chosen)
{
    bool seen[MAX_V] = {0};
    u32 distinct = 0, only = IR_PROMOTE_NONE;
    if (p->value.value >= old)
    {
        for (IrIncoming const* in = p->first_incoming; in; in = in->next)
        {
            u32 root = oracle_root(arena, replacements, in->value.value);
            if (root != p->value.value && !seen[root])
            {
                seen[root] = true;
                distinct += 1;
                only = root;
            }
        }
    }
    bool expected = p->value.value >= old && distinct == 1;
    arena->oracle_failures += (u64)(removed != expected || (removed && chosen != only));
}

static void probe_removed(Arena* arena, u32 value, u32 replacement, u64 sweep)
{
    require(arena->trace_count < MAX_P && sweep <= MAX_P + 1u, "trace capacity");
    u32* row = arena->trace[arena->trace_count++];
    row[0] = value; row[1] = replacement; row[2] = (u32)sweep;
}

static void ir_rewrite_compact(Arena* arena, IrProgram* program, IrFunction* function, u32* replacements, u8* removed)
{
    // Only the final root flattening and last-parameter maintenance are modeled.
    for (u32 v = 0; v < function->value_count; v += 1) arena->final[v] = source_root(replacements, v);
    for (u32 b = 0; b < function->block_count; b += 1)
    {
        function->blocks[b].last_parameter = 0;
        for (IrBlockParameter* p = function->blocks[b].first_parameter; p; p = p->next)
            function->blocks[b].last_parameter = p;
    }
    (void)program; (void)removed;
}
#include "baseline.inc"
#include "candidate.inc"
#include "stale.inc"
#include "self.inc"

static void reset_input(Input* in, u32 parameters, u32 blocks)
{
    require(parameters <= MAX_P - 1u && OLD + parameters <= MAX_V && blocks && blocks <= MAX_B, "input dimensions");
    memset(in, 0, sizeof(*in));
    in->values = OLD + parameters;
    in->blocks = blocks;
    for (u32 v = 0; v < in->values; v += 1) in->aliases[v] = v;
}

static void add_parameter(Input* in, u32 value, u32 block, u32 const* values, u32 count)
{
    require(in->parameters < MAX_P && count <= MAX_E - in->edges && value < in->values && block < in->blocks, "parameter bounds");
    InputParameter* p = in->p + in->parameters++;
    *p = (InputParameter){value, block, in->edges, count};
    for (u32 i = 0; i < count; i += 1)
    {
        require(values[i] < in->values, "incoming bound");
        in->incoming[in->edges++] = values[i];
    }
}

static void initialize(State* state, Input const* in)
{
    memset(state, 0, sizeof(*state));
    memset(state->arena.scratch.bytes, 0xa5, sizeof(state->arena.scratch.bytes));
    state->arena.count = in->values;
    state->function = (IrFunction){in->values, in->blocks, state->blocks};
    memcpy(state->aliases, in->aliases, sizeof(state->aliases));
    for (u32 i = 0; i < in->parameters; i += 1)
    {
        InputParameter p = in->p[i];
        IrBlockParameter* parameter = state->parameters + i;
        parameter->value.value = p.value;
        parameter->first_incoming = p.count ? state->incoming + p.first : 0;
        for (u32 e = 0; e < p.count; e += 1)
        {
            state->incoming[p.first + e].value.value = in->incoming[p.first + e];
            state->incoming[p.first + e].next = e + 1u < p.count ? state->incoming + p.first + e + 1u : 0;
        }
        IrBlock* block = state->blocks + p.block;
        if (block->last_parameter) block->last_parameter->next = parameter;
        else block->first_parameter = parameter;
        block->last_parameter = parameter;
        block->parameter_count += 1;
    }
    // This seam also verifies that final mandatory tail maintenance isn't omitted.
    for (u32 b = 0; b < in->blocks; b += 1) state->blocks[b].last_parameter = 0;
}

static u32 position(State const* state, IrBlockParameter const* p)
{
    return p ? (u32)(p - state->parameters) : IR_PROMOTE_NONE;
}

static bool equivalent(State const* a, State const* b, Input const* in)
{
    bool result = a->stats.parameter_sweeps == b->stats.parameter_sweeps &&
        a->stats.parameter_visits == b->stats.parameter_visits &&
        a->stats.parameter_block_visits == b->stats.parameter_block_visits &&
        a->stats.removed_parameters == b->stats.removed_parameters &&
        a->arena.trace_count == b->arena.trace_count;
    for (u32 v = 0; v < in->values; v += 1) result &= a->arena.final[v] == b->arena.final[v];
    for (u32 i = 0; i < a->arena.trace_count && i < b->arena.trace_count; i += 1)
        for (u32 j = 0; j < 3; j += 1) result &= a->arena.trace[i][j] == b->arena.trace[i][j];
    for (u32 i = 0; i < in->parameters; i += 1)
        result &= position(a, a->parameters[i].next) == position(b, b->parameters[i].next);
    for (u32 i = 0; i < in->edges; i += 1)
        result &= a->incoming[i].value.value == in->incoming[i] && b->incoming[i].value.value == in->incoming[i];
    for (u32 i = 0; i < in->blocks; i += 1)
    {
        result &= a->blocks[i].parameter_count == b->blocks[i].parameter_count;
        result &= position(a, a->blocks[i].first_parameter) == position(b, b->blocks[i].first_parameter);
        result &= position(a, a->blocks[i].last_parameter) == position(b, b->blocks[i].last_parameter);
    }
    return result;
}

static State reference_state, candidate_state, broken_state;
static Input input;
static u64 cases;

static void check(Input const* in, char const* name, u32 negative)
{
    initialize(&reference_state, in); initialize(&candidate_state, in);
    baseline_compact(&reference_state.arena, 0, &reference_state.function, OLD, reference_state.aliases, 0, &reference_state.stats);
    candidate_compact(&candidate_state.arena, 0, &candidate_state.function, OLD, candidate_state.aliases, 0, &candidate_state.stats);
    require(!reference_state.arena.oracle_failures && !candidate_state.arena.oracle_failures, "independent decision oracle");
    require(equivalent(&reference_state, &candidate_state, in), "ordered cleanup differential");
    if (negative)
    {
        initialize(&broken_state, in);
        if (negative == 1) stale_compact(&broken_state.arena, 0, &broken_state.function, OLD, broken_state.aliases, 0, &broken_state.stats);
        else self_compact(&broken_state.arena, 0, &broken_state.function, OLD, broken_state.aliases, 0, &broken_state.stats);
        require(broken_state.arena.oracle_failures && !equivalent(&reference_state, &broken_state, in), "negative control must be rejected");
        printf("NEGATIVE %s rejected oracle_failures=%" PRIu64 "\n", name, broken_state.arena.oracle_failures);
    }
    if (name)
    {
        printf("CASE %s sweeps=%" PRIu64 " incoming=%" PRIu64 "/%" PRIu64
               " roots=%" PRIu64 "/%" PRIu64 " checks=%" PRIu64 " hits=%" PRIu64
               " witness_stores=%" PRIu64 " extra_payload=%zu\n", name,
               reference_state.stats.parameter_sweeps, reference_state.stats.parameter_incoming_visits,
               candidate_state.stats.parameter_incoming_visits, reference_state.arena.roots, candidate_state.arena.roots,
               candidate_state.arena.checks, candidate_state.arena.hits, candidate_state.arena.stores,
               candidate_state.arena.payload - reference_state.arena.payload);
    }
    cases += 1;
}

static u32 random_u32(u32* state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void random_cases(u32 seed, u32 count, char const* name)
{
    u32 state = seed, values[96];
    for (u32 c = 0; c < count; c += 1)
    {
        u32 n = 1u + random_u32(&state) % 48u;
        reset_input(&input, n, 1u + random_u32(&state) % MAX_B);
        input.aliases[5] = 6; input.aliases[6] = 7;
        input.aliases[7] = random_u32(&state) % (n + 4u);
        if (input.aliases[7] >= 4u) input.aliases[7] += OLD - 4u;
        for (u32 p = 0; p <= n; p += 1)
        {
            u32 length = random_u32(&state) % 97u;
            for (u32 e = 0; e < length; e += 1) values[e] = random_u32(&state) % input.values;
            add_parameter(&input, p ? OLD + p - 1u : 4u, random_u32(&state) % input.blocks, values, length);
        }
        check(&input, 0, 0);
    }
    printf("RANDOM %s seed=0x%08" PRIx32 " cases=%" PRIu32 " PASS\n", name, seed, count);
}

int main(void)
{
    u32 values[256] = {0};
    reset_input(&input, 0, 3); check(&input, "no-candidates", 0);
    add_parameter(&input, 4, 2, values, 2); check(&input, "preexisting-only", 0);
    reset_input(&input, 1, 1); values[0] = 0; values[1] = 1;
    add_parameter(&input, OLD, 0, values, 2); check(&input, "two-input-no-revisit", 0);
    reset_input(&input, 1, 1); add_parameter(&input, OLD, 0, values, 0); check(&input, "empty", 0);
    values[0] = OLD; values[1] = OLD;
    reset_input(&input, 1, 1); add_parameter(&input, OLD, 0, values, 2); check(&input, "self-only", 0);
    for (u32 negative = 1; negative <= 2; negative += 1)
    {
        reset_input(&input, 2, 2); values[0] = OLD + 1u; values[1] = 0;
        add_parameter(&input, OLD, 0, values, 2);
        values[0] = negative == 1 ? 0 : OLD;
        add_parameter(&input, OLD + 1u, 1, values, 1);
        check(&input, negative == 1 ? "stale-witness" : "witness-becomes-self", negative);
    }
    // Same chain with either a late conflicting root or a cheap early conflict.
    for (u32 wide = 0; wide < 2; wide += 1)
    {
        reset_input(&input, 17, 32);
        u32 length = wide ? 256u : 2u;
        memset(values, 0, sizeof(values)); values[length - 1u] = 1;
        add_parameter(&input, OLD, 0, values, length);
        for (u32 p = 1; p <= 16; p += 1)
        {
            values[0] = 0; values[1] = p == 16 ? 0 : OLD + p + 1u;
            add_parameter(&input, OLD + p, p, values, 2);
        }
        check(&input, wide ? "late-conflict-long-prefix" : "early-conflict-long-chain", 0);
    }
    reset_input(&input, 32, 32); memset(values, 0, sizeof(values));
    for (u32 p = 0; p < 32; p += 1) add_parameter(&input, OLD + p, p, values, 128);
    check(&input, "dense-immediately-trivial", 0);
    // Every length 0..2 incoming sequence over {0,1,p,q}, both visit orders.
    u32 alphabet[] = {0, 1, OLD, OLD + 1u};
    for (u32 order = 0; order < 2; order += 1)
        for (u32 a = 0; a < 21; a += 1)
            for (u32 b = 0; b < 21; b += 1)
            {
                reset_input(&input, 2, 2);
                for (u32 p = 0; p < 2; p += 1)
                {
                    u32 code = p ? b : a;
                    u32 length = code == 0 ? 0 : (code <= 4 ? 1 : 2);
                    u32 payload = code ? code - (length == 1 ? 1u : 5u) : 0;
                    values[0] = alphabet[payload % 4u]; values[1] = alphabet[payload / 4u];
                    add_parameter(&input, OLD + p, p ^ order, values, length);
                }
                check(&input, 0, 0);
            }
    puts("EXHAUSTIVE two-parameter length<=2 cases=882 PASS");
    random_cases(UINT32_C(0x59062307), 5000, "exploration");
    random_cases(UINT32_C(0xc0f1a57e), 20000, "held-out");
    printf("PASS total=%" PRIu64 " negative_controls=2 performance=UNMEASURED canonical_validation=NOT_RUN\n", cases);
    return 0;
}
