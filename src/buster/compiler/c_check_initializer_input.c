// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Validate every source initializer after expression checking, including automatic objects. Compact retained
// plans own their converted subobject writes; a declaration-token column gives consumers direct access without
// reparsing or name lookup. This phase is required for source acceptance, independently of data/code emission.
// Include after c_check_input.c. Output and scratch are disjoint and the output remains live with CheckInput.
#ifndef BUSTER_C_CHECK_INITIALIZER_INPUT_INCLUDED
#define BUSTER_C_CHECK_INITIALIZER_INPUT_INCLUDED

typedef struct CheckInitializers CheckInitializers;
struct CheckInitializers
{
    CheckInitializer* plans;
    uint32_t* declaration_tokens;
    uint32_t count;
    uint32_t token_count;
    // Every original warning token, deduplicated across repeated range/compound planning.
    uint32_t* warning_tokens;
    uint32_t warning_count;
    CheckInitStatus status;
    uint32_t token;
};

// The plan's index plus one, or zero for a token with no explicit initializer. The producer writes declaration_tokens
// once in ascending token order; consumers need only this bounded lower bound, rather than a zero-filled token column.
static uint32_t check_initializers_plan_index(const CheckInitializers* initializers, uint32_t token)
{
    uint32_t base = 0;
    uint32_t length = initializers->count;
    while (length > 1)
    {
        const uint32_t half = length / 2;
        base = initializers->declaration_tokens[base + half] < token ? base + half : base;
        length -= half;
    }
    const uint32_t lower = length != 0 ? base + (initializers->declaration_tokens[base] < token) : 0;
    const uint32_t index = lower < initializers->count && initializers->declaration_tokens[lower] == token ? lower + 1 : 0;
    return index;
}

static bool check_init_object_candidate(const CheckInput* input, const TypeTable* types, uint32_t token)
{
    const bool initialized = input->objects != NULL && input->objects[token] != 0 &&
                             input->initializer_begins[token] < input->initializer_ends[token] && type_is_valid(types, input->declared_types[token]);
    const TypeId type = initialized ? type_strip_aligned(types, input->declared_types[token]) : 0;
    return initialized && check_vector_type(types, type) &&
           (type_qualifiers(type) & (BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE)) == BUSTER_TYPE_CONST;
}

// The declarations among tokens 0 to `end` - 1 that have an initializer (initializer_begins isn't BUSTER_CHECK_NONE), in
// token order into `declarations`, which has room for `end`: how many. Few of a unit's tokens declare with an initializer
// (#592), so the vector path tests 16 tokens at a time; the scalar path takes every token.
static uint32_t check_init_declarations(const CheckInput* input, uint32_t end, uint32_t* declarations, bool scalar)
{
    uint32_t count = 0;
    for (uint32_t first = 0; first < end; first += 16)
    {
        const uint32_t lanes = scalar ? (uint32_t)check_input_scan_all(first, end) & 0xffff :
                                        check_input_scan_below(input->initializer_begins, first, end, BUSTER_CHECK_NONE);
        for (uint32_t selected = lanes; selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
            declarations[count] = token;
            count += input->initializer_begins[token] != BUSTER_CHECK_NONE;
        }
    }
    return count;
}

// The table of constant objects' bytes, with room for the candidates among `declarations` (check_init_declarations')
// whose initializers end by `cutoff`: an object can only be a candidate where its declaration has an initializer.
static bool check_init_objects_open(CheckInitObjects* objects, const CheckInput* input, const TypeTable* types, const uint32_t* declarations,
                                    uint32_t declaration_count, uint32_t cutoff, Arena* scratch)
{
    *objects = (CheckInitObjects){ 0 };
    uint32_t count = 0;
    for (uint32_t d = 0; d < declaration_count; d += 1)
    {
        const uint32_t token = declarations[d];
        count += check_init_object_candidate(input, types, token) && input->initializer_ends[token] <= cutoff;
    }
    uint64_t capacity = count != 0 ? 1 : 0;
    while (capacity < (uint64_t)count * 2)
    {
        capacity *= 2;
    }
    bool valid = capacity <= UINT32_MAX;
    if (valid && capacity != 0)
    {
        objects->entries = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckInitObject, capacity);
        valid = objects->entries != NULL;
        if (valid)
        {
            memset(objects->entries, 0, capacity * sizeof(CheckInitObject));
            objects->capacity = (uint32_t)capacity;
        }
    }
    return valid;
}

