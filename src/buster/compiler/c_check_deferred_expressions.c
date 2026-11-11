// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Post-merge expression construction (#1404). A part contains complete roots of the parser's forest, so no part
// consumes another part's value chain. Token columns remain disjoint; graph columns, value_next, append lists,
// warning sets and arenas are private. Immutable type views permit lookups, never insertion. Parts needing source
// evaluation or type insertion run on the caller after the round. Publication concatenates parts in postorder and
// relocates every retained node reference. No partial result escapes: an error retries the complete source-order reader.
// Included by c_check_deferred.c after its lane dispatch and warning merge helpers.

_Static_assert(sizeof(CheckOp) == sizeof(uint32_t), "expression relocation loads 32-bit opcodes");
_Static_assert(BUSTER_CHECK_EXPRESSION_CODES <= 32, "leaf candidates use one bit per expression class");

#define BUSTER_CHECK_DEFERRED_ROOT_TOKENS 256u
#define BUSTER_CHECK_DEFERRED_EXPRESSION_TOKENS 8192u
#define BUSTER_CHECK_DEFERRED_POST_BODIES 16u

static uint32_t check_deferred_post_group(uint32_t bodies, uint32_t lanes)
{
    const uint32_t paid = bodies / (lanes * 4);
    return paid == 0 ? 1 : paid < BUSTER_CHECK_DEFERRED_POST_BODIES ? paid : BUSTER_CHECK_DEFERRED_POST_BODIES;
}

typedef struct CheckDeferredExpressionPart CheckDeferredExpressionPart;
struct CheckDeferredExpressionPart
{
    CheckGraph graph;
    CheckWarningEvents warnings;
    uint32_t* next;
    uint32_t begin;
    uint32_t end;
    uint32_t first_token;
    uint32_t end_token;
    uint32_t capacity;
    uint32_t offset;
    uint32_t leaf_offset;
    uint32_t leaf_capacity;
    uint32_t base;
    uint32_t lane;
    int64_t constant_warnings;
    uint64_t ticks[BUSTER_CHECK_EXPRESSION_PARTS];
    uint32_t acted[BUSTER_CHECK_EXPRESSION_CODES];
    uint32_t left[BUSTER_CHECK_EXPRESSION_CODES];
    bool parallel;
    bool valid;
};

typedef struct CheckDeferredExpressions CheckDeferredExpressions;
struct CheckDeferredExpressions
{
    CheckDeferred* deferred;
    CheckInputReader* reader;
    CheckDeferredExpressionPart* parts;
    uint32_t* order;
    // One result per constant-context group, published after the worker round joins.
    uint8_t* context_valid;
    uint32_t count;
    uint32_t group_size;
};

typedef struct CheckDeferredControlPart CheckDeferredControlPart;
struct CheckDeferredControlPart
{
    uint32_t* fallthroughs;
    uint32_t fallthrough_count;
    uint32_t label_addresses;
    uint32_t indirect_gotos;
    bool valid;
};

typedef struct CheckDeferredControls CheckDeferredControls;
struct CheckDeferredControls
{
    CheckDeferred* deferred;
    CheckInputReader* reader;
    CheckDeferredControlPart* parts;
    uint32_t group_size;
    uint32_t groups;
};

static void check_deferred_control_lane(void* argument)
{
    CheckDeferredControls* run = argument;
    CheckDeferred* deferred = run->deferred;
    CheckDeferredArenas* arenas = deferred->memory.lanes + lane_index();
    const uint32_t bodies = deferred->body_count;
    const uint32_t groups = run->groups;
    const uint32_t group_size = run->group_size;
    for (uint64_t g = lane_take(groups); g < groups; g = lane_take(groups))
    {
        const uint64_t mark = arenas->scratch.length;
        CheckInput input = *run->reader->output;
        CheckInputReader reader = *run->reader;
        CheckInputReport report;
        reader.output = &input;
        reader.arena = &arenas->results;
        reader.scratch = &arenas->scratch;
        reader.fallthrough_tokens = NULL;
        reader.fallthrough_count = 0;
        reader.fallthrough_capacity = 0;
        reader.indirect_gotos = 0;
        input.reports = &report;
        input.report_count = 0;
        input.report_capacity = 1;
        input.label_address_count = 0;
        const uint32_t b = (uint32_t)g * group_size;
        const uint32_t end = bodies - b < group_size ? bodies : b + group_size;
        check_input_controls_range(&reader, BUSTER_CHECK_NONE, deferred->cuts[b].opening, deferred->cuts[end - 1].closing + 1);
        run->parts[g] = (CheckDeferredControlPart)
        {
            .fallthroughs = reader.fallthrough_tokens,
            .fallthrough_count = reader.fallthrough_count,
            .label_addresses = input.label_address_count,
            .indirect_gotos = reader.indirect_gotos,
            .valid = !input.resource && input.report_count == 0,
        };
        arena_pop_to(&arenas->scratch, mark);
    }
}

