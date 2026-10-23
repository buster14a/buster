// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Predefined function-name arrays. Include after CheckInputReader and check_input_spelling. Preparation runs in
// source order, while function_body identifies the enclosing definition. The expression/constant readers may
// subsequently request the cached object without reconstructing that context. No parser or evaluator callbacks
// are used here: pretty names overlay resolved source spelling on the type printer's explicit work stack.

#define BUSTER_CHECK_FUNCTION_NAME_IMPLEMENTATION 1

typedef struct CheckFunctionSpelling CheckFunctionSpelling;
struct CheckFunctionSpelling
{
    TypeId alias;
    TypeId type;
    uint32_t alias_first;
    uint32_t alias_end;
    uint32_t functions;
    uint32_t name;
    uint32_t qualifiers;
};

typedef struct CheckFunctionPrintContext CheckFunctionPrintContext;
struct CheckFunctionPrintContext
{
    uint32_t spelling;
    uint32_t function;
};

typedef struct CheckFunctionPrinter CheckFunctionPrinter;
struct CheckFunctionPrinter
{
    TypePrinter printer;
    CheckInputReader* reader;
    CheckFunctionSpelling* spellings;
    CheckFunctionPrintContext* contexts;
    uint32_t* next_functions;
    uint32_t* parameter_cursors;
    uint32_t first;
    uint32_t end;
    uint32_t spelling_count;
    uint32_t capacity;
    bool malformed;
};

static void check_function_require(CheckFunctionPrinter* output, bool valid)
{
    output->malformed |= !valid;
    output->printer.valid &= valid;
}

static bool check_input_function_name_token(const Parser* parser, uint32_t token)
{
    const uint32_t name = token < parser->tokens.count ? parser->tokens.names[token] : 0;
    return name == C_NAME_FUNC || name == C_NAME_FUNCTION || name == C_NAME_PRETTY_FUNCTION;
}