// This reader only consumes a completed plan. It cannot invoke expression evaluation or follow an object
// reference, so source-ordered constant-object retention adds no recursive dependency traversal.
static bool check_init_object_bytes(const TypeTable* types, const CheckInitializer* plan, uint8_t* bytes, uint64_t size)
{
    bool valid = size <= UINT64_MAX / 8 && size == plan->object_size && plan->status == CHECK_INIT_OK;
    if (valid)
    {
        memset(bytes, 0, size);
    }
    for (uint32_t i = 0; valid && i < plan->count; i += 1)
    {
        const CheckInitPart* part = plan->parts + i;
        uint64_t bits = 0;
        uint8_t scalar[16] = { 0 };
        const uint8_t* source = part->bytes;
        valid = check_initializer_part_bits(types, part, &bits) == CHECK_INIT_OK && part->bit_offset <= size * 8 && bits <= size * 8 - part->bit_offset;
        if (valid && part->kind == CHECK_INIT_VALUE)
        {
            valid = bits <= sizeof(scalar) * 8 && check_init_scalar_bytes(types, &part->value, scalar);
            source = scalar;
        }
        if (valid && part->kind != CHECK_INIT_CLEAR)
        {
            valid = bits == 0 || source != NULL;
        }
        if (valid && (part->bit_offset % 8) == 0 && (bits % 8) == 0)
        {
            if (part->kind == CHECK_INIT_CLEAR)
            {
                memset(bytes + part->bit_offset / 8, 0, bits / 8);
            }
            else if (bits != 0)
            {
                memcpy(bytes + part->bit_offset / 8, source, bits / 8);
            }
        }
        else if (valid)
        {
            for (uint64_t bit = 0; bit < bits; bit += 1)
            {
                const uint64_t destination = part->bit_offset + bit;
                const uint8_t mask = (uint8_t)(1u << (destination % 8));
                const bool set = part->kind != CHECK_INIT_CLEAR && ((source[bit / 8] >> (bit % 8)) & 1) != 0;
                bytes[destination / 8] = (uint8_t)((bytes[destination / 8] & ~mask) | (set ? mask : 0));
            }
        }
    }
    return valid;
}

static bool check_init_object_retain(CheckInitObjects* objects, const CheckInput* input, const CheckStream* stream,
                                      const Parser* parser, TypeTable* types, uint32_t token, const CheckInitializer* plan,
                                      bool diagnostic_only, Arena* output, Arena* scratch)
{
    bool valid = true;
    if (check_init_object_candidate(input, types, token))
    {
        const uint32_t slot = check_init_object_slot(objects, input->objects[token]);
        const uint64_t mark = scratch->length;
        const uint64_t size = type_size(types, input->declared_types[token]);
        uint8_t* bytes = size != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, size) : NULL;
        valid = slot != BUSTER_CHECK_NONE && bytes != NULL;
        bool known = valid && check_init_object_bytes(types, plan, bytes, size);
        if (valid && !known && !input->static_objects[token])
        {
            // Automatic const vectors may have runtime initializers. A bounded static-mode attempt reuses
            // earlier retained bytes; an ordinary nonconstant result does not reject the automatic object.
            const uint64_t output_mark = output->length;
            CheckInitializer constant = { 0 };
            const bool accepted = check_initializer_plan_warnings(types, parser, input->token_nodes, stream, input->strings, input->string_count,
                                       input->declared_types[token], input->initializer_begins[token], input->initializer_ends[token],
                                       true, diagnostic_only, true, output, scratch, &constant, &input->graph, NULL, objects);
            valid = accepted || constant.status != CHECK_INIT_RESOURCE;
            known = accepted && check_init_object_bytes(types, &constant, bytes, size);
            arena_pop_to(output, output_mark);
        }
        if (known)
        {
            objects->entries[slot] = (CheckInitObject){
                .object = input->objects[token],
                .end = input->initializer_ends[token],
                .type = type_unqualified(type_strip_aligned(types, input->declared_types[token])),
                .bytes = bytes,
            };
        }
        else
        {
            arena_pop_to(scratch, mark);
        }
    }
    return valid;
}

static bool check_init_warning_collector(CheckInitWarningCollector* collector, CheckInitWarningSink* sink, uint32_t token_count, Arena* scratch)
{
    *collector = (CheckInitWarningCollector){ .sink = sink, .token_count = token_count };
    bool valid = sink == NULL || (sink->count <= sink->capacity && (sink->capacity == 0 || sink->tokens != NULL));
    if (valid && sink != NULL)
    {
        collector->seen = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, token_count);
        valid = token_count == 0 || collector->seen != NULL;
        if (valid)
        {
            if (token_count != 0)
            {
                memset(collector->seen, 0, token_count);
            }
            for (uint32_t i = 0; valid && i < sink->count; i += 1)
            {
                valid = sink->tokens[i] < token_count;
                if (valid)
                {
                    collector->seen[sink->tokens[i]] = 2;
                }
            }
        }
    }
    return valid;
}

