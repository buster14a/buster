// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Source return-warning facts for a checked graph. Calls use the callee's function type and
// any source-visible declaration contract. These facts are local to the warning CFG.

#define BUSTER_CHECK_RETURN_STOPS 1u
#define BUSTER_CHECK_RETURN_UNKNOWN 2u

// The shared callee resolver is defined with the target-feature consumer later in c_check_program.c. It reads the
// call's checked designator/overload, not the program's later-populated object-to-declaration table.
static uint32_t check_target_feature_noreturn_object(const CheckProgram* program, uint32_t node, uint32_t* name);

static const BuiltinIntrinsicInfo* check_program_intrinsic_info(const CheckProgram* program, uint32_t node)
{
    const CheckInput* input = program->input;
    const uint32_t closing = input->graph.tokens[node];
    const uint32_t id = input->intrinsic_ids != NULL && closing < input->token_count ? input->intrinsic_ids[closing] : 0;
    const CheckInputIntrinsicCall* call = id != 0 && id <= input->intrinsic_call_count ? input->intrinsic_calls + id - 1 : NULL;
    return call != NULL && call->declaration < input->intrinsic_declaration_count ? input->intrinsic_declarations[call->declaration].info : NULL;
}

// Read only the declaration visible at the call. The binder carries inherited contracts
// onto compatible redeclarations, including a selected overload's own declaration.
static bool check_program_return_declared(const CheckInput* input, const InternTable* names, uint32_t declaration)
{
    bool stops = input->function_flags != NULL && (input->function_flags[declaration] & CHECK_INPUT_FUNCTION_NORETURN) != 0;
    const CheckInputAttributes* attributes = !stops && check_sparse_wide_present(input->attributes) ? check_input_attributes_at(input, declaration) : NULL;
    for (uint32_t i = 0; !stops && attributes != NULL && i < attributes->count; i += 1)
    {
        stops = check_input_analyzer_noreturn(names, attributes->records + i);
    }
    return stops;
}

static uint8_t check_program_return_call(const CheckProgram* program, uint32_t node)
{
    const CheckInput* input = program->input;
    const CheckStream* stream = program->stream;
    const uint32_t callee = input->graph.operand0[node];
    const uint32_t overload = check_program_builtin_index(stream->overloads, stream->overload_count, sizeof(CheckOverloadEffect), node);
    const CheckOverloadResult* selected = overload != BUSTER_CHECK_NONE ? &stream->overloads[overload].selected : NULL;
    const TypeId type = selected != NULL ? selected->type : callee < input->graph.count ? check_stream_type(stream, callee) : 0;
    uint32_t use = BUSTER_CHECK_NONE;
    const uint32_t object = selected != NULL ? selected->object : check_target_feature_noreturn_object(program, node, &use);
    const uint32_t declaration = selected != NULL ? selected->declaration :
                                 input->declarations != NULL && use < input->token_count ? input->declarations[use] : BUSTER_CHECK_NONE;
    uint8_t result = 0;
    if (type != 0 && type_callee_noreturn(program->types, type))
    {
        result = BUSTER_CHECK_RETURN_STOPS;
    }
    else if (object != 0)
    {
        const bool bound = declaration < input->token_count && input->objects != NULL && input->objects[declaration] == object &&
                           input->function_flags != NULL && input->declaration_kinds != NULL && input->declaration_kinds[declaration] == C_BINDING_FUNCTION;
        result = !bound ? BUSTER_CHECK_RETURN_UNKNOWN :
                 check_program_return_declared(input, program->identifiers, declaration) ? BUSTER_CHECK_RETURN_STOPS : 0;
    }
    else if (type == 0)
    {
        result = BUSTER_CHECK_RETURN_UNKNOWN;
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
static uint8_t check_program_return_node(CheckProgram* program, const uint8_t* facts, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    uint8_t result = op == CHECK_OP_CALL ? check_program_return_call(program, node) : 0;
    bool omit_arguments = false;
    if (op == CHECK_OP_BUILTIN_MISC)
    {
        const uint32_t index = check_program_builtin_index(stream->misc, stream->misc_count, sizeof(CheckBuiltinMiscEffect), node);
        result = index == BUSTER_CHECK_NONE || graph->members[node] == CBUILTIN_CALL_WITH_STATIC_CHAIN ? BUSTER_CHECK_RETURN_UNKNOWN :
                 stream->misc[index].terminates ? BUSTER_CHECK_RETURN_STOPS : 0;
    }
    else if (op == CHECK_OP_BUILTIN_X86)
    {
        const BuiltinIntrinsicInfo* info = check_program_intrinsic_info(program, node);
        if (info != NULL && memchr(info->attributes, 'r', info->attributes_length) != NULL)
        {
            result = BUSTER_CHECK_RETURN_STOPS;
        }
        else if (info != NULL && graph->argument_counts[node] == 1 &&
                 (strcmp(info->name, "__builtin_assume") == 0 || strcmp(info->name, "__assume") == 0))
        {
            const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node]];
            bool truth = false;
            omit_arguments = argument < node && (check_stream_flags(stream, argument) & BUSTER_CHECK_SIDE_EFFECT) != 0;
            if (argument < node && !omit_arguments && check_program_runtime_truth(program, node, argument, &truth) && !truth)
            {
                result = BUSTER_CHECK_RETURN_STOPS;
            }
        }
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
            // Clang's warning CFG visits every fpclassify selector and constant_p's
            // operand, even when runtime evaluation would skip them.
            const bool classifier = op == CHECK_OP_BUILTIN_FLOAT && graph->members[node] == CBUILTIN_FPCLASSIFY;
            const bool constant = op == CHECK_OP_BUILTIN_QUERY && graph->members[node] == CBUILTIN_CONSTANT_P;
            omit_arguments |= op == CHECK_OP_BUILTIN_QUERY && !constant;
            for (uint32_t a = 0; !omit_arguments && a < graph->argument_counts[node]; a += 1)
            {
                const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + a];
                const bool all = op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_X86 || classifier || constant;
                const bool evaluated = all || (a < 32 && (arguments & (1u << a)) != 0);
                result |= evaluated ? check_program_return_fact(facts, node, argument) : 0;
            }
        }
    }
    return result;
}

