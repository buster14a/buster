// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Attribute arguments are expressions before a handler asks whether they are constants or counts them. Reuse the
// declaration evaluator's iterative type/actions and retain only the sparse failure evidence needed by diagnostics.
// Kept outside c_check_attribute.c so the evaluator's own type-attribute reader remains acyclic.

static bool check_input_attribute_expression(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute,
                                              uint32_t first, uint32_t end, bool scoped, CheckValue* value, bool* call_failure)
{
    const Parser* parser = reader->parser;
    CheckConstantFailure failure = { .token = BUSTER_CHECK_NONE, .callee = BUSTER_CHECK_NONE };
    uint32_t failed = BUSTER_CHECK_NONE;
    TypeId unused = 0;
    const uint32_t before = reader->output->report_count;
    const bool valid = check_input_constant_read(reader, first, end, false, false, scoped, value, &unused, &failed, &failure);
    *call_failure = false;
    if (!valid && !reader->output->resource && reader->output->report_count == before)
    {
        const uint32_t token = failure.diagnostic != CHECK_DIAG_NONE && failure.token < parser->tokens.count ? failure.token :
                               failed != BUSTER_CHECK_NONE ? failed : first;
        uint32_t open = BUSTER_CHECK_NONE;
        for (uint32_t i = first; i < end && i < token; i += 1)
        {
            if (parser->tokens.kinds[i] == PARSE_KIND_L_PAREN && parser->tree.parents[i] >= token && parser->tree.parents[i] < end)
            {
                open = i;
                *call_failure |= i > first && parser->tokens.kinds[i - 1] == PARSE_KIND_IDENTIFIER;
            }
        }
        *call_failure |= failure.op == CHECK_OP_CALL && failure.diagnostic != CHECK_DIAG_NONE;
        const bool undeclared = failure.diagnostic == CHECK_DIAG_NONE && token < end && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER &&
                                (!scoped || reader->declared_types[token] == 0) &&
                                c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[token]) == 0;
        const bool type_tail = undeclared && open != BUSTER_CHECK_NONE && open + 1 < token && check_input_attribute_type_start(reader, open + 1);
        if (undeclared && !type_tail)
        {
            check_input_report(reader, token, CHECK_INPUT_UNDECLARED);
            attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
            attributes->error_first = token;
            attributes->error_end = token + 1;
        }
        else
        {
            const bool member = failure.op == CHECK_OP_MEMBER || failure.op == CHECK_OP_ARROW;
            TypeId type = failure.type;
            type = member && failure.op == CHECK_OP_ARROW && type != 0 && type_kind(reader->types, type) == BUSTER_TYPE_KIND_POINTER ?
                   type_element(reader->types, type) : type;
            const uint8_t kind = type != 0 ? type_kind(reader->types, type_strip_aligned(reader->types, type)) : 0;
            const bool record = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION;
            const bool size = failure.diagnostic == CHECK_DIAG_INCOMPLETE && (failure.op == CHECK_OP_SIZEOF || failure.op == CHECK_OP_ALIGNOF);
            const bool call = failure.diagnostic == CHECK_DIAG_ARGUMENT_COUNT && failure.op == CHECK_OP_CALL;
            const bool binary = failure.second_type != 0 && (failure.diagnostic == CHECK_DIAG_ARITHMETIC_REQUIRED ||
                                failure.diagnostic == CHECK_DIAG_INTEGER_REQUIRED || failure.diagnostic == CHECK_DIAG_SCALAR_REQUIRED ||
                                failure.diagnostic == CHECK_DIAG_INCOMPATIBLE);
            const CheckAttributeArgumentError error = type_tail ? CHECK_ARGUMENT_EXPECTED_PAREN :
                member && failure.diagnostic == CHECK_DIAG_MEMBER ? (record ? CHECK_ARGUMENT_EXPRESSION_MEMBER_NAME : CHECK_ARGUMENT_EXPRESSION_MEMBER_BASE) :
                size ? CHECK_ARGUMENT_EXPRESSION_SIZEOF : call ? CHECK_ARGUMENT_EXPRESSION_CALL_COUNT :
                binary ? CHECK_ARGUMENT_EXPRESSION_BINARY : CHECK_ARGUMENT_EXPECTED_EXPRESSION;
            check_input_attribute_argument_error(reader, attributes, attribute, token, error, call ? failure.expected : 0);
            if (reader->output->report_count > before)
            {
                CheckInputReport* report = reader->output->reports + before;
                report->type = type;
                report->value = call ? failure.actual | ((uint64_t)failure.variadic << 32) : member ? failure.name : binary ? failure.second_type : 0;
                report->type_spelling = check_input_typedef_spelling(reader->output, parser, first);
                report->note_token = BUSTER_CHECK_NONE;
                if (type_tail)
                {
                    report->note_token = open;
                    report->note_kind = CHECK_INPUT_NOTE_MATCH_PAREN;
                }
                else if (size && (record || kind == BUSTER_TYPE_KIND_ENUM))
                {
                    const uint32_t name = reader->types->tags.name[reader->types->types.index[type_index(type_strip_aligned(reader->types, type))]];
                    const uint32_t binding = c_bindings_lookup(&reader->bindings, C_BINDING_TAG, name);
                    report->note_token = binding != 0 ? reader->bindings.declaration[binding - 1] : BUSTER_CHECK_NONE;
                    report->note_kind = CHECK_INPUT_NOTE_FORWARD_DECLARATION;
                }
                else if (call && failure.callee < parser->tokens.count)
                {
                    const uint32_t declaration = reader->output->declarations[failure.callee];
                    report->note_token = declaration < parser->tokens.count ? declaration : BUSTER_CHECK_NONE;
                    report->note_kind = CHECK_INPUT_NOTE_FUNCTION_DECLARATION;
                }
            }
        }
    }
    return valid;
}

