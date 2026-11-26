// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Late transparent-union attributes (#1844). Clang keeps the first ABI arrangement of a complete function
// signature, including a variadic call's promoted tail. The emission walk supplies the source completion points;
// this immutable, sparse directory supplies the same answer to every lowering lane. ABI-only ordinary-union rows
// preserve the pre-attribute convention without changing language types or the calling convention's hot kernels.

typedef struct CheckLowerTransparentUse CheckLowerTransparentUse;
#define BUSTER_CHECK_LOWER_SIGNATURE_DEFINITION (UINT32_MAX - 1u)
struct CheckLowerTransparentUse
{
    TypeId function;
    uint32_t node;
    uint32_t point;
    bool chain;
};

struct CheckLowerTransparent
{
    CheckLowerTransparentUse* uses;
    uint32_t* keys;
    TypeId* ordinary;
    uint8_t* chain_calls;
    uint32_t count;
    uint32_t first;
    bool unmodeled_attributes;
};

static uint32_t check_lower_transparent_rows(const CheckProgram* program)
{
    const TypeTable* types = program->types;
    const uint32_t count = check_program_abi_late(program, NULL) ? types->types.count : 0;
    uint32_t rows = 0;
    for (uint32_t i = 0; i < count; i += 64)
    {
        const uint32_t left = count - i;
        const __mmask64 active = left >= 64 ? UINT64_MAX : (UINT64_C(1) << left) - 1;
        const __m512i kinds = _mm512_maskz_loadu_epi8(active, types->types.kind + i);
        const __m512i flags = _mm512_maskz_loadu_epi8(active, types->types.flags + i);
        const __mmask64 unions = active & _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPE_KIND_UNION)) &
                                 _mm512_test_epi8_mask(flags, _mm512_set1_epi8((char)BUSTER_TYPE_FLAG_TRANSPARENT_UNION));
        rows += (uint32_t)__builtin_popcountll(unions);
    }
    return rows;
}

static TypeId check_lower_transparent_function(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    TypeId type = 0;
    if (node < graph->count)
    {
        const uint32_t callee = graph->ops[node] == CHECK_OP_CALL ? graph->operand0[node] : node;
        if (callee < graph->count)
        {
            const CheckValue value = check_stream_value(program->stream, callee);
            type = graph->ops[node] == CHECK_OP_CALL || (value.category == CHECK_FUNCTION && value.constant_kind == CHECK_CONSTANT_ADDRESS) ? value.type : 0;
            type = type_strip_aligned(program->types, type);
            if (type_kind(program->types, type) == BUSTER_TYPE_KIND_POINTER)
            {
                type = type_strip_aligned(program->types, type_element(program->types, type));
            }
            type = type_kind(program->types, type) == BUSTER_TYPE_KIND_FUNCTION ? type : 0;
        }
        if (graph->ops[node] == CHECK_OP_CALL)
        {
            const uint32_t overload = check_program_builtin_index(program->stream->overloads, program->stream->overload_count, sizeof(CheckOverloadEffect), node);
            type = overload != BUSTER_CHECK_NONE ? program->stream->overloads[overload].selected.type : type;
        }
        else if (program->abi_emission_types != NULL && program->abi_emission_types[node] != 0)
        {
            type = program->abi_emission_types[node];
        }
    }
    return type;
}

