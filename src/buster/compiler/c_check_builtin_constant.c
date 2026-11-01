// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Declaration-time builtin evaluation uses the same typed modules as runtime graphs. Their source constant
// contracts may visit fewer operands than runtime execution; in particular expect's constant evaluator returns only
// the converted first argument, where its runtime call evaluates both unless that argument folds.

static bool check_constant_call(CheckConstantInput* input, uint32_t keyword, uint32_t closing, CBuiltinId builtin,
                                TypeId destination_type, const CheckValue* arguments, uint32_t count, CheckValue* value)
{
    CheckInputReader* reader = input->reader;
    const uint32_t extensions = check_input_expression_extensions(reader->parser);
    CheckBuiltinCall evidence = { 0 };
    const uint32_t first = keyword + 2;
    const uint32_t end = check_constant_punctuation(reader->parser, first, closing, PARSE_KIND_COMMA);
    check_input_builtin_evidence(reader, first, end, &evidence);
    CheckActionResult result = { 0 };
    const CheckOp family = check_graph_builtin_op(builtin);
    if (builtin == CBUILTIN_BIT_CAST)
    {
        // Integer operands provide an exact object representation only after the complete expression has
        // evaluated without effects, faults or volatile access. The module independently checks destination
        // eligibility and size; no lvalue conversion, array decay or half promotion precedes that check.
        const CheckValue* source = count == 1 ? arguments : 0;
        const bool constant = source != 0 && type_is_valid(reader->types, source->type) && type_is_valid(reader->types, destination_type) &&
                              source->category == CHECK_RVALUE && source->constant_kind == CHECK_CONSTANT_INTEGER &&
                              source->fault == CHECK_FAULT_NONE && (source->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_BIT_FIELD)) == 0 &&
                              (type_qualifiers(type_strip_aligned(reader->types, source->type)) & BUSTER_TYPE_VOLATILE) == 0 &&
                              check_action_is_integer(reader->types, source->type);
        // Clang never takes __builtin_bit_cast as a C integer constant expression: a strict context (an aligned
        // attribute's operand, choose_expr's condition) rejects it, while enum values and _Static_assert fold it
        // as a GNU extension. The source therefore certifies a constant, never an ICE.
        const CheckBuiltinBitCastAction action =
        {
            .id = builtin, .destination_type = destination_type, .arguments = arguments, .argument_count = count, .node = closing,
            .tag_completions = reader->tag_completions, .tag_completion_count = reader->types->tags.capacity,
            .source_position = closing + 1,
            .source_proof = constant ? CHECK_BUILTIN_BIT_CAST_PROOF_CONSTANT : CHECK_BUILTIN_BIT_CAST_PROOF_NONE,
        };
        CheckBuiltinBitCastResult checked;
        check_builtin_bit_cast(reader->types, &action, &checked, 1);
        result = checked.action;
        if (result.diagnostic == CHECK_DIAG_NONE && (checked.unsupported & BUSTER_CHECK_BUILTIN_BIT_CAST_RESULT_TYPE) != 0)
        {
            check_action_diagnose(&result, CHECK_DIAG_UNSUPPORTED, 0);
        }
    }
    else if (family == CHECK_OP_BUILTIN_MISC)
    {
        const CheckBuiltinMiscAction action =
        {
            .id = builtin, .arguments = arguments, .argument_count = count, .node = closing,
            .tag_completions = reader->tag_completions, .tag_completion_count = reader->types->tags.capacity,
            .source_position = closing + 1, .extensions = extensions,
            .const_values = &reader->output->const_values, .constant_evaluated = true,
            .pointer_object = evidence.base_object, .pointer_alignment = evidence.base_alignment,
        };
        CheckBuiltinMiscResult checked;
        check_builtin_misc(reader->types, &action, &checked, 1);
        result = checked.action;
        if (checked.constant_evaluated_operands != 0)
        {
            result.value = checked.constant_value;
        }
        // __builtin_strlen and memcmp fold as the graph folds them (check_builtin_library_fold_memory).
        const CheckAction folding = {
            .arguments = arguments,
            .argument_count = count,
            .node = closing,
            .tag_completions = reader->tag_completions,
            .tag_completion_count = reader->types->tags.capacity,
            .source_position = closing + 1,
            .extensions = extensions,
            .const_values = &reader->output->const_values,
            .constant_evaluated = true,
        };
        check_builtin_library_fold_memory(reader->types, builtin, &folding, checked.argument_conversions, &result);
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
            .const_values = &reader->output->const_values, .constant_evaluated = true,
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
            .const_values = &reader->output->const_values, .constant_evaluated = true,
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
    else if (family == CHECK_OP_BUILTIN_VECTOR)
    {
        const uint64_t mark = reader->scratch->length;
        CheckBuiltinVectorAction action =
        {
            .id = builtin,
            .arguments = arguments,
            .argument_count = count,
            .destination_type = destination_type,
            .node = closing,
            .tag_completions = reader->tag_completions,
            .tag_completion_count = reader->types->tags.capacity,
            .source_position = closing + 1,
            .extensions = extensions,
            .allow_gnu_index_ice = reader->parser->language.gnu,
        };
        if (builtin == CBUILTIN_SHUFFLEVECTOR && count > 2)
        {
            uint8_t* ice = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count - 2);
            if (ice == 0)
            {
                check_action_diagnose(&result, CHECK_DIAG_RESOURCE, 0);
            }
            else
            {
                for (uint32_t i = 2; i < count; i += 1)
                {
                    const CheckValue* index = arguments + i;
                    const bool eligible = type_is_valid(reader->types, index->type) && check_action_is_integer(reader->types, index->type) &&
                                          index->constant_kind == CHECK_CONSTANT_INTEGER &&
                                          index->fault == CHECK_FAULT_NONE && (index->flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
                    ice[i - 2] = !eligible ? CHECK_BUILTIN_VECTOR_INDEX_ICE_NONE : (index->flags & BUSTER_CHECK_ICE) != 0 ? CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT :
                                 (index->flags & BUSTER_CHECK_GNU_ICE_FORM) != 0 ? CHECK_BUILTIN_VECTOR_INDEX_ICE_GNU : CHECK_BUILTIN_VECTOR_INDEX_ICE_NONE;
                }
                action.shuffle_index_ice = ice;
                action.shuffle_index_ice_count = count - 2;
            }
        }
        if (result.diagnostic == CHECK_DIAG_NONE)
        {
            CheckBuiltinVectorResult checked;
            check_builtin_vector(reader->types, &action, &checked, 1);
            result = checked.action;
            result.warnings |= (checked.warnings & BUSTER_CHECK_BUILTIN_VECTOR_WARN_QUALIFIERS) != 0 ? CHECK_WARNING_DISCARDED_QUALIFIERS : 0;
        }
        arena_pop_to(reader->scratch, mark);
    }
    else if (family == CHECK_OP_BUILTIN_ATOMIC)
    {
        const CheckBuiltinAtomicAction action =
        {
            .id = builtin, .arguments = arguments, .argument_count = count, .node = closing,
            .tag_completions = reader->tag_completions, .tag_completion_count = reader->types->tags.capacity,
            .source_position = closing + 1, .extensions = extensions,
            .minimum_alignment = evidence.minimum_alignment, .alignment_known = evidence.alignment_known,
            .const_values = &reader->output->const_values, .constant_evaluated = true,
        };
        CheckBuiltinAtomicResult checked;
        check_builtin_atomic_family(reader->types, &action, &checked);
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
                .transparent_unions = reader->transparent_unions,
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
            .const_values = &reader->output->const_values, .constant_evaluated = true,
        };
        for (uint32_t i = 0; i < count && i < 3; i += 1)
        {
            action.operands[i] = arguments[i];
        }
        if (check_builtin_typed_overflow_info(builtin) != NULL)
        {
            CheckBuiltinTypedOverflowResult checked;
            check_builtin_typed_overflow(reader->types, &action, &checked, 1, BUSTER_SCALAR_PATH(reader->parser->options.scalar));
            result = checked.builtin.action;
        }
        else
        {
            CheckBuiltinResult checked;
            check_builtins(reader->types, &action, &checked, 1, BUSTER_SCALAR_PATH(reader->parser->options.scalar));
            result = checked.action;
        }
    }
    reader->output->resource |= result.diagnostic == CHECK_DIAG_RESOURCE;
    check_input_warning_add_at(reader, closing, result.warnings, closing + 1u);
    *value = result.value;
    value->flags &= ~BUSTER_CHECK_LITERAL_ZERO;
    return result.diagnostic == CHECK_DIAG_NONE;
}
