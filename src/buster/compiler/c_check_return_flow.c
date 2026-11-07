// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Source return-warning facts for a structurally and semantically checked graph. Ordinary direct declarations return
// unless their retained contract proves otherwise. Indirect/overloaded calls, library policy not retained here and
// semantic queries remain conservative. These facts affect only the warning walk, never emission or graph validity.

#define BUSTER_CHECK_RETURN_STOPS 1u
#define BUSTER_CHECK_RETURN_UNKNOWN 2u

// The shared callee resolver is defined with the target-feature consumer later in c_check_program.c. It reads the
// call's checked designator/overload, not the program's later-populated object-to-declaration table.
static uint32_t check_target_feature_noreturn_object(const CheckProgram* program, uint32_t node, uint32_t* name);
static bool check_target_feature_intrinsic_noreturn(const CheckProgram* program, uint32_t node);

// analyzer_noreturn is accepted source metadata but does not set FUNCTION_NORETURN. An ignored noreturn annotation
// can also have reached the flag before its placement was refused. Retain their first declaration per entity so a
// redeclaration cannot hide uncertainty, and a later annotation cannot change an earlier call. Allocate only when
// needed; no type-based guess can distinguish qualified function pointers yet. Only a token with attributes can have
// either, so the walk visits the attributes column's tokens in order (check_input_attributed_next: the vector path skips
// 256 tokens without attributes a load, the scalar path, its twin, visits every group).
static bool check_program_return_unknowns(const CheckProgram* program, Arena* scratch, uint32_t** first, bool scalar)
{
    const CheckInput* input = program->input;
    const uint32_t count = input->token_count;
    bool ready = true;
    *first = NULL;
    for (uint32_t group = program->identifiers != NULL && check_sparse_wide_present(input->attributes) ? check_input_attributed_next(input->attributes, 0, count, scalar) : count; ready && group < count;
         group = check_input_attributed_next(input->attributes, group + 16, count, scalar))
    {
        for (uint32_t lanes = check_input_attributed_group(input->attributes, group, count, scalar); ready && lanes != 0; lanes &= lanes - 1)
        {
            const uint32_t token = group + (uint32_t)__builtin_ctz(lanes);
            const CheckInputAttributes* attributes = input->declaration_kinds[token] == C_BINDING_FUNCTION ? check_input_attributes_at(input, token) : NULL;
            for (uint32_t a = 0; ready && attributes != NULL && a < attributes->count; a += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + a;
                const String8 name = check_input_attribute_spelling(program->identifiers, attribute->name);
                const uint32_t object = input->objects[token];
                const bool analyzer = !attribute->ignored && !attribute->subject_ignored && check_input_attribute_equal(name, "analyzer_noreturn");
                const bool ignored = attribute->kind == CHECK_ATTRIBUTE_NORETURN && (attribute->ignored || attribute->subject_ignored);
                const bool unknown = (analyzer || ignored) && object != 0 && object <= count;
                if (unknown && *first == NULL)
                {
                    *first = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
                    ready = *first != NULL;
                    if (ready)
                    {
                        memset(*first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
                    }
                }
                if (ready && unknown && (*first)[object] == BUSTER_CHECK_NONE)
                {
                    (*first)[object] = token;
                }
            }
        }
    }
    return ready;
}

// What a call's direct callee's name says of its return (check_program_return_call): a library builtin that clang's table
// says doesn't return, whose contract depends on options this fact doesn't retain. One answer per identifier, looked up
// once: BUSTER_CHECK_RETURN_NAME_UNSEEN until then, a name's own lookup after it. Null where the identifiers are absent or
// the memo couldn't be made, every call then looking its name up.
#define BUSTER_CHECK_RETURN_NAME_UNSEEN 0u
#define BUSTER_CHECK_RETURN_NAME_RETURNS 1u
#define BUSTER_CHECK_RETURN_NAME_LIBRARY 2u

static uint8_t check_program_return_call(const CheckProgram* program, uint32_t node, const uint32_t* unknowns, uint8_t* names)
{
    const CheckInput* input = program->input;
    const CheckStream* stream = program->stream;
    uint8_t result = BUSTER_CHECK_RETURN_UNKNOWN;
    const uint32_t overload = check_program_builtin_index(stream->overloads, stream->overload_count, sizeof(CheckOverloadEffect), node);
    uint32_t use = BUSTER_CHECK_NONE;
    const uint32_t object = overload == BUSTER_CHECK_NONE ? check_target_feature_noreturn_object(program, node, &use) : 0;
    const uint32_t declaration = input->declarations != NULL && use < input->token_count ? input->declarations[use] : BUSTER_CHECK_NONE;
    const bool direct = object != 0 && declaration < input->token_count && input->objects[declaration] == object &&
                        input->declaration_kinds[declaration] == C_BINDING_FUNCTION && input->function_flags != NULL;
    const bool known = direct && (unknowns == NULL || unknowns[object] >= use);
    if (known && (input->function_flags[declaration] & CHECK_INPUT_FUNCTION_NORETURN) != 0)
    {
        result = BUSTER_CHECK_RETURN_STOPS;
    }
    else if (known && program->identifiers != NULL)
    {
        const uint32_t name = program->parser->tokens.names[declaration];
        uint8_t answer = names != NULL && name < program->identifiers->count ? names[name] : BUSTER_CHECK_RETURN_NAME_UNSEEN;
        if (answer == BUSTER_CHECK_RETURN_NAME_UNSEEN)
        {
            const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup((const char*)intern_bytes(program->identifiers, name), intern_length(program->identifiers, name));
            // Library noreturn depends on builtin options and named typedef visibility. Those policies are not the
            // declaration's FUNCTION_NORETURN bit, so preserve conservative handling rather than guess from its name.
            const bool unknown = info != NULL && memchr(info->attributes, 'r', info->attributes_length) != NULL;
            answer = unknown ? BUSTER_CHECK_RETURN_NAME_LIBRARY : BUSTER_CHECK_RETURN_NAME_RETURNS;
            if (names != NULL && name < program->identifiers->count)
            {
                names[name] = answer;
            }
        }
        result = answer == BUSTER_CHECK_RETURN_NAME_LIBRARY ? BUSTER_CHECK_RETURN_UNKNOWN : 0;
    }
    return result;
}

static uint8_t check_program_return_fact(const uint8_t* facts, uint32_t count, uint32_t node)
{
    return node < count ? facts[node] : 0;
}

// One node's facts, from its own contract and its operands' and arguments' facts, which precede it: a guaranteed stop
// and a possible unknown control contract. Conditional operands retain only executable arms; an unknown condition
// guarantees a stop only if both alternatives stop. An operand or argument that doesn't precede the node counts for
// nothing.
static uint8_t check_program_return_node(CheckProgram* program, const uint8_t* facts, uint32_t node, const uint32_t* unknowns, uint8_t* names)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    uint8_t result = op == CHECK_OP_CALL ? check_program_return_call(program, node, unknowns, names) :
                     op == CHECK_OP_BUILTIN_QUERY ? BUSTER_CHECK_RETURN_UNKNOWN : 0;
    if (op == CHECK_OP_BUILTIN_MISC)
    {
        const uint32_t index = check_program_builtin_index(stream->misc, stream->misc_count, sizeof(CheckBuiltinMiscEffect), node);
        result = index == BUSTER_CHECK_NONE || graph->members[node] == CBUILTIN_CALL_WITH_STATIC_CHAIN ? BUSTER_CHECK_RETURN_UNKNOWN :
                 stream->misc[index].terminates ? BUSTER_CHECK_RETURN_STOPS : 0;
    }
    else if (op == CHECK_OP_BUILTIN_X86 && check_target_feature_intrinsic_noreturn(program, node))
    {
        result = BUSTER_CHECK_RETURN_STOPS;
    }
    const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
    const bool selection = op == CHECK_OP_CONDITIONAL || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR;
    if (selection)
    {
        const uint8_t condition = check_program_return_fact(facts, node, operands[0]);
        const uint8_t yes = check_program_return_fact(facts, node, operands[1]);
        const uint8_t no = check_program_return_fact(facts, node, operands[2]);
        bool truth = false;
        const bool known = check_program_runtime_truth(program, node, operands[0], &truth);
        result |= condition;
        if (known)
        {
            result |= op == CHECK_OP_CONDITIONAL ? (truth ? yes : no) : truth != (op == CHECK_OP_LOGICAL_OR) ? yes : 0;
        }
        else
        {
            result |= (yes | no) & BUSTER_CHECK_RETURN_UNKNOWN;
            result |= op == CHECK_OP_CONDITIONAL && (yes & no & BUSTER_CHECK_RETURN_STOPS) != 0 ? BUSTER_CHECK_RETURN_STOPS : 0;
        }
    }
    else
    {
        uint32_t mask = UINT32_MAX;
        if (op == CHECK_OP_VALUE)
        {
            mask = 0;
        }
        else if (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF)
        {
            mask = operands[0] < node && (graph->ops[operands[0]] == CHECK_OP_VLA_BOUND || graph->ops[operands[0]] == CHECK_OP_VLA_EXTENT) ? 1u : 0;
        }
        else if (op == CHECK_OP_VARARGS)
        {
            const uint32_t index = graph->argument_offsets[node];
            mask = index < stream->varargs_count && stream->varargs[index].node == node ? stream->varargs[index].evaluated_operands : 0;
        }
        for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(operands); o += 1)
        {
            result |= (mask & (1u << o)) != 0 ? check_program_return_fact(facts, node, operands[o]) : 0;
        }
        if (check_graph_has_arguments(op))
        {
            const uint32_t arguments = check_program_argument_mask(program, node);
            const uint32_t floating = op == CHECK_OP_BUILTIN_FLOAT ?
                                          check_program_builtin_index(stream->floating, stream->floating_count, sizeof(CheckBuiltinFloatEffect), node) :
                                          BUSTER_CHECK_NONE;
            const uint32_t definite = floating != BUSTER_CHECK_NONE ? stream->floating[floating].definite_operands : arguments;
            for (uint32_t a = 0; a < graph->argument_counts[node]; a += 1)
            {
                const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + a];
                const bool all = op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_X86;
                const bool evaluated = all || (a < 32 && (arguments & (1u << a)) != 0);
                const bool guaranteed = all || (a < 32 && (definite & (1u << a)) != 0);
                const uint8_t fact = evaluated ? check_program_return_fact(facts, node, argument) : 0;
                // fpclassify's possible selector union is not an execution schedule. A stop in a conditional
                // selector remains uncertain here; only a definitely evaluated operand can end this edge.
                result |= guaranteed ? fact : fact != 0 ? BUSTER_CHECK_RETURN_UNKNOWN : 0;
            }
        }
    }
    // Query runtime plans and Clang's source-warning CFG do not share an execution contract here. Even a
    // child's proven stop cannot turn the retained conservative query boundary into a guaranteed stop.
    const uint8_t fact = op == CHECK_OP_BUILTIN_QUERY ? BUSTER_CHECK_RETURN_UNKNOWN : result;
    return fact;
}

