// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Attribute arguments are expressions before a handler asks whether they are constants or counts them. Reuse the
// declaration evaluator's iterative type/actions and retain only the sparse failure evidence needed by diagnostics.
// Kept outside c_check_attribute.c so the evaluator's own type-attribute reader remains acyclic.

// A constant reading's failure evidence (CheckConstantFailure) in clang's words (#147, #1447): the error's kind, its token,
// and the report's type, value and note, as CHECK_INPUT_ATTRIBUTE_ARGUMENT's and CHECK_INPUT_CONSTANT_EXPRESSION's are
// printed (check_driver_attribute_argument). `token` is the failure's own place; CHECK_ARGUMENT_EXPECTED_EXPRESSION where
// no message of clang's fits the evidence.
typedef struct CheckConstantError CheckConstantError;
struct CheckConstantError
{
    CheckAttributeArgumentError error;
    uint32_t token;
    uint32_t number;
    TypeId type;
    uint64_t value;
    uint32_t note_token;
    uint8_t note_kind;
};

// The declaration of the tag of `type` (a struct, union or enum, its qualifiers and alignment aside) that names it, or
// BUSTER_CHECK_NONE: clang's "forward declaration of" note's place.
static uint32_t check_constant_failure_tag(CheckInputReader* reader, TypeId type)
{
    const TypeId bare = type != 0 ? type_strip_aligned(reader->types, type) : 0;
    const uint8_t kind = bare != 0 ? type_kind(reader->types, bare) : 0;
    const bool tagged = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM;
    const uint32_t name = tagged ? reader->types->tags.name[reader->types->types.index[type_index(bare)]] : 0;
    const uint32_t binding = name != 0 ? c_bindings_lookup(&reader->bindings, C_BINDING_TAG, name) : 0;
    return binding != 0 ? reader->bindings.declaration[binding - 1] : BUSTER_CHECK_NONE;
}

static CheckConstantError check_constant_failure_error(CheckInputReader* reader, const CheckConstantFailure* failure, uint32_t token)
{
    const Parser* parser = reader->parser;
    const TypeTable* types = reader->types;
    const uint32_t count = parser->tokens.count;
    const bool member = failure->op == CHECK_OP_MEMBER || failure->op == CHECK_OP_ARROW;
    TypeId type = failure->type;
    const uint8_t written = type != 0 ? type_kind(types, type_strip_aligned(types, type)) : 0;
    // `->` of no pointer is clang's own error, at the member's name, naming the operand's type.
    const bool arrow_pointer = member && failure->op == CHECK_OP_ARROW && type != 0 && written != BUSTER_TYPE_KIND_POINTER &&
                               (failure->diagnostic == CHECK_DIAG_POINTER_REQUIRED || failure->diagnostic == CHECK_DIAG_MEMBER);
    type = member && failure->op == CHECK_OP_ARROW && written == BUSTER_TYPE_KIND_POINTER ? type_element(types, type) : type;
    const uint8_t kind = type != 0 ? type_kind(types, type_strip_aligned(types, type)) : 0;
    const bool record = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION;
    const bool size = failure->diagnostic == CHECK_DIAG_INCOMPLETE && (failure->op == CHECK_OP_SIZEOF || failure->op == CHECK_OP_ALIGNOF);
    const bool call = failure->diagnostic == CHECK_DIAG_ARGUMENT_COUNT && failure->op == CHECK_OP_CALL;
    const bool binary = failure->second_type != 0 && (failure->diagnostic == CHECK_DIAG_ARITHMETIC_REQUIRED ||
                        failure->diagnostic == CHECK_DIAG_INTEGER_REQUIRED || failure->diagnostic == CHECK_DIAG_SCALAR_REQUIRED ||
                        failure->diagnostic == CHECK_DIAG_INCOMPATIBLE);
    // Pointer arithmetic on an incomplete pointee: the pointer operand's, the first's or the second's.
    const TypeId second = failure->second_type;
    const bool first_pointer = failure->type != 0 && check_action_kind(types, failure->type) == BUSTER_TYPE_KIND_POINTER;
    const bool second_pointer = second != 0 && check_action_kind(types, second) == BUSTER_TYPE_KIND_POINTER;
    const bool arithmetic = failure->diagnostic == CHECK_DIAG_INCOMPLETE && (failure->op == CHECK_OP_ADD || failure->op == CHECK_OP_SUBTRACT) &&
                            (first_pointer || second_pointer);
    const TypeId pointee = arithmetic ? type_element(types, type_strip_aligned(types, first_pointer ? failure->type : second)) : 0;
    CheckConstantError result =
    {
        .error = member && (failure->diagnostic == CHECK_DIAG_MEMBER || arrow_pointer) ?
                 (arrow_pointer ? CHECK_ARGUMENT_EXPRESSION_ARROW_POINTER : record ? CHECK_ARGUMENT_EXPRESSION_MEMBER_NAME : CHECK_ARGUMENT_EXPRESSION_MEMBER_BASE) :
                 size ? CHECK_ARGUMENT_EXPRESSION_SIZEOF : call ? CHECK_ARGUMENT_EXPRESSION_CALL_COUNT : binary ? CHECK_ARGUMENT_EXPRESSION_BINARY :
                 arithmetic ? CHECK_ARGUMENT_EXPRESSION_INCOMPLETE_ARITHMETIC : CHECK_ARGUMENT_EXPECTED_EXPRESSION,
        .token = token,
        .number = call ? failure->expected : 0,
        .type = arithmetic ? pointee : type,
        .value = call ? failure->actual | ((uint64_t)failure->variadic << 32) : member ? failure->name : binary ? failure->second_type : 0,
        .note_token = BUSTER_CHECK_NONE,
    };
    // A member's name follows its operator, where clang reports a missing member and `->` of no pointer.
    const bool named = result.error == CHECK_ARGUMENT_EXPRESSION_MEMBER_NAME || result.error == CHECK_ARGUMENT_EXPRESSION_ARROW_POINTER;
    result.token = named && token + 1 < count && parser->tokens.kinds[token + 1] == PARSE_KIND_IDENTIFIER ? token + 1 : token;
    if ((size && (record || kind == BUSTER_TYPE_KIND_ENUM)) || arithmetic)
    {
        result.note_token = check_constant_failure_tag(reader, arithmetic ? pointee : type);
        result.note_kind = CHECK_INPUT_NOTE_FORWARD_DECLARATION;
    }
    else if (call && failure->callee < count)
    {
        const uint32_t declaration = reader->output->declarations[failure->callee];
        result.note_token = declaration < count ? declaration : BUSTER_CHECK_NONE;
        result.note_kind = CHECK_INPUT_NOTE_FUNCTION_DECLARATION;
        // Too many arguments is clang's error at the first one past the parameters.
        const uint32_t open = failure->callee + 1;
        const bool opened = open < count && parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] > open &&
                            parser->tree.parents[open] < count;
        const uint32_t close = opened ? parser->tree.parents[open] : open;
        uint32_t commas = 0;
        // With no parameters, the first argument is the first extra one (review on #1900).
        result.token = opened && failure->expected == 0 && failure->actual > 0 && open + 1 < close ? open + 1 : result.token;
        for (uint32_t at = open + 1; opened && failure->actual > failure->expected && at < close && commas < failure->expected; at += 1)
        {
            const uint8_t kind_at = parser->tokens.kinds[at];
            const bool nested = (kind_at == PARSE_KIND_L_PAREN || kind_at == PARSE_KIND_L_SQUARE || kind_at == PARSE_KIND_L_BRACE) &&
                                parser->tree.parents[at] > at && parser->tree.parents[at] < close;
            commas += kind_at == PARSE_KIND_COMMA ? 1u : 0u;
            result.token = kind_at == PARSE_KIND_COMMA && commas == failure->expected ? at + 1 : result.token;
            at = nested ? parser->tree.parents[at] : at;
        }
    }
    return result;
}

