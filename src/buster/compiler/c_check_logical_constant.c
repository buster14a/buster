// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source-constant replay for logical-operand diagnostics (#2900). Runtime checking deliberately leaves const-object
// reads unresolved, and a runtime comma need not record that its discarded operand prevents constant evaluation.
// Replay only an actual warning candidate's dependencies, using the existing scalar contracts and source cuts.
// The pass owns this sparse memo in scratch; the graph, checked stream and their conversion records stay immutable.

typedef struct CheckLogicalConstantEntry CheckLogicalConstantEntry;
struct CheckLogicalConstantEntry
{
    uint32_t key;
    bool complete;
    CheckValue value;
};

typedef struct CheckLogicalConstants CheckLogicalConstants;
struct CheckLogicalConstants
{
    CheckLogicalConstantEntry* entries;
    uint32_t count;
    uint32_t capacity;
    CheckValue* arguments;
    CheckConversion* conversions;
    uint32_t argument_capacity;
};

typedef struct CheckLogicalConstantFrame CheckLogicalConstantFrame;
struct CheckLogicalConstantFrame
{
    uint32_t node;
    uint64_t next;
    const CheckRegionProof* region;
};

static CheckLogicalConstantEntry* check_warning_logical_constant_find(const CheckLogicalConstants* cache, uint32_t node)
{
    CheckLogicalConstantEntry* result = NULL;
    if (cache->capacity != 0)
    {
        const uint32_t key = node + 1;
        uint32_t slot = (node * UINT32_C(2654435761)) & (cache->capacity - 1);
        while (cache->entries[slot].key != 0 && cache->entries[slot].key != key)
        {
            slot = (slot + 1) & (cache->capacity - 1);
        }
        result = cache->entries[slot].key == key ? cache->entries + slot : NULL;
    }
    return result;
}

static bool check_warning_logical_constant_insert(CheckInputReader* reader, CheckLogicalConstants* cache, uint32_t node)
{
    bool valid = true;
    if (cache->count >= cache->capacity / 2)
    {
        const uint32_t capacity = cache->capacity == 0 ? 32 : cache->capacity <= UINT32_MAX / 2 ? cache->capacity * 2 : 0;
        CheckLogicalConstantEntry* entries = capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckLogicalConstantEntry, capacity) : NULL;
        valid = entries != NULL;
        if (valid)
        {
            memset(entries, 0, (uint64_t)capacity * sizeof(*entries));
            const uint32_t old_capacity = cache->capacity;
            const CheckLogicalConstantEntry* const old_entries = cache->entries;
            for (uint32_t i = 0; i < old_capacity; i += 1)
            {
                const uint32_t key = old_entries[i].key;
                if (key != 0)
                {
                    uint32_t slot = ((key - 1) * UINT32_C(2654435761)) & (capacity - 1);
                    while (entries[slot].key != 0)
                    {
                        slot = (slot + 1) & (capacity - 1);
                    }
                    entries[slot] = old_entries[i];
                }
            }
            cache->entries = entries;
            cache->capacity = capacity;
        }
    }
    if (valid)
    {
        uint32_t slot = (node * UINT32_C(2654435761)) & (cache->capacity - 1);
        while (cache->entries[slot].key != 0)
        {
            slot = (slot + 1) & (cache->capacity - 1);
        }
        cache->entries[slot].key = node + 1;
        cache->count += 1;
    }
    reader->output->resource |= !valid;
    return valid;
}

static const CheckX86Call* check_warning_logical_constant_library(const CheckGraph* graph, uint32_t node)
{
    uint32_t first = 0;
    uint32_t last = graph->x86_count;
    while (first < last)
    {
        const uint32_t middle = first + (last - first) / 2;
        if (graph->x86_calls[middle].node < node)
        {
            first = middle + 1;
        }
        else
        {
            last = middle;
        }
    }
    const CheckX86Call* result = first < graph->x86_count && graph->x86_calls[first].node == node ? graph->x86_calls + first : NULL;
    if (result != NULL)
    {
        const BuiltinIntrinsicInfo* info = result->info;
        const CBuiltinLibraryId id = cbuiltin_library_id(info->name, info->name_length);
        bool wide = false;
        if (id == CBUILTIN_LIBRARY_COUNT || cbuiltin_library_fold(id, &wide) == CBUILTIN_LIBRARY_FOLD_NONE)
        {
            result = NULL;
        }
    }
    return result;
}

