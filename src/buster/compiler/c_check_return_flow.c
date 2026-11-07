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
// needed; no type-based guess can distinguish qualified function pointers yet.
static bool check_program_return_unknowns(const CheckProgram* program, Arena* scratch, uint32_t** first)
{
    const CheckInput* input = program->input;
    const uint32_t count = input->token_count;
    bool ready = true;
    *first = NULL;
    for (uint32_t token = 0; ready && program->identifiers != NULL && token < count; token += 1)
    {
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
    return ready;
}

// A library builtin's 'r' attribute depends on the declaration's interned name alone. `names`, when present, holds
// one byte per id of program->identifiers: 0 before the first lookup of that name, otherwise its result plus one, so
// each name is searched in the catalogue once however many calls reach it (#1709). NULL searches every time.
static uint8_t check_program_return_library(const CheckProgram* program, uint32_t name, uint8_t* names)
{
    const uint8_t cached = names != NULL ? names[name] : 0;
    uint8_t result = cached != 0 ? (uint8_t)(cached - 1) : 0;
    if (cached == 0)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup((const char*)intern_bytes(program->identifiers, name), intern_length(program->identifiers, name));
        // Library noreturn depends on builtin options and named typedef visibility. Those policies are not the
        // declaration's FUNCTION_NORETURN bit, so preserve conservative handling rather than guess from its name.
        const bool unknown = info != NULL && memchr(info->attributes, 'r', info->attributes_length) != NULL;
        result = unknown ? BUSTER_CHECK_RETURN_UNKNOWN : 0;
    }
    if (names != NULL)
    {
        names[name] = (uint8_t)(result + 1);
    }
    return result;
}

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
        result = check_program_return_library(program, name, name < program->identifiers->count ? names : NULL);
    }
    return result;
}

static uint8_t check_program_return_fact(const uint8_t* facts, uint32_t count, uint32_t node)
{
    return node < count ? facts[node] : 0;
}

// One postorder reduction gives each node a guaranteed stop and a possible unknown control contract. Conditional
// operands retain only executable arms; an unknown condition guarantees a stop only if both alternatives stop.
static uint8_t* check_program_return_facts(CheckProgram* program, Arena* scratch, uint64_t* work)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint32_t count = graph->count;
    uint8_t* facts = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    uint32_t* unknowns = NULL;
    const bool ready = (count == 0 || facts != NULL) && check_program_return_unknowns(program, scratch, &unknowns);
    // The names' memo is an optimisation only: without its bytes every call searches the catalogue, as before.
    const uint32_t name_count = ready && count != 0 && program->identifiers != NULL ? program->identifiers->count : 0;
    uint8_t* names = name_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, name_count) : NULL;
    if (names != NULL)
    {
        memset(names, 0, name_count);
    }
    if (!ready)
    {
        check_program_fail(program, CHECK_PROGRAM_RESOURCE, 0);
        facts = NULL;
    }
    for (uint32_t node = 0; ready && node < count; node += 1)
    {
        const CheckOp op = graph->ops[node];
        uint8_t result = op == CHECK_OP_CALL ? check_program_return_call(program, node, unknowns, names) :
                         op == CHECK_OP_BUILTIN_QUERY ? BUSTER_CHECK_RETURN_UNKNOWN : 0;
        if (work != NULL)
        {
            *work += 1;
        }
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
        facts[node] = op == CHECK_OP_BUILTIN_QUERY ? BUSTER_CHECK_RETURN_UNKNOWN : result;
    }
    return facts;
}
