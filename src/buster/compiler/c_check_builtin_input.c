// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Builtin calls retain their actual source argument count and enclosing function. In particular va_arg's type
// operand is elaborated at its own source position; it never enters the ordinary expression binding stream.

static bool check_input_vararg_id(uint16_t builtin)
{
    return builtin >= CBUILTIN_VA_START && builtin <= CBUILTIN_VA_END;
}

static bool check_input_builtin_type_prepare(CheckInputReader* reader, uint32_t keyword, uint16_t builtin)
{
    const Parser* parser = reader->parser;
    const uint32_t opening = keyword + 1;
    const uint32_t closing = opening < parser->tokens.count ? parser->tree.parents[opening] : parser->tokens.count;
    bool valid = opening < closing && closing < parser->tokens.count && parser->tree.roles[opening] == PARSE_ROLE_OPEN_KEYWORD;
    const uint32_t separator = valid ? check_constant_punctuation(parser, opening + 1, closing, PARSE_KIND_COMMA) : closing;
    valid &= opening + 1 < separator && separator + 1 < closing &&
             check_constant_punctuation(parser, separator + 1, closing, PARSE_KIND_COMMA) == closing;
    if (valid)
    {
        const bool first_type = builtin == CBUILTIN_BIT_CAST;
        const uint32_t type_first = first_type ? opening + 1 : separator + 1;
        const uint32_t type_end = first_type ? separator : closing;
        check_input_rare_claim(reader, &reader->builtins[closing]);
        reader->builtins[closing] = builtin;
        check_input_rare_claim(reader, &reader->builtin_type_ends[type_first]);
        reader->builtin_type_ends[type_first] = closing;
        check_input_visit(reader, type_first, BUSTER_CHECK_RESOLVE_QUERY);
        reader->ignored[keyword] = true;
        memset(reader->ignored + type_first, 1, type_end - type_first);
    }
    else
    {
        check_input_report(reader, keyword, CHECK_INPUT_DECLARATION);
    }
    return valid;
}

static void check_input_builtin_type(CheckInputReader* reader, uint32_t first)
{
    const uint32_t closing = reader->builtin_type_ends[first];
    const uint32_t end = reader->builtins[closing] == CBUILTIN_BIT_CAST ?
                         check_constant_punctuation(reader->parser, first, closing, PARSE_KIND_COMMA) : closing;
    const TypeId type = check_input_constant_type(reader, first, end);
    reader->query_types[closing] = type;
    if (type == 0)
    {
        check_input_report(reader, first, CHECK_INPUT_DECLARATION);
    }
}

static uint16_t check_input_builtin_id(CheckInputReader* reader, uint32_t token)
{
    const uint32_t identifier = reader->parser->tokens.names[token];
    uint16_t builtin = reader->builtin_names[identifier];
    if (builtin == UINT16_MAX)
    {
        const InternSpan span = reader->pp->table->spans[identifier];
        const char* spelling = (const char*)reader->pp->table->records + span.offset;
        builtin = (uint16_t)cbuiltin_resolve(spelling, span.length);
        reader->builtin_names[identifier] = builtin;
    }
    return builtin;
}

static bool check_input_vararg_prepare(CheckInputReader* reader, uint32_t keyword, uint16_t builtin)
{
    const Parser* parser = reader->parser;
    const uint32_t opening = keyword + 1;
    const uint32_t closing = opening < parser->tokens.count ? parser->tree.parents[opening] : parser->tokens.count;
    bool valid = opening < closing && closing < parser->tokens.count &&
                 (parser->tree.roles[opening] == PARSE_ROLE_OPEN_CALL || parser->tree.roles[opening] == PARSE_ROLE_OPEN_KEYWORD);
    uint32_t arguments = 0;
    uint32_t second = BUSTER_CHECK_NONE;
    if (valid)
    {
        for (uint32_t first = opening + 1; first < closing;)
        {
            const uint32_t separator = check_constant_punctuation(parser, first, closing, PARSE_KIND_COMMA);
            second = arguments == 1 ? first : second;
            arguments += 1;
            first = separator + 1;
        }
        valid = reader->vararg_count < reader->vararg_capacity;
        reader->output->resource |= !valid;
    }
    if (valid)
    {
        CheckVarargsContext context =
        {
            .va_list_type = check_input_predefined_type(reader, reader->predefined_names[4]),
            .last_parameter = BUSTER_CHECK_VARARGS_NO_PARAMETER,
            .standard = parser->language.standard,
        };
        const uint32_t body = reader->function_body;
        if (body < parser->tokens.count)
        {
            const uint32_t function = reader->output->body_declarations[body];
            context.function_type = reader->declared_types[function];
            context.variadic = (reader->types->types.flags[type_index(context.function_type)] & BUSTER_TYPE_FLAG_VARIADIC) != 0;
            for (uint32_t parameter = reader->parameters[body]; parameter != BUSTER_CHECK_NONE; parameter = reader->parameter_next[parameter])
            {
                context.last_parameter = parameter;
                context.last_parameter_type = reader->declared_types[parameter];
                context.last_parameter_flags = reader->registers[parameter] ? BUSTER_CHECK_REGISTER : 0;
            }
        }
        const uint32_t index = reader->vararg_count++;
        reader->varargs[index] = (CheckInputVarargs)
        {
            .context = context,
            .closing = closing,
            .argument_count = arguments,
            .second_argument = second,
        };
        check_input_rare_claim(reader, &reader->builtins[closing]);
        reader->builtins[closing] = builtin;
        check_input_rare_claim(reader, &reader->vararg_calls[closing]);
        reader->vararg_calls[closing] = index + 1;
        reader->ignored[keyword] = true;
        if (builtin == CBUILTIN_VA_ARG && second < closing)
        {
            check_input_rare_claim(reader, &reader->vararg_types[second]);
            reader->vararg_types[second] = index + 1;
            check_input_visit(reader, second, BUSTER_CHECK_RESOLVE_QUERY);
            memset(reader->ignored + second, 1, closing - second);
        }
    }
    if (!valid)
    {
        check_input_report(reader, keyword, CHECK_INPUT_UNSUPPORTED);
    }
    return valid;
}