// Source spans are disjoint between a declarator and its function parameters. Skip parameter lists, array bounds,
// attributes and type queries here; each parameter is visited once when its type is printed. Grouped declarators
// remain visible, so function suffixes follow the same outside-to-inside order as the semantic type's base chain.
static uint32_t check_function_spelling(CheckFunctionPrinter* output, uint32_t first, uint32_t end, TypeId type, bool parameter)
{
    CheckInputReader* reader = output->reader;
    const Parser* parser = reader->parser;
    const uint32_t index = output->spelling_count++;
    CheckFunctionSpelling* result = index < output->capacity ? &output->spellings[index] : 0;
    check_function_require(output, result != 0 && first >= output->first && end <= output->end && first <= end);
    if (output->printer.valid)
    {
        *result = (CheckFunctionSpelling)
        {
            .type = type,
            .alias_first = BUSTER_CHECK_NONE,
            .alias_end = BUSTER_CHECK_NONE,
            .functions = BUSTER_CHECK_NONE,
            .name = BUSTER_CHECK_NONE,
        };
        uint32_t previous = BUSTER_CHECK_NONE;
        uint32_t depth = 0;
        uint32_t root_depth = 0;
        uint8_t root = 0;
        uint32_t base_qualifiers = 0;
        uint32_t root_qualifiers = 0;
        bool specifiers = true;
        for (uint32_t i = first; output->printer.valid && i < end;)
        {
            const uint8_t kind = parser->tokens.kinds[i];
            const uint8_t role = parser->tree.roles[i];
            const uint32_t name = parser->tokens.names[i];
            const uint32_t close = parser->tree.parents[i];
            uint32_t qualifiers = 0;
            if (check_input_attribute_starts(parser, i, end))
            {
                const uint32_t next = check_input_skip_attributes(parser, i, end);
                check_function_require(output, next > i && next <= end);
                i = next;
            }
            else if ((kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATOMIC || name == C_NAME_BITINT || name == C_NAME_EXTINT) &&
                     i + 1 < end && parser->tokens.kinds[i + 1] == PARSE_KIND_L_PAREN)
            {
                const uint32_t query_end = parser->tree.parents[i + 1];
                check_function_require(output, query_end > i + 1 && query_end < end);
                if (kind == PARSE_KIND_TYPEOF && reader->declared_types[i] != 0)
                {
                    result->alias = reader->declared_types[i];
                    result->alias_first = i;
                    result->alias_end = query_end + 1;
                }
                i = query_end + 1;
            }
            else if (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM)
            {
                i += 1;
                if (i < end && parser->tokens.kinds[i] == PARSE_KIND_IDENTIFIER)
                {
                    i += 1;
                }
                if (i < end && parser->tokens.kinds[i] == PARSE_KIND_L_BRACE)
                {
                    const uint32_t tag_end = parser->tree.parents[i];
                    check_function_require(output, tag_end > i && tag_end < end);
                    i = tag_end + 1;
                }
            }
            else if (role == PARSE_ROLE_OPEN_FUNCTION || role == PARSE_ROLE_OPEN_CALL)
            {
                check_function_require(output, close > i && close < end);
                if (previous == BUSTER_CHECK_NONE)
                {
                    result->functions = i;
                }
                else
                {
                    output->next_functions[previous - output->first] = i;
                }
                output->next_functions[i - output->first] = BUSTER_CHECK_NONE;
                output->parameter_cursors[i - output->first] = i + 1;
                previous = i;
                if (root == 0 || depth > root_depth || (depth == root_depth && root == BUSTER_TYPE_KIND_POINTER))
                {
                    root = BUSTER_TYPE_KIND_FUNCTION;
                    root_depth = depth;
                    root_qualifiers = 0;
                }
                specifiers = false;
                i = close + 1;
            }
            else if (kind == PARSE_KIND_L_SQUARE)
            {
                check_function_require(output, close > i && close < end);
                for (uint32_t j = i + 1; j < close && (parser->tokens.names[j] == C_NAME_STATIC || check_input_qualifier(parser->tokens.names[j], &qualifiers)); j += 1)
                {
                }
                if (root == 0 || depth > root_depth || (depth == root_depth && root == BUSTER_TYPE_KIND_POINTER))
                {
                    root = BUSTER_TYPE_KIND_ARRAY;
                    root_depth = depth;
                    root_qualifiers = qualifiers;
                }
                specifiers = false;
                i = close + 1;
            }
            else if (kind == PARSE_KIND_STAR)
            {
                uint32_t next = i + 1;
                while (next < end && check_input_qualifier(parser->tokens.names[next], &qualifiers))
                {
                    next += 1;
                }
                if (root == 0 || depth > root_depth || (depth == root_depth && root == BUSTER_TYPE_KIND_POINTER))
                {
                    root = BUSTER_TYPE_KIND_POINTER;
                    root_depth = depth;
                    root_qualifiers = qualifiers;
                }
                specifiers = false;
                i = next;
            }
            else
            {
                if (kind == PARSE_KIND_L_PAREN)
                {
                    depth += 1;
                    specifiers = false;
                }
                else if (kind == PARSE_KIND_R_PAREN)
                {
                    check_function_require(output, depth != 0);
                    depth -= depth != 0;
                }
                else if (specifiers && check_input_qualifier(name, &qualifiers))
                {
                    base_qualifiers |= qualifiers;
                }
                else if (specifiers && kind == PARSE_KIND_IDENTIFIER && role == PARSE_ROLE_SPECIFIER && reader->declared_types[i] != 0)
                {
                    result->alias = reader->declared_types[i];
                    result->alias_first = i;
                    result->alias_end = i + 1;
                }
                else if (kind == PARSE_KIND_IDENTIFIER && role == PARSE_ROLE_OPERAND && reader->declared_types[i] != 0)
                {
                    result->name = i;
                    specifiers = false;
                }
                i += 1;
            }
        }
        if (parameter)
        {
            const uint8_t alias_kind = type_kind(reader->types, result->alias);
            result->qualifiers = root != 0 ? root_qualifiers : type_kind_is_array(alias_kind) || alias_kind == BUSTER_TYPE_KIND_FUNCTION ? 0 : base_qualifiers | type_qualifiers(result->alias);
            result->type = result->name != BUSTER_CHECK_NONE ? reader->declared_types[result->name] : type_qualify(reader->types, type, result->qualifiers);
        }
    }
    return index;
}

static bool check_function_alias(const CheckFunctionPrinter* output, uint32_t context, TypeId type)
{
    const CheckFunctionSpelling* spelling = &output->spellings[context];
    return spelling->alias != 0 && type_unqualified(type_strip_aligned(output->reader->types, type)) == type_unqualified(type_strip_aligned(output->reader->types, spelling->alias));
}

