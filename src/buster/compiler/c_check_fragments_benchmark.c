// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-parser --expressions (#107): the four complete paths of Salter's experiment (#104), from a unit's
// preprocessed tokens to the checker's results, timed in one process. main.c sends benchmark-parser here when it has
// --expressions, since only the fragments' unit has the sink; without it, benchmark-parser is the parser's own
// benchmark (c_parser_benchmark.c).
//
//   postorder (A)  the scalar parser's tree and postorder, the checker's postorder reader (check_input_expressions)
//   prefix (B)     the scalar parser with the fragment sink, each expression flattened to prefix kinds, the reader
//   fragments (C)  the same sink, the reader straight from the fragments' windows
//   sorted (D)     the vector parser's tree, its postorder by the counting sort, the postorder reader
//
// Each path's run is parse_translation_unit, check_input and check_graph_run (the vector schedule, the default) on one
// file; B and C also make the reader's index and walk the fragments (c_check_fragments.c), with the interleave's vector
// path or its scalar twin (--interleave). The type table and the arenas are set up outside the time, and reused warm
// from run to run, but for the parser's own columns, which parse_translation_unit maps afresh each time as it does in
// the compiler. The files go in batches (--batch, 128): a batch is preprocessed outside the time, then a first pass
// checks that every path gives every file the reference's (A's) graph, token nodes, reports and typed stream, and
// records each path's memory (files the checker doesn't accept are left out and counted), then a warm-up pass and
// --passes interleaved ones (21) run every file through each path, the paths' order rotating with the pass, each run
// timed alone: cycles, instructions and the --counters set's three events (perf, read around each run, its own cost
// subtracted), nanoseconds, and the phases in TSC ticks (the parse, the reader's index, the rest of check_input, its
// expressions' walk, the graph's checking, and with --to-ir the rest to Tier 0's typed streams). A pass's totals are
// its sums over the batches. It reports each path's medians over the passes per parser token, with the fastest and
// slowest pass, and its memory. Run it pinned to one CPU, on the 9700X under its lock.
//
// Layout: the counters; the paths' runs and their comparison; run_fragment_benchmark.

#ifndef BUSTER_C_CHECK_FRAGMENTS_BENCHMARK_INCLUDED
#define BUSTER_C_CHECK_FRAGMENTS_BENCHMARK_INCLUDED

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

#define BUSTER_FRAGMENTS_BENCHMARK_PASSES 21u
#define BUSTER_FRAGMENTS_BENCHMARK_MAX_PASSES 101u
#define BUSTER_FRAGMENTS_BENCHMARK_COUNTERS 5u
#define BUSTER_FRAGMENTS_BENCHMARK_FILES (1u << 16)
#define BUSTER_FRAGMENTS_BENCHMARK_BATCH 128u

// ---------------------------------------------------------------------------------------------------------------------
// The counters

typedef enum CheckFragmentsBenchmarkSet
{
    CHECK_FRAGMENTS_BENCHMARK_PIPELINE,
    CHECK_FRAGMENTS_BENCHMARK_BRANCHES,
    CHECK_FRAGMENTS_BENCHMARK_MEMORY,
    CHECK_FRAGMENTS_BENCHMARK_STALLS,
    CHECK_FRAGMENTS_BENCHMARK_SETS,
} CheckFragmentsBenchmarkSet;

static const char* const check_fragments_benchmark_set_names[CHECK_FRAGMENTS_BENCHMARK_SETS] =
{
    "pipeline",
    "branches",
    "memory",
    "stalls",
};

// Zen 5's raw events (AMD's encoding: the event's low byte, the unit mask in bits 8-15, the event's high nibble in bits
// 32-35): ex_ret_ops, de_no_dispatch_per_slot.no_ops_from_frontend and .backend_stalls; ex_ret_brn, ex_ret_brn_misp and
// ex_ret_brn_tkn; ls_dmnd_fills_from_sys.local_l2, .local_ccx and .dram_io_near; ls_bad_status2.stli_other, the dispatch
// slots the per-core power cap takes (#199) and de_dispatch_stall_cycle_dynamic_tokens_part1.fp_sch_rsrc_stall.
static const uint64_t check_fragments_benchmark_configs[CHECK_FRAGMENTS_BENCHMARK_SETS][3] =
{
    { 0xc1, 0x1000001a0, 0x100001ea0 },
    { 0xc2, 0xc3, 0xc4 },
    { 0x143, 0x243, 0x843 },
    { 0x224, 0x1000008a0, 0x40ae },
};

static const char* const check_fragments_benchmark_event_names[CHECK_FRAGMENTS_BENCHMARK_SETS][3] =
{
    { "ex_ret_ops", "no_ops_from_frontend", "backend_stalls" },
    { "branches", "branch_misses", "taken_branches" },
    { "fills_local_l2", "fills_local_ccx", "fills_dram" },
    { "stli_other", "throttled_slots", "fp_sch_rsrc_stall" },
};

// A perf group of cycles, instructions and the set's three events, read together around each run: `slots` maps each
// counter to its place in the group's read, or is NONE for one perf couldn't open.
typedef struct CheckFragmentsCounters CheckFragmentsCounters;
struct CheckFragmentsCounters
{
    int descriptors[BUSTER_FRAGMENTS_BENCHMARK_COUNTERS];
    uint32_t slots[BUSTER_FRAGMENTS_BENCHMARK_COUNTERS];
    int leader;
    uint32_t opened;
    CheckFragmentsBenchmarkSet set;
    // A read found the group multiplexed: its counts aren't a run's.
    bool multiplexed;
};