// Runtime builtin folds may grant ICE to a merely known payload. Replay supported actions even then: a comma or
// statement prefix beneath that fold can prevent source evaluation. Literal VALUE leaves need no replay.
static bool check_warning_logical_constant_replay(const CheckGraph* graph, const CheckStream* stream, uint32_t node)
{
    const CheckOp op = graph->ops[node];
    const bool scalar = (op >= CHECK_OP_POSITIVE && op <= CHECK_OP_DEREFERENCE) || op == CHECK_OP_COMMA || op == CHECK_OP_SUBSCRIPT ||
                        op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT || op == CHECK_OP_CONDITION ||
                        op == CHECK_OP_SELECT || op == CHECK_OP_STATEMENT_VALUE || op == CHECK_OP_REAL || op == CHECK_OP_IMAGINARY;
    const bool builtin = op == CHECK_OP_BUILTIN_UNARY || op == CHECK_OP_BUILTIN_BIT_CAST;
    const bool library = (op == CHECK_OP_CALL && graph->members != NULL && (graph->members[node] & BUSTER_CHECK_LIBRARY_CALL) != 0) ||
                         (op == CHECK_OP_BUILTIN_X86 && check_warning_logical_constant_library(graph, node) != NULL);
    const bool result = stream->diagnostics[node] == CHECK_DIAG_NONE && (scalar || builtin || library);
    return result;
}

// An unresolved integer VALUE cannot change without an object receipt. A direct required operand of arithmetic
// therefore rejects common `a + b` candidates without allocating a memo or walking either subtree. Do not apply
// this to a known runtime payload (commas need their source proof), or infer it across pointer/address expressions:
// constant-mode address provenance can prove an integer expression even when the receipt table is empty.
static bool check_warning_logical_constant_unresolved(const CheckInputReader* reader, const CheckStream* stream, uint32_t node)
{
    const CheckGraph* graph = &reader->output->graph;
    const CheckConstValues* values = &reader->output->const_values;
    const CheckOp op = graph->ops[node];
    const bool required = (op >= CHECK_OP_POSITIVE && op <= CHECK_OP_BIT_XOR) || op == CHECK_OP_CAST ||
                          op == CHECK_OP_CONVERT || op == CHECK_OP_CONDITION;
    bool result = false;
    if (required && check_stream_constant_kind(stream, node) == CHECK_CONSTANT_NONE && values->count == 0 &&
        (values->prefix == NULL || values->prefix->count == 0))
    {
        const uint32_t arity = check_graph_arity(op, graph->types != NULL ? graph->types[node] : 0);
        const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        for (uint32_t i = 0; i < arity; i += 1)
        {
            const uint32_t child = operands[i];
            if (child < node && child < stream->count && graph->ops[child] == CHECK_OP_VALUE &&
                check_action_is_integer(reader->types, check_stream_type(stream, child)))
            {
                const CheckValue value = check_stream_value(stream, child);
                result |= value.constant_kind == CHECK_CONSTANT_NONE ||
                          (value.category == CHECK_LVALUE && (value.constant_kind == CHECK_CONSTANT_ADDRESS || (value.flags & BUSTER_CHECK_ICE) == 0));
            }
        }
    }
    return result;
}

static CheckValue check_warning_logical_constant_operand(const CheckLogicalConstants* cache, const CheckStream* stream, uint32_t node)
{
    const CheckLogicalConstantEntry* entry = check_warning_logical_constant_find(cache, node);
    const CheckValue result = entry != NULL && entry->complete ? entry->value : check_stream_value(stream, node);
    return result;
}