// The warnings' tokens the collector added to its sink from `first` on, in token order: the tokens it saw, 64 at a time
// (the scalar path takes every token), where it added any.
static void check_init_warning_order(CheckInitWarningCollector* collector, uint32_t first, bool scalar)
{
    if (collector->sink != NULL && collector->sink->count != first)
    {
        uint32_t at = first;
        for (uint32_t token = 0; token < collector->token_count; token += 64)
        {
            const uint64_t lanes = scalar ? check_input_scan_all(token, collector->token_count) :
                                            check_input_scan_equal(collector->seen, token, collector->token_count, 1);
            for (uint64_t selected = lanes; selected != 0; selected &= selected - 1)
            {
                const uint32_t warned = token + (uint32_t)__builtin_ctzll(selected);
                if (collector->seen[warned] == 1)
                {
                    collector->sink->tokens[at] = warned;
                    at += 1;
                }
            }
        }
    }
}

typedef struct CheckInitDiagnostic CheckInitDiagnostic;
struct CheckInitDiagnostic
{
    CheckInitStatus status;
    uint32_t token;
    uint32_t warning_count;
    uint32_t warning_token;
};

// Diagnose complete roots strictly before the earliest existing input/graph report and before_token (exclusive).
// BUSTER_CHECK_NONE supplies no additional cutoff. The real stream is never edited or relabeled successful.
// Each selected root's mapped nodes must have a checked dependency closure within the cutoff. Missing structural
// nodes are normal; a missing value/designator root makes the planner decline that root without inventing a
// diagnostic. Source-produced diagnostic_graph retains exactly such complete earlier expression fragments.
//
// True means the diagnostic scan completed, including a semantic error in result. False means resource failure
// or an unusable input/stream, with the corresponding status. No CheckInitializers or parts are published, and
// both arenas always return to their entry lengths. The strict publishing API below remains unchanged.
// Optional warning sink storage belongs to the caller and precedes both arena suffixes. It receives distinct
// original token IDs in source order, including warnings before a later semantic failure. capacity == parser->tokens.count is
// sufficient for an empty sink. Resource/input failure restores its entry count; arenas always restore fully.
static bool check_initializers_diagnose_warnings(CheckInitDiagnostic* result, const CheckInput* input, const CheckStream* stream,
                                                 const Parser* parser, TypeTable* types, uint32_t before_token, Arena* output, Arena* scratch,
                                                 CheckInitWarningSink* warnings)
{
    *result = (CheckInitDiagnostic){
        .token = BUSTER_CHECK_NONE,
        .warning_token = BUSTER_CHECK_NONE,
    };
    const uint64_t mark = output != 0 ? output->length : 0;
    const uint64_t temporary = scratch != 0 ? scratch->length : 0;
    const uint32_t warning_start = warnings != NULL ? warnings->count : 0;
    bool valid = input != 0 && stream != 0 && parser != 0 && types != 0 && output != 0 && scratch != 0 && output != scratch;
    if (valid)
    {
        valid = input->token_count == parser->tokens.count && input->token_count < UINT32_MAX && stream->count == input->graph.count &&
                stream->status == CHECK_STATUS_OK && !input->resource && (input->report_count == 0 || input->diagnostic_graph) &&
                (input->token_count == 0 || (input->initializer_begins != 0 && input->initializer_ends != 0 && input->declared_types != 0 &&
                 input->static_objects != 0 && input->token_nodes != 0)) && (input->report_count == 0 || input->reports != 0) &&
                (stream->report_count == 0 || stream->reports != 0) && (stream->count == 0 ||
                 (stream->values != 0 && stream->diagnostics != 0 && input->graph.ops != 0 && input->graph.tokens != 0 &&
                  input->graph.operand0 != 0 && input->graph.operand1 != 0 && input->graph.operand2 != 0));
    }
    if (!valid)
    {
        result->status = input != 0 && input->resource ? CHECK_INIT_RESOURCE :
                         stream != 0 && stream->status == CHECK_STATUS_RESOURCE ? CHECK_INIT_RESOURCE : CHECK_INIT_CHECK_FAILED;
    }
    uint32_t cutoff = valid ? input->token_count : 0;
    if (before_token < cutoff)
    {
        cutoff = before_token;
    }
    for (uint32_t i = 0; valid && i < input->report_count; i += 1)
    {
        cutoff = input->reports[i].token < cutoff ? input->reports[i].token : cutoff;
    }
    for (uint32_t i = 0; valid && i < stream->report_count; i += 1)
    {
        cutoff = stream->reports[i].token < cutoff ? stream->reports[i].token : cutoff;
    }
    uint8_t* complete = valid && stream->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, stream->count) : 0;
    if (valid && stream->count != 0 && complete == 0)
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    for (uint32_t node = 0; valid && node < stream->count; node += 1)
    {
        const CheckGraph* graph = &input->graph;
        bool checked = graph->tokens[node] < cutoff && (graph->source_positions == 0 || graph->source_positions[node] <= cutoff) &&
                       stream->diagnostics[node] == CHECK_DIAG_NONE && type_is_valid(types, stream->values[node].type);
        const uint32_t operands[] = {
            graph->operand0[node],
            graph->operand1[node],
            graph->operand2[node],
        };
        for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(operands); i += 1)
        {
            checked = operands[i] == BUSTER_CHECK_NONE || (operands[i] < node && complete[operands[i]]);
        }
        if (checked && check_graph_has_arguments(graph->ops[node]) && graph->argument_counts != 0)
        {
            const uint32_t first = graph->argument_offsets != 0 ? graph->argument_offsets[node] : 0;
            const uint32_t count = graph->argument_counts[node];
            checked = first <= graph->argument_count && count <= graph->argument_count - first && (count == 0 || graph->argument_nodes != 0);
            for (uint32_t i = 0; checked && i < count; i += 1)
            {
                const uint32_t operand = graph->argument_nodes[first + i];
                checked = operand < node && complete[operand];
            }
        }
        complete[node] = checked;
    }
    // The declarations with initializers before the cutoff: those of any other token begin at BUSTER_CHECK_NONE, which
    // makes them ineligible below.
    uint32_t* declarations = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)cutoff + 1) : 0;
    const uint32_t declaration_count = declarations != 0 ? check_init_declarations(input, cutoff, declarations, parser->options.scalar) : 0;
    if (valid && declarations == 0)
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    CheckInitWarningCollector collector = { 0 };
    if (valid && !check_init_warning_collector(&collector, warnings, parser->tokens.count, scratch))
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    CheckInitObjects objects = { 0 };
    if (valid && !check_init_objects_open(&objects, input, types, declarations, declaration_count, cutoff, scratch))
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    for (uint32_t d = 0; valid && d < declaration_count; d += 1)
    {
        const uint32_t declaration = declarations[d];
        const uint32_t begin = input->initializer_begins[declaration];
        const uint32_t end = input->initializer_ends[declaration];
        bool eligible = begin < end && end <= cutoff && type_is_valid(types, input->declared_types[declaration]);
        for (uint32_t token = begin; eligible && token < end; token += 1)
        {
            const uint32_t node = input->token_nodes[token];
            eligible = node == BUSTER_CHECK_NONE || (node < stream->count && complete[node]);
        }
        if (eligible)
        {
            CheckInitializer plan = { 0 };
            const bool accepted = check_initializer_plan_warnings(types, parser, input->token_nodes, stream, input->strings, input->string_count,
                                                         input->declared_types[declaration], begin, end, input->static_objects[declaration] != 0,
                                                         true, true, output, scratch, &plan, &input->graph, warnings != NULL ? &collector : NULL, &objects);
            const bool retained = !accepted || check_init_object_retain(&objects, input, stream, parser, types, declaration, &plan, true, output, scratch);
            if (plan.warning_count != 0)
            {
                result->warning_count += 1;
                result->warning_token = plan.warning_token < result->warning_token ? plan.warning_token : result->warning_token;
            }
            if (!retained || (!accepted && plan.status == CHECK_INIT_RESOURCE))
            {
                valid = false;
                result->status = CHECK_INIT_RESOURCE;
                result->token = retained ? plan.failed_token : begin;
            }
            else if (!accepted && plan.status != CHECK_INIT_CHECK_FAILED && plan.failed_token < result->token)
            {
                result->status = plan.status;
                result->token = plan.failed_token;
            }
            arena_pop_to(output, mark);
        }
    }
    if (valid)
    {
        check_init_warning_order(&collector, warning_start, parser->options.scalar);
    }
    if (output != 0)
    {
        arena_pop_to(output, mark);
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, temporary);
    }
    if (warnings != NULL)
    {
        warnings->count = valid ? warnings->count : warning_start;
        result->warning_count = valid ? warnings->count - warning_start : 0;
        if (!valid)
        {
            result->warning_token = BUSTER_CHECK_NONE;
        }
    }
    return valid;
}

