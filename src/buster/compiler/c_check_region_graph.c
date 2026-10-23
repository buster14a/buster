// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Bounded typed statement-prefix schedules. These prove values/effects; the program body remains the runtime.
#ifndef BUSTER_C_CHECK_REGION_GRAPH_INCLUDED
#define BUSTER_C_CHECK_REGION_GRAPH_INCLUDED

typedef struct CheckRegionValues CheckRegionValues;
struct CheckRegionValues
{
    const CheckValue* values;
    const bool* valid;
    const uint32_t* indices;
    size_t stride;
};

typedef struct CheckRegionResult CheckRegionResult;
struct CheckRegionResult
{
    CheckValue effects;
    CheckDiagnostic diagnostic;
    bool constant;
    bool ended;
};

static const CheckRegionProof* check_graph_region_proof(const CheckGraph* graph, uint32_t node)
{
    uint32_t first = 0;
    uint32_t last = graph->region_proof_count;
    while (first < last)
    {
        const uint32_t middle = first + (last - first) / 2;
        if (graph->region_proofs[middle].node < node)
        {
            first = middle + 1;
        }
        else
        {
            last = middle;
        }
    }
    return first < graph->region_proof_count && graph->region_proofs[first].node == node ? graph->region_proofs + first : NULL;
}

static CheckValue check_region_value(const CheckRegionValues* values, uint32_t node)
{
    const uint32_t index = values->indices != NULL ? values->indices[node] : node;
    return *(const CheckValue*)((const uint8_t*)values->values + (size_t)index * values->stride);
}

static CheckValue check_region_operand(TypeTable* types, const CheckGraph* graph, const CheckRegionValues* values,
                                       uint32_t node, bool runtime, CheckDiagnostic* diagnostic)
{
    const CheckAction action = { .op = CHECK_OP_VALUE, .node = node, .operands = { check_region_value(values, node) },
        .tag_completions = graph->tag_completions, .transparent_unions = graph->transparent_unions,
        .tag_completion_count = graph->tag_completion_count,
        .source_position = graph->source_positions != NULL ? graph->source_positions[node] : 0, .extensions = graph->extensions };
    CheckActionResult checked = { 0 };
    CheckValue value = check_action_operand(types, &action, &checked, 0);
    if (*diagnostic == CHECK_DIAG_NONE)
    {
        *diagnostic = checked.diagnostic;
    }
    if (runtime && graph->ops[node] == CHECK_OP_INITIALIZE && graph->types != NULL &&
        ((type_qualifiers(graph->types[node]) & BUSTER_TYPE_VOLATILE) != 0 || check_action_kind(types, graph->types[node]) == BUSTER_TYPE_KIND_ATOMIC))
    {
        value.flags |= BUSTER_CHECK_SIDE_EFFECT;
    }
    return value;
}