static bool check_input_attribute_expressions(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute)
{
    const Parser* parser = reader->parser;
    const uint8_t schema = attribute->parse_arguments ? attribute->parsed_schema : (uint8_t)attribute->schema;
    uint32_t first = attribute->argument_first;
    uint32_t index = 0;
    bool valid = true;
    bool call_failure = false;
    while (valid && first < attribute->argument_end)
    {
        const uint32_t end = check_constant_punctuation(parser, first, attribute->argument_end, PARSE_KIND_COMMA);
        const bool identifier = schema == CHECK_ATTRIBUTE_IDENTIFIER_LIST || (index == 0 &&
                               (schema == CHECK_ATTRIBUTE_IDENTIFIER || schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS));
        const bool string = schema == CHECK_ATTRIBUTE_STRING_LIST || (index == 0 && schema == CHECK_ATTRIBUTE_STRING);
        if (string && (first == end || parser->tokens.kinds[first] != PARSE_KIND_STRING))
        {
            // The string slot precedes a trailing empty argument's expression error (section(,), for example).
            check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_STRING_LITERAL, 0);
            valid = false;
        }
        else if (!identifier && !string)
        {
            CheckValue value = { 0 };
            valid = check_input_attribute_expression(reader, attributes, attribute, first, end, false, &value, &call_failure);
        }
        first = end + 1;
        index += 1;
    }
    if (valid && attribute->argument_first < attribute->argument_end && parser->tokens.kinds[attribute->argument_end - 1] == PARSE_KIND_COMMA)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->argument_end, CHECK_ARGUMENT_EXPECTED_EXPRESSION, 0);
        valid = false;
    }
    if (!valid && call_failure && attribute->count_check != BUSTER_CHECK_COUNT_NONE)
    {
        const uint32_t before = reader->output->report_count;
        check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, attribute->count_check | attribute->count_bound << 8);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].sort = attribute->argument_end + 1;
            reader->output->reports[before].completion = attribute->argument_end + 1;
        }
    }
    return valid;
}

// Annotate's extra arguments accept any constant expression, including floating values and addresses. Its parameter
// numbering starts with the first extra argument, after the annotation string. The main error names the expression's
// beginning; the note names the failing subexpression or arithmetic operator.
static bool check_input_attribute_annotate(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute)
{
    const Parser* parser = reader->parser;
    uint32_t first = check_constant_punctuation(parser, attribute->argument_first, attribute->argument_end, PARSE_KIND_COMMA) + 1;
    bool valid = true;
    for (uint32_t index = 1; valid && first < attribute->argument_end; index += 1)
    {
        const uint32_t end = check_constant_punctuation(parser, first, attribute->argument_end, PARSE_KIND_COMMA);
        CheckValue value = { 0 };
        bool call_failure = false;
        valid = check_input_attribute_expression(reader, attributes, attribute, first, end, false, &value, &call_failure);
        const bool constant = valid && check_input_const_rvalue(reader, end, &value) && value.constant_kind != CHECK_CONSTANT_NONE &&
                              value.fault == CHECK_FAULT_NONE && (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_GNU_DIVISION)) == 0;
        if (valid && !constant)
        {
            uint32_t note = value.fault != CHECK_FAULT_NONE && value.fault_node < parser->tokens.count ? value.fault_node : first;
            if (value.fault == CHECK_FAULT_NONE)
            {
                for (uint32_t token = first; token < end; token += 1)
                {
                    const uint32_t declaration = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ? reader->output->declarations[token] : BUSTER_CHECK_NONE;
                    const TypeId type = declaration < parser->tokens.count ? reader->declared_types[declaration] : 0;
                    if (type != 0 && reader->declaration_kinds[declaration] != C_BINDING_ENUMERATOR &&
                        ((type_qualifiers(type) & BUSTER_TYPE_CONST) == 0 || type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION))
                    {
                        note = token;
                        break;
                    }
                }
            }
            const uint32_t before = reader->output->report_count;
            check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_ANNOTATE_CONSTANT, index);
            if (reader->output->report_count > before)
            {
                reader->output->reports[before].note_token = note;
                reader->output->reports[before].note_kind = value.fault == CHECK_FAULT_DIVIDE_BY_ZERO ? CHECK_INPUT_NOTE_CONSTANT_DIVISION :
                                                                                                  CHECK_INPUT_NOTE_CONSTANT_SUBEXPRESSION;
            }
            valid = false;
        }
        first = end + 1;
    }
    return valid;
}