// A declarator's constant's failure (#1447): its evidence's error in clang's words (check_constant_failure_error), else the
// generic declaration error at `fallback`. `first` is the constant's first token, from which a typedef's spelling is read.
static void check_input_constant_failure_report(CheckInputReader* reader, const CheckConstantFailure* failure, uint32_t first, uint32_t fallback)
{
    const Parser* parser = reader->parser;
    const bool evidence = failure != NULL && failure->diagnostic != CHECK_DIAG_NONE && failure->token < parser->tokens.count;
    const CheckConstantError mapped = evidence ? check_constant_failure_error(reader, failure, failure->token) :
                                                 (CheckConstantError){ .error = CHECK_ARGUMENT_EXPECTED_EXPRESSION };
    const uint32_t before = reader->output->report_count;
    if (mapped.error == CHECK_ARGUMENT_EXPECTED_EXPRESSION)
    {
        check_input_report(reader, fallback, CHECK_INPUT_DECLARATION);
    }
    else
    {
        check_input_report_argument(reader, mapped.token, CHECK_INPUT_CONSTANT_EXPRESSION, (uint32_t)mapped.error | mapped.number << 8);
    }
    if (mapped.error != CHECK_ARGUMENT_EXPECTED_EXPRESSION && reader->output->report_count > before)
    {
        CheckInputReport* report = reader->output->reports + before;
        // (The token its message's attribute name would be read from, which these messages don't name.)
        report->attribute = mapped.token;
        report->type = mapped.type;
        report->value = mapped.value;
        report->type_spelling = check_input_typedef_spelling(reader->output, parser, first);
        report->note_token = mapped.note_token;
        report->note_kind = mapped.note_kind;
    }
}

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
            const CheckConstantError mapped = check_constant_failure_error(reader, &failure, token);
            const CheckAttributeArgumentError error = type_tail ? CHECK_ARGUMENT_EXPECTED_PAREN : mapped.error;
            check_input_attribute_argument_error(reader, attributes, attribute, type_tail ? token : mapped.token, error, mapped.number);
            if (reader->output->report_count > before)
            {
                CheckInputReport* report = reader->output->reports + before;
                report->type = mapped.type;
                report->value = mapped.value;
                report->type_spelling = check_input_typedef_spelling(reader->output, parser, first);
                report->note_token = type_tail ? open : mapped.note_token;
                report->note_kind = type_tail ? CHECK_INPUT_NOTE_MATCH_PAREN : mapped.note_kind;
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
