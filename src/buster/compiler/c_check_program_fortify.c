// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Fortified calls use the ordinary call ABI, including va_list, default promotions and stack-passed varargs.
// Strip checks only from certified effect-free constants. In particular printf's nonzero flag guards %n even
// with an unknown destination size, and strncat's source limit is not a bound on the complete output string.

static const CheckLowerRuntime check_lower_fortify_checked[CBUILTIN_FORTIFY_COUNT] =
{
    CHECK_LOWER_RUNTIME_MEMCPY_CHK,
    CHECK_LOWER_RUNTIME_MEMMOVE_CHK,
    CHECK_LOWER_RUNTIME_MEMPCPY_CHK,
    CHECK_LOWER_RUNTIME_MEMSET_CHK,
    CHECK_LOWER_RUNTIME_STRCPY_CHK,
    CHECK_LOWER_RUNTIME_STPCPY_CHK,
    CHECK_LOWER_RUNTIME_STRNCPY_CHK,
    CHECK_LOWER_RUNTIME_STPNCPY_CHK,
    CHECK_LOWER_RUNTIME_STRCAT_CHK,
    CHECK_LOWER_RUNTIME_STRNCAT_CHK,
    CHECK_LOWER_RUNTIME_SPRINTF_CHK,
    CHECK_LOWER_RUNTIME_SNPRINTF_CHK,
    CHECK_LOWER_RUNTIME_VSPRINTF_CHK,
    CHECK_LOWER_RUNTIME_VSNPRINTF_CHK,
};

static const CheckLowerRuntime check_lower_fortify_plain[CBUILTIN_FORTIFY_COUNT] =
{
    CHECK_LOWER_RUNTIME_MEMCPY,
    CHECK_LOWER_RUNTIME_MEMMOVE,
    CHECK_LOWER_RUNTIME_MEMPCPY,
    CHECK_LOWER_RUNTIME_MEMSET,
    CHECK_LOWER_RUNTIME_STRCPY,
    CHECK_LOWER_RUNTIME_STPCPY,
    CHECK_LOWER_RUNTIME_STRNCPY,
    CHECK_LOWER_RUNTIME_STPNCPY,
    CHECK_LOWER_RUNTIME_STRCAT,
    CHECK_LOWER_RUNTIME_STRNCAT,
    CHECK_LOWER_RUNTIME_SPRINTF,
    CHECK_LOWER_RUNTIME_SNPRINTF,
    CHECK_LOWER_RUNTIME_VSPRINTF,
    CHECK_LOWER_RUNTIME_VSNPRINTF,
};

static bool check_lower_fortify_constant(const CheckLowerWriter* writer, uint32_t node, uint32_t argument, uint64_t* bits)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint32_t operand = graph->argument_nodes[graph->argument_offsets[node] + argument];
    CheckValue value = stream->values[operand];
    bool allowed = true;
    if (graph->ops[operand] == CHECK_OP_BUILTIN_QUERY)
    {
        const uint32_t query = check_program_builtin_index(stream->queries, stream->query_count, sizeof(CheckBuiltinQueryEffect), operand);
        const CheckBuiltinQueryEffect* effect = query != BUSTER_CHECK_NONE ? stream->queries + query : NULL;
        allowed = effect != NULL && check_program_query_known(program, operand, effect) &&
                  effect->runtime_plan == CHECK_BUILTIN_QUERY_RUNTIME_SKIP && effect->evaluated_operands == 0;
        if (allowed)
        {
            value = effect->runtime_value;
        }
    }
    const bool constant = allowed && value.category == CHECK_RVALUE && value.constant_kind == CHECK_CONSTANT_INTEGER &&
                          value.fault == CHECK_FAULT_NONE && (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    if (constant)
    {
        // All bounds/lengths have the target's size_t prototype (unsigned long); flag uses its low int bits below.
        *bits = (uint64_t)value.integer;
    }
    return constant;
}

// A literal's first terminating byte, including a proven interior pointer. Never read source-program memory or
// mistake a mutable pointer variable/array initializer for an immutable string literal.
static bool check_lower_fortify_string(const CheckLowerWriter* writer, uint32_t node, uint64_t* length)
{
    const CheckInput* input = writer->program->input;
    const uint32_t source = input->graph.argument_nodes[input->graph.argument_offsets[node] + 1];
    const CheckValue* value = writer->program->stream->values + source;
    const bool address = value->constant_kind == CHECK_CONSTANT_ADDRESS && value->object != 0 && value->object <= input->token_count &&
                         value->offset >= 0 && value->fault == CHECK_FAULT_NONE &&
                         (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    const uint32_t index = address && input->string_ids != NULL ? input->string_ids[value->object - 1] : BUSTER_CHECK_NONE;
    bool known = false;
    if (index < input->string_count)
    {
        const CheckInitString* string = input->strings + index;
        const uint64_t offset = (uint64_t)value->offset;
        if (offset < string->byte_count)
        {
            const uint8_t* begin = string->bytes + offset;
            const uint8_t* end = memchr(begin, 0, string->byte_count - offset);
            if (end != NULL)
            {
                *length = (uint64_t)(end - begin) + 1;
                known = true;
            }
        }
    }
    return known;
}

static void check_lower_fortify(CheckLowerWriter* writer, uint32_t node, CBuiltinFortifyId id)
{
    const CBuiltinFortify* call = cbuiltin_fortify_calls + id;
    uint64_t bound = 0;
    uint64_t length = 0;
    uint64_t flag = 0;
    const bool bound_known = check_lower_fortify_constant(writer, node, call->bound, &bound);
    const bool no_flag = call->flag == UINT8_MAX ||
                         (check_lower_fortify_constant(writer, node, call->flag, &flag) && (uint32_t)flag == 0);
    const bool string = id == CBUILTIN_FORTIFY_STRCPY || id == CBUILTIN_FORTIFY_STPCPY;
    const bool safe = bound_known && no_flag && (bound == UINT64_MAX ||
                      (call->length != UINT8_MAX && check_lower_fortify_constant(writer, node, call->length, &length) && length <= bound) ||
                      (string && check_lower_fortify_string(writer, node, &length) && length <= bound));
    const uint32_t omit = call->flag == UINT8_MAX ? call->bound : call->flag;
    const uint32_t omitted = safe ? 1 + (call->flag != UINT8_MAX) : 0;
    check_lower_call_to(writer, node, safe ? check_lower_fortify_plain[id] : check_lower_fortify_checked[id], omit, omitted);
}