// A source/speculative proof follows one known path and refuses a revisited step. The runtime effect summary
// visits each reachable step once, unioning unknown branches without interpreting a variable environment.
static bool check_region_walk(TypeTable* types, const CheckGraph* graph, const CheckRegionProof* region,
                              const CheckRegionValues* values, bool runtime, bool environment_unknown,
                              Arena* scratch, uint64_t* scratch_peak, CheckRegionResult* result)
{
    *result = (CheckRegionResult){ .constant = (region->flags & CHECK_REGION_UNSUPPORTED) == 0 };
    const uint64_t start = scratch->length;
    uint8_t* visited = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, region->count);
    uint32_t* pending = runtime ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, region->count) : NULL;
    bool ready = visited != NULL && (!runtime || pending != NULL);
    if (ready)
    {
        memset(visited, 0, region->count);
        uint32_t read = 0;
        uint32_t count = 1;
        uint32_t position = 0;
        if (runtime)
        {
            pending[0] = 0;
            visited[0] = 1;
        }
        while (runtime ? read < count : position < region->count)
        {
            if (runtime)
            {
                position = pending[read++];
            }
            else if (visited[position] != 0)
            {
                result->constant = false;
                break;
            }
            visited[position] = 1;
            const CheckRegionProofStep* step = graph->region_steps + region->first + position;
            uint32_t next[2] = { position + 1, BUSTER_CHECK_NONE };
            if (step->op == CHECK_REGION_END)
            {
                result->ended = true;
                next[0] = BUSTER_CHECK_NONE;
            }
            else if (step->op == CHECK_REGION_JUMP)
            {
                next[0] = step->target;
            }
            else
            {
                CheckValue value = check_region_operand(types, graph, values, step->node, runtime, &result->diagnostic);
                if (result->diagnostic == CHECK_DIAG_RESOURCE)
                {
                    ready = false;
                    break;
                }
                if (values->valid != NULL)
                {
                    const uint32_t index = values->indices != NULL ? values->indices[step->node] : step->node;
                    result->constant &= *(const bool*)((const uint8_t*)values->valid + (size_t)index * values->stride);
                }
                if (runtime && step->op == CHECK_REGION_BRANCH && graph->fp_environments != NULL &&
                    graph->fp_environments[step->node] != 0 && check_action_floating_type(types, value.type))
                {
                    value.flags |= BUSTER_CHECK_FP_ENVIRONMENT;
                }
                check_action_inherit(&result->effects, &value);
                result->constant &= value.fault == CHECK_FAULT_NONE && (value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                                    (value.constant_kind != CHECK_CONSTANT_NONE || check_action_kind(types, value.type) == BUSTER_TYPE_KIND_VOID);
                if (step->op == CHECK_REGION_BRANCH || step->op == CHECK_REGION_CASE)
                {
                    bool truth = false;
                    bool known = false;
                    if (step->op == CHECK_REGION_CASE)
                    {
                        CheckValue alternative = check_region_value(values, step->operand);
                        check_action_convert_value(types, &alternative, value.type, region->node);
                        known = value.constant_kind == CHECK_CONSTANT_INTEGER && alternative.constant_kind == CHECK_CONSTANT_INTEGER &&
                                value.fault == CHECK_FAULT_NONE && alternative.fault == CHECK_FAULT_NONE;
                        truth = known && value.integer == alternative.integer;
                    }
                    else
                    {
                        known = check_action_truth(types, &value, &truth);
                    }
                    known &= !environment_unknown || (value.flags & BUSTER_CHECK_FP_ENVIRONMENT) == 0;
                    if (known)
                    {
                        next[0] = truth ? position + 1 : step->target;
                    }
                    else
                    {
                        result->constant = false;
                        next[1] = step->target;
                        if (!runtime)
                        {
                            break;
                        }
                    }
                }
            }
            if (runtime)
            {
                for (uint32_t i = 0; i < 2; i += 1)
                {
                    if (next[i] <= position)
                    {
                        // A reached loop backedge may not terminate. A constant result cannot elide the body.
                        result->effects.flags |= BUSTER_CHECK_SIDE_EFFECT;
                    }
                    if (next[i] < region->count && visited[next[i]] == 0)
                    {
                        visited[next[i]] = 1;
                        pending[count++] = next[i];
                    }
                }
            }
            else
            {
                position = next[0];
            }
        }
    }
    result->constant &= result->ended && result->diagnostic == CHECK_DIAG_NONE;
    if ((region->flags & CHECK_REGION_UNSUPPORTED) != 0)
    {
        result->effects.flags |= BUSTER_CHECK_SIDE_EFFECT;
    }
    if (scratch_peak != NULL && *scratch_peak < scratch->length)
    {
        *scratch_peak = scratch->length;
    }
    arena_pop_to(scratch, start);
    return ready;
}

static void check_graph_region_effects(TypeTable* types, const CheckGraph* graph, CheckStream* stream,
                                       uint32_t node, Arena* scratch, CheckActionResult* result)
{
    const CheckRegionProof* region = check_graph_region_proof(graph, node);
    if (region != NULL && result->diagnostic == CHECK_DIAG_NONE)
    {
        const CheckRegionValues values = { .values = stream->values, .stride = sizeof(CheckValue) };
        CheckRegionResult checked;
        if (!check_region_walk(types, graph, region, &values, true, true, scratch, &stream->statistics.scratch_bytes, &checked))
        {
            check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
        }
        else
        {
            check_action_diagnose(result, checked.diagnostic, 0);
            check_action_inherit(&result->value, &checked.effects);
        }
    }
}

#endif