static void check_function_alias_print(CheckFunctionPrinter* output, uint32_t context, TypeId type)
{
    TypePrinter* printer = &output->printer;
    const CheckFunctionSpelling* spelling = &output->spellings[context];
    const Parser* parser = output->reader->parser;
    const uint32_t qualifiers = type_qualifiers(type) & ~type_qualifiers(spelling->alias);
    if (qualifiers != 0)
    {
        type_print_qualifiers(printer, qualifiers);
        type_print_text(printer, " ");
    }
    if (spelling->alias_end == spelling->alias_first + 1)
    {
        const uint32_t name = parser->tokens.names[spelling->alias_first];
        type_print_bytes(printer, (const char*)intern_bytes(output->reader->pp->table, name), intern_length(output->reader->pp->table, name));
    }
    else
    {
        const uint32_t first = spelling->alias_first;
        const uint32_t name = parser->tokens.names[first];
        const bool unqualified = name == C_NAME_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL2;
        const bool type_operand = parser->tree.roles[first + 2] == PARSE_ROLE_SPECIFIER;
        type_print_text(printer, unqualified ? "typeof_unqual" : "typeof");
        type_print_text(printer, type_operand ? "(" : " (");
        for (uint32_t i = first + 2; printer->valid && i + 1 < spelling->alias_end; i += 1)
        {
            const uint8_t* bytes = 0;
            uint32_t length = 0;
            if (parser->tokens.names[i] != 0)
            {
                bytes = intern_bytes(output->reader->pp->table, parser->tokens.names[i]);
                length = intern_length(output->reader->pp->table, parser->tokens.names[i]);
            }
            else
            {
                printer->valid &= check_input_spelling(output->reader, i, &bytes, &length);
            }
            const uint8_t role = parser->tree.roles[i];
            const uint8_t previous = i > first + 2 ? parser->tree.roles[i - 1] : PARSE_ROLE_NONE;
            const bool binary = (role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_ASSIGN) || role == PARSE_ROLE_OPEN_TERNARY || role == PARSE_ROLE_CLOSE_TERNARY;
            const bool previous_binary = (previous >= PARSE_ROLE_MULTIPLICATIVE && previous <= PARSE_ROLE_ASSIGN) ||
                                         previous == PARSE_ROLE_OPEN_TERNARY || previous == PARSE_ROLE_CLOSE_TERNARY;
            const bool separated = i > first + 2 && (binary || previous_binary ||
                                   (parser->tokens.names[i - 1] == C_NAME_SIZEOF && parser->tokens.kinds[i] != PARSE_KIND_L_PAREN) ||
                                   parser->tokens.kinds[i - 1] == PARSE_KIND_COMMA ||
                                   ((role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_OPERAND) &&
                                    (previous == PARSE_ROLE_SPECIFIER || previous == PARSE_ROLE_OPERAND)) ||
                                   (type_operand && parser->tokens.kinds[i] == PARSE_KIND_STAR));
            type_print_text(printer, separated ? " " : "");
            type_print_bytes(printer, (const char*)bytes, length);
        }
        type_print_text(printer, ")");
    }
    type_print_space(printer);
}

static uint32_t check_function_parameter_end(const Parser* parser, uint32_t first, uint32_t end)
{
    uint32_t i = first;
    while (i < end && parser->tokens.kinds[i] != PARSE_KIND_COMMA)
    {
        const uint8_t kind = parser->tokens.kinds[i];
        if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
        {
            const uint32_t close = parser->tree.parents[i];
            i = close > i && close < end ? close + 1 : end;
        }
        else
        {
            i += 1;
        }
    }
    return i;
}