// A complex component's constant read may use the whole object's receipt. Taking that component's address instead
// needs its original designator, reconstructed from the same checked component action and replayed base.
static CheckValue check_warning_logical_constant_designator(CheckInputReader* reader, const CheckStream* stream, const CheckLogicalConstants* cache, uint32_t node)
{
    const CheckGraph* graph = &reader->output->graph;
    CheckValue result = check_warning_logical_constant_operand(cache, stream, node);
    if (result.category == CHECK_RVALUE && check_stream_category(stream, node) == CHECK_LVALUE)
    {
        uint32_t source = node;
        while (graph->operand0[source] < source)
        {
            const CheckOp op = graph->ops[source];
            const bool transparent = op == CHECK_OP_SELECT ||
                                     (op == CHECK_OP_REAL && check_action_kind(reader->types, check_stream_type(stream, graph->operand0[source])) != BUSTER_TYPE_KIND_COMPLEX);
            if (!transparent)
            {
                break;
            }
            source = graph->operand0[source];
        }
        if (graph->ops[source] == CHECK_OP_REAL || graph->ops[source] == CHECK_OP_IMAGINARY)
        {
            CheckAction action;
            check_graph_action(graph, stream, source, NULL, &action);
            action.operands[0] = check_warning_logical_constant_operand(cache, stream, graph->operand0[source]);
            CheckActionResult checked;
            check_action_scalar(reader->types, &action, &checked);
            reader->output->resource |= checked.diagnostic == CHECK_DIAG_RESOURCE;
            if (checked.diagnostic == CHECK_DIAG_NONE)
            {
                result = checked.value;
                result.type = check_stream_type(stream, node);
            }
        }
    }
    return result;
}