// Label and jump ownership is function-local. Private label tables and fallthrough suffixes are merged in source
// order. The complete-unit indirect-jump check waits for every label target; errors use full diagnostic recovery.
static bool check_deferred_controls(CheckDeferred* deferred, CheckInputReader* reader)
{
    const uint32_t lanes = check_deferred_post_lanes(deferred);
    const bool used = !deferred->unsupported && lanes > 1 && reader->output->report_count == 0 && !reader->output->resource && reader->fallthrough_count == 0;
    if (used)
    {
        const uint64_t mark = reader->scratch->length;
        const uint32_t bodies = deferred->body_count;
        const uint32_t group_size = check_deferred_post_group(bodies, lanes);
        const uint32_t groups = (bodies + group_size - 1) / group_size;
        CheckDeferredControlPart* parts = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckDeferredControlPart, groups);
        CheckDeferredControls run = { .deferred = deferred, .reader = reader, .parts = parts, .group_size = group_size, .groups = groups };
        bool valid = parts != NULL && check_deferred_lanes_run(deferred, BUSTER_CHECK_DEFERRED_ROUND_CONTROLS, lanes, check_deferred_control_lane, &run);
        uint32_t begin = 0;
        uint64_t fallthroughs = reader->fallthrough_count;
        for (uint32_t g = 0; valid && g < groups; g += 1)
        {
            const uint32_t b = g * group_size;
            const uint32_t end = bodies - b < group_size ? bodies : b + group_size;
            check_input_controls_range(reader, BUSTER_CHECK_NONE, begin, deferred->cuts[b].opening);
            begin = deferred->cuts[end - 1].closing + 1;
            valid &= parts[g].valid;
            fallthroughs += parts[g].fallthrough_count;
            reader->output->label_address_count += parts[g].label_addresses;
            reader->indirect_gotos += parts[g].indirect_gotos;
        }
        if (valid)
        {
            check_input_controls_range(reader, BUSTER_CHECK_NONE, begin, reader->parser->tokens.count);
            valid = !reader->output->resource && reader->output->report_count == 0 && fallthroughs <= UINT32_MAX;
        }
        uint32_t* tokens = valid && fallthroughs != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, fallthroughs) : NULL;
        valid &= fallthroughs == 0 || tokens != NULL;
        if (valid && fallthroughs != 0)
        {
            // No valid fallthrough attribute occurs outside a function, so the ordered gaps above append none.
            uint32_t at = 0;
            for (uint32_t b = 0; b < groups; b += 1)
            {
                if (parts[b].fallthrough_count != 0)
                {
                    memcpy(tokens + at, parts[b].fallthroughs, (uint64_t)parts[b].fallthrough_count * sizeof(uint32_t));
                }
                at += parts[b].fallthrough_count;
            }
            reader->fallthrough_tokens = tokens;
            reader->fallthrough_count = at;
            reader->fallthrough_capacity = (uint32_t)fallthroughs;
        }
        if (valid && reader->indirect_gotos != 0)
        {
            check_input_indirect_gotos(reader);
            valid = !reader->output->resource && reader->output->report_count == 0;
        }
        deferred->expression_retry |= !valid;
        reader->output->resource |= !valid;
        arena_pop_to(reader->scratch, mark);
    }
    return used;
}

typedef struct CheckDeferredBindings CheckDeferredBindings;
struct CheckDeferredBindings
{
    CheckDeferred* deferred;
    CheckInputReader* reader;
    const uint32_t* resolved;
    uint8_t* valid;
    uint32_t groups;
    uint32_t group_size;
};

static void check_deferred_binding_lane(void* argument)
{
    CheckDeferredBindings* run = argument;
    const uint32_t groups = run->groups;
    const uint32_t bodies = run->deferred->body_count;
    const uint32_t group_size = run->group_size;
    for (uint64_t g = lane_take(groups); g < groups; g = lane_take(groups))
    {
        CheckInput input = *run->reader->output;
        CheckInputReader reader = *run->reader;
        CheckInputReport report;
        reader.output = &input;
        input.reports = &report;
        input.report_capacity = 1;
        input.report_count = 0;
        const uint32_t first = (uint32_t)g * group_size;
        const uint32_t end = bodies - first < group_size ? bodies : first + group_size;
        for (uint32_t b = first; b < end && !input.resource && input.report_count == 0; b += 1)
        {
            const CheckDeferredResult* body = run->deferred->results + b;
            for (uint32_t e = body->event_begin; e < body->event_begin + body->event_count && !input.resource && input.report_count == 0; e += 1)
            {
                check_input_binding_answer(&reader, run->resolved, e);
            }
        }
        run->valid[g] = !input.resource && input.report_count == 0;
    }
}

// The merge retains whole-body event slices. Verify that every use writes its own body's tokens: a query that
// revisits an earlier source span retains the serial path. Canonical declaration types are immutable. The global
// unused-declaration analysis still runs after all answers, since an external declaration can be used by any body.
static bool check_deferred_binding_answers(CheckDeferred* deferred, CheckInputReader* reader, const uint32_t* resolved)
{
    const uint32_t lanes = check_deferred_post_lanes(deferred);
    bool used = !deferred->unsupported && lanes > 1 && reader->output->report_count == 0 && !reader->output->resource;
    const CBindings* bindings = &reader->bindings;
    for (uint32_t b = 0; used && b < deferred->body_count; b += 1)
    {
        const CheckDeferredResult* body = deferred->results + b;
        const CheckDeferredCut* cut = deferred->cuts + b;
        for (uint32_t e = body->event_begin; used && e < body->event_begin + body->event_count; e += 1)
        {
            const uint32_t token = bindings->declaration[e];
            used = bindings->operation[e] != C_BINDING_USE || (token >= cut->opening && token <= cut->closing);
        }
    }
    if (used)
    {
        // No typo is corrected here, nor on the lanes, which share the reader's state (#1447): a unit with an undeclared
        // name fails the lanes and is read again in source order (check_input_deferred_run), which corrects it.
        const bool typo_disabled = reader->typo_disabled;
        reader->typo_disabled = true;
        const uint64_t mark = reader->scratch->length;
        const uint32_t group_size = check_deferred_post_group(deferred->body_count, lanes);
        const uint32_t groups = (deferred->body_count + group_size - 1) / group_size;
        uint8_t* passed = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, groups);
        bool valid = passed != NULL;
        if (valid)
        {
            memset(passed, 1, groups);
            uint32_t begin = 0;
            for (uint32_t b = 0; b < deferred->body_count; b += 1)
            {
                const CheckDeferredResult* body = deferred->results + b;
                for (uint32_t e = begin; e < body->event_begin; e += 1)
                {
                    check_input_binding_answer(reader, resolved, e);
                }
                begin = body->event_begin + body->event_count;
            }
            for (uint32_t e = begin; e < bindings->count; e += 1)
            {
                check_input_binding_answer(reader, resolved, e);
            }
            CheckDeferredBindings run = { .deferred = deferred, .reader = reader, .resolved = resolved, .valid = passed, .groups = groups, .group_size = group_size };
            valid = !reader->output->resource && reader->output->report_count == 0 &&
                    check_deferred_lanes_run(deferred, BUSTER_CHECK_DEFERRED_ROUND_BINDINGS, lanes, check_deferred_binding_lane, &run);
            for (uint32_t g = 0; valid && g < groups; g += 1)
            {
                valid &= passed[g] != 0;
            }
        }
        deferred->expression_retry |= !valid;
        reader->output->resource |= !valid;
        arena_pop_to(reader->scratch, mark);
        reader->typo_disabled = typo_disabled;
    }
    return used;
}