static bool check_initializers_diagnose(CheckInitDiagnostic* result, const CheckInput* input, const CheckStream* stream,
                                        const Parser* parser, TypeTable* types, uint32_t before_token, Arena* output, Arena* scratch)
{
    return check_initializers_diagnose_warnings(result, input, stream, parser, types, before_token, output, scratch, NULL);
}

static bool check_initializers_run(CheckInitializers* result, const CheckInput* input, const CheckStream* stream,
                                   const Parser* parser, TypeTable* types, Arena* output, Arena* scratch)
{
    *result = (CheckInitializers){ .token = BUSTER_CHECK_NONE };
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const uint32_t tokens = input->token_count;
    bool valid = output != scratch && tokens == parser->tokens.count && tokens < UINT32_MAX &&
                 stream->count == input->graph.count && (tokens == 0 ||
                 (input->initializer_begins != 0 && input->initializer_ends != 0 && input->declared_types != 0 &&
                  input->static_objects != 0 && input->token_nodes != 0));
    result->status = valid ? CHECK_INIT_OK : CHECK_INIT_INVALID;
    if (valid && (input->resource || input->report_count != 0 || stream->status != CHECK_STATUS_OK || stream->report_count != 0))
    {
        valid = false;
        result->status = input->resource || stream->status == CHECK_STATUS_RESOURCE ? CHECK_INIT_RESOURCE : CHECK_INIT_CHECK_FAILED;
    }
    // The declarations with initializers, which the checks, the objects' table and the plans visit rather than every token
    // (#592).
    uint32_t* declarations = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)tokens + 1) : 0;
    const uint32_t count = declarations != 0 ? check_init_declarations(input, tokens, declarations, parser->options.scalar) : 0;
    if (valid && declarations == 0)
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    for (uint32_t d = 0; valid && d < count; d += 1)
    {
        const uint32_t token = declarations[d];
        const uint32_t begin = input->initializer_begins[token];
        const uint32_t end = input->initializer_ends[token];
        valid = begin < end && end <= tokens && type_is_valid(types, input->declared_types[token]);
        if (!valid)
        {
            result->status = CHECK_INIT_INVALID;
            result->token = token;
        }
    }
    if (valid)
    {
        result->plans = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckInitializer, count) : 0;
        result->declaration_tokens = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count) : 0;
        result->warning_tokens = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, tokens) : 0;
        valid = count == 0 || (result->plans != 0 && result->declaration_tokens != 0 && result->warning_tokens != 0);
        result->status = valid ? CHECK_INIT_OK : CHECK_INIT_RESOURCE;
    }
    CheckInitWarningSink warnings = { .tokens = result->warning_tokens, .capacity = count != 0 ? tokens : 0 };
    CheckInitWarningCollector collector = { 0 };
    if (valid && !check_init_warning_collector(&collector, &warnings, tokens, scratch))
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    CheckInitObjects objects = { 0 };
    if (valid && !check_init_objects_open(&objects, input, types, declarations, count, tokens, scratch))
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    for (uint32_t at = 0; valid && at < count; at += 1)
    {
        const uint32_t token = declarations[at];
        CheckInitializer* plan = result->plans + at;
        valid = check_initializer_plan_warnings(types, parser, input->token_nodes, stream, input->strings, input->string_count,
                                      input->declared_types[token], input->initializer_begins[token], input->initializer_ends[token],
                                      input->static_objects[token] != 0, false, true, output, scratch, plan, &input->graph, &collector, &objects);
        if (valid && !check_init_object_retain(&objects, input, stream, parser, types, token, plan, false, output, scratch))
        {
            valid = false;
            plan->status = CHECK_INIT_RESOURCE;
            plan->failed_token = input->initializer_begins[token];
        }
        if (valid)
        {
            result->declaration_tokens[at] = token;
        }
        else
        {
            result->status = plan->status;
            result->token = plan->failed_token;
        }
    }
    if (valid)
    {
        check_init_warning_order(&collector, 0, parser->options.scalar);
        result->count = count;
        result->token_count = tokens;
        result->warning_count = warnings.count;
    }
    else
    {
        const CheckInitStatus status = result->status;
        const uint32_t token = result->token;
        arena_pop_to(output, output_mark);
        *result = (CheckInitializers){ .status = status, .token = token };
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

#endif
