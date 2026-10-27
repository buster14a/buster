// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Naked-function constraints use the reader's existing statement tree and resolved declaration identities.
// The ordinary path retains only one function flag; only units declaring naked functions scan their bodies.
// An error keeps its completion position apart from its spelling: asm operands are checked at the asm's end,
// and non-asm statements at the body's end, after errors within them. No extra token columns or reparsing.

static const CheckInputAttribute* check_input_naked_attribute(const CheckInputAttributes* attributes)
{
    const CheckInputAttribute* result = NULL;
    for (uint32_t i = 0; result == NULL && attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        result = attribute->kind == CHECK_ATTRIBUTE_NAKED && !attribute->ignored ? attribute : NULL;
    }
    return result;
}

// A naked prototype's later declarations inherit the attribute, including its original note location. Copy
// the accumulator before appending: a declaration's metadata is immutable once another reader can borrow it.
static void check_input_naked_inherit(CheckInputReader* reader, uint32_t token, uint32_t previous)
{
    CheckInput* input = reader->output;
    if (reader->naked_functions && (input->function_flags[previous] & CHECK_INPUT_FUNCTION_NAKED) != 0 &&
        (input->function_flags[token] & CHECK_INPUT_FUNCTION_NAKED) == 0)
    {
        const CheckInputAttribute* attribute = check_input_naked_attribute(input->attributes[previous]);
        CheckInputAttributes* attributes = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputAttributes, 1);
        bool valid = attributes != NULL && attribute != NULL;
        if (valid)
        {
            *attributes = (CheckInputAttributes){ 0 };
            valid = input->attributes[token] == NULL || check_input_attribute_copy(reader, input->attributes[token], attributes);
            valid = valid && check_input_attribute_append(reader, attributes, *attribute);
        }
        input->resource |= !valid;
        if (valid)
        {
            input->attributes[token] = attributes;
            input->function_flags[token] |= CHECK_INPUT_FUNCTION_NAKED;
            reader->naked_functions = true;
        }
    }
}

static bool check_input_naked_registers(const CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    bool found = false;
    const uint8_t kind = parser->tokens.kinds[first];
    bool valid = parser->tree.roles[end] == PARSE_ROLE_TERMINATOR &&
                 (parser->tree.roles[first] == PARSE_ROLE_SPECIFIER || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ||
                  kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_EXTENSION || check_input_attribute_starts(parser, first, end));
    for (uint32_t token = first; valid && token < end; token += 1)
    {
        if (input->declarations[token] == token)
        {
            bool label = false;
            const CheckInputAttributes* attributes = input->attributes[token];
            for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
            {
                label |= attributes->records[i].kind == CHECK_ATTRIBUTE_ASM_LABEL && !attributes->records[i].ignored;
            }
            found = true;
            valid = input->declaration_kinds[token] == C_BINDING_OBJECT && label &&
                    input->initializer_begins[token] == input->initializer_ends[token];
        }
    }
    return found && valid;
}

static uint32_t check_input_naked_parameter(const CheckInputReader* reader, const CheckAsmOperand* operand, uint32_t body)
{
    const CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    uint32_t result = BUSTER_CHECK_NONE;
    // Expression children are visited rightmost first, including unevaluated/discarded operands. A GNU
    // statement expression's compound body is a statement child, so it is outside this expression walk.
    for (uint32_t token = operand->closing; result == BUSTER_CHECK_NONE && token > operand->opening + 1;)
    {
        token -= 1;
        const uint32_t region = input->expression_regions[token];
        if (region != 0 && token == input->regions[region - 1].closing)
        {
            token = input->regions[region - 1].opening;
        }
        else if (parser->tree.roles[token] == PARSE_ROLE_CLOSE_SPECIFIER)
        {
            token = check_input_first_token(reader, token);
        }
        else if (parser->tree.roles[token] == PARSE_ROLE_OPERAND)
        {
            const uint32_t declaration = input->declarations[token];
            if (declaration < body && input->declaration_bodies[declaration] == body &&
                input->declaration_kinds[declaration] == C_BINDING_OBJECT)
            {
                result = token;
            }
        }
    }
    return result;
}

static void check_input_naked_finish(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    CheckInputNakedDiagnostic diagnostic = { .position = BUSTER_CHECK_NONE };
    CheckInputMessage message = CHECK_INPUT_NAKED_STATEMENT;
    for (uint32_t body = 0; reader->naked_functions && !input->resource && body < reader->expression_limit && diagnostic.position == BUSTER_CHECK_NONE; body += 1)
    {
        const uint32_t declaration = input->body_declarations[body];
        if (parser->tree.roles[body] != PARSE_ROLE_OPEN_BODY || declaration == BUSTER_CHECK_NONE ||
            (input->function_flags[declaration] & CHECK_INPUT_FUNCTION_NAKED) == 0 ||
            (input->deferred_tokens != NULL && input->deferred_tokens[body] != 0))
        {
            continue;
        }
        const uint32_t closing = parser->tree.parents[body];
        const CheckInputAttribute* attribute = check_input_naked_attribute(input->attributes[declaration]);
        if (attribute == NULL)
        {
            continue;
        }
        diagnostic.attribute = attribute->name_token;
        for (uint32_t child = closing < reader->expression_limit ? reader->first_child[closing] : BUSTER_CHECK_NONE;
             child != BUSTER_CHECK_NONE && diagnostic.position == BUSTER_CHECK_NONE; child = reader->next_child[child])
        {
            const uint32_t first = check_input_first_token(reader, child);
            const bool empty = parser->tree.roles[child] == PARSE_ROLE_TERMINATOR && reader->first_child[child] == BUSTER_CHECK_NONE;
            if (child > body && !empty && input->asm_ids[child] == 0 && !check_input_naked_registers(reader, first, child))
            {
                diagnostic.position = closing;
                diagnostic.token = first;
            }
        }
        for (uint32_t statement = 0; statement < input->asm_count; statement += 1)
        {
            const CheckInputAsm* assembly = input->asm_statements + statement;
            if (assembly->body == body && assembly->closing < reader->expression_limit && assembly->closing < diagnostic.position)
            {
                for (uint32_t i = 0; i < assembly->output_count + assembly->input_count; i += 1)
                {
                    const uint32_t token = check_input_naked_parameter(reader, assembly->operands + i, body);
                    if (token != BUSTER_CHECK_NONE)
                    {
                        diagnostic.position = assembly->closing;
                        diagnostic.token = token;
                        message = CHECK_INPUT_NAKED_PARAMETER;
                        break;
                    }
                }
            }
        }
    }
    if (diagnostic.position != BUSTER_CHECK_NONE)
    {
        input->naked_diagnostic = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputNakedDiagnostic, 1);
        input->resource |= input->naked_diagnostic == NULL;
        if (input->naked_diagnostic != NULL)
        {
            *input->naked_diagnostic = diagnostic;
            check_input_report(reader, diagnostic.position, message);
        }
    }
}