// A context writes only its initializer/case/assertion's span, wholly inside the same body's token interval. Worker
// warning suppressions touch only those token bytes; the shared count and interval index are restored after the join.
// A static assertion's message also spells strings into scratch (#1761): each worker must use its own lane's arena,
// and report resource failures only after the join, never through the shared reader.
static void check_deferred_context_lane(void* argument)
{
    CheckDeferredExpressions* run = argument;
    CheckDeferredArenas* arenas = run->deferred->memory.lanes + lane_index();
    const uint32_t bodies = run->deferred->body_count;
    const uint32_t group_size = run->group_size;
    const uint32_t groups = run->count;
    for (uint64_t g = lane_take(groups); g < groups; g = lane_take(groups))
    {
        const uint64_t mark = arenas->scratch.length;
        CheckInput input = *run->reader->output;
        CheckInputReader reader = *run->reader;
        CheckInputReport report;
        reader.output = &input;
        reader.scratch = &arenas->scratch;
        input.reports = &report;
        input.report_count = 0;
        input.report_capacity = 1;
        const uint32_t b = (uint32_t)g * group_size;
        const uint32_t end = bodies - b < group_size ? bodies : b + group_size;
        check_input_constant_contexts_range(&reader, run->deferred->cuts[b].opening, run->deferred->cuts[end - 1].closing + 1);
        run->context_valid[g] = !input.resource && input.report_count == 0;
        arena_pop_to(&arenas->scratch, mark);
    }
}

static bool check_deferred_constant_contexts(CheckDeferred* deferred, CheckInputReader* reader)
{
    const uint32_t lanes = check_deferred_post_lanes(deferred);
    bool used = !deferred->unsupported && lanes > 1 && reader->output->report_count == 0 && !reader->output->resource;
    if (used)
    {
        const uint64_t mark = reader->scratch->length;
        const uint32_t group_size = check_deferred_post_group(deferred->body_count, lanes);
        const uint32_t groups = (deferred->body_count + group_size - 1) / group_size;
        uint8_t* passed = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, groups);
        CheckDeferredExpressions run = { .deferred = deferred, .reader = reader, .context_valid = passed, .group_size = group_size, .count = groups };
        reader->parallel_warning_ranges = true;
        used = passed != NULL && check_deferred_lanes_run(deferred, BUSTER_CHECK_DEFERRED_ROUND_CONTEXTS, lanes, check_deferred_context_lane, &run);
        reader->parallel_warning_ranges = false;
        bool valid = used;
        for (uint32_t g = 0; valid && g < groups; g += 1)
        {
            valid &= passed[g] != 0;
        }
        deferred->expression_retry |= used && !valid;
        reader->output->resource |= used && !valid;
        if (valid)
        {
            // No per-token worker writes were counted, and the index may summarize the pre-round warning bits.
            // Rebuild once with the existing SIMD/scalar byte counter, before the ordered top-level gaps run.
            reader->output->constant_warning_count = reader->output->constant_warnings != NULL ?
                check_deferred_count_bytes(reader->output->constant_warnings, 0, reader->parser->tokens.count,
                                           !BUSTER_SCALAR_PATH(reader->parser->options.scalar)) : 0;
            if (reader->warning_index_ready)
            {
                memset(reader->warning_index, 0xff, (uint64_t)reader->warning_index_base * 2 * sizeof(uint16_t));
            }
            uint32_t begin = 0;
            const uint32_t bodies = deferred->body_count;
            for (uint32_t b = 0; b < bodies; b += group_size)
            {
                const uint32_t end = bodies - b < group_size ? bodies : b + group_size;
                check_input_constant_contexts_range(reader, begin, deferred->cuts[b].opening);
                begin = deferred->cuts[end - 1].closing + 1;
            }
            check_input_constant_contexts_range(reader, begin, reader->parser->tokens.count);
            if (reader->warning_index_ready)
            {
                memset(reader->warning_index, 0xff, (uint64_t)reader->warning_index_base * 2 * sizeof(uint16_t));
            }
        }
        arena_pop_to(reader->scratch, mark);
    }
    return used;
}

