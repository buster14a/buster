// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Declaration-time builtin evaluation uses the same typed modules as runtime graphs. Their source constant
// contracts may visit fewer operands than runtime execution; in particular expect preserves both runtime
// argument evaluations while its constant evaluator returns only the converted first argument.

static bool check_constant_call(CheckConstantInput* input, uint32_t keyword, uint32_t closing, CBuiltinId builtin,
                                const CheckValue* arguments, uint32_t count, CheckValue* value)
{
    CheckInputReader* reader = input->reader;
    const uint32_t extensions = check_input_expression_extensions(reader->parser);
    CheckBuiltinCall evidence = { 0 };
    const uint32_t first = keyword + 2;
    const uint32_t end = check_constant_punctuation(reader->parser, first, closing, PARSE_KIND_COMMA);
    check_input_builtin_evidence(reader, first, end, &evidence);
    CheckActionResult result = { 0 };
    const CheckOp family = check_graph_builtin_op(builtin);
    if (family == CHECK_OP_BUILTIN_MISC)
    {
        const CheckBuiltinMiscAction action =
        {
            .id = builtin, .arguments = arguments, .argument_count = count, .node = closing,
            .tag_completions = reader->tag_completions, .tag_completion_count = reader->types->tags.capacity,
            .source_position = closing + 1, .extensions = extensions,
        };
        CheckBuiltinMiscResult checked;
        check_builtin_misc(reader->types, &action, &checked, 1);
        result = checked.action;
        if (checked.constant_evaluated_operands != 0)
        {
            result.value = checked.constant_value;
        }
    }
    else if (builtin == CBUILTIN_FLT_ROUNDS)
    {
        const CheckBuiltinRoundsAction action = { .id = builtin, .arguments = arguments, .argument_count = count, .node = closing };
        CheckBuiltinRoundsResult checked;
        check_builtin_rounds(&action, &checked, 1);
        result = checked.action;
    }
    else if (check_builtin_math_kind(builtin) != CHECK_BUILTIN_MATH_NONE)
    {
        const CheckBuiltinMathAction action =
        {
            .id = builtin, .arguments = arguments, .argument_count = count, .node = closing,
            .tag_completions = reader->tag_completions, .tag_completion_count = reader->types->tags.capacity,
            .source_position = closing + 1, .extensions = extensions,
        };
        CheckBuiltinMathResult checked;
        check_builtin_math(reader->types, &action, &checked, 1);
        result = checked.action;
    }
    else if (family == CHECK_OP_BUILTIN_FLOAT)
    {
        const CheckBuiltinFloatAction action =
        {
            .id = builtin, .arguments = arguments, .argument_count = count, .node = closing,
            .tag_completions = reader->tag_completions, .tag_completion_count = reader->types->tags.capacity,
            .source_position = closing + 1, .extensions = extensions,
            .nan_payload = evidence.nan_payload, .nan_payload_length = evidence.nan_payload_length, .nan_payload_known = evidence.nan_payload_known,
        };
        CheckBuiltinFloatResult checked;
        check_builtin_float(reader->types, &action, &checked, 1);
        result = checked.action;
    }
    else if (family == CHECK_OP_BUILTIN_QUERY)
    {
        CheckBuiltinQueryAction action =
        {
            .id = builtin, .arguments = arguments, .argument_count = count, .node = closing,
            .tag_completions = reader->tag_completions, .tag_completion_count = reader->types->tags.capacity,
            .source_position = closing + 1, .extensions = extensions,
            .string_literal_start = evidence.string_literal_start, .absolute_address_constant = evidence.absolute_address_constant,
            .complex_constant = count != 0 && arguments[0].constant_kind == CHECK_CONSTANT_COMPLEX &&
                                arguments[0].fault == CHECK_FAULT_NONE && (arguments[0].flags & BUSTER_CHECK_SIDE_EFFECT) == 0,
        };
        memcpy(action.object_size_known, evidence.object_size_known, sizeof(action.object_size_known));
        memcpy(action.object_size_source_constant, evidence.object_size_source_constant, sizeof(action.object_size_source_constant));
        memcpy(action.object_sizes, evidence.object_sizes, sizeof(action.object_sizes));
        CheckBuiltinQueryResult checked;
        check_builtin_query(reader->types, &action, &checked, 1);
        result = checked.action;
    }
    else if (family == CHECK_OP_BUILTIN_ATOMIC)
    {
        const CheckBuiltinAtomicAction action =
        {
            .id = builtin, .arguments = arguments, .argument_count = count, .node = closing,
            .tag_completions = reader->tag_completions, .tag_completion_count = reader->types->tags.capacity,
            .source_position = closing + 1, .extensions = extensions,
            .minimum_alignment = evidence.minimum_alignment, .alignment_known = evidence.alignment_known,
        };
        CheckBuiltinAtomicResult checked;
        check_builtin_atomic(reader->types, &action, &checked, 1);
        result = checked.action;
    }
    else if (check_input_complex_builtin(builtin))
    {
        if (count != 1)
        {
            check_action_diagnose(&result, CHECK_DIAG_ARGUMENT_COUNT, 0);
        }
        else
        {
            CheckAction action =
            {
                .op = CHECK_OP_CONVERT,
                .type = check_input_type_result(reader, type_complex(reader->types, check_input_complex_format(builtin))),
                .operands = { arguments[0] },
                .node = closing,
                .tag_completions = reader->tag_completions,
                .tag_completion_count = reader->types->tags.capacity,
                .source_position = closing + 1,
                .extensions = extensions,
            };
            check_action_scalar(reader->types, &action, &result);
            if (result.diagnostic == CHECK_DIAG_NONE)
            {
                action.op = builtin >= CBUILTIN_CIMAG ? CHECK_OP_IMAGINARY : CHECK_OP_REAL;
                action.type = 0;
                action.operands[0] = result.value;
                check_action_scalar(reader->types, &action, &result);
            }
        }
    }
    else
    {
        CheckBuiltinAction action =
        {
            .id = builtin, .argument_count = count, .node = closing,
            .tag_completions = reader->tag_completions, .tag_completion_count = reader->types->tags.capacity,
            .source_position = closing + 1, .extensions = extensions,
        };
        for (uint32_t i = 0; i < count && i < 3; i += 1)
        {
            action.operands[i] = arguments[i];
        }
        if (builtin == CBUILTIN_UMULL_OVERFLOW)
        {
            CheckBuiltinTypedOverflowResult checked;
            check_builtin_typed_overflow(reader->types, &action, &checked, 1, reader->parser->options.scalar);
            result = checked.builtin.action;
        }
        else
        {
            CheckBuiltinResult checked;
            check_builtins(reader->types, &action, &checked, 1, reader->parser->options.scalar);
            result = checked.action;
        }
    }
    reader->output->resource |= result.diagnostic == CHECK_DIAG_RESOURCE;
    *value = result.value;
    return result.diagnostic == CHECK_DIAG_NONE;
}