static void check_function_print_step(CheckFunctionPrinter* output, TypePrintFrame frame, CheckFunctionPrintContext context)
{
    TypePrinter* printer = &output->printer;
    CheckInputReader* reader = output->reader;
    const TypeId type = type_strip_aligned(reader->types, frame.type);
    const TypeId base = reader->types->types.base[type_index(type)];
    const uint8_t kind = type_kind(reader->types, type);
    const uint64_t depth = printer->depth;
    const bool alias = check_function_alias(output, context.spelling, type);
    const bool outer_function = context.spelling == 0 && type_unqualified(type) == type_unqualified(output->spellings[0].type);
    uint32_t parameter_context = BUSTER_CHECK_NONE;
    if (alias && frame.operation == BUSTER_TYPE_PRINT_BEFORE)
    {
        check_function_alias_print(output, context.spelling, type);
    }
    else if (alias && frame.operation == BUSTER_TYPE_PRINT_AFTER)
    {
    }
    else if (outer_function && frame.operation == BUSTER_TYPE_PRINT_FUNCTION_END)
    {
        printer->empty = frame.saved;
    }
    else if (outer_function && frame.operation == BUSTER_TYPE_PRINT_AFTER)
    {
        type_print_push(printer, BUSTER_TYPE_PRINT_RESTORE, type, printer->empty, false, 0);
        printer->empty = false;
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, base, false, false, 0);
        if ((reader->types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0)
        {
            type_print_text(printer, "(");
            type_print_push(printer, BUSTER_TYPE_PRINT_PARAMETERS, type, false, false, 0);
        }
        else
        {
            type_print_text(printer, "()");
        }
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_PARAMETERS && context.function != BUSTER_CHECK_NONE)
    {
        uint32_t count = 0;
        const TypeId* parameters = type_parameters(reader->types, type, &count);
        if (frame.step < count)
        {
            const uint32_t slot = context.function - output->first;
            const uint32_t first = output->parameter_cursors[slot];
            const uint32_t end = check_function_parameter_end(reader->parser, first, reader->parser->tree.parents[context.function]);
            output->parameter_cursors[slot] = end + 1;
            parameter_context = check_function_spelling(output, first, end, parameters[frame.step], true);
            type_print_text(printer, frame.step != 0 ? ", " : "");
            type_print_push(printer, BUSTER_TYPE_PRINT_PARAMETERS, type, false, false, frame.step + 1);
            type_print_push(printer, BUSTER_TYPE_PRINT_TYPE, output->spellings[parameter_context].type, false, false, 0);
        }
        else
        {
            type_print_step(printer, frame);
        }
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_POINTER_END && check_function_alias(output, context.spelling, base))
    {
        type_print_text(printer, "*");
        printer->empty = frame.saved;
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_AFTER && kind == BUSTER_TYPE_KIND_POINTER && check_function_alias(output, context.spelling, base))
    {
        type_print_push(printer, BUSTER_TYPE_PRINT_RESTORE, type, printer->empty, false, 0);
        printer->empty = false;
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, base, false, false, 0);
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_BEFORE && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM))
    {
        const uint32_t name = reader->types->tags.name[reader->types->types.index[type_index(type)]];
        const uint32_t qualifiers = type_qualifiers(type);
        if (qualifiers != 0)
        {
            type_print_qualifiers(printer, qualifiers);
            type_print_text(printer, " ");
        }
        type_print_text(printer, kind == BUSTER_TYPE_KIND_STRUCT ? "struct " : kind == BUSTER_TYPE_KIND_UNION ? "union " : "enum ");
        type_print_bytes(printer, name != 0 ? (const char*)intern_bytes(reader->pp->table, name) : "(unnamed)", name != 0 ? intern_length(reader->pp->table, name) : 9);
        type_print_space(printer);
    }
    else
    {
        type_print_step(printer, frame);
    }
    printer->valid &= printer->depth <= output->capacity;
    for (uint64_t i = depth; printer->valid && i < printer->depth; i += 1)
    {
        const TypePrintFrame added = ((TypePrintFrame*)printer->frames)[i];
        output->contexts[i] = context;
        if (kind == BUSTER_TYPE_KIND_FUNCTION && added.type == base && context.function != BUSTER_CHECK_NONE)
        {
            output->contexts[i].function = output->next_functions[context.function - output->first];
        }
        if (parameter_context != BUSTER_CHECK_NONE && i + 1 == printer->depth)
        {
            output->contexts[i].spelling = parameter_context;
            output->contexts[i].function = output->spellings[parameter_context].functions;
        }
    }
}

static String8 check_function_pretty(CheckInputReader* reader, uint32_t body)
{
    const uint32_t first = reader->function_specifiers[body];
    const uint32_t name_token = reader->output->body_declarations[body];
    const uint32_t name = reader->parser->tokens.names[name_token];
    const uint32_t capacity = first < body && body - first <= (UINT32_MAX - 16) / 8 ? (body - first) * 8 + 16 : 0;
    const uint64_t scratch_mark = reader->scratch->length;
    CheckFunctionPrinter output =
    {
        .printer =
        {
            .table = reader->types,
            .arena = reader->arena,
            .name =
            {
                .pointer = (char*)intern_bytes(reader->pp->table, name),
                .length = intern_length(reader->pp->table, name),
            },
            .options = reader->parser->language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0,
            .empty = true,
            .scratch = reader->scratch,
        },
        .reader = reader,
        .first = first,
        .end = body,
        .capacity = capacity,
    };
    output.spellings = arena_push_array(reader->scratch, capacity, sizeof(CheckFunctionSpelling), 4);
    output.contexts = arena_push_array(reader->scratch, capacity, sizeof(CheckFunctionPrintContext), 4);
    output.next_functions = arena_push_array(reader->scratch, capacity, sizeof(uint32_t), 4);
    output.parameter_cursors = arena_push_array(reader->scratch, capacity, sizeof(uint32_t), 4);
    output.printer.valid = capacity != 0 && output.spellings != 0 && output.contexts != 0 && output.next_functions != 0 && output.parameter_cursors != 0;
    reader->output->resource |= !output.printer.valid;
    String8 result = { 0 };
    if (output.printer.valid)
    {
        const TypeId type = reader->declared_types[name_token];
        const uint32_t context = check_function_spelling(&output, first, body, type, false);
        type_print_push(&output.printer, BUSTER_TYPE_PRINT_TYPE, type, false, true, 0);
        output.contexts[0] = (CheckFunctionPrintContext)
        {
            .spelling = context,
            .function = output.spellings[context].functions,
        };
        while (output.printer.valid && output.printer.depth != 0)
        {
            output.printer.depth -= 1;
            check_function_print_step(&output, ((TypePrintFrame*)output.printer.frames)[output.printer.depth], output.contexts[output.printer.depth]);
        }
        if (output.printer.valid)
        {
            result = (String8)
            {
                .pointer = output.printer.begin,
                .length = output.printer.length,
            };
        }
    }
    reader->output->resource |= !output.printer.valid && !output.malformed;
    arena_pop_to(reader->scratch, scratch_mark);
    return result;
}