// Only the coded actions and the explicitly audited scalar actions below may run against shared token columns.
// In particular, evaluators, function-name caches, VLA capture and region proofs retain ordered execution.
static bool check_deferred_expression_parallel(const CheckInputReader* reader, uint32_t token)
{
    const uint8_t visit = reader->visits[token];
    const uint32_t code = (visit & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    bool valid = true;
    if ((visit & BUSTER_CHECK_EXPRESSION_ACTS) != 0 && code != CHECK_INPUT_CODE_DROP)
    {
        const Parser* parser = reader->parser;
        const uint8_t role = parser->tree.roles[token];
        const uint8_t kind = parser->tokens.kinds[token];
        const uint16_t builtin = reader->builtins[token];
        const bool builtin_safe = builtin == 0 || builtin < CBUILTIN_COUNT || builtin == CBUILTIN_UNREACHABLE ||
                                  (builtin == CBUILTIN_EXPECT && reader->constant_contexts[token] == 0) || builtin == CBUILTIN_EXPECT_WITH_PROBABILITY;
        valid = reader->output->kr_parameter_count == 0 && reader->output->expression_regions[token] == 0 &&
                builtin_safe && (reader->output->intrinsic_ids == NULL || reader->output->intrinsic_ids[token] == 0) &&
                reader->vla_ends[token] == 0 && reader->vararg_calls[token] == 0 && reader->specifier_finishes[token] == NULL;
        if (valid && (reader->syntax[token] & BUSTER_CHECK_SYNTAX_STRING) != 0 && role == PARSE_ROLE_OPERAND)
        {
            valid = check_sparse_get(reader->string_ids, token) != BUSTER_CHECK_NONE;
        }
        if (valid && role == PARSE_ROLE_PREFIX)
        {
            valid = kind != PARSE_KIND_AMPAMP;
            if (kind == PARSE_KIND_SIZEOF)
            {
                const TypeId type = reader->query_types[token] != 0 ? reader->query_types[token] : reader->sizeof_types[token];
                valid = parser->tokens.names[token] == C_NAME_SIZEOF && reader->sizeof_types[token] != 0 &&
                        type != 0 && !check_input_vla_runtime_size(reader->types, type);
            }
        }
        if (valid && role == PARSE_ROLE_RETURN && kind != PARSE_KIND_RETURN)
        {
            valid = false;
        }
        if (valid && code == CHECK_INPUT_CODE_SCALAR && !reader->ignored[token])
        {
            valid = builtin != 0 || reader->constant_present[token] != 0 || reader->selected_tokens[token] < parser->tokens.count ||
                    reader->initializers[token] != 0 || role == PARSE_ROLE_CLOSE_INITIALIZER || role == PARSE_ROLE_CLOSE_COMPOUND_BODY ||
                    role == PARSE_ROLE_CLOSE_SIZEOF || role == PARSE_ROLE_RANGE || role == PARSE_ROLE_CLOSE_CASE || role == PARSE_ROLE_CLOSE_KEYWORD ||
                    ((reader->syntax[token] & BUSTER_CHECK_SYNTAX_STRING) != 0 && role == PARSE_ROLE_OPERAND);
        }
    }
    return valid;
}

// Allocate just this part's bounded graph and side lists, retaining the merged graph's immutable source facts.
static bool check_deferred_expression_graph(CheckDeferredExpressionPart* part, const CheckGraph* source, uint32_t* next, Arena* arena)
{
    CheckGraph* graph = &part->graph;
    const uint32_t tokens = part->end - part->begin;
    bool valid = part->offset <= source->capacity && part->capacity <= source->capacity - part->offset;
    if (valid)
    {
        *graph = *source;
        graph->count = 0;
        graph->leaf_count = 0;
        graph->capacity = part->capacity;
        graph->argument_count = 0;
        graph->argument_capacity = tokens;
        graph->recorded_nodes = 0;
        graph->recorded_ready = false;
        graph->recorded = (CheckGraphValidation){ 0 };
        graph->const_warning_count = 0;
        graph->const_warnings = NULL;
#define BUSTER_CHECK_EXPRESSION_SLICE(field) graph->field = source->field != NULL ? source->field + part->offset : NULL
        BUSTER_CHECK_EXPRESSION_SLICE(ops);
        BUSTER_CHECK_EXPRESSION_SLICE(tokens);
        BUSTER_CHECK_EXPRESSION_SLICE(operand0);
        BUSTER_CHECK_EXPRESSION_SLICE(operand1);
        BUSTER_CHECK_EXPRESSION_SLICE(operand2);
        BUSTER_CHECK_EXPRESSION_SLICE(leaf_slots);
        BUSTER_CHECK_EXPRESSION_SLICE(types);
        BUSTER_CHECK_EXPRESSION_SLICE(members);
        BUSTER_CHECK_EXPRESSION_SLICE(contexts);
        BUSTER_CHECK_EXPRESSION_SLICE(source_positions);
        BUSTER_CHECK_EXPRESSION_SLICE(argument_offsets);
        BUSTER_CHECK_EXPRESSION_SLICE(argument_counts);
        BUSTER_CHECK_EXPRESSION_SLICE(recorded_pure);
        BUSTER_CHECK_EXPRESSION_SLICE(warning_suppressed);
        BUSTER_CHECK_EXPRESSION_SLICE(fp_environments);
#undef BUSTER_CHECK_EXPRESSION_SLICE
        // In the closed parallel action set, only identifier/literal/string and scalar-class tokens make VALUEs,
        // at most one each. Label addresses, VM sizes, region proofs and constant-context expect stay ordered.
        // Their compact leaf reservation avoids faulting a leaf at every node's worst-case token slot. Its leaves' rows
        // take the same range of the graph's rows, as a leaf takes one at most (#628), numbered from the part's first.
        graph->leaf_records = source->leaf_records != NULL ? source->leaf_records + part->leaf_offset : NULL;
        graph->leaf_payloads = source->leaf_payloads != NULL ? source->leaf_payloads + part->leaf_offset : NULL;
        graph->dense_leaves = source->dense_leaves != NULL ? source->dense_leaves + part->leaf_offset : NULL;
        graph->leaf_payload_count = 0;
        graph->leaf_payload_capacity = part->leaf_capacity;
        // Arguments use postorder-sized slots; node slots include the producer's VLA/sizeof headroom.
        graph->argument_nodes = source->argument_nodes + part->begin;
        graph->argument_tokens = source->argument_tokens + part->begin;
        part->next = next + part->offset;
#define BUSTER_CHECK_EXPRESSION_LIST(field, counter, Type) \
        graph->counter##_count = 0; \
        graph->counter##_capacity = source->counter##_capacity < tokens ? source->counter##_capacity : tokens; \
        graph->field = graph->counter##_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, Type, graph->counter##_capacity) : NULL; \
        valid &= graph->counter##_capacity == 0 || graph->field != NULL
        BUSTER_CHECK_EXPRESSION_LIST(builtin_calls, builtin, CheckBuiltinCall);
        BUSTER_CHECK_EXPRESSION_LIST(vector_members, vector_member, CheckVectorCall);
#undef BUSTER_CHECK_EXPRESSION_LIST
        graph->varargs_calls = NULL;
        graph->varargs_count = 0;
        graph->varargs_capacity = 0;
        graph->x86_calls = NULL;
        graph->x86_count = 0;
        graph->x86_capacity = 0;
        graph->overload_calls = NULL;
        graph->overload_count = 0;
        graph->overload_capacity = 0;
        graph->region_proofs = NULL;
        graph->region_proof_count = 0;
        graph->region_proof_capacity = 0;
        graph->region_steps = NULL;
        graph->region_step_count = 0;
        graph->region_step_capacity = 0;
    }
    return valid;
}

// Each lane owns one arena and takes complete forest parts. The type descriptor is private; capacities at the current
// counts prohibit writes through its borrowed columns, including an index growth on a missing derived type.
static void check_deferred_expression_lane(void* argument)
{
    CheckDeferredExpressions* run = argument;
    const CheckInputReader* top = run->reader;
    const uint32_t lane = lane_index();
    CheckDeferredArenas* arenas = &run->deferred->memory.lanes[lane];
    const uint32_t count = run->count;
    const uint32_t* order = run->order;
    CheckDeferredExpressionPart* parts = run->parts;
    uint16_t* warning_index = NULL;
    while (run->deferred->late_first && lane == 0 && __atomic_load_n(&run->deferred->others_took, __ATOMIC_ACQUIRE) + 1 < lane_count())
    {
        sched_yield();
    }
    uint64_t taken = lane == 0 ? 0 : 1 + lane_take(count - 1);
    if (run->deferred->late_first && lane != 0)
    {
        __atomic_fetch_add(&run->deferred->others_took, 1, __ATOMIC_RELEASE);
    }
    for (; taken < count; taken = 1 + lane_take(count - 1))
    {
        CheckDeferredExpressionPart* part = parts + order[taken];
        if (part->parallel)
        {
            const uint64_t scratch_mark = arenas->scratch.length;
            CheckInput input = *top->output;
            CheckInputReader reader = *top;
            TypeTable types = *top->types;
            types.types.capacity = types.types.count;
            types.parameters.capacity = types.parameters.count;
            types.changes = NULL;
            reader.output = &input;
            reader.types = &types;
            reader.arena = &arenas->results;
            reader.scratch = &arenas->scratch;
            reader.warning_query_arena = &arenas->results;
            reader.warning_events = (CheckWarningBuffer){ .arena = reader.arena };
            reader.source_warning_keys.arena = reader.arena;
            reader.source_warning_keys.borrowed = true;
            reader.literal_warnings.arena = reader.arena;
            reader.literal_warnings.borrowed = true;
            // Token warning bytes are disjoint, but their interval summaries have shared ancestors. Reuse one
            // private reservation per lane, conservatively reopened for each forest part that lane takes.
            if (warning_index == NULL)
            {
                warning_index = BUSTER_ARENA_PUSH_ARRAY(reader.arena, uint16_t, (uint64_t)reader.warning_index_base * 2);
            }
            reader.warning_index = warning_index;
            reader.warning_index_ready = false;
            reader.const_warning_capacity = 0;
            input.warning_events = (CheckWarningEvents){ 0 };
            input.reports = BUSTER_ARENA_PUSH_ARRAY(reader.arena, CheckInputReport, 1);
            input.report_count = 0;
            input.report_capacity = 1;
            memset(input.expression_left, 0, sizeof(input.expression_left));
            part->valid = input.reports != NULL && warning_index != NULL && check_deferred_expression_graph(part, &top->output->graph, top->value_next, reader.arena);
            if (part->valid)
            {
                input.graph = part->graph;
                input.graph.const_values = &input.const_values;
                reader.value_next = part->next;
                // The part's nodes are its own, numbered apart from the top level's hops (CheckInputReader.walk_ends).
                reader.walk_ends = NULL;
                reader.walk_counts = NULL;
                check_input_expressions_coded_range(&reader, part->begin, part->end);
                if (!input.resource)
                {
                    check_graph_record_finish(&input.graph);
                }
                part->graph = input.graph;
                memcpy(part->ticks, input.expression_part_ticks, sizeof(part->ticks));
                memcpy(part->acted, input.expression_acted, sizeof(part->acted));
                memcpy(part->left, input.expression_left, sizeof(part->left));
                part->warnings = check_warning_view(&reader.warning_events);
                part->constant_warnings = (int64_t)input.constant_warning_count - (int64_t)top->output->constant_warning_count;
                part->valid = !input.resource && input.report_count == 0 && input.graph.leaf_count <= part->leaf_capacity;
                part->lane = lane;
            }
            arena_pop_to(&arenas->scratch, scratch_mark);
        }
    }
}

// The part `from`'s leaves appended to `to`'s, which has `leaves` of them: its records, and its rows after `to`'s rows, its
// records' rows renumbered after them. Both move toward the graph's start, from the part's reserved range (or stay).
// Renumbering visits the part's leaves only where both graphs have rows, which few units do (on sqlite3.c, 80 leaves of
// 144,720 have one), so it is a scalar loop.
static void check_deferred_expression_leaves(CheckGraph* to, const CheckGraph* from, uint32_t leaves)
{
    if (to->dense_leaves != NULL)
    {
        memmove(to->dense_leaves + leaves, from->dense_leaves, (uint64_t)from->leaf_count * sizeof(CheckValue));
    }
    else
    {
        const uint32_t rows = to->leaf_payload_count;
        memmove(to->leaf_payloads + rows, from->leaf_payloads, (uint64_t)from->leaf_payload_count * sizeof(CheckValue));
        memmove(to->leaf_records + leaves, from->leaf_records, (uint64_t)from->leaf_count * sizeof(CheckGraphLeaf));
        for (uint32_t leaf = 0; rows != 0 && from->leaf_payload_count != 0 && leaf < from->leaf_count; leaf += 1)
        {
            CheckGraphLeaf* record = to->leaf_records + leaves + leaf;
            record->row += record->row != 0 ? rows : 0;
        }
        to->leaf_payload_count = rows + from->leaf_payload_count;
    }
}

static uint32_t check_deferred_expression_node(uint32_t node, uint32_t base)
{
    return node == BUSTER_CHECK_NONE ? node : node + base;
}

// Source-order publication. The hot columns are copied in contiguous slices; only node-bearing fields are relocated.
// None of the local graph's allocation capacities or scheduling choices enters the published representation.
static bool check_deferred_expression_append(CheckInputReader* reader, const CheckDeferredExpressionPart* part, bool vector)
{
    CheckGraph* to = &reader->output->graph;
    const CheckGraph* from = &part->graph;
    const uint32_t base = to->count;
    const uint32_t leaves = to->leaf_count;
    bool valid = from->count <= to->capacity - base && from->argument_count <= to->argument_capacity - to->argument_count;
#define BUSTER_CHECK_EXPRESSION_ROOM(counter) valid &= from->counter##_count <= to->counter##_capacity - to->counter##_count
    BUSTER_CHECK_EXPRESSION_ROOM(varargs);
    BUSTER_CHECK_EXPRESSION_ROOM(builtin);
    BUSTER_CHECK_EXPRESSION_ROOM(vector_member);
    BUSTER_CHECK_EXPRESSION_ROOM(x86);
    BUSTER_CHECK_EXPRESSION_ROOM(overload);
    BUSTER_CHECK_EXPRESSION_ROOM(region_proof);
    BUSTER_CHECK_EXPRESSION_ROOM(region_step);
#undef BUSTER_CHECK_EXPRESSION_ROOM
    if (valid)
    {
#define BUSTER_CHECK_EXPRESSION_COPY(field) memmove(to->field + base, from->field, (uint64_t)from->count * sizeof(*from->field))
        BUSTER_CHECK_EXPRESSION_COPY(ops);
        BUSTER_CHECK_EXPRESSION_COPY(tokens);
        BUSTER_CHECK_EXPRESSION_COPY(types);
        BUSTER_CHECK_EXPRESSION_COPY(members);
        BUSTER_CHECK_EXPRESSION_COPY(contexts);
        BUSTER_CHECK_EXPRESSION_COPY(source_positions);
        BUSTER_CHECK_EXPRESSION_COPY(warning_suppressed);
        BUSTER_CHECK_EXPRESSION_COPY(recorded_pure);
        if (to->fp_environments != NULL)
        {
            BUSTER_CHECK_EXPRESSION_COPY(fp_environments);
        }
#undef BUSTER_CHECK_EXPRESSION_COPY
        check_deferred_expression_leaves(to, from, leaves);
        uint32_t n = 0;
        for (; !BUSTER_SCALAR_PATH(!vector) && n < from->count; n += 16)
        {
            const uint32_t length = from->count - n < 16 ? from->count - n : 16;
            const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, length);
            const __m512i bias = _mm512_set1_epi32((int32_t)base);
            const __m512i none = _mm512_set1_epi32((int32_t)BUSTER_CHECK_NONE);
#define BUSTER_CHECK_EXPRESSION_RELOCATE(field, delta) \
            { \
                const __m512i old = _mm512_maskz_loadu_epi32(lanes, from->field + n); \
                const __mmask16 present = _mm512_mask_cmpneq_epu32_mask(lanes, old, none); \
                _mm512_mask_storeu_epi32(to->field + base + n, lanes, _mm512_mask_add_epi32(old, present, old, delta)); \
            }
            BUSTER_CHECK_EXPRESSION_RELOCATE(operand0, bias);
            BUSTER_CHECK_EXPRESSION_RELOCATE(operand1, bias);
            BUSTER_CHECK_EXPRESSION_RELOCATE(operand2, bias);
            BUSTER_CHECK_EXPRESSION_RELOCATE(leaf_slots, _mm512_set1_epi32((int32_t)leaves));
#undef BUSTER_CHECK_EXPRESSION_RELOCATE
            const __m512i op = _mm512_maskz_loadu_epi32(lanes, to->ops + base + n);
            const __m512i relative = _mm512_sub_epi32(op, _mm512_set1_epi32(CHECK_OP_CALL));
            const __m512i low = _mm512_srlv_epi32(_mm512_set1_epi32((int32_t)check_graph_argument_operators), relative);
            const __m512i high = _mm512_srlv_epi32(_mm512_set1_epi32((int32_t)(check_graph_argument_operators >> 32)), _mm512_sub_epi32(relative, _mm512_set1_epi32(32)));
            const __mmask16 varargs = _mm512_mask_cmpeq_epi32_mask(lanes, op, _mm512_set1_epi32(CHECK_OP_VARARGS));
            const __mmask16 arguments = _mm512_mask_test_epi32_mask(lanes, _mm512_or_si512(low, high), _mm512_set1_epi32(1)) | varargs;
            const __m512i delta = _mm512_mask_mov_epi32(_mm512_set1_epi32((int32_t)to->argument_count), varargs, _mm512_set1_epi32((int32_t)to->varargs_count));
            const __m512i offsets = _mm512_maskz_loadu_epi32(lanes, from->argument_offsets + n);
            const __m512i counts = _mm512_maskz_loadu_epi32(lanes, from->argument_counts + n);
            _mm512_mask_storeu_epi32(to->argument_offsets + base + n, lanes, _mm512_mask_add_epi32(offsets, arguments, offsets, delta));
            _mm512_mask_storeu_epi32(to->argument_counts + base + n, lanes, counts);
        }
        for (; n < from->count; n += 1)
        {
            to->operand0[base + n] = check_deferred_expression_node(from->operand0[n], base);
            to->operand1[base + n] = check_deferred_expression_node(from->operand1[n], base);
            to->operand2[base + n] = check_deferred_expression_node(from->operand2[n], base);
            to->leaf_slots[base + n] = check_deferred_expression_node(from->leaf_slots[n], leaves);
            to->argument_counts[base + n] = from->argument_counts[n];
            const CheckOp op = to->ops[base + n];
            const uint32_t offset = op == CHECK_OP_VARARGS ? to->varargs_count : to->argument_count;
            to->argument_offsets[base + n] = from->argument_offsets[n] + (from->argument_counts[n] != 0 || op == CHECK_OP_VARARGS || check_graph_has_arguments(op) ? offset : 0);
        }
        for (uint32_t a = 0; a < from->argument_count; a += 1)
        {
            to->argument_nodes[to->argument_count + a] = from->argument_nodes[a] + base;
            to->argument_tokens[to->argument_count + a] = from->argument_tokens[a];
        }
#define BUSTER_CHECK_EXPRESSION_APPEND(field, counter) \
        for (uint32_t i = 0; i < from->counter##_count; i += 1) \
        { \
            to->field[to->counter##_count + i] = from->field[i]; \
            to->field[to->counter##_count + i].node = check_deferred_expression_node(from->field[i].node, base); \
        }
        BUSTER_CHECK_EXPRESSION_APPEND(varargs_calls, varargs);
        BUSTER_CHECK_EXPRESSION_APPEND(builtin_calls, builtin);
        BUSTER_CHECK_EXPRESSION_APPEND(vector_members, vector_member);
        BUSTER_CHECK_EXPRESSION_APPEND(x86_calls, x86);
        BUSTER_CHECK_EXPRESSION_APPEND(overload_calls, overload);
        BUSTER_CHECK_EXPRESSION_APPEND(region_proofs, region_proof);
#undef BUSTER_CHECK_EXPRESSION_APPEND
        for (uint32_t i = 0; i < from->region_proof_count; i += 1)
        {
            to->region_proofs[to->region_proof_count + i].first += to->region_step_count;
        }
        for (uint32_t i = 0; i < from->region_step_count; i += 1)
        {
            CheckRegionProofStep step = from->region_steps[i];
            step.node = check_deferred_expression_node(step.node, base);
            step.operand = check_deferred_expression_node(step.operand, base);
            to->region_steps[to->region_step_count + i] = step;
        }
        for (uint32_t i = 0; i < from->const_warning_count; i += 1)
        {
            const CheckConstWarning* warning = from->const_warnings + i;
            check_input_const_warning(reader, warning->node + base, warning->event.token, warning->event.completion_kind, warning->event.completion);
        }
        to->count += from->count;
        to->leaf_count += from->leaf_count;
        to->argument_count += from->argument_count;
        to->varargs_count += from->varargs_count;
        to->builtin_count += from->builtin_count;
        to->vector_member_count += from->vector_member_count;
        to->arrow_vector_members |= from->arrow_vector_members;
        to->x86_count += from->x86_count;
        to->overload_count += from->overload_count;
        to->region_proof_count += from->region_proof_count;
        to->region_step_count += from->region_step_count;
        // Every operand stays in its forest part, so the purity proof is invariant under this relocation. Later
        // finishes add diagnostic context alone; the final record finish publishes the combined side-list counts.
        to->recorded.maximum_arguments = from->recorded.maximum_arguments > to->recorded.maximum_arguments ? from->recorded.maximum_arguments : to->recorded.maximum_arguments;
        to->recorded.maximum_effects += from->recorded.maximum_effects;
        to->recorded.arguments += from->recorded.arguments;
        to->recorded.varargs += from->recorded.varargs;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(to->recorded.builtins); i += 1)
        {
            to->recorded.builtins[i] += from->recorded.builtins[i];
        }
        to->recorded_nodes = to->count;
    }
    return valid;
}

// The caller reaches this only after the successful merge, binding recovery, selection, constant contexts, region
// setup and sizeof preparation. Unsuccessful/prefix/numbering readings retain the original complete-unit walk.
static bool check_deferred_expressions(CheckDeferred* deferred, CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t lanes = check_deferred_post_lanes(deferred);
    bool used = !deferred->unsupported && lanes > 1 && input->report_count == 0 && !input->resource && input->graph.count == 0 &&
                input->diagnostic_limits == NULL && input->deferred_tokens == NULL && reader->expression_limit == count &&
                !reader->pp->options.expression_numbering && !reader->pp->options.expression_timing;
    if (used)
    {
        const uint64_t plan_start = __rdtsc();
        const uint64_t room = (uint64_t)deferred->body_count + count / BUSTER_CHECK_DEFERRED_ROOT_TOKENS + 1;
        CheckDeferredExpressionPart* parts = BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, CheckDeferredExpressionPart, room);
        bool valid = parts != NULL;
        uint32_t n = 0;
        uint32_t begin = 0;
        uint32_t first_token = count;
        uint32_t end_token = 0;
        uint32_t extra = 0;
        uint32_t offset = 0;
        uint32_t leaf_room = 0;
        bool parallel = true;
        bool body = false;
        for (uint32_t order = 0; valid && order < count; order += 1)
        {
            const uint32_t token = reader->postorder[order];
            const uint32_t parent = parser->tree.parents[token];
            const uint8_t kind = parser->tokens.kinds[token];
            const uint8_t visit = reader->visits[token];
            const uint32_t code = (visit & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
            const uint32_t leaf_codes = (1u << CHECK_INPUT_CODE_IDENTIFIER) | (1u << CHECK_INPUT_CODE_LITERAL) |
                                        (1u << CHECK_INPUT_CODE_STRING) | (1u << CHECK_INPUT_CODE_SCALAR);
            leaf_room += (visit & BUSTER_CHECK_EXPRESSION_ACTS) != 0 && ((leaf_codes >> code) & 1) != 0;
            first_token = token < first_token ? token : first_token;
            end_token = token + 1 > end_token ? token + 1 : end_token;
            extra += kind == PARSE_KIND_L_SQUARE ? 8 : kind == PARSE_KIND_SIZEOF ? 4 : 0;
            parallel = parallel && check_deferred_expression_parallel(reader, token);
            body |= parser->tree.roles[token] == PARSE_ROLE_CLOSE_BODY;
            if (parent >= count && (body || order + 1 - begin >= BUSTER_CHECK_DEFERRED_ROOT_TOKENS || order + 1 == count))
            {
                parts[n++] = (CheckDeferredExpressionPart){ .begin = begin, .end = order + 1, .first_token = first_token,
                    .end_token = end_token, .capacity = order + 1 - begin + extra, .offset = offset,
                    .leaf_capacity = parallel ? leaf_room : order + 1 - begin + extra, .parallel = parallel };
                offset += order + 1 - begin + extra;
                valid = end_token - first_token == order + 1 - begin;
                valid &= n < room || order + 1 == count;
                begin = order + 1;
                first_token = count;
                end_token = 0;
                extra = 0;
                leaf_room = 0;
                parallel = true;
                body = false;
            }
        }
        valid &= begin == count;
        // Keep the forest cuts, but amortize a reader and append-state setup over adjacent roots with the same
        // eligibility. An exceptional function never sends its otherwise independent neighbors to source order.
        uint32_t grouped = 0;
        const uint32_t share = count / (lanes * 4);
        const uint32_t group_tokens = share == 0 ? 1 : share < BUSTER_CHECK_DEFERRED_EXPRESSION_TOKENS ? share : BUSTER_CHECK_DEFERRED_EXPRESSION_TOKENS;
        for (uint32_t p = 0; valid && p < n; p += 1)
        {
            const CheckDeferredExpressionPart part = parts[p];
            CheckDeferredExpressionPart* previous = grouped != 0 ? parts + grouped - 1 : NULL;
            if (previous != NULL && previous->parallel == part.parallel && previous->end_token == part.first_token && previous->end - previous->begin < group_tokens)
            {
                previous->end = part.end;
                previous->end_token = part.end_token;
                previous->capacity += part.capacity;
                previous->leaf_capacity += part.leaf_capacity;
            }
            else
            {
                parts[grouped++] = part;
            }
        }
        n = grouped;
        uint32_t* order = valid ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, uint32_t, n) : NULL;
        valid &= order != NULL;
        uint32_t largest = 0;
        uint32_t largest_tokens = 0;
        uint32_t leaf_offset = 0;
        for (uint32_t p = 0; valid && p < n; p += 1)
        {
            order[p] = p;
            parts[p].leaf_offset = leaf_offset;
            leaf_offset += parts[p].leaf_capacity;
            const uint32_t tokens = parts[p].parallel ? parts[p].end - parts[p].begin : 0;
            largest = tokens > largest_tokens ? p : largest;
            largest_tokens = tokens > largest_tokens ? tokens : largest_tokens;
        }
        valid &= n != 0 && offset <= input->graph.capacity && leaf_offset <= input->graph.capacity;
        if (valid && n != 0)
        {
            order[largest] = 0;
            order[0] = largest;
        }
        CheckDeferredExpressions run = { .deferred = deferred, .reader = reader, .parts = parts, .order = order, .count = n };
        const uint64_t round_start = __rdtsc();
        deferred->expression_ticks[0] = round_start - plan_start;
        if (deferred->late_first)
        {
            __atomic_store_n(&deferred->others_took, 0, __ATOMIC_RELEASE);
        }
        valid = valid && check_deferred_lanes_run(deferred, BUSTER_CHECK_DEFERRED_ROUND_EXPRESSIONS, lanes, check_deferred_expression_lane, &run);
        // No later phase runs a lane round. Return spare workers before ordered roots, graph publication and
        // serial finishing; the outer cleanup remains the once-only guard for every earlier exit.
        check_deferred_give_back(deferred);
        const uint64_t publish_start = __rdtsc();
        // Workers changed token-local warning facts without updating the caller's shared interval summaries.
        reader->warning_index_ready = false;
        deferred->expression_ticks[1] = publish_start - round_start;
        uint64_t serial_ticks = 0;
        uint64_t ticks[BUSTER_CHECK_EXPRESSION_PARTS] = { 0 };
        uint32_t acted[BUSTER_CHECK_EXPRESSION_CODES] = { 0 };
        uint32_t left[BUSTER_CHECK_EXPRESSION_CODES] = { 0 };
        ticks[BUSTER_CHECK_EXPRESSION_PART_CLASSES] = input->expression_part_ticks[BUSTER_CHECK_EXPRESSION_PART_CLASSES];
        for (uint32_t p = 0; valid && p < n; p += 1)
        {
            CheckDeferredExpressionPart* part = parts + p;
            if (!part->parallel)
            {
                // Ordered work appends directly at its final identities. Its bounded prefix cannot reach the next
                // part's reserved slots. In particular the graph-indexed builtin caches keep one continuous numbering.
                const uint64_t start = __rdtsc();
                memset(input->expression_left, 0, sizeof(input->expression_left));
                check_input_expressions_coded_range(reader, part->begin, part->end);
                if (!input->resource)
                {
                    check_graph_record_finish(&input->graph);
                }
                valid = !input->resource && input->report_count == 0 && input->graph.count <= part->offset + part->capacity &&
                        input->graph.argument_count <= part->end && input->graph.leaf_count <= part->leaf_offset + part->leaf_capacity;
                for (uint32_t i = 1; i < BUSTER_CHECK_EXPRESSION_PARTS; i += 1)
                {
                    ticks[i] += input->expression_part_ticks[i];
                }
                for (uint32_t i = 0; i < BUSTER_CHECK_EXPRESSION_CODES; i += 1)
                {
                    acted[i] += input->expression_acted[i];
                    left[i] += input->expression_left[i];
                }
                deferred->expression_serial_tokens += part->end - part->begin;
                serial_ticks += __rdtsc() - start;
                continue;
            }
            deferred->expression_parallel_tokens += part->end - part->begin;
            deferred->expression_worker_mask |= UINT64_C(1) << part->lane;
            part->base = input->graph.count;
            valid = part->valid && check_deferred_expression_append(reader, part, !deferred->scalar_merge);
            input->constant_warning_count += part->constant_warnings;
            for (uint32_t i = 1; i < BUSTER_CHECK_EXPRESSION_PARTS; i += 1)
            {
                ticks[i] += part->ticks[i];
            }
            for (uint32_t i = 0; i < BUSTER_CHECK_EXPRESSION_CODES; i += 1)
            {
                acted[i] += part->acted[i];
                left[i] += part->left[i];
            }
            // Value chains, first/last_value, region_roots and vla_sizes have no consumer after this boundary.
            // Only the retained token roots and sparse receipts need final node identities.
            uint32_t t = part->first_token;
            for (; valid && !BUSTER_SCALAR_PATH(deferred->scalar_merge) && t < part->end_token; t += 16)
            {
                const uint32_t length = part->end_token - t < 16 ? part->end_token - t : 16;
                const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, length);
                const __m512i old = _mm512_maskz_loadu_epi32(lanes, input->token_nodes + t);
                const __mmask16 present = _mm512_mask_cmpneq_epu32_mask(lanes, old, _mm512_set1_epi32((int32_t)BUSTER_CHECK_NONE));
                _mm512_mask_storeu_epi32(input->token_nodes + t, present, _mm512_add_epi32(old, _mm512_set1_epi32((int32_t)part->base)));
            }
            for (; valid && t < part->end_token; t += 1)
            {
                input->token_nodes[t] = check_deferred_expression_node(input->token_nodes[t], part->base);
            }
            const uint64_t mark = reader->scratch->length;
            uint32_t* positions = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)part->end_token - part->first_token);
            valid &= positions != NULL;
            CheckSparseColumn columns[] = { input->initializer_nodes, input->evaluation_nodes, input->case_ranges };
            for (uint32_t c = 0; valid && c < BUSTER_ARRAY_LENGTH(columns); c += 1)
            {
                const uint32_t entries = check_sparse_entries(columns[c], part->first_token, part->end_token, !BUSTER_SCALAR_PATH(deferred->scalar_merge), positions);
                for (uint32_t e = 0; e < entries; e += 1)
                {
                    const uint32_t token = positions[e];
                    check_sparse_set(input->sparse_pool, columns[c], token, check_sparse_get(columns[c], token) + part->base);
                }
            }
            arena_pop_to(reader->scratch, mark);
            for (uint32_t i = 0; valid && i < part->warnings.count; i += 1)
            {
                const CheckWarningEvent* event = part->warnings.events + i;
                const CheckWarningNote* notes = event->note_count != 0 ? part->warnings.notes + event->note_first : NULL;
                valid = check_input_warning_event(reader, event, notes, event->note_count);
                const uint64_t key = (uint64_t)event->kind << 32 | ((uint64_t)event->token + 1);
                valid = valid && check_warning_set_add(&reader->source_warning_keys, key);
            }
        }
        memcpy(input->expression_part_ticks, ticks, sizeof(ticks));
        memcpy(input->expression_acted, acted, sizeof(acted));
        memcpy(input->expression_left, left, sizeof(left));
        deferred->expression_parts = n;
        valid &= !deferred->fail_after_expressions;
        deferred->expression_retry = !valid;
        input->resource |= !valid;
        input->warning_events = check_warning_view(&reader->warning_events);
        input->graph.const_values = &input->const_values;
        deferred->expression_ticks[2] = serial_ticks;
        deferred->expression_ticks[3] = __rdtsc() - publish_start - serial_ticks;
    }
    return used;
}
