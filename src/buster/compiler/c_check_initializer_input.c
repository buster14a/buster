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
    // Index plus one into plans; zero means this declaration has no explicit initializer.
    uint32_t* declaration_plans;
    uint32_t count;
    uint32_t token_count;
    // Every original warning token, deduplicated across repeated range/compound planning.
    uint32_t* warning_tokens;
    uint32_t warning_count;
    CheckInitStatus status;
    uint32_t token;
};

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

static void check_init_warning_order(CheckInitWarningCollector* collector, uint32_t first)
{
    if (collector->sink != NULL)
    {
        uint32_t at = first;
        for (uint32_t token = 0; token < collector->token_count; token += 1)
        {
            if (collector->seen[token] == 1)
            {
                collector->sink->tokens[at++] = token;
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
    CheckInitWarningCollector collector = { 0 };
    if (valid && !check_init_warning_collector(&collector, warnings, parser->tokens.count, scratch))
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    for (uint32_t declaration = 0; valid && declaration < cutoff; declaration += 1)
    {
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
                                                         true, true, output, scratch, &plan, &input->graph, warnings != NULL ? &collector : NULL);
            if (plan.warning_count != 0)
            {
                result->warning_count += 1;
                result->warning_token = plan.warning_token < result->warning_token ? plan.warning_token : result->warning_token;
            }
            if (!accepted && plan.status == CHECK_INIT_RESOURCE)
            {
                valid = false;
                result->status = CHECK_INIT_RESOURCE;
                result->token = plan.failed_token;
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
        check_init_warning_order(&collector, warning_start);
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
    uint32_t count = 0;
    for (uint32_t token = 0; valid && token < tokens; token += 1)
    {
        if (input->initializer_begins[token] != BUSTER_CHECK_NONE)
        {
            const uint32_t begin = input->initializer_begins[token];
            const uint32_t end = input->initializer_ends[token];
            valid = begin < end && end <= tokens && type_is_valid(types, input->declared_types[token]);
            if (valid)
            {
                count += 1;
            }
            else
            {
                result->status = CHECK_INIT_INVALID;
                result->token = token;
            }
        }
    }
    if (valid)
    {
        result->plans = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckInitializer, count) : 0;
        result->declaration_tokens = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count) : 0;
        result->warning_tokens = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, tokens) : 0;
        result->declaration_plans = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)tokens + 1);
        valid = result->declaration_plans != 0 && (count == 0 || (result->plans != 0 && result->declaration_tokens != 0 && result->warning_tokens != 0));
        result->status = valid ? CHECK_INIT_OK : CHECK_INIT_RESOURCE;
        if (valid)
        {
            memset(result->declaration_plans, 0, ((uint64_t)tokens + 1) * sizeof(uint32_t));
        }
    }
    CheckInitWarningSink warnings = { .tokens = result->warning_tokens, .capacity = count != 0 ? tokens : 0 };
    CheckInitWarningCollector collector = { 0 };
    if (valid && !check_init_warning_collector(&collector, &warnings, tokens, scratch))
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    uint32_t at = 0;
    for (uint32_t token = 0; valid && token < tokens; token += 1)
    {
        if (input->initializer_begins[token] != BUSTER_CHECK_NONE)
        {
            CheckInitializer* plan = result->plans + at;
            valid = check_initializer_plan_warnings(types, parser, input->token_nodes, stream, input->strings, input->string_count,
                                          input->declared_types[token], input->initializer_begins[token], input->initializer_ends[token],
                                          input->static_objects[token] != 0, false, true, output, scratch, plan, &input->graph, &collector);
            if (valid)
            {
                result->declaration_tokens[at] = token;
                result->declaration_plans[token] = at + 1;
                at += 1;
            }
            else
            {
                result->status = plan->status;
                result->token = plan->failed_token;
            }
        }
    }
    if (valid)
    {
        check_init_warning_order(&collector, 0);
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
