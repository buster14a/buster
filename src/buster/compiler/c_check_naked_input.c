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

static bool check_input_naked_registers(const CheckInputReader* reader, uint32_t first, uint32_t end, uint32_t body)
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
        const uint8_t declaration_kind = input->declaration_kinds[token];
        if (input->declarations[token] == token && input->declaration_bodies[token] == body &&
            declaration_kind != C_BINDING_ENUMERATOR && declaration_kind != C_BINDING_TAG_KIND)
        {
            // The range also contains member names, prototype parameters and declarations in nested
            // statement expressions. Only declarators owned by this declaration statement qualify.
            uint32_t owner = parser->tree.parents[token];
            while (owner < parser->tokens.count && owner != end && parser->tree.roles[owner] != PARSE_ROLE_TERMINATOR)
            {
                owner = parser->tree.parents[owner];
            }
            if (owner != end)
            {
                continue;
            }
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

typedef struct CheckInputNakedRange CheckInputNakedRange;
struct CheckInputNakedRange
{
    uint32_t first;
    uint32_t end;
};

static bool check_input_naked_bound(const CheckInputReader* reader, const CheckInputVla* vla, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    bool visible = vla->declaration == BUSTER_CHECK_NONE && vla->bound_begin >= first && vla->bound_end < end;
    for (uint32_t owner = vla->scope_begin; visible && owner < parser->tokens.count && owner != end; owner = parser->tree.parents[owner])
    {
        // A typeof/typedef type wrapper contributes no expression children to Clang's asm operand walk.
        // The parser can also hang a later array declarator on a completed specifier. Its bound is
        // outside that specifier's parentheses and remains an expression child of the type trait.
        visible = parser->tree.roles[owner] != PARSE_ROLE_CLOSE_SPECIFIER || owner < vla->scope_begin;
    }
    return visible;
}

static uint32_t check_input_naked_parameter(CheckInputReader* reader, const CheckAsmOperand* operand, uint32_t body)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const uint64_t mark = reader->scratch->length;
    const uint32_t capacity = operand->closing - operand->opening;
    CheckInputNakedRange* pending = NULL;
    uint32_t depth = 0;
    CheckInputNakedRange range =
    {
        .first = operand->opening + 1,
        .end = operand->closing,
    };
    uint32_t result = BUSTER_CHECK_NONE;
    // Expression children are visited rightmost first, including unevaluated/discarded operands. A GNU
    // statement expression's compound body is a statement child, so it is outside this expression walk.
    while (result == BUSTER_CHECK_NONE && !input->resource)
    {
        if (range.end <= range.first)
        {
            if (depth == 0)
            {
                break;
            }
            range = pending[--depth];
            continue;
        }
        const uint32_t token = --range.end;
        const uint32_t region = input->expression_regions[token];
        if (region != 0 && token == input->regions[region - 1].closing)
        {
            range.end = input->regions[region - 1].opening;
        }
        else if (parser->tree.roles[token] == PARSE_ROLE_CLOSE_SIZEOF && reader->query_types[token] != 0)
        {
            const uint32_t first = check_input_first_token(reader, token);
            const CheckInputNakedRange rest =
            {
                .first = range.first,
                .end = first,
            };
            bool saved = false;
            TypeId type = type_strip_aligned(reader->types, reader->query_types[token]);
            // A type-form sizeof/alignof exposes its VLA size expressions, not arbitrary names in the
            // type spelling. Queue only retained inline bounds, including those below constant dimensions.
            for (uint32_t part = 0; !input->resource && part < reader->types->types.count &&
                 type_kind_is_array(type_kind(reader->types, type)); part += 1)
            {
                const uint64_t extent = reader->types->types.extent[type_index(type)];
                const uint32_t identity = type_kind(reader->types, type) == BUSTER_TYPE_KIND_VARIABLE_ARRAY && extent != 0 &&
                                          extent <= parser->tokens.count ? reader->vla_bounds[extent - 1] : 0;
                const CheckInputVla* vla = identity != 0 && identity <= input->vla_count ? input->vlas + identity - 1 : NULL;
                if (vla != NULL && check_input_naked_bound(reader, vla, first, token))
                {
                    if (pending == NULL)
                    {
                        pending = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckInputNakedRange, capacity);
                    }
                    input->resource |= pending == NULL || depth >= capacity - 1;
                    if (!input->resource)
                    {
                        if (!saved)
                        {
                            pending[depth++] = rest;
                            saved = true;
                        }
                        pending[depth++] = (CheckInputNakedRange)
                        {
                            .first = vla->bound_begin,
                            .end = vla->bound_end,
                        };
                    }
                }
                type = type_strip_aligned(reader->types, type_element(reader->types, type));
            }
            range.end = first;
            if (saved && !input->resource)
            {
                range = pending[--depth];
            }
        }
        else if (parser->tree.roles[token] == PARSE_ROLE_CLOSE_SPECIFIER)
        {
            range.end = check_input_first_token(reader, token);
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
    arena_pop_to(reader->scratch, mark);
    return result;
}

static void check_input_naked_finish(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    CheckInputNakedDiagnostic diagnostic = { .position = BUSTER_CHECK_NONE };
    CheckInputMessage message = CHECK_INPUT_NAKED_STATEMENT;
    uint32_t statement = 0;
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
            if (child > body && !empty && input->asm_ids[child] == 0 && !check_input_naked_registers(reader, first, child, body))
            {
                diagnostic.position = closing;
                diagnostic.token = first;
            }
        }
        // The reader and deferred merge both retain asm statements in source order. Advance once through
        // that list, including ordinary functions and file-scope asm, rather than rescanning it per body.
        while (statement < input->asm_count && input->asm_statements[statement].token < body)
        {
            statement += 1;
        }
        for (; statement < input->asm_count && input->asm_statements[statement].token < closing; statement += 1)
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
    if (diagnostic.position != BUSTER_CHECK_NONE && !input->resource)
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