// check_program_return_facts' vector path: only a source, a node whose own contract may be a stop or unknown (a call, a
// miscellaneous or x86 builtin, a query), or a node one of whose operands' or arguments' facts isn't zero can have facts,
// so that each node's test reads its operands' facts, a byte each (an absent one the sink's, `facts[count]`, zero), and
// only those nodes take check_program_return_node. A node's operands precede it, so their facts are made; one that doesn't
// precede it has none yet, which check_program_return_node reads as none too. The sources are 16 operators at a time,
// whose bits each block's nodes then take. Every other node's facts are zero, as the scalar path makes them.
static void check_program_return_sparse(CheckProgram* program, uint8_t* facts, const uint32_t* unknowns, uint8_t* names, uint64_t* work)
{
    const CheckGraph* graph = &program->input->graph;
    const uint32_t count = graph->count;
    const bool arguments = graph->argument_offsets != NULL && graph->argument_counts != NULL;
    memset(facts, 0, (uint64_t)count + 1);
    const __m512i call = _mm512_set1_epi32(CHECK_OP_CALL);
    const __m512i query = _mm512_set1_epi32(CHECK_OP_BUILTIN_QUERY);
    const __m512i misc = _mm512_set1_epi32(CHECK_OP_BUILTIN_MISC);
    const __m512i x86 = _mm512_set1_epi32(CHECK_OP_BUILTIN_X86);
    const __m512i limit = _mm512_set1_epi32((int)count);
    for (uint32_t first = 0; first < count; first += 16)
    {
        const __mmask16 live = count - first >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << (count - first)) - 1);
        const __m512i ops = _mm512_maskz_loadu_epi32(live, graph->ops + first);
        const uint32_t sources = _mm512_mask_cmpeq_epi32_mask(live, ops, call) | _mm512_mask_cmpeq_epi32_mask(live, ops, query) |
                                 _mm512_mask_cmpeq_epi32_mask(live, ops, misc) | _mm512_mask_cmpeq_epi32_mask(live, ops, x86);
        // Each operand clamped to the sink, so that its fact is a plain load.
        _Alignas(64) uint32_t operands[3][16];
        _mm512_store_si512(operands[0], _mm512_min_epu32(_mm512_maskz_loadu_epi32(live, graph->operand0 + first), limit));
        _mm512_store_si512(operands[1], _mm512_min_epu32(_mm512_maskz_loadu_epi32(live, graph->operand1 + first), limit));
        _mm512_store_si512(operands[2], _mm512_min_epu32(_mm512_maskz_loadu_epi32(live, graph->operand2 + first), limit));
        const uint32_t lanes = count - first < 16 ? count - first : 16;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            const uint32_t node = first + lane;
            uint32_t any = (sources >> lane) & 1;
            any |= facts[operands[0][lane]] | facts[operands[1][lane]] | facts[operands[2][lane]];
            if (arguments && check_graph_has_arguments(graph->ops[node]))
            {
                for (uint32_t a = 0; a < graph->argument_counts[node]; a += 1)
                {
                    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + a];
                    any |= facts[argument < count ? argument : count];
                }
            }
            if (any != 0)
            {
                facts[node] = check_program_return_node(program, facts, node, unknowns, names);
                if (work != NULL)
                {
                    *work += 1;
                }
            }
        }
    }
}