static uint32_t check_lower_transparent_arguments(const CheckProgram* program, CheckLowerTransparentUse use, uint32_t* required)
{
    const TypeTable* types = program->types;
    uint32_t parameters = 0;
    (void)type_parameters(types, use.function, &parameters);
    const uint8_t flags = types->types.flags[type_index(use.function)];
    const bool prototype = (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
    const bool variadic = (flags & BUSTER_TYPE_FLAG_VARIADIC) != 0;
    const bool call = use.node < program->input->graph.count;
    const bool definition = use.node == BUSTER_CHECK_LOWER_SIGNATURE_DEFINITION;
    const uint32_t count = call && (variadic || !prototype) ? program->input->graph.argument_counts[use.node] : prototype || definition ? parameters : 0;
    *required = variadic ? parameters : prototype || use.node == BUSTER_CHECK_LOWER_SIGNATURE_DEFINITION ? UINT32_MAX : call ? count : 0;
    return count;
}

static TypeId check_lower_transparent_argument_type(const CheckProgram* program, CheckLowerTransparentUse use, uint32_t argument)
{
    uint32_t parameters = 0;
    const TypeId* declared = type_parameters(program->types, use.function, &parameters);
    const CheckGraph* graph = &program->input->graph;
    const bool prototype = (program->types->types.flags[type_index(use.function)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
    const bool definition = use.node == BUSTER_CHECK_LOWER_SIGNATURE_DEFINITION;
    TypeId type = argument < parameters && (prototype || definition) ? declared[argument] :
                        program->stream->argument_conversions[graph->argument_offsets[use.node] + argument].type;
    type = definition && !prototype ? check_input_kr_parameter_type(program->types, type) : type;
    return type_unqualified(type_strip_aligned(program->types, type));
}

static uint32_t check_lower_transparent_key(const CheckProgram* program, CheckLowerTransparentUse use)
{
    uint32_t required = 0;
    const uint32_t count = check_lower_transparent_arguments(program, use, &required);
    const uint32_t abi = type_function_abi_kind(program->types, use.function);
    return count * UINT32_C(0x9e3779b9) ^ required * UINT32_C(0x85ebca6b) ^ abi ^ (use.chain ? UINT32_C(0xc2b2ae35) : 0);
}

static TypeAnswer check_lower_transparent_same(const CheckProgram* program, CheckLowerTransparentUse a, CheckLowerTransparentUse b)
{
    const TypeTable* types = program->types;
    uint32_t a_required = 0;
    uint32_t b_required = 0;
    const uint32_t a_count = check_lower_transparent_arguments(program, a, &a_required);
    const uint32_t b_count = check_lower_transparent_arguments(program, b, &b_required);
    const TypeId a_result = type_unqualified(type_return(types, a.function));
    const TypeId b_result = type_unqualified(type_return(types, b.function));
    bool same = a.chain == b.chain && a_count == b_count && a_required == b_required &&
                type_function_abi_kind(types, a.function) == type_function_abi_kind(types, b.function) &&
                ((types->types.flags[type_index(a.function)] ^ types->types.flags[type_index(b.function)]) & BUSTER_TYPE_FLAG_NORETURN) == 0;
    TypeAnswer answer = same && a_result != b_result ? type_same_canonical(types, a_result, b_result) : same ? TYPE_ANSWER_YES : TYPE_ANSWER_NO;
    same = answer == TYPE_ANSWER_YES;
    for (uint32_t i = 0; same && i < a_count; i += 1)
    {
        const TypeId left = check_lower_transparent_argument_type(program, a, i);
        const TypeId right = check_lower_transparent_argument_type(program, b, i);
        same = type_parameter_abi(types, a.function, i) == type_parameter_abi(types, b.function, i);
        answer = same && left != right ? type_same_canonical(types, left, right) : same ? TYPE_ANSWER_YES : TYPE_ANSWER_NO;
        same = answer == TYPE_ANSWER_YES;
    }
    return answer;
}

static void check_lower_transparent_use(CheckLowerTransparent* directory, const CheckProgram* program, TypeId function, uint32_t node, uint32_t point)
{
    if (function != 0 && point != BUSTER_CHECK_NONE)
    {
        directory->first = point < directory->first ? point : directory->first;
        const CheckLowerTransparentUse use = {
            .function = type_strip_aligned(program->types, function),
            .node = node,
            .point = point,
            .chain = directory->chain_calls != NULL && node < program->input->graph.count && directory->chain_calls[node] != 0,
        };
        uint32_t required = 0;
        const uint32_t count = check_lower_transparent_arguments(program, use, &required);
        bool relevant = false;
        for (uint32_t i = 0; !relevant && i < count; i += 1)
        {
            relevant = type_is_transparent_union(program->types, check_lower_transparent_argument_type(program, use, i));
        }
        if (relevant)
        {
            directory->uses[directory->count] = use;
            directory->keys[directory->count++] = check_lower_transparent_key(program, use);
        }
    }
}

// A static-chain call has a separate clang CGFunctionInfo key. Its fixed hidden pointer and nest parameter are
// implicit here: the chain bit separates it from every ordinary signature, and the remaining arguments distinguish
// chain calls from one another. Find the call nodes once, sixteen graph operators at a time; no call scans parents.
static bool check_lower_transparent_chains(CheckLowerTransparent* directory, const CheckProgram* program, Arena* arena)
{
    const CheckGraph* graph = &program->input->graph;
    const uint32_t count = program->builtin_count != 0 ? graph->count : 0;
    bool valid = true;
    for (uint32_t n = 0; valid && n < count; n += 16)
    {
        const uint32_t left = count - n;
        const __mmask16 active = left >= 16 ? UINT16_MAX : (__mmask16)((1u << left) - 1);
        const __mmask16 misc = active & _mm512_cmpeq_epi32_mask(_mm512_maskz_loadu_epi32(active, graph->ops + n), _mm512_set1_epi32(CHECK_OP_BUILTIN_MISC));
        uint32_t chains = misc & _mm512_cmpeq_epi32_mask(_mm512_maskz_loadu_epi32(misc, graph->members + n), _mm512_set1_epi32(CBUILTIN_CALL_WITH_STATIC_CHAIN));
        if (chains != 0 && directory->chain_calls == NULL)
        {
            directory->chain_calls = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)graph->count + 1);
            valid = directory->chain_calls != NULL;
            if (valid)
            {
                memset(directory->chain_calls, 0, (uint64_t)graph->count + 1);
            }
        }
        for (; valid && chains != 0; chains &= chains - 1)
        {
            const uint32_t node = n + (uint32_t)__builtin_ctz(chains);
            const uint32_t call = graph->argument_counts[node] != 0 ? graph->argument_nodes[graph->argument_offsets[node]] : BUSTER_CHECK_NONE;
            if (call < graph->count && graph->ops[call] == CHECK_OP_CALL)
            {
                directory->chain_calls[call] = 1;
            }
        }
    }
    return valid;
}

// These attributes contribute metadata to clang's CGFunctionInfo key that the canonical function types do not yet
// retain here. Read the sparse attribute records once, only in a unit with a late union; ignored attributes (including
// ns_returns_retained in ordinary C) have no effect. Passing a late union whose ABI can change keeps a named boundary.
static bool check_lower_transparent_attributes(const CheckProgram* program)
{
    const CheckInput* input = program->input;
    const CheckSparseWide all = input->attributes;
    const uint32_t end = input->token_count;
    bool found = false;
    for (uint32_t first = check_input_attributed_next(all, 0, end); !found && first < end;
         first = check_input_attributed_next(all, first + 16, end))
    {
        for (uint32_t selected = check_input_attributed_group(all, first, end); !found && selected != 0; selected &= selected - 1)
        {
            const CheckInputAttributes* attributes = check_input_attributes_at(input, first + (uint32_t)__builtin_ctz(selected));
            for (uint32_t a = 0; !found && attributes != NULL && a < attributes->count; a += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + a;
                if (!attribute->ignored && !attribute->subject_ignored)
                {
                    const String8 name = attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword ?
                                         check_input_attribute_spelling(program->identifiers, attribute->name) : (String8){ 0 };
                    found = attribute->kind == CHECK_ATTRIBUTE_REGPARM || attribute->kind == CHECK_ATTRIBUTE_PASS_OBJECT_SIZE ||
                            attribute->kind == CHECK_ATTRIBUTE_PASS_DYNAMIC_OBJECT_SIZE || check_input_attribute_equal(name, "noescape") ||
                            check_input_attribute_equal(name, "ns_returns_retained");
                }
            }
        }
    }
    return found;
}

static bool check_lower_transparent_prepare(CheckLowerTransparent* directory, const CheckProgram* program, AbiTable* abi, Arena* arena, uint32_t rows)
{
    const TypeTable* types = program->types;
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const uint32_t count = types->types.count;
    const uint64_t room = (uint64_t)graph->count + program->alias_count + program->body_count;
    bool valid = rows == 0 || room <= UINT32_MAX;
    if (valid && rows != 0)
    {
        directory->first = BUSTER_CHECK_NONE;
        directory->ordinary = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, count);
        directory->uses = BUSTER_ARENA_PUSH_ARRAY(arena, CheckLowerTransparentUse, room + 1);
        directory->keys = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, room + 1);
        valid = directory->ordinary != NULL && directory->uses != NULL && directory->keys != NULL && check_lower_transparent_chains(directory, program, arena);
        if (valid)
        {
            directory->unmodeled_attributes = check_lower_transparent_attributes(program);
            memset(directory->ordinary, 0, (uint64_t)count * sizeof(TypeId));
            uint32_t next = count;
            for (uint32_t i = 0; i < count; i += 1)
            {
                if (types->types.kind[i] == BUSTER_TYPE_KIND_UNION && (types->types.flags[i] & BUSTER_TYPE_FLAG_TRANSPARENT_UNION) != 0)
                {
                    directory->ordinary[i] = next << BUSTER_TYPE_INDEX_SHIFT;
                    memcpy(abi->rows + (uint64_t)next * BUSTER_ABI_ROW_BYTES, abi->rows + (uint64_t)i * BUSTER_ABI_ROW_BYTES, BUSTER_ABI_ROW_BYTES);
                    abi->pass[next] = abi->pass[i];
                    abi->stack_bytes[next] = abi->stack_bytes[i];
                    abi->alignment_log2[next] = abi->alignment_log2[i];
                    abi->storage[next] = abi->storage[i];
                    abi->layout[next] = abi->layout[i];
                    abi->results[next] = abi->results[i];
                    abi->transparent[next++] = 0;
                }
            }
            for (uint32_t n = 0; program->abi_emission_nodes != NULL && n < graph->count; n += 16)
            {
                const uint32_t left = graph->count - n;
                const __mmask16 active = left >= 16 ? UINT16_MAX : (__mmask16)((1u << left) - 1);
                uint32_t emitted = active & _mm512_cmpneq_epi32_mask(_mm512_maskz_loadu_epi32(active, program->abi_emission_nodes + n), _mm512_set1_epi32(-1));
                for (; emitted != 0; emitted &= emitted - 1)
                {
                    const uint32_t node = n + (uint32_t)__builtin_ctz(emitted);
                    const bool call = graph->ops[node] == CHECK_OP_CALL;
                    const TypeId function = check_lower_transparent_function(program, node);
                    check_lower_transparent_use(directory, program, function, call ? node : BUSTER_CHECK_NONE, program->abi_emission_nodes[node]);
                }
            }
            for (uint32_t b = 0; b < program->body_count; b += 1)
            {
                const CheckProgramBody* body = program->bodies + b;
                const bool eager = program->body_eager == NULL || program->body_eager[b] != 0;
                const uint32_t point = eager && program->parser != NULL ? program->parser->tree.parents[body->opening] + 1 : BUSTER_CHECK_NONE;
                check_lower_transparent_use(directory, program, input->declared_types[body->declaration], BUSTER_CHECK_LOWER_SIGNATURE_DEFINITION, point);
            }
            for (uint32_t a = 0; a < program->alias_count; a += 1)
            {
                const CheckProgramAlias* alias = program->aliases + a;
                const TypeId type = input->declared_types[alias->token];
                if (alias->kind != CHECK_PROGRAM_WEAKREF && type_kind(types, type) == BUSTER_TYPE_KIND_FUNCTION)
                {
                    check_lower_transparent_use(directory, program, type, BUSTER_CHECK_NONE, alias->token);
                }
            }
        }
    }
    return valid;
}