// Only already-checked actions are replayed. Their types and the pointer/composite forms their conversions need
// already exist in the same table; changing operand values neither reconstructs declarations nor creates a type view.
static CheckValue check_warning_logical_constant_action(CheckInputReader* reader, const CheckStream* stream, CheckLogicalConstants* cache,
                                                        uint32_t node, uint32_t completion)
{
    const CheckGraph* graph = &reader->output->graph;
    const CheckOp op = graph->ops[node];
    const uint32_t argument_count = check_graph_has_arguments(op) && graph->argument_counts != NULL ? graph->argument_counts[node] : 0;
    bool valid = true;
    if (argument_count > cache->argument_capacity)
    {
        uint32_t capacity = cache->argument_capacity != 0 ? cache->argument_capacity : 4;
        while (capacity < argument_count && capacity <= UINT32_MAX / 2)
        {
            capacity *= 2;
        }
        capacity = capacity < argument_count ? argument_count : capacity;
        CheckValue* arguments = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckValue, capacity);
        CheckConversion* conversions = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckConversion, capacity);
        valid = arguments != NULL && conversions != NULL;
        if (valid)
        {
            cache->arguments = arguments;
            cache->conversions = conversions;
            cache->argument_capacity = capacity;
        }
    }
    CheckValue result = check_stream_value(stream, node);
    if (valid)
    {
        CheckAction action;
        check_graph_action(graph, stream, node, cache->arguments, &action);
        const uint32_t arity = check_graph_arity(op, action.type);
        const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        for (uint32_t i = 0; i < arity; i += 1)
        {
            action.operands[i] = op == CHECK_OP_ADDRESS ? check_warning_logical_constant_designator(reader, stream, cache, operands[i]) :
                                                       check_warning_logical_constant_operand(cache, stream, operands[i]);
        }
        for (uint32_t i = 0; i < argument_count; i += 1)
        {
            cache->arguments[i] = check_warning_logical_constant_operand(cache, stream, graph->argument_nodes[graph->argument_offsets[node] + i]);
        }
        // The action may write argument conversions, so it never receives the checked stream's array for those writes.
        action.argument_conversions = cache->conversions;
        action.context.constant_evaluated = true;
        action.context.const_values = &reader->output->const_values;
        action.context.source_position = graph->source_positions != NULL && graph->source_positions[node] != 0 ? graph->source_positions[node] :
                                         graph->tokens != NULL && graph->tokens[node] < UINT32_MAX ? graph->tokens[node] + 1 : completion;
        const CheckRegionProof* region = op == CHECK_OP_STATEMENT_VALUE ? check_graph_region_proof(graph, node) : NULL;
        if (region != NULL)
        {
            action.member = 0;
        }
        if ((op == CHECK_OP_REAL || op == CHECK_OP_IMAGINARY) && action.operands[0].category == CHECK_LVALUE &&
            check_action_kind(reader->types, action.operands[0].type) == BUSTER_TYPE_KIND_COMPLEX)
        {
            CheckActionResult converted = { 0 };
            const CheckValue value = check_action_operand(reader->types, &action, &converted, 0);
            reader->output->resource |= converted.diagnostic == CHECK_DIAG_RESOURCE;
            if (converted.diagnostic == CHECK_DIAG_NONE && value.constant_kind == CHECK_CONSTANT_COMPLEX)
            {
                action.operands[0] = value;
            }
        }
        CheckActionResult checked;
        if (op == CHECK_OP_BUILTIN_UNARY)
        {
            const CheckBuiltinAction builtin = {
                .id = (CBuiltinId)action.member,
                .operands = { action.operands[0] },
                .argument_count = 1,
                .node = node,
                .context = action.context,
            };
            CheckBuiltinResult folded;
            check_builtins(reader->types, &builtin, &folded, 1, true);
            checked = folded.action;
        }
        else if (op == CHECK_OP_BUILTIN_X86)
        {
            const CheckX86Call* call = check_warning_logical_constant_library(graph, node);
            CheckBuiltinX86Result folded;
            check_x86_action(reader->types, call, &action, &folded);
            checked = folded.action;
        }
        else if (op == CHECK_OP_BUILTIN_BIT_CAST)
        {
            const CheckBuiltinCall* witness = check_graph_builtin_call(graph, node);
            CheckAction operand = action;
            operand.operands[0] = argument_count == 1 ? cache->arguments[0] : (CheckValue){ 0 };
            CheckActionResult converted = { 0 };
            CheckValue value = argument_count == 1 ? check_action_operand(reader->types, &operand, &converted, 0) : (CheckValue){ 0 };
            const bool constant = argument_count == 1 && converted.diagnostic == CHECK_DIAG_NONE && check_value_evaluated_constant(&value) &&
                                  value.constant_kind == CHECK_CONSTANT_INTEGER && (value.flags & BUSTER_CHECK_BIT_FIELD) == 0;
            const CheckBuiltinBitCastAction builtin = {
                .id = (CBuiltinId)action.member,
                .destination_type = action.type,
                .arguments = constant ? &value : action.arguments,
                .argument_count = argument_count,
                .node = node,
                .context = action.context,
                .source_proof = constant ? CHECK_BUILTIN_BIT_CAST_PROOF_CONSTANT : witness != NULL ? witness->bit_cast_proof : CHECK_BUILTIN_BIT_CAST_PROOF_NONE,
                .source_bytes = witness != NULL ? witness->bit_cast_bytes : NULL,
                .source_byte_count = witness != NULL ? witness->bit_cast_byte_count : 0,
            };
            CheckBuiltinBitCastResult folded;
            check_builtin_bit_cast(reader->types, &builtin, &folded, 1);
            checked = folded.action;
            check_action_diagnose(&checked, converted.diagnostic, 0);
            if (argument_count == 1)
            {
                check_action_inherit(&checked.value, &value);
            }
        }
        else
        {
            check_action_scalar(reader->types, &action, &checked);
            if (op == CHECK_OP_CALL)
            {
                check_builtin_library_fold_plain(reader->types, action.member, &action, &checked);
            }
        }
        if (region != NULL && checked.diagnostic == CHECK_DIAG_NONE)
        {
            const CheckRegionValues values = {
                .stream = stream,
                .values = &cache->entries[0].value,
                .stride = sizeof(*cache->entries),
                .keys = &cache->entries[0].key,
                .capacity = cache->capacity,
                .const_values = &reader->output->const_values,
                .constant_evaluated = true,
            };
            CheckRegionResult prefix;
            if (!check_region_walk(reader->types, graph, region, &values, false, false, reader->scratch, NULL, &prefix))
            {
                check_action_diagnose(&checked, CHECK_DIAG_RESOURCE, 0);
            }
            else
            {
                check_action_diagnose(&checked, prefix.diagnostic, 0);
                check_action_inherit(&checked.value, &prefix.effects);
                checked.value.flags |= prefix.constant ? 0 : BUSTER_CHECK_DISCARDED_UNEVALUABLE;
            }
        }
        reader->output->resource |= checked.diagnostic == CHECK_DIAG_RESOURCE;
        if (checked.diagnostic == CHECK_DIAG_NONE)
        {
            result = checked.value;
        }
        else
        {
            result.constant_kind = CHECK_CONSTANT_NONE;
        }
    }
    reader->output->resource |= !valid;
    return result;
}