// One postorder reduction gives each node a guaranteed stop and a possible unknown control contract
// (check_program_return_node). The scalar path visits every node in order; the vector path
// (check_program_return_sparse), only the nodes that can have facts. Their `count` + 1 entries end with a sink, zero.
static uint8_t* check_program_return_facts(CheckProgram* program, Arena* scratch, uint64_t* work, bool scalar)
{
    (void)scalar;

    const CheckGraph* graph = &program->input->graph;
    const uint32_t count = graph->count;
    uint8_t* facts = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : NULL;
    uint32_t* unknowns = NULL;
    const bool ready = (count == 0 || facts != NULL) && check_program_return_unknowns(program, scratch, &unknowns, scalar);
    const uint32_t name_count = program->identifiers != NULL ? program->identifiers->count : 0;
    uint8_t* names = ready && name_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, name_count) : NULL;
    if (names != NULL)
    {
        memset(names, BUSTER_CHECK_RETURN_NAME_UNSEEN, name_count);
    }
    if (!ready)
    {
        check_program_fail(program, CHECK_PROGRAM_RESOURCE, 0);
        facts = NULL;
    }
    if (ready && !BUSTER_SCALAR_PATH(scalar) && count != 0)
    {
        check_program_return_sparse(program, facts, unknowns, names, work);
    }
    for (uint32_t node = 0; ready && BUSTER_SCALAR_PATH(scalar) && node < count; node += 1)
    {
        facts[node] = check_program_return_node(program, facts, node, unknowns, names);
        if (work != NULL)
        {
            *work += 1;
        }
    }
    if (ready && count != 0)
    {
        facts[count] = 0;
    }
    return facts;
}
