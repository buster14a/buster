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
        reader->builtins[closing] = builtin;
        reader->builtin_type_ends[separator + 1] = closing;
        reader->ignored[keyword] = true;
        memset(reader->ignored + separator + 1, 1, closing - separator - 1);
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
    const TypeId type = check_input_constant_type(reader, first, closing);
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
        reader->builtins[closing] = builtin;
        reader->vararg_calls[closing] = index + 1;
        reader->ignored[keyword] = true;
        if (builtin == CBUILTIN_VA_ARG && second < closing)
        {
            reader->vararg_types[second] = index + 1;
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