static uint32_t check_lower_transparent_point(CheckLowerWriter* writer, TypeId function, uint32_t node)
{
    const CheckLowerTransparent* directory = writer->transparent;
    uint32_t point = BUSTER_CHECK_NONE;
    if (directory != NULL && directory->count != 0)
    {
        const CheckLowerTransparentUse use = {
            .function = type_strip_aligned(writer->program->types, function),
            .node = node,
            .chain = directory->chain_calls != NULL && node < writer->program->input->graph.count && directory->chain_calls[node] != 0,
        };
        const __m512i key = _mm512_set1_epi32((int)check_lower_transparent_key(writer->program, use));
        const uint32_t count = directory->count;
        for (uint32_t i = 0; i < count; i += 16)
        {
            const uint32_t left = count - i;
            const __mmask16 active = left >= 16 ? UINT16_MAX : (__mmask16)((1u << left) - 1);
            uint32_t matches = active & _mm512_cmpeq_epi32_mask(key, _mm512_maskz_loadu_epi32(active, directory->keys + i));
            for (; matches != 0; matches &= matches - 1)
            {
                const CheckLowerTransparentUse candidate = directory->uses[i + (uint32_t)__builtin_ctz(matches)];
                const TypeAnswer same = candidate.point < point ? check_lower_transparent_same(writer->program, use, candidate) : TYPE_ANSWER_NO;
                if (same == TYPE_ANSWER_FULL)
                {
                    check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
                }
                else if (same == TYPE_ANSWER_YES)
                {
                    point = candidate.point;
                }
            }
        }
    }
    return point;
}

static bool check_lower_transparent_active(const CheckLowerWriter* writer, TypeId type, uint32_t point)
{
    const TypeTable* types = writer->program->types;
    const CheckGraph* graph = &writer->program->input->graph;
    const uint32_t tag = types->types.index[type_index(type_strip_aligned(types, type))];
    return graph->transparent_unions == NULL || tag >= graph->tag_completion_count || graph->transparent_unions[tag] <= point;
}

static TypeId check_lower_transparent_abi(const CheckLowerWriter* writer, TypeId type, bool ordinary)
{
    TypeId result = type;
    if (ordinary && writer->transparent != NULL && writer->transparent->ordinary != NULL)
    {
        const TypeId row = writer->transparent->ordinary[type_index(type_strip_aligned(writer->program->types, type))];
        result = row != 0 ? row : type;
    }
    return result;
}