static void check_fragments_counters_open(CheckFragmentsCounters* counters, CheckFragmentsBenchmarkSet set)
{
    *counters = (CheckFragmentsCounters){ .set = set, .leader = -1 };
    int leader = -1;
    for (uint32_t c = 0; c < BUSTER_FRAGMENTS_BENCHMARK_COUNTERS; c += 1)
    {
        struct perf_event_attr attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.size = sizeof(attributes);
        attributes.type = c < 2 ? PERF_TYPE_HARDWARE : PERF_TYPE_RAW;
        attributes.config = c == 0 ? PERF_COUNT_HW_CPU_CYCLES : c == 1 ? PERF_COUNT_HW_INSTRUCTIONS : check_fragments_benchmark_configs[set][c - 2];
        attributes.disabled = leader < 0;
        attributes.exclude_kernel = 1;
        attributes.exclude_hv = 1;
        attributes.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
        const int descriptor = (int)syscall(SYS_perf_event_open, &attributes, 0, -1, leader, PERF_FLAG_FD_CLOEXEC);
        counters->descriptors[c] = descriptor;
        counters->slots[c] = descriptor >= 0 ? counters->opened : BUSTER_CHECK_NONE;
        counters->opened += descriptor >= 0;
        leader = leader < 0 ? descriptor : leader;
    }
    counters->leader = leader;
    if (leader >= 0)
    {
        ioctl(leader, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
        ioctl(leader, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }
}

static void check_fragments_counters_close(CheckFragmentsCounters* counters)
{
    for (uint32_t c = 0; c < BUSTER_FRAGMENTS_BENCHMARK_COUNTERS; c += 1)
    {
        if (counters->descriptors[c] >= 0)
        {
            close(counters->descriptors[c]);
        }
    }
}

// The group's counts now, in counter order (0 where a counter didn't open).
static void check_fragments_counters_read(CheckFragmentsCounters* counters, uint64_t* values)
{
    uint64_t buffer[3 + BUSTER_FRAGMENTS_BENCHMARK_COUNTERS] = { 0 };
    const bool read_ok = counters->leader >= 0 && read(counters->leader, buffer, sizeof(buffer)) >= (ssize_t)(3 * sizeof(uint64_t));
    counters->multiplexed |= read_ok && buffer[1] != buffer[2];
    for (uint32_t c = 0; c < BUSTER_FRAGMENTS_BENCHMARK_COUNTERS; c += 1)
    {
        values[c] = read_ok && counters->slots[c] < buffer[0] ? buffer[3 + counters->slots[c]] : 0;
    }
}

static uint64_t check_fragments_benchmark_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static uint64_t check_fragments_benchmark_order(const uint64_t* values, uint32_t count, uint32_t rank)
{
    uint64_t ordered[BUSTER_FRAGMENTS_BENCHMARK_MAX_PASSES];
    for (uint32_t i = 0; i < count; i += 1)
    {
        uint32_t at = i;
        while (at != 0 && ordered[at - 1] > values[i])
        {
            ordered[at] = ordered[at - 1];
            at -= 1;
        }
        ordered[at] = values[i];
    }
    return count != 0 ? ordered[rank < count ? rank : count - 1] : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The paths' runs and their comparison

typedef enum CheckFragmentsBenchmarkPath
{
    CHECK_FRAGMENTS_POSTORDER,
    CHECK_FRAGMENTS_PREFIX,
    CHECK_FRAGMENTS_FRAGMENTS,
    CHECK_FRAGMENTS_SORTED,
    CHECK_FRAGMENTS_PATHS,
} CheckFragmentsBenchmarkPath;

static const char* const check_fragments_benchmark_paths[CHECK_FRAGMENTS_PATHS] =
{
    "postorder",
    "prefix",
    "fragments",
    "sorted",
};

static const char* const check_fragments_benchmark_variants[CHECK_FRAGMENTS_PATHS] = { "A", "B", "C", "D" };

// One run's measurements: the counters, nanoseconds, and the phases in TSC ticks.
typedef struct CheckFragmentsBenchmarkSample CheckFragmentsBenchmarkSample;
struct CheckFragmentsBenchmarkSample
{
    uint64_t counts[BUSTER_FRAGMENTS_BENCHMARK_COUNTERS];
    uint64_t nanoseconds;
    uint64_t parse;
    uint64_t index;
    uint64_t input;
    uint64_t walk;
    uint64_t graph;
    uint64_t ir;
};

#define BUSTER_FRAGMENTS_BENCHMARK_MEASURES (BUSTER_FRAGMENTS_BENCHMARK_COUNTERS + 7)

// The measure m of a sample, in the report's order: the counters, then nanoseconds and the six phases.
static uint64_t check_fragments_benchmark_measure(const CheckFragmentsBenchmarkSample* sample, uint32_t m)
{
    const uint64_t others[7] =
    {
        sample->nanoseconds,
        sample->parse,
        sample->index,
        sample->input,
        sample->walk,
        sample->graph,
        sample->ir,
    };
    return m < BUSTER_FRAGMENTS_BENCHMARK_COUNTERS ? sample->counts[m] : others[m - BUSTER_FRAGMENTS_BENCHMARK_COUNTERS];
}

// A path's memory, summed over the files (bytes) or the largest (peaks): the parser's tree, postorder and sizes, the
// sink's storage (its arena, of which the hot records, the identities used, the per-token syntax and the expressions),
// the reader's index (kept and temporary) and state, the checker's input and scratch arenas, and the deepest nesting of
// cursors and frames; with the sink's statistics.
typedef struct CheckFragmentsBenchmarkMemory CheckFragmentsBenchmarkMemory;
struct CheckFragmentsBenchmarkMemory
{
    uint64_t tree_bytes;
    uint64_t sink_arena_bytes;
    uint64_t sink_hot_bytes;
    uint64_t sink_identity_bytes;
    uint64_t sink_syntax_bytes;
    uint64_t index_bytes;
    uint64_t index_temporary_bytes;
    uint64_t state_bytes;
    uint64_t flat_positions;
    uint64_t input_bytes;
    uint64_t scratch_peak;
    uint64_t expressions;
    uint64_t operators;
    uint64_t raw_operators;
    uint64_t events;
    uint32_t cursor_peak;
    uint32_t frame_peak;
    CFragmentStatistics sink;
};

typedef struct CheckFragmentsBenchmarkFile CheckFragmentsBenchmarkFile;
struct CheckFragmentsBenchmarkFile
{
    const char* path;
    InternTable table;
    Preprocessor pp;
    uint32_t tokens;
    uint32_t expanded;
    bool timed;
};

// A run's storage, reused from run to run.
typedef struct CheckFragmentsBenchmarkSlot CheckFragmentsBenchmarkSlot;
struct CheckFragmentsBenchmarkSlot
{
    Parser parsers[CHECK_FRAGMENTS_PATHS];
    Arena types_arena;
    Arena index_arena;
    Arena input_arena;
    Arena output_arena;
    Arena scratch;
    Arena fragment_arena;
    Arena fragment_temporary;
    TypeTable types;
    CheckInput input;
    CheckStream stream;
    CFragmentRequest request;
    // --to-ir's: the checked program and its lowering to Tier 0's typed streams.
    CheckAsms asms;
    CheckInitializers initializers;
    CheckDataPrepared data;
    CheckProgram program;
    CheckProgramSelection selected;
    CheckProgramLowered lowered;
};

static bool check_fragments_benchmark_slot_open(CheckFragmentsBenchmarkSlot* slot, CLanguage language)
{
    *slot = (CheckFragmentsBenchmarkSlot){ 0 };
    bool valid = true;
    for (uint32_t path = 0; path < CHECK_FRAGMENTS_PATHS; path += 1)
    {
        const ParseOptions options = { .scalar = path != CHECK_FRAGMENTS_SORTED };
        valid = valid && parse_initialize(&slot->parsers[path], &options, language);
    }
    slot->input_arena = arena_reserve(UINT64_C(1) << 36);
    slot->output_arena = arena_reserve(UINT64_C(1) << 36);
    slot->scratch = arena_reserve(UINT64_C(1) << 36);
    slot->fragment_arena = arena_reserve(UINT64_C(1) << 37);
    slot->fragment_temporary = arena_reserve(UINT64_C(1) << 36);
    return valid && slot->input_arena.base != 0 && slot->output_arena.base != 0 && slot->scratch.base != 0 && slot->fragment_arena.base != 0 &&
           slot->fragment_temporary.base != 0;
}

static void check_fragments_benchmark_slot_close(CheckFragmentsBenchmarkSlot* slot)
{
    for (uint32_t path = 0; path < CHECK_FRAGMENTS_PATHS; path += 1)
    {
        parse_release(&slot->parsers[path]);
    }
    arena_release(&slot->fragment_temporary);
    arena_release(&slot->fragment_arena);
    arena_release(&slot->scratch);
    arena_release(&slot->output_arena);
    arena_release(&slot->input_arena);
    arena_release(&slot->index_arena);
    arena_release(&slot->types_arena);
}

typedef struct CheckFragmentsBenchmarkOptions CheckFragmentsBenchmarkOptions;
struct CheckFragmentsBenchmarkOptions
{
    uint32_t payload;
    bool vector;
    // --to-ir: each run goes on to the typed streams, as -c does before Tier 0: the asm operands, the initializers, the
    // static data, the checked program, the reachable definitions' selection and their lowering.
    bool ir;
};

// One path's run on one file in `slot`: set up outside the time (the type table, arenas popped to their starts, the
// request for B and C), then the timed parse, check_input and check_graph_run. Whether the file parsed, the checker
// accepted it with no report, and a fragment reader read all of it.
static bool check_fragments_benchmark_run(CheckFragmentsBenchmarkSlot* slot, CheckFragmentsBenchmarkFile* file, uint32_t path, const CheckFragmentsBenchmarkOptions* options,
                                          CheckFragmentsCounters* counters, CheckFragmentsBenchmarkSample* sample)
{
    Parser* parser = &slot->parsers[path];
    // The parser's tokens once a run has counted them; before, the preprocessor's, of which there are about as many.
    const uint32_t tokens = (file->tokens > file->pp.output.count ? file->tokens : file->pp.output.count) + 64;
    const TypeCapacity capacity =
    {
        .types = tokens < (1u << 27) ? tokens * 4 + 64 : 0,
        .parameters = tokens + 1,
        .tags = tokens + 1,
        .members = tokens + 1,
    };
    if (slot->types_arena.base == 0 || slot->types_arena.capacity < type_capacity_bytes(capacity) || slot->index_arena.capacity < type_index_capacity_bytes(capacity))
    {
        arena_release(&slot->types_arena);
        arena_release(&slot->index_arena);
        slot->types_arena = arena_reserve(type_capacity_bytes(capacity));
        slot->index_arena = arena_reserve(type_index_capacity_bytes(capacity));
    }
    arena_pop_to(&slot->types_arena, 0);
    arena_pop_to(&slot->index_arena, 0);
    arena_pop_to(&slot->input_arena, 0);
    arena_pop_to(&slot->output_arena, 0);
    arena_pop_to(&slot->scratch, 0);
    arena_pop_to(&slot->fragment_arena, 0);
    arena_pop_to(&slot->fragment_temporary, 0);
    bool valid = capacity.types != 0 && type_initialize(&slot->types, &slot->types_arena, &slot->index_arena, capacity);
    const bool fragments = path == CHECK_FRAGMENTS_PREFIX || path == CHECK_FRAGMENTS_FRAGMENTS;
    slot->request = (CFragmentRequest)
    {
        .arena = &slot->fragment_arena,
        .temporary = &slot->fragment_temporary,
        .payload = options->payload,
        .flat = path == CHECK_FRAGMENTS_PREFIX,
        .vector = options->vector,
    };
    check_fragments_ticks = (CheckFragmentTicks){ 0 };
    c_fragment_request = fragments ? &slot->request : 0;
    const CheckOptions check_options = { .scalar = false };
    uint64_t before[BUSTER_FRAGMENTS_BENCHMARK_COUNTERS];
    uint64_t after[BUSTER_FRAGMENTS_BENCHMARK_COUNTERS];
    check_fragments_counters_read(counters, before);
    const uint64_t begin = check_fragments_benchmark_now();
    const uint64_t t0 = __rdtsc();
    const bool parsed = valid && parse_translation_unit(parser, &file->pp);
    const uint64_t t1 = __rdtsc();
    const bool adapted = parsed && check_input(&slot->input, parser, &file->pp, &slot->types, &slot->input_arena, &slot->scratch);
    const uint64_t t2 = __rdtsc();
    const bool checked = adapted && check_graph_run(&slot->types, &slot->input.graph, &slot->output_arena, &slot->scratch, &check_options, &slot->stream);
    const uint64_t t3 = __rdtsc();
    bool lowered = true;
    if (options->ir)
    {
        lowered = checked && slot->stream.report_count == 0 &&
                  check_asms_run(&slot->asms, &slot->input, &slot->stream, &slot->types, &slot->output_arena, &slot->scratch) &&
                  check_initializers_run(&slot->initializers, &slot->input, &slot->stream, parser, &slot->types, &slot->output_arena, &slot->scratch) &&
                  check_data_prepare(&slot->data, &slot->input, &slot->stream, &slot->initializers, &slot->types, &slot->output_arena, &slot->scratch);
        const CheckProgramOptions program_options =
        {
            .initializers = &slot->initializers,
            .asms = &slot->asms,
            .identifiers = file->pp.table,
            .initializer_references = slot->data.references,
            .initializer_reference_count = slot->data.reference_count,
            .scalar = false,
        };
        lowered = lowered && check_program(&slot->program, parser, &slot->input, &slot->stream, &slot->types, program_options, &slot->output_arena, &slot->scratch) &&
                  check_program_select_mode(&slot->selected, &slot->program, &slot->data, C_LOWERING_REACHABLE, false, &slot->output_arena, &slot->scratch) &&
                  check_program_lower(&slot->lowered, &slot->selected.program, false, &slot->output_arena, &slot->scratch);
    }
    const uint64_t t4 = __rdtsc();
    const uint64_t end = check_fragments_benchmark_now();
    check_fragments_counters_read(counters, after);
    c_fragment_request = 0;
    for (uint32_t c = 0; c < BUSTER_FRAGMENTS_BENCHMARK_COUNTERS; c += 1)
    {
        sample->counts[c] = after[c] - before[c];
    }
    sample->nanoseconds = end - begin;
    sample->parse = t1 - t0;
    sample->index = check_fragments_ticks.index;
    sample->walk = check_fragments_ticks.walk;
    sample->input = t2 - t1 - sample->index - sample->walk;
    sample->graph = t3 - t2;
    sample->ir = t4 - t3;
    const CheckFragments* consumer = slot->request.consumer;
    valid = checked && lowered && slot->input.report_count == 0 && slot->stream.report_count == 0 &&
            (!fragments || (slot->request.filled && consumer != 0 && consumer->valid));
    file->tokens = parsed ? parser->tokens.count : file->tokens;
    return valid;
}

// Whether two runs' checker results are the same: the graph, the token nodes, the reports and the typed stream (and with
// --to-ir the lowering's functions and bytes, which only the checker's results decide).
static bool check_fragments_benchmark_same(const CheckFragmentsBenchmarkSlot* left, const CheckFragmentsBenchmarkSlot* right, bool ir)
{
    const CheckInput* a = &left->input;
    const CheckInput* b = &right->input;
    const CheckStream* sa = &left->stream;
    const CheckStream* sb = &right->stream;
    const CheckGraph* x = &a->graph;
    const CheckGraph* y = &b->graph;
    bool same = a->token_count == b->token_count && a->report_count == b->report_count && x->count == y->count && x->argument_count == y->argument_count &&
                sa->count == sb->count && sa->report_count == sb->report_count;
    for (uint32_t token = 0; same && token < a->token_count; token += 1)
    {
        same = a->token_nodes[token] == b->token_nodes[token];
    }
    for (uint32_t node = 0; same && node < x->count; node += 1)
    {
        same = x->ops[node] == y->ops[node] && x->tokens[node] == y->tokens[node] && x->operand0[node] == y->operand0[node] &&
               x->operand1[node] == y->operand1[node] && x->operand2[node] == y->operand2[node] && x->types[node] == y->types[node] &&
               x->members[node] == y->members[node] && x->contexts[node] == y->contexts[node] &&
               x->argument_offsets[node] == y->argument_offsets[node] && x->argument_counts[node] == y->argument_counts[node] &&
               (x->ops[node] != CHECK_OP_VALUE || check_test_value_equal(x->leaves + node, y->leaves + node));
    }
    for (uint32_t argument = 0; same && argument < x->argument_count; argument += 1)
    {
        same = x->argument_nodes[argument] == y->argument_nodes[argument];
    }
    same = same && (!ir || (left->lowered.count == right->lowered.count && left->lowered.output_bytes == right->lowered.output_bytes));
    return same && check_test_stream_equal(sa, sb, x->argument_count);
}

// The memory one run of a path used, added to its totals.
static void check_fragments_benchmark_memory(CheckFragmentsBenchmarkMemory* memory, const CheckFragmentsBenchmarkSlot* slot, uint32_t path)
{
    const Parser* parser = &slot->parsers[path];
    const uint64_t tokens = parser->tokens.count;
    // The tree's roles, parents and depths, and the postorder and the subtree sizes.
    memory->tree_bytes += tokens * (sizeof(uint8_t) + 4 * sizeof(uint32_t));
    memory->input_bytes += slot->input_arena.length;
    memory->scratch_peak = slot->stream.statistics.scratch_bytes > memory->scratch_peak ? slot->stream.statistics.scratch_bytes : memory->scratch_peak;
    const CheckFragments* consumer = slot->request.consumer;
    if ((path == CHECK_FRAGMENTS_PREFIX || path == CHECK_FRAGMENTS_FRAGMENTS) && consumer != 0)
    {
        const CFragmentSink* sink = &slot->request.sink;
        const CFragmentStatistics* statistics = &sink->store.statistics;
        memory->sink_arena_bytes += slot->fragment_arena.length - consumer->statistics.index_bytes - consumer->statistics.state_bytes;
        memory->sink_hot_bytes += (uint64_t)sink->store.allocated * sink->store.stride;
        memory->sink_identity_bytes += statistics->positions * sizeof(uint32_t);
        memory->sink_syntax_bytes += ((uint64_t)sink->token_count + 1) * (sizeof(CFragmentSyntax) + sizeof(uint8_t)) +
                                     (uint64_t)sink->expression_count * sizeof(CFragmentExpression);
        memory->index_bytes += consumer->statistics.index_bytes;
        memory->index_temporary_bytes = consumer->statistics.index_temporary_bytes > memory->index_temporary_bytes ? consumer->statistics.index_temporary_bytes :
                                        memory->index_temporary_bytes;
        memory->state_bytes += consumer->statistics.state_bytes;
        memory->flat_positions += consumer->statistics.flat_positions;
        memory->expressions += consumer->statistics.expressions;
        memory->operators += consumer->statistics.operators;
        memory->raw_operators += consumer->statistics.raw_operators;
        memory->events += consumer->statistics.events;
        memory->cursor_peak = consumer->statistics.cursor_peak > memory->cursor_peak ? consumer->statistics.cursor_peak : memory->cursor_peak;
        memory->frame_peak = consumer->statistics.frame_peak > memory->frame_peak ? consumer->statistics.frame_peak : memory->frame_peak;
        memory->sink.reductions += statistics->reductions;
        memory->sink.copies += statistics->copies;
        memory->sink.copied_bytes += statistics->copied_bytes;
        memory->sink.identity_bytes_copied += statistics->identity_bytes_copied;
        memory->sink.links += statistics->links;
        memory->sink.linked_bytes += statistics->linked_bytes;
        memory->sink.positions += statistics->positions;
        memory->sink.operators += statistics->operators;
        memory->sink.fragments += statistics->fragments;
        memory->sink.peak_live = statistics->peak_live > memory->sink.peak_live ? statistics->peak_live : memory->sink.peak_live;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// run_fragment_benchmark

// Whether benchmark-parser's arguments ask for this benchmark: --expressions.
static bool check_fragments_benchmark_requested(int argc, char** argv)
{
    bool requested = false;
    for (int i = 0; i < argc; i += 1)
    {
        requested |= strcmp(argv[i], "--expressions") == 0;
    }
    return requested;
}

static int run_fragment_benchmark(int argument_count, char** arguments)
{
    char** options_arguments = malloc(((uint64_t)argument_count + 2) * sizeof(char*));
    char** names = malloc((uint64_t)BUSTER_FRAGMENTS_BENCHMARK_FILES * sizeof(char*));
    uint32_t name_count = 0;
    int option_count = 0;
    uint32_t passes = BUSTER_FRAGMENTS_BENCHMARK_PASSES;
    uint32_t batch = BUSTER_FRAGMENTS_BENCHMARK_BATCH;
    CheckFragmentsBenchmarkOptions options = { .payload = 16, .vector = true };
    CheckFragmentsBenchmarkSet set = CHECK_FRAGMENTS_BENCHMARK_PIPELINE;
    bool selected[CHECK_FRAGMENTS_PATHS] = { false, false, false, false };
    bool valid = options_arguments != 0 && names != 0;
    for (int k = 0; valid && k < argument_count; k += 1)
    {
        const char* argument = arguments[k];
        const char* value = k + 1 < argument_count ? arguments[k + 1] : 0;
        if (strcmp(argument, "--expressions") == 0 && value != 0)
        {
            // A comma-separated list of the paths, or all.
            k += 1;
            const bool all = strcmp(value, "all") == 0;
            for (uint32_t path = 0; path < CHECK_FRAGMENTS_PATHS; path += 1)
            {
                const char* name = check_fragments_benchmark_paths[path];
                const size_t length = strlen(name);
                for (const char* at = value; !all && at != 0 && *at != 0; at = strchr(at, ',') != 0 ? strchr(at, ',') + 1 : 0)
                {
                    selected[path] |= strncmp(at, name, length) == 0 && (at[length] == ',' || at[length] == 0);
                }
                selected[path] |= all;
            }
            valid = selected[0] || selected[1] || selected[2] || selected[3];
        }
        else if (strcmp(argument, "--passes") == 0 && value != 0)
        {
            k += 1;
            passes = (uint32_t)strtoul(value, 0, 10);
            valid = passes != 0 && passes <= BUSTER_FRAGMENTS_BENCHMARK_MAX_PASSES;
        }
        else if (strcmp(argument, "--batch") == 0 && value != 0)
        {
            k += 1;
            batch = (uint32_t)strtoul(value, 0, 10);
            valid = batch != 0 && batch <= BUSTER_FRAGMENTS_BENCHMARK_FILES;
        }
        else if (strcmp(argument, "--payload") == 0 && value != 0)
        {
            k += 1;
            options.payload = (uint32_t)strtoul(value, 0, 10);
            valid = options.payload == 16 || options.payload == 32 || options.payload == 48;
        }
        else if (strcmp(argument, "--to-ir") == 0)
        {
            options.ir = true;
        }
        else if (strcmp(argument, "--interleave") == 0 && value != 0)
        {
            k += 1;
            options.vector = strcmp(value, "vector") == 0;
            valid = options.vector || strcmp(value, "scalar") == 0;
        }
        else if (strcmp(argument, "--counters") == 0 && value != 0)
        {
            k += 1;
            set = CHECK_FRAGMENTS_BENCHMARK_SETS;
            for (uint32_t s = 0; s < CHECK_FRAGMENTS_BENCHMARK_SETS; s += 1)
            {
                set = strcmp(value, check_fragments_benchmark_set_names[s]) == 0 ? (CheckFragmentsBenchmarkSet)s : set;
            }
            valid = set < CHECK_FRAGMENTS_BENCHMARK_SETS;
        }
        else if (strcmp(argument, "--files-from") == 0 && value != 0)
        {
            k += 1;
            FILE* list = fopen(value, "r");
            valid = list != 0;
            char line[4096];
            while (valid && fgets(line, sizeof(line), list) != 0)
            {
                const size_t length = strcspn(line, "\r\n");
                line[length] = 0;
                valid = length < sizeof(line) - 1 && name_count < BUSTER_FRAGMENTS_BENCHMARK_FILES;
                if (valid && length != 0)
                {
                    names[name_count] = strdup(line);
                    valid = names[name_count] != 0;
                    name_count += valid;
                }
            }
            if (list != 0)
            {
                fclose(list);
            }
        }
        else if (argument[0] != '-')
        {
            valid = name_count < BUSTER_FRAGMENTS_BENCHMARK_FILES;
            names[name_count] = valid ? strdup(argument) : 0;
            valid = valid && names[name_count] != 0;
            name_count += valid;
        }
        else
        {
            options_arguments[option_count] = arguments[k];
            option_count += 1;
            const bool valued = pp_own_option_with_value(argument) || strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 ||
                                strcmp(argument, "-isystem") == 0 || strcmp(argument, "-include") == 0 || strcmp(argument, "-iquote") == 0 ||
                                strcmp(argument, "-idirafter") == 0 || strcmp(argument, "-x") == 0 || strcmp(argument, "-target") == 0;
            if (valued && value != 0)
            {
                k += 1;
                options_arguments[option_count] = arguments[k];
                option_count += 1;
            }
        }
    }
    valid = valid && name_count != 0 && (selected[0] || selected[1] || selected[2] || selected[3]);
    Arena option_arena = arena_reserve(UINT64_C(1) << 26);
    PPOptions pp_options;
    const char* input;
    const char* output;
    bool markers;
    if (valid)
    {
        options_arguments[option_count] = "placeholder.c";
        valid = pp_parse_options(option_count + 1, options_arguments, &pp_options, &option_arena, &input, &output, &markers);
    }
    // The files go a batch at a time (--batch, 128 by default): each preprocessed once, outside the time, then compared
    // and timed through every pass, then released, since every preprocessor reserves arenas whose address space all of a
    // corpus's at once would exhaust. A pass's totals are its sums over the batches. The preprocessors and parsers need
    // their types' alignment, which an arena gives and calloc doesn't.
    Arena state_arena = arena_reserve(UINT64_C(1) << 36);
    CheckFragmentsBenchmarkFile* files = valid ? BUSTER_ARENA_PUSH_ARRAY(&state_arena, CheckFragmentsBenchmarkFile, batch) : 0;
    CheckFragmentsBenchmarkSlot* slots = valid ? BUSTER_ARENA_PUSH_ARRAY(&state_arena, CheckFragmentsBenchmarkSlot, 2) : 0;
    valid = valid && files != 0 && slots != 0 && check_fragments_benchmark_slot_open(&slots[0], pp_options.language) &&
            check_fragments_benchmark_slot_open(&slots[1], pp_options.language);
    CheckFragmentsCounters counters = { 0 };
    check_fragments_counters_open(&counters, set);
    // The cost of reading the counters around an empty run, to subtract.
    uint64_t overhead[BUSTER_FRAGMENTS_BENCHMARK_COUNTERS] = { 0 };
    if (valid)
    {
        uint64_t empty[BUSTER_FRAGMENTS_BENCHMARK_COUNTERS][BUSTER_FRAGMENTS_BENCHMARK_MAX_PASSES];
        for (uint32_t r = 0; r < BUSTER_FRAGMENTS_BENCHMARK_MAX_PASSES; r += 1)
        {
            uint64_t before[BUSTER_FRAGMENTS_BENCHMARK_COUNTERS];
            uint64_t after[BUSTER_FRAGMENTS_BENCHMARK_COUNTERS];
            check_fragments_counters_read(&counters, before);
            check_fragments_counters_read(&counters, after);
            for (uint32_t c = 0; c < BUSTER_FRAGMENTS_BENCHMARK_COUNTERS; c += 1)
            {
                empty[c][r] = after[c] - before[c];
            }
        }
        for (uint32_t c = 0; c < BUSTER_FRAGMENTS_BENCHMARK_COUNTERS; c += 1)
        {
            overhead[c] = check_fragments_benchmark_order(empty[c], BUSTER_FRAGMENTS_BENCHMARK_MAX_PASSES, BUSTER_FRAGMENTS_BENCHMARK_MAX_PASSES / 2);
        }
    }
    CheckFragmentsBenchmarkMemory memory[CHECK_FRAGMENTS_PATHS] = { 0 };
    static uint64_t totals[CHECK_FRAGMENTS_PATHS][BUSTER_FRAGMENTS_BENCHMARK_MEASURES][BUSTER_FRAGMENTS_BENCHMARK_MAX_PASSES];
    memset(totals, 0, sizeof(totals));
    uint32_t timed_files = 0;
    uint64_t tokens = 0;
    uint64_t expanded = 0;
    uint32_t rejected = 0;
    for (uint32_t first = 0; valid && first < name_count; first += batch)
    {
        const uint32_t count = name_count - first < batch ? name_count - first : batch;
        memset(files, 0, (uint64_t)count * sizeof(CheckFragmentsBenchmarkFile));
        uint32_t prepared = 0;
        for (uint32_t f = 0; valid && f < count; f += 1)
        {
            files[f].path = names[first + f];
            valid = intern_initialize(&files[f].table) && pp_initialize(&files[f].pp, &pp_options, &files[f].table);
            prepared += valid;
            valid = valid && pp_run(&files[f].pp, files[f].path);
            files[f].expanded = valid ? files[f].pp.output.count : 0;
            if (!valid)
            {
                fprintf(stderr, "benchmark-parser --expressions: cannot preprocess %s\n", files[f].path);
            }
        }
        // The comparison: each selected path against the reference (A) on every file, with each path's memory.
        for (uint32_t f = 0; valid && f < count; f += 1)
        {
            CheckFragmentsBenchmarkFile* file = &files[f];
            CheckFragmentsBenchmarkSample sample;
            const bool accepted = check_fragments_benchmark_run(&slots[0], file, CHECK_FRAGMENTS_POSTORDER, &options, &counters, &sample);
            file->timed = accepted;
            rejected += !accepted;
            if (accepted)
            {
                check_fragments_benchmark_memory(&memory[CHECK_FRAGMENTS_POSTORDER], &slots[0], CHECK_FRAGMENTS_POSTORDER);
            }
            for (uint32_t path = 1; accepted && valid && path < CHECK_FRAGMENTS_PATHS; path += 1)
            {
                if (selected[path])
                {
                    const bool ran = check_fragments_benchmark_run(&slots[1], file, path, &options, &counters, &sample);
                    const bool same = ran && check_fragments_benchmark_same(&slots[0], &slots[1], options.ir);
                    check_fragments_benchmark_memory(&memory[path], &slots[1], path);
                    if (!same)
                    {
                        fprintf(stderr, "benchmark-parser --expressions: %s: the %s path %s\n", file->path, check_fragments_benchmark_paths[path],
                                ran ? "gives other results than postorder" : "failed");
                    }
                    valid = same;
                }
            }
            if (accepted)
            {
                timed_files += 1;
                tokens += file->tokens;
                expanded += file->expanded;
            }
        }
        // A warm-up pass, then the timed ones: each path over every file, the paths in an order that rotates by pass.
        for (uint32_t pass = 0; valid && pass <= passes; pass += 1)
        {
            for (uint32_t step = 0; valid && step < CHECK_FRAGMENTS_PATHS; step += 1)
            {
                const uint32_t path = (pass + step) % CHECK_FRAGMENTS_PATHS;
                for (uint32_t f = 0; valid && selected[path] && f < count; f += 1)
                {
                    if (files[f].timed)
                    {
                        CheckFragmentsBenchmarkSample sample;
                        valid = check_fragments_benchmark_run(&slots[0], &files[f], path, &options, &counters, &sample);
                        for (uint32_t c = 0; c < BUSTER_FRAGMENTS_BENCHMARK_COUNTERS; c += 1)
                        {
                            sample.counts[c] -= sample.counts[c] >= overhead[c] ? overhead[c] : sample.counts[c];
                        }
                        for (uint32_t m = 0; pass != 0 && m < BUSTER_FRAGMENTS_BENCHMARK_MEASURES; m += 1)
                        {
                            totals[path][m][pass - 1] += check_fragments_benchmark_measure(&sample, m);
                        }
                    }
                }
            }
        }
        for (uint32_t f = 0; f < prepared; f += 1)
        {
            pp_release(&files[f].pp);
            intern_release(&files[f].table);
        }
    }
    valid = valid && timed_files != 0;
    if (valid)
    {
        printf("benchmark-parser --expressions: files=%u timed=%u rejected=%u tokens=%" PRIu64 " expanded_tokens=%" PRIu64 " passes=%u payload=%u interleave=%s to_ir=%u counters=%s%s\n",
               name_count, timed_files, rejected, tokens, expanded, passes, options.payload, options.vector ? "vector" : "scalar", options.ir, check_fragments_benchmark_set_names[set],
               counters.opened == 0 ? " (unavailable)" : counters.multiplexed ? " (multiplexed: invalid)" : "");
        const char* const measure_names[BUSTER_FRAGMENTS_BENCHMARK_MEASURES] =
        {
            "cycles",
            "instructions",
            check_fragments_benchmark_event_names[set][0],
            check_fragments_benchmark_event_names[set][1],
            check_fragments_benchmark_event_names[set][2],
            "ns",
            "parse_ticks",
            "index_ticks",
            "input_ticks",
            "walk_ticks",
            "graph_ticks",
            "ir_ticks",
        };
        for (uint32_t path = 0; path < CHECK_FRAGMENTS_PATHS; path += 1)
        {
            if (selected[path])
            {
                printf("path=%s variant=%s", check_fragments_benchmark_paths[path], check_fragments_benchmark_variants[path]);
                for (uint32_t m = 0; m < BUSTER_FRAGMENTS_BENCHMARK_MEASURES; m += 1)
                {
                    const uint64_t median = check_fragments_benchmark_order(totals[path][m], passes, passes / 2);
                    const uint64_t fastest = check_fragments_benchmark_order(totals[path][m], passes, 0);
                    const uint64_t slowest = check_fragments_benchmark_order(totals[path][m], passes, passes - 1);
                    printf(" %s/token=%.3f [%.3f %.3f]", measure_names[m], (double)median / (double)tokens, (double)fastest / (double)tokens, (double)slowest / (double)tokens);
                }
                const double cycles = (double)check_fragments_benchmark_order(totals[path][0], passes, passes / 2);
                const double instructions = (double)check_fragments_benchmark_order(totals[path][1], passes, passes / 2);
                printf(" ipc=%.3f\n", cycles != 0 ? instructions / cycles : 0.0);
                const CheckFragmentsBenchmarkMemory* m = &memory[path];
                printf("memory path=%s tree_bytes/token=%.3f input_bytes/token=%.3f scratch_peak=%" PRIu64, check_fragments_benchmark_paths[path],
                       (double)m->tree_bytes / (double)tokens, (double)m->input_bytes / (double)tokens, m->scratch_peak);
                if (path == CHECK_FRAGMENTS_PREFIX || path == CHECK_FRAGMENTS_FRAGMENTS)
                {
                    printf(" sink_arena_bytes/token=%.3f sink_hot_bytes/token=%.3f sink_identity_bytes/token=%.3f sink_syntax_bytes/token=%.3f index_bytes/token=%.3f"
                           " index_temporary_peak=%" PRIu64 " state_bytes/token=%.3f flat_positions/token=%.3f expressions=%" PRIu64 " operators=%" PRIu64 " tree_operators=%" PRIu64
                           " events/token=%.3f cursor_peak=%u frame_peak=%u fragments/expression=%.3f position_occupancy=%.4f operator_occupancy=%.4f"
                           " operator_bytes_copied/reduction=%.3f identity_bytes_copied/reduction=%.3f next_bytes/reduction=%.3f peak_live_records=%u",
                           (double)m->sink_arena_bytes / (double)tokens, (double)m->sink_hot_bytes / (double)tokens, (double)m->sink_identity_bytes / (double)tokens,
                           (double)m->sink_syntax_bytes / (double)tokens, (double)m->index_bytes / (double)tokens, m->index_temporary_bytes, (double)m->state_bytes / (double)tokens,
                           (double)m->flat_positions / (double)tokens, m->expressions, m->operators, m->raw_operators, (double)m->events / (double)tokens, m->cursor_peak, m->frame_peak,
                           m->expressions != 0 ? (double)m->sink.fragments / (double)m->expressions : 0.0,
                           m->sink.fragments != 0 ? (double)m->sink.positions / (64.0 * (double)m->sink.fragments) : 0.0,
                           m->sink.fragments != 0 ? (double)m->sink.operators / ((double)options.payload * (double)m->sink.fragments) : 0.0,
                           m->sink.reductions != 0 ? (double)m->sink.copied_bytes / (double)m->sink.reductions : 0.0,
                           m->sink.reductions != 0 ? (double)m->sink.identity_bytes_copied / (double)m->sink.reductions : 0.0,
                           m->sink.reductions != 0 ? (double)m->sink.linked_bytes / (double)m->sink.reductions : 0.0, m->sink.peak_live);
                }
                printf("\n");
            }
        }
    }
    else
    {
        fprintf(stderr, "usage: benchmark-parser --expressions postorder|prefix|fragments|sorted[,...]|all [--passes N] [--batch N] [--payload 16|32|48]\n"
                        "                        [--interleave vector|scalar] [--to-ir] [--counters pipeline|branches|memory|stalls] [--files-from LIST] [-E's options] file...\n"
                        "(or a file didn't preprocess, no file was accepted, or a path's results differed)\n");
    }
    check_fragments_counters_close(&counters);
    if (slots != 0)
    {
        check_fragments_benchmark_slot_close(&slots[0]);
        check_fragments_benchmark_slot_close(&slots[1]);
    }
    arena_release(&state_arena);
    for (uint32_t f = 0; f < name_count; f += 1)
    {
        free(names[f]);
    }
    free(names);
    free(options_arguments);
    arena_release(&option_arena);
    return valid ? 0 : 1;
}

#endif
