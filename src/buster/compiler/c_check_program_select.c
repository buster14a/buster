// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Checked #133 definitions/references -> immutable #129 reachability planner -> emission views. Include after
// CheckProgram and before its machine consumers. All ordinary-mode bodies have already been checked; optional
// #155 mode has checked the independently demanded subset in original source order. Both modes use this exact
// emission policy. A call/address edge is the checked expression walk's evaluated reference, not identifier text.
// Static initializer edges are the final live writes retained by CheckDataPrepared, including strings and cycles.
//
// Definition IDs interleave code and data in original definition order. Canonical objects join redeclarations;
// prototype declarations are not graph definitions. C99 inline-only bodies have an external symbol but no local
// emission/dependencies. GNU inline uses the retained declaration spelling and gnu_inline attribute. There is no
// inliner here; any future inliner must attribute its retained dependencies to the emitted caller before this pass.
// A body also selects its own block-scope statics, referenced or not, as clang emits every static local of a function
// it emits, even at -O0 (#129's comparison with clang's objects, c_lowering_symbols.c).
//
// check_program_select_mode with C_LOWERING_ALL selects every definition that may be emitted, referenced or not (an
// inline-only one is never emitted): the driver's --lower-all, #129's test option for what the selection saves.
//
// The result borrows the full typed graph/control stream and immutable initializer plans. It owns compact body,
// reference, data and builtin views plus a deterministic object-ID-ordered symbol map. Source inputs outlive it;
// output and scratch are distinct, no padding is required, and failure restores both marks with no partial view.

#ifndef BUSTER_CHECK_PROGRAM_SELECT_INCLUDED
#define BUSTER_CHECK_PROGRAM_SELECT_INCLUDED 1

#include "buster/compiler/c_lowering.c"

typedef struct CheckProgramSelection CheckProgramSelection;
struct CheckProgramSelection
{
    CheckProgram program;
    CheckDataPrepared data;
    CLoweringStatistics statistics;
    uint32_t defined_functions;
    uint32_t selected_functions;
    uint32_t defined_objects;
    uint32_t selected_objects;
    uint32_t token;
    CheckProgramStatus status;
};

static uint32_t check_program_attribute_roots(const CheckInputAttributes* attributes, bool function, bool* gnu_inline)
{
    uint32_t flags = 0;
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        if (!attribute->ignored)
        {
            flags |= attribute->kind == CHECK_ATTRIBUTE_USED || attribute->kind == CHECK_ATTRIBUTE_RETAIN ? BUSTER_C_LOWERING_USED : 0;
            flags |= function && attribute->kind == CHECK_ATTRIBUTE_CONSTRUCTOR ? BUSTER_C_LOWERING_CONSTRUCTOR : 0;
            flags |= function && attribute->kind == CHECK_ATTRIBUTE_DESTRUCTOR ? BUSTER_C_LOWERING_DESTRUCTOR : 0;
            *gnu_inline |= attribute->kind == CHECK_ATTRIBUTE_GNU_INLINE;
        }
    }
    return flags;
}