static void check_input_vararg_type(CheckInputReader* reader, uint32_t token)
{
    CheckInputVarargs* call = &reader->varargs[reader->vararg_types[token] - 1];
    if (call->argument_count == 2)
    {
        call->requested_type = check_input_constant_type(reader, token, call->closing);
        reader->query_types[call->closing] = call->requested_type;
        if (call->requested_type == 0)
        {
            check_input_report(reader, token, CHECK_INPUT_DECLARATION);
        }
    }
}

// The miscellaneous builtins' evidence (cbuiltin_misc_recorded), the call's record made: an alignment builtin's
// alignment, its second argument from past `end` (its first's) to the call's closing parenthesis `token`, as clang's
// evaluator reads it (BuiltinAlignment's EvaluateAsInt), the source evaluator's integer read as a constant context's
// rvalue, which reads a const object's constant initializer wherever the expression holds one (`const int a = 3;`'s `a`,
// `a + 0`, `(int)d` of a const double), where it gives one without a fault: the graph's runtime view of the argument reads
// none, so the call's checks take this value instead (CheckBuiltinCall.alignment_evaluated). And expect_with_probability's
// probability, which clang evaluates as a constant expression (EvaluateAsConstantExpr), const objects anywhere in it
// read too (`one ? 0.5 : 0.25`): it never runs, so its subtree is checked as a constant context's
// (BUSTER_CHECK_CONTEXT_VALUE, which reports no constraint of its own). Out of line, so that check_input_expressions'
// walk keeps its frame and registers: inlined, the evaluator's state took the walk's frame from 376 to 904 bytes.
static __attribute__((noinline)) void check_input_misc_evidence(CheckInputReader* reader, uint32_t node, uint32_t token, uint32_t count, uint32_t end,
                                                                CheckBuiltinCall* evidence)
{
    CheckGraph* graph = &reader->output->graph;
    const uint16_t builtin = reader->builtins[token];
    const bool align = cbuiltin_align(builtin) && count == 2 && end < token;
    const uint32_t last = align ? check_constant_punctuation(reader->parser, end + 1, token, PARSE_KIND_COMMA) : token;
    CheckValue value = { 0 };
    if (align && last == token && end + 1 < last && check_input_evaluate_cached(reader, end + 1, last, &value) && check_input_const_rvalue(reader, last, &value) &&
        value.constant_kind == CHECK_CONSTANT_INTEGER && value.fault == CHECK_FAULT_NONE && (value.flags & BUSTER_CHECK_MODIFICATION) == 0)
    {
        evidence->alignment_evaluated = true;
        evidence->alignment_side_effects = (value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0;
        evidence->alignment_low = (uint64_t)value.integer;
        evidence->alignment_high = (uint64_t)(value.integer >> 64);
    }
    if (builtin == CBUILTIN_EXPECT_WITH_PROBABILITY && count == 3 && graph->argument_count >= 3 &&
        graph->argument_offsets[node] == graph->argument_count - 3)
    {
        graph->contexts[graph->argument_nodes[graph->argument_count - 1]] |= BUSTER_CHECK_CONTEXT_VALUE;
    }
}

static void check_input_shuffle_evidence(CheckInputReader* reader, uint32_t keyword, uint32_t closing, uint32_t argument_count, CheckBuiltinCall* evidence)
{
    const uint32_t count = argument_count > 2 ? argument_count - 2 : 0;
    if (count != 0)
    {
        uint8_t* indices = arena_push_array(reader->arena, count, sizeof(uint8_t), 1);
        reader->output->resource |= indices == 0;
        if (indices != 0)
        {
            memset(indices, CHECK_BUILTIN_VECTOR_INDEX_ICE_NONE, count);
            uint32_t first = keyword + 2;
            for (uint32_t argument = 0; first < closing && argument < argument_count; argument += 1)
            {
                const uint32_t end = check_constant_punctuation(reader->parser, first, closing, PARSE_KIND_COMMA);
                if (argument >= 2)
                {
                    CheckValue value = { 0 };
                    const bool evaluated = check_input_evaluate_cached(reader, first, end, &value);
                    if (evaluated && value.constant_kind == CHECK_CONSTANT_INTEGER && value.fault == CHECK_FAULT_NONE &&
                        (value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
                    {
                        indices[argument - 2] = (value.flags & BUSTER_CHECK_ICE) != 0 ? CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT :
                                                (value.flags & BUSTER_CHECK_GNU_ICE_FORM) != 0 ? CHECK_BUILTIN_VECTOR_INDEX_ICE_GNU :
                                                CHECK_BUILTIN_VECTOR_INDEX_ICE_NONE;
                    }
                }
                first = end + 1;
            }
            evidence->shuffle_index_ice = indices;
            evidence->shuffle_index_ice_count = count;
        }
    }
}