static const CheckInitString* check_input_function_name(CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const CheckInitString* result = 0;
    if (check_input_function_name_token(parser, token))
    {
        uint32_t index = reader->string_ids[token];
        if (index == BUSTER_CHECK_NONE)
        {
            const uint32_t body = reader->function_body;
            const uint32_t kind = parser->tokens.names[token] == C_NAME_FUNC ? 0 : parser->tokens.names[token] == C_NAME_FUNCTION ? 1 : 2;
            const bool file = body == BUSTER_CHECK_NONE;
            if (!file && reader->function_name_body != body)
            {
                reader->function_name_body = body;
                for (uint32_t i = 0; i < 3; i += 1)
                {
                    reader->function_name_ids[i] = BUSTER_CHECK_NONE;
                }
            }
            uint32_t* cache = file ? &reader->file_function_name_ids[kind] : &reader->function_name_ids[kind];
            index = *cache;
            if (index == BUSTER_CHECK_NONE)
            {
                const uint64_t mark = reader->arena->length;
                String8 value = { 0 };
                bool valid = true;
                if (!file && kind == 2)
                {
                    value = check_function_pretty(reader, body);
                    valid = value.pointer != 0;
                }
                else
                {
                    const uint32_t name_token = file ? BUSTER_CHECK_NONE : reader->output->body_declarations[body];
                    const uint32_t name = file ? 0 : parser->tokens.names[name_token];
                    value.length = file ? (kind == 2 ? 9 : 0) : intern_length(reader->pp->table, name);
                    value.pointer = arena_push(reader->arena, value.length, 1);
                    valid = value.length == 0 || value.pointer != 0;
                    reader->output->resource |= !valid;
                    if (valid && value.length != 0)
                    {
                        memcpy(value.pointer, file ? (const uint8_t*)"top level" : intern_bytes(reader->pp->table, name), value.length);
                    }
                }
                uint8_t* zero = valid ? arena_push(reader->arena, 1, 1) : 0;
                reader->output->resource |= valid && zero == 0;
                valid &= zero != 0;
                if (valid)
                {
                    *zero = 0;
                    value.pointer = value.length != 0 ? value.pointer : (char*)zero;
                    const TypeId type = check_input_type_result(reader, type_array(reader->types, type_qualify(reader->types, BUSTER_TYPE_CHAR, BUSTER_TYPE_CONST), value.length + 1));
                    valid = type != 0;
                    if (valid)
                    {
                        index = reader->output->string_count++;
                        reader->output->strings[index] = (CheckInitString)
                        {
                            .object = token + 1,
                            .token = token,
                            .type = type,
                            .bytes = (const uint8_t*)value.pointer,
                            .byte_count = value.length + 1,
                            .predefined = true,
                        };
                        *cache = index;
                    }
                }
                if (!valid)
                {
                    arena_pop_to(reader->arena, mark);
                    if (!reader->output->resource)
                    {
                        check_input_report(reader, token, CHECK_INPUT_UNSUPPORTED);
                    }
                }
            }
            if (index != BUSTER_CHECK_NONE)
            {
                reader->string_ids[token] = index;
                reader->declared_types[token] = reader->output->strings[index].type;
            }
        }
        result = index != BUSTER_CHECK_NONE ? &reader->output->strings[index] : 0;
    }
    return result;
}