static bool check_program_select_mode(CheckProgramSelection* result, const CheckProgram* full, const CheckDataPrepared* data, CLoweringMode mode,
                                      bool scalar, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    const uint64_t temporary = scratch->length;
    CheckProgramSelection candidate = { .token = BUSTER_CHECK_NONE, .status = CHECK_PROGRAM_INVALID };
    const CheckInput* input = full->input;
    const uint32_t objects = full->object_count;
    const uint64_t definitions64 = (uint64_t)full->body_count + data->count;
    // A reference of each body and each object, and an edge from a body to each of its block-scope statics.
    const uint64_t references64 = (uint64_t)full->reference_count + data->reference_count + data->count;
    bool valid = output != scratch && full->status == CHECK_PROGRAM_OK && data->status == CHECK_DATA_INPUT_OK && input != 0 &&
                 objects == data->object_count && objects == (uint64_t)input->token_count + 1 && definitions64 < UINT32_MAX && references64 < UINT32_MAX;
    const uint32_t definitions = valid ? (uint32_t)definitions64 : 0;
    const uint32_t reference_capacity = valid ? (uint32_t)references64 : 0;
    uint64_t* order = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)definitions + 1) : 0;
    uint64_t* sorting = order != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)definitions + 1) : 0;
    uint32_t* definition_objects = sorting != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)definitions + 1) : 0;
    uint32_t* definition_flags = definition_objects != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)definitions + 1) : 0;
    uint32_t* object_definitions = definition_flags != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, objects) : 0;
    uint32_t* roots = object_definitions != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, objects) : 0;
    uint8_t* inline_rules = roots != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, objects) : 0;
    uint8_t* needed = inline_rules != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, objects) : 0;
    uint8_t* body_selected = needed != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)full->body_count + 1) : 0;
    uint8_t* data_selected = body_selected != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)data->count + 1) : 0;
    uint32_t* body_map = data_selected != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)full->body_count + 1) : 0;
    uint32_t* token_bodies = body_map != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, objects) : 0;
    uint32_t* sources = token_bodies != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)reference_capacity + 1) : 0;
    uint32_t* targets = sources != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)reference_capacity + 1) : 0;
    uint32_t* kinds = targets != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)reference_capacity + 1) : 0;
    if (valid)
    {
        candidate.status = CHECK_PROGRAM_RESOURCE;
    }
    valid &= kinds != 0;
    uint32_t edges = 0;
    uint32_t body_references = 0;
    uint32_t data_references = 0;
    CLowering plan = { 0 };
    if (valid)
    {
        memset(object_definitions, 0, (uint64_t)objects * sizeof(uint32_t));
        memset(roots, 0, (uint64_t)objects * sizeof(uint32_t));
        memset(inline_rules, 1, objects);
        memset(needed, 0, objects);
        memset(body_selected, 0, (uint64_t)full->body_count + 1);
        memset(data_selected, 0, (uint64_t)data->count + 1);
        memset(body_map, 0xff, ((uint64_t)full->body_count + 1) * sizeof(uint32_t));
        memset(token_bodies, 0, (uint64_t)objects * sizeof(uint32_t));
        for (uint32_t token = 0; token < input->token_count; token += 1)
        {
            const uint32_t object = input->objects[token];
            if (input->declarations[token] == token && input->static_objects[token] && object != 0 && object < objects)
            {
                const bool function = input->declaration_kinds[token] == C_BINDING_FUNCTION;
                bool gnu_inline = false;
                roots[object] |= check_program_attribute_roots(input->attributes[token], function, &gnu_inline);
                inline_rules[object] |= gnu_inline ? 2 : 0;
                if (function && input->declaration_bodies[token] == BUSTER_CHECK_NONE)
                {
                    const uint8_t spelling = input->function_flags != 0 ? input->function_flags[token] : 0;
                    if ((spelling & CHECK_INPUT_FUNCTION_INLINE) == 0 || (spelling & CHECK_INPUT_FUNCTION_EXTERN) != 0)
                    {
                        inline_rules[object] &= (uint8_t)~1u;
                    }
                }
            }
        }
        for (uint32_t body = 0; body < full->body_count; body += 1)
        {
            const CheckProgramBody* function = full->bodies + body;
            order[body] = ((uint64_t)function->declaration << 32) | body;
            const uint32_t close = full->parser->tree.parents[function->opening];
            for (uint32_t token = function->opening; token <= close && token < input->token_count; token += 1)
            {
                token_bodies[token] = body + 1;
            }
        }
        for (uint32_t object = 0; object < data->count; object += 1)
        {
            order[full->body_count + object] = ((uint64_t)data->objects[object].token << 32) | (full->body_count + object);
        }
        order = c_bindings_sort(order, sorting, definitions, !scalar, UINT32_MAX);
        for (uint32_t definition = 0; valid && definition < definitions; definition += 1)
        {
            const uint32_t raw = (uint32_t)order[definition];
            const bool function = raw < full->body_count;
            const uint32_t object = function ? full->bodies[raw].object : data->objects[raw - full->body_count].object;
            valid = object != 0 && object < objects && object_definitions[object] == 0;
            if (valid)
            {
                object_definitions[object] = definition + 1;
                definition_objects[definition] = object;
                uint32_t flags = (function ? BUSTER_C_LOWERING_FUNCTION : BUSTER_C_LOWERING_OBJECT) | roots[object];
                const uint32_t declaration = full->object_declarations[object];
                if (declaration != BUSTER_CHECK_NONE && input->linkage[declaration] == 2)
                {
                    bool inline_only = false;
                    if (function)
                    {
                        const uint32_t body_declaration = full->bodies[raw].declaration;
                        const uint8_t spelling = input->function_flags != 0 ? input->function_flags[body_declaration] : 0;
                        inline_only = (inline_rules[object] & 2) != 0 && (spelling & CHECK_INPUT_FUNCTION_INLINE) != 0 ?
                                      (spelling & CHECK_INPUT_FUNCTION_EXTERN) != 0 : (inline_rules[object] & 1) != 0;
                    }
                    flags |= inline_only ? BUSTER_C_LOWERING_INLINE_ONLY : BUSTER_C_LOWERING_EXTERNAL;
                }
                definition_flags[definition] = flags;
            }
        }
        candidate.status = valid ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_INVALID;
        for (uint32_t definition = 0; valid && definition < definitions; definition += 1)
        {
            const uint32_t raw = (uint32_t)order[definition];
            const bool function = raw < full->body_count;
            const uint32_t first = function ? full->bodies[raw].reference_first : data->reference_offsets[raw - full->body_count];
            const uint32_t count = function ? full->bodies[raw].reference_count : data->reference_counts[raw - full->body_count];
            const uint32_t* references = function ? full->references : data->references;
            const uint32_t limit = function ? full->reference_count : data->reference_count;
            valid = first <= limit && count <= limit - first;
            for (uint32_t reference = 0; valid && reference < count; reference += 1)
            {
                const uint32_t object = references[first + reference];
                valid = object != 0 && object < objects && edges < reference_capacity;
                if (valid)
                {
                    const uint32_t target = object_definitions[object];
                    sources[edges] = definition;
                    targets[edges] = target != 0 ? target - 1 : BUSTER_C_LOWERING_NONE;
                    kinds[edges++] = target != 0 ? C_LOWERING_REFERENCE_EVALUATED : C_LOWERING_REFERENCE_EXTERNAL;
                }
            }
            // A block-scope static (declared in a body, without linkage, not a compound literal), from its body.
            const uint32_t object = function ? 0 : data->objects[raw - full->body_count].object;
            const uint32_t declaration = object != 0 ? full->object_declarations[object] : BUSTER_CHECK_NONE;
            const uint32_t body = declaration < input->token_count && input->declaration_bodies[declaration] != BUSTER_CHECK_NONE &&
                                  input->linkage[declaration] == 0 && !input->compound_objects[declaration] ? token_bodies[declaration] : 0;
            if (valid && body != 0)
            {
                const uint32_t source = object_definitions[full->bodies[body - 1].object];
                valid = source != 0 && edges < reference_capacity;
                if (valid)
                {
                    sources[edges] = source - 1;
                    targets[edges] = definition;
                    kinds[edges++] = C_LOWERING_REFERENCE_EVALUATED;
                }
            }
        }
        const CLoweringInput graph = { .definition_count = definitions, .reference_count = edges, .definition_flags = definition_flags,
                                      .reference_source = sources, .reference_target = targets, .reference_kind = kinds };
        candidate.status = valid ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_INVALID;
        valid = valid && c_lowering_prepare(scratch, graph, &plan) == C_LOWERING_OK;
        valid = valid && (scalar ? c_lowering_scalar(&plan, mode) : c_lowering_vector(&plan, mode)) == C_LOWERING_OK;
        if (valid)
        {
            for (uint32_t selected = 0; selected < plan.selected_count; selected += 1)
            {
                const uint32_t definition = plan.selected[selected];
                const uint32_t raw = (uint32_t)order[definition];
                needed[definition_objects[definition]] = 1;
                if (raw < full->body_count)
                {
                    body_selected[raw] = 1;
                    candidate.selected_functions += 1;
                    body_references += full->bodies[raw].reference_count;
                }
                else
                {
                    data_selected[raw - full->body_count] = 1;
                    candidate.selected_objects += 1;
                    data_references += data->reference_counts[raw - full->body_count];
                }
            }
        }
    }
    if (valid)
    {
        candidate.program = *full;
        candidate.data = *data;
        candidate.program.body_count = candidate.selected_functions;
        candidate.program.reference_count = body_references;
        candidate.program.builtin_count = 0;
        candidate.program.symbol_count = 0;
        candidate.data.count = candidate.selected_objects;
        candidate.data.reference_count = data_references;
        candidate.program.bodies = candidate.selected_functions != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramBody, candidate.selected_functions) : 0;
        candidate.program.references = body_references != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, body_references) : 0;
        candidate.program.regions = full->region_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramRegion, full->region_count) : 0;
        candidate.data.objects = candidate.selected_objects != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckDataObject, candidate.selected_objects) : 0;
        candidate.data.reference_offsets = candidate.selected_objects != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, candidate.selected_objects) : 0;
        candidate.data.reference_counts = candidate.selected_objects != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, candidate.selected_objects) : 0;
        candidate.data.references = data_references != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, data_references) : 0;
        candidate.program.object_symbols = BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, objects);
        valid = (candidate.selected_functions == 0 || candidate.program.bodies != 0) && (body_references == 0 || candidate.program.references != 0) &&
                (full->region_count == 0 || candidate.program.regions != 0) && candidate.program.object_symbols != 0 &&
                (candidate.selected_objects == 0 || (candidate.data.objects != 0 && candidate.data.reference_offsets != 0 && candidate.data.reference_counts != 0)) &&
                (data_references == 0 || candidate.data.references != 0);
    }
    if (valid)
    {
        uint32_t bodies = 0;
        uint32_t references = 0;
        for (uint32_t body = 0; body < full->body_count; body += 1)
        {
            if (body_selected[body])
            {
                const CheckProgramBody* source = full->bodies + body;
                body_map[body] = bodies;
                candidate.program.bodies[bodies] = *source;
                candidate.program.bodies[bodies++].reference_first = references;
                for (uint32_t r = 0; r < source->reference_count; r += 1)
                {
                    const uint32_t object = full->references[source->reference_first + r];
                    candidate.program.references[references++] = object;
                    needed[object] = 1;
                }
            }
        }
        for (uint32_t region = 0; region < full->region_count; region += 1)
        {
            candidate.program.regions[region] = full->regions[region];
            const uint32_t body = full->regions[region].body;
            candidate.program.regions[region].body = body < full->body_count ? body_map[body] : BUSTER_CHECK_NONE;
        }
        uint32_t written = 0;
        references = 0;
        for (uint32_t record = 0; record < data->count; record += 1)
        {
            if (data_selected[record])
            {
                candidate.data.objects[written] = data->objects[record];
                candidate.data.reference_offsets[written] = references;
                candidate.data.reference_counts[written++] = data->reference_counts[record];
                for (uint32_t r = 0; r < data->reference_counts[record]; r += 1)
                {
                    const uint32_t object = data->references[data->reference_offsets[record] + r];
                    candidate.data.references[references++] = object;
                    needed[object] = 1;
                }
            }
        }
        for (uint32_t builtin = 0; builtin < full->builtin_count; builtin += 1)
        {
            const uint32_t node = full->builtin_nodes[builtin];
            const uint32_t body = token_bodies[input->graph.tokens[node]];
            candidate.program.builtin_count += body != 0 && body_selected[body - 1];
        }
        candidate.program.builtin_nodes = candidate.program.builtin_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, candidate.program.builtin_count) : 0;
        valid = candidate.program.builtin_count == 0 || candidate.program.builtin_nodes != 0;
        uint32_t builtins = 0;
        for (uint32_t builtin = 0; valid && builtin < full->builtin_count; builtin += 1)
        {
            const uint32_t node = full->builtin_nodes[builtin];
            const uint32_t body = token_bodies[input->graph.tokens[node]];
            if (body != 0 && body_selected[body - 1])
            {
                candidate.program.builtin_nodes[builtins++] = node;
            }
        }
        memset(candidate.program.object_symbols, 0, (uint64_t)objects * sizeof(SymbolId));
        for (uint32_t object = 1; object < objects; object += 1)
        {
            if (needed[object])
            {
                candidate.program.object_symbols[object] = ++candidate.program.symbol_count;
            }
        }
        candidate.program.symbol_objects = candidate.program.symbol_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, candidate.program.symbol_count) : 0;
        valid = valid && (candidate.program.symbol_count == 0 || candidate.program.symbol_objects != 0);
        for (uint32_t object = 1; valid && object < objects; object += 1)
        {
            const SymbolId symbol = candidate.program.object_symbols[object];
            if (symbol != 0)
            {
                candidate.program.symbol_objects[symbol - 1] = object;
            }
        }
    }
    if (valid)
    {
        candidate.defined_functions = full->body_count;
        candidate.defined_objects = data->count;
        candidate.statistics = plan.statistics;
        candidate.status = CHECK_PROGRAM_OK;
        candidate.program.output_bytes = output->length - mark;
        candidate.data.capacity.atoms = candidate.data.count;
        candidate.data.capacity.fixups = candidate.data.reference_count;
        candidate.data.capacity.wide_addends = candidate.data.reference_count;
        *result = candidate;
    }
    else
    {
        arena_pop_to(output, mark);
        *result = (CheckProgramSelection){ .status = candidate.status, .token = candidate.token };
    }
    arena_pop_to(scratch, temporary);
    return valid;
}

// The definitions reachable from the unit's roots: what -c and the direct path lower.
static bool check_program_select(CheckProgramSelection* result, const CheckProgram* full, const CheckDataPrepared* data, bool scalar,
                                 Arena* output, Arena* scratch)
{
    return check_program_select_mode(result, full, data, C_LOWERING_REACHABLE, scalar, output, scratch);
}

#endif