// Walk [first, end) in topological order. Only a source, a node whose own contract may be a stop or unknown (a call, a
// miscellaneous or x86 builtin, a query), or a node one of whose operands' or arguments' facts isn't zero can have facts,
// so that each node's test reads its operands' facts, a byte each (an absent one the sink's, `facts[count]`, zero), and
// only those nodes take check_program_return_node. A node's operands precede it, so their facts are made; one that doesn't
// precede it has none yet, which check_program_return_node reads as none too. The sources are 16 operators at a time,
// whose bits each block's nodes then take. Every other node's facts are zero, as check_program_return_node would make
// them.
static void check_program_return_sparse(CheckProgram* program, uint8_t* facts, uint32_t first, uint32_t end, uint64_t* work)
{
    const CheckGraph* graph = &program->input->graph;
    const uint32_t count = graph->count;
    const bool arguments = graph->argument_offsets != NULL && graph->argument_counts != NULL;
    const __m512i call = _mm512_set1_epi32(CHECK_OP_CALL);
    const __m512i query = _mm512_set1_epi32(CHECK_OP_BUILTIN_QUERY);
    const __m512i misc = _mm512_set1_epi32(CHECK_OP_BUILTIN_MISC);
    const __m512i x86 = _mm512_set1_epi32(CHECK_OP_BUILTIN_X86);
    const __m512i limit = _mm512_set1_epi32((int)count);
    while (first < end)
    {
        const __mmask16 live = end - first >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << (end - first)) - 1);
        const __m512i ops = _mm512_maskz_loadu_epi32(live, graph->ops + first);
        const uint32_t sources = _mm512_mask_cmpeq_epi32_mask(live, ops, call) | _mm512_mask_cmpeq_epi32_mask(live, ops, query) |
                                 _mm512_mask_cmpeq_epi32_mask(live, ops, misc) | _mm512_mask_cmpeq_epi32_mask(live, ops, x86);
        // Each operand clamped to the sink, so that its fact is a plain load.
        _Alignas(64) uint32_t operands[3][16];
        _mm512_store_si512(operands[0], _mm512_min_epu32(_mm512_maskz_loadu_epi32(live, graph->operand0 + first), limit));
        _mm512_store_si512(operands[1], _mm512_min_epu32(_mm512_maskz_loadu_epi32(live, graph->operand1 + first), limit));
        _mm512_store_si512(operands[2], _mm512_min_epu32(_mm512_maskz_loadu_epi32(live, graph->operand2 + first), limit));
        const uint32_t lanes = end - first < 16 ? end - first : 16;
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
                facts[node] = check_program_return_node(program, facts, node);
                if (work != NULL)
                {
                    *work += 1;
                }
            }
        }
        first += lanes;
    }
}

typedef struct CheckReturnFacts CheckReturnFacts;
struct CheckReturnFacts
{
    Arena storage;
    uint8_t* facts;
    uint64_t* work;
    uint32_t computed;
};

static bool check_program_return_prepare(CheckProgram* program, CheckReturnFacts* cache, Arena* scratch)
{
    const uint64_t bytes = (uint64_t)program->input->graph.count + 1;
    cache->facts = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, bytes);
    if (cache->facts == NULL)
    {
        check_program_fail(program, CHECK_PROGRAM_RESOURCE, 0);
        return false;
    }
    // This includes a zero sink for absent operands, and zeroes for forward references the checked walk may read.
    memset(cache->facts, 0, bytes);
    return true;
}

// The complete postorder reduction for the diagnostic census and the checker's independent comparison.
static __attribute__((unused)) uint8_t* check_program_return_facts(CheckProgram* program, Arena* scratch, uint64_t* work, bool scalar)
{
    const uint32_t count = program->input->graph.count;
    CheckReturnFacts cache = { 0 };
    if (count == 0 || !check_program_return_prepare(program, &cache, scratch))
    {
        return NULL;
    }
    check_program_return_sparse(program, cache.facts, 0, count, work);
    return cache.facts;
}

// The source warning asks only about controls in a body that can fall through. A node's operands precede it:
// retain completed vector blocks across bodies, extending the prefix only as far as a demanded root requires.
static uint8_t check_program_return_cached(CheckProgram* program, CheckReturnFacts* cache, uint32_t root)
{
    const uint32_t count = program->input->graph.count;
    if (root >= count || program->status != CHECK_PROGRAM_OK)
    {
        return 0;
    }
    if (root >= cache->computed)
    {
        if (cache->facts == NULL)
        {
            // Body windows reuse their scratch, so this cache owns its storage until check_program_finish.
            const uint64_t bytes = (uint64_t)count + 1;
            cache->storage = arena_reserve(bytes);
            if (!check_program_return_prepare(program, cache, &cache->storage))
            {
                return 0;
            }
        }
        const uint32_t block = root & ~15u;
        const uint32_t end = count - block <= 16 ? count : block + 16;
        check_program_return_sparse(program, cache->facts, cache->computed, end, cache->work);
        cache->computed = end;
    }
    return cache->facts[root];
}