// Ordinary object operands retain their designator for the warning's final lvalue conversion. A proved complex
// component may already be a value; address consumers recover its designator above. Each graph dependency is
// memoized once for this immutable graph/stream pair.
static CheckValue check_warning_logical_constant(CheckInputReader* reader, const CheckStream* stream, uint32_t node, uint32_t completion,
                                                 CheckLogicalConstants* cache)
{
    const CheckGraph* graph = &reader->output->graph;
    const uint32_t count = graph->count;
    CheckValue result = node < count && node < stream->count ? check_stream_value(stream, node) : (CheckValue){ 0 };
    const TypeId original_type = result.type;
    const bool replay = !reader->output->resource && node < count && node < stream->count &&
                        check_warning_logical_constant_replay(graph, stream, node) && !check_warning_logical_constant_unresolved(reader, stream, node);
    CheckLogicalConstantEntry* entry = replay ? check_warning_logical_constant_find(cache, node) : NULL;
    if (replay && entry == NULL && check_warning_logical_constant_insert(reader, cache, node))
    {
        CheckLogicalConstantFrame local[16];
        CheckLogicalConstantFrame* work = local;
        uint32_t capacity = BUSTER_ARRAY_LENGTH(local);
        uint32_t depth = 1;
        work[0] = (CheckLogicalConstantFrame){ .node = node, .region = graph->ops[node] == CHECK_OP_STATEMENT_VALUE ? check_graph_region_proof(graph, node) : NULL };
        while (depth != 0 && !reader->output->resource)
        {
            const uint32_t current = work[depth - 1].node;
            const CheckOp op = graph->ops[current];
            const TypeId type = graph->types != NULL ? graph->types[current] : 0;
            const uint32_t arity = check_graph_arity(op, type);
            const uint32_t arguments = check_graph_has_arguments(op) && graph->argument_counts != NULL ? graph->argument_counts[current] : 0;
            const CheckRegionProof* region = work[depth - 1].region;
            const uint64_t expression_count = (uint64_t)arity + arguments;
            const uint64_t dependency_count = expression_count + (region != NULL ? (uint64_t)region->count * 2 : 0);
            const uint64_t next = work[depth - 1].next;
            if (next < dependency_count)
            {
                const uint32_t operands[] = { graph->operand0[current], graph->operand1[current], graph->operand2[current] };
                uint32_t child = BUSTER_CHECK_NONE;
                if (next < expression_count)
                {
                    child = next < arity ? operands[next] : graph->argument_nodes[(uint64_t)graph->argument_offsets[current] + next - arity];
                }
                else
                {
                    const uint64_t offset = next - expression_count;
                    const CheckRegionProofStep* step = graph->region_steps + region->first + offset / 2;
                    child = (offset & 1) == 0 ? step->node : step->operand;
                }
                work[depth - 1].next += 1;
                // Checked operand edges are postorder; payload slots outside arity are never read as dependencies.
                if (child != BUSTER_CHECK_NONE && (child >= current || child >= stream->count))
                {
                    reader->output->resource = true;
                }
                else if (child != BUSTER_CHECK_NONE && check_warning_logical_constant_replay(graph, stream, child) && !check_warning_logical_constant_unresolved(reader, stream, child) &&
                         check_warning_logical_constant_find(cache, child) == NULL &&
                         check_warning_logical_constant_insert(reader, cache, child))
                {
                    if (depth == capacity)
                    {
                        const uint32_t grown_capacity = capacity <= UINT32_MAX / 2 ? capacity * 2 : 0;
                        CheckLogicalConstantFrame* grown = grown_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckLogicalConstantFrame, grown_capacity) : NULL;
                        reader->output->resource |= grown == NULL;
                        if (grown != NULL)
                        {
                            memcpy(grown, work, (uint64_t)depth * sizeof(*work));
                            work = grown;
                            capacity = grown_capacity;
                        }
                    }
                    if (!reader->output->resource)
                    {
                        work[depth++] = (CheckLogicalConstantFrame){ .node = child, .region = graph->ops[child] == CHECK_OP_STATEMENT_VALUE ? check_graph_region_proof(graph, child) : NULL };
                    }
                }
            }
            else
            {
                const CheckValue value = check_warning_logical_constant_action(reader, stream, cache, current, completion);
                // Dependencies may have grown the hash table; reacquire the entry after every such operation.
                CheckLogicalConstantEntry* finished = check_warning_logical_constant_find(cache, current);
                finished->value = value;
                finished->complete = true;
                depth -= 1;
            }
        }
        entry = check_warning_logical_constant_find(cache, node);
    }
    if (entry != NULL && entry->complete && !reader->output->resource)
    {
        result = entry->value;
    }
    // The warning's source-type exclusions (in particular atomic operands) use the original expression type.
    result.type = original_type;
    if (reader->output->resource)
    {
        result.constant_kind = CHECK_CONSTANT_NONE;
    }
    return result;
}
