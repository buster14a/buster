// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Declaration-time integer expressions and type names. Source-order binding capture precedes the parser's
// postorder evaluation; every value retains its actual declaration and target type. Type-name declarators use
// an explicit stack and consume array bounds already evaluated in postorder. They never call the declaration
// reader, which itself calls this module for bounds and attributes. This keeps the entire call graph acyclic.
// Temporary arrays have one record per token in the requested bounded slice, with no padding assumption.

typedef struct CheckConstantAttributes CheckConstantAttributes;
struct CheckConstantAttributes
{
    CheckInputAttributes parsed;
    uint32_t owner;
    bool publish;
};

typedef struct CheckConstantInput CheckConstantInput;
struct CheckConstantInput
{
    CheckInputReader* reader;
    uint32_t first;
    uint32_t end;
    CheckValue* values;
    uint8_t* present;
    uint8_t* syntax;
    uint32_t* first_value;
    uint32_t* last_value;
    uint32_t* next_value;
    uint32_t* type_begin;
    TypeId* queries;
    uint8_t* attribute_syntax;
    CheckConstantAttributes** attribute_starts;
    CheckConstantAttributes** attribute_ends;
    uint32_t* tag_finishes;
    uint32_t* tag_next;
    bool raw;
    bool cached;
};

typedef struct CheckConstantPart CheckConstantPart;
struct CheckConstantPart
{
    uint32_t next;
    uint32_t token;
    uint32_t qualifiers;
    uint32_t parameter_first;
    uint32_t parameter_last;
    uint32_t parameter_count;
    uint64_t extent;
    uint8_t kind;
    uint8_t flags;
};

typedef struct CheckConstantParameter CheckConstantParameter;
struct CheckConstantParameter
{
    TypeId type;
    uint32_t next;
    uint32_t name;
};

typedef struct CheckConstantTypeFrame CheckConstantTypeFrame;
struct CheckConstantTypeFrame
{
    TypeId base;
    uint32_t cursor;
    uint32_t end;
    uint32_t first;
    uint32_t last;
    uint32_t pointers;
    uint32_t function;
    uint32_t name;
    uint8_t state;
    bool group;
    bool parameter;
};

static CheckConstantTypeFrame check_constant_type_frame(TypeId base, uint32_t first, uint32_t end, bool group, bool parameter)
{
    return (CheckConstantTypeFrame)
    {
        .base = base,
        .cursor = first,
        .end = end,
        .first = BUSTER_CHECK_NONE,
        .last = BUSTER_CHECK_NONE,
        .pointers = BUSTER_CHECK_NONE,
        .function = BUSTER_CHECK_NONE,
        .name = BUSTER_CHECK_NONE,
        .group = group,
        .parameter = parameter,
    };
}

// This intentionally does not call check_input_specifiers: that reader may evaluate an attribute operand.
// All identifier and tag lookup was captured by check_constant_bind in source order before this pass.
static TypeId check_constant_specifiers(CheckConstantInput* input, uint32_t* cursor, uint32_t end, bool parameter)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    uint32_t i = *cursor;
    uint32_t qualifiers = 0;
    uint32_t longs = 0;
    uint32_t shorts = 0;
    uint32_t signedness = 0;
    TypeId base = 0;
    bool integer_seen = false;
    bool atomic = false;
    bool complex = false;
    bool named_type = false;
    bool bit_int_specifier = false;
    bool register_seen = false;
    bool saw = false;
    bool valid = true;
    bool reading = true;
    while (reading && i < end)
    {
        const uint32_t name = parser->tokens.names[i];
        const uint8_t kind = parser->tokens.kinds[i];
        bool consumed = true;
        if (check_input_qualifier(name, &qualifiers))
        {
        }
        else if (kind == PARSE_KIND_STORAGE)
        {
            valid &= parameter && name == C_NAME_REGISTER && !register_seen;
            register_seen = true;
        }
        else if (name == C_NAME_COMPLEX_C99 || name == C_NAME_GNU_COMPLEX || name == C_NAME_GNU_COMPLEX2)
        {
            valid &= !complex;
            complex = true;
        }
        else if (kind == PARSE_KIND_ATOMIC && (i + 1 >= end || parser->tokens.kinds[i + 1] != PARSE_KIND_L_PAREN))
        {
            atomic = true;
        }
        else if ((kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATOMIC) && i + 1 < end &&
                 (parser->tree.roles[i + 1] == PARSE_ROLE_OPEN_SPECIFIER || (input->raw && parser->tokens.kinds[i + 1] == PARSE_KIND_L_PAREN)))
        {
            const uint32_t close = parser->tree.parents[i + 1];
            const TypeId selected = close > i + 1 && close < end ? input->queries[close - input->first] : 0;
            valid &= selected != 0 && base == 0;
            base = selected;
            named_type = true;
            bit_int_specifier = name == C_NAME_BITINT || name == C_NAME_EXTINT;
            i = close > i + 1 && close < end ? close : i;
        }
        else if (name == C_NAME_LONG)
        {
            longs += 1;
        }
        else if (name == C_NAME_SHORT)
        {
            shorts += 1;
        }
        else if (name == C_NAME_SIGNED || name == C_NAME_GNU_SIGNED || name == C_NAME_GNU_SIGNED2 || name == C_NAME_UNSIGNED)
        {
            valid &= signedness == 0;
            signedness = name == C_NAME_UNSIGNED ? 2 : 1;
        }
        else if (name == C_NAME_INT)
        {
            valid &= !integer_seen;
            integer_seen = true;
        }
        else if (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM)
        {
            const TypeId selected = reader->tag_types[i];
            valid &= selected != 0 && base == 0;
            base = selected;
            named_type = true;
            i = selected != 0 ? reader->tag_ends[i] - 1 : i;
        }
        else if (kind == PARSE_KIND_TYPE)
        {
            TypeId selected = 0;
            switch (name)
            {
                case C_NAME_CHAR: selected = BUSTER_TYPE_CHAR; break;
                case C_NAME_VOID: selected = BUSTER_TYPE_VOID; break;
                case C_NAME_BOOL: case C_NAME_BOOL_C99: selected = BUSTER_TYPE_BOOL; break;
                case C_NAME_FLOAT: selected = BUSTER_TYPE_FLOAT; break;
                case C_NAME_DOUBLE: selected = BUSTER_TYPE_DOUBLE; break;
                case C_NAME_INT128: selected = BUSTER_TYPE_INT128; break;
                case C_NAME_FLOAT16: selected = BUSTER_TYPE_FLOAT16; break;
                case C_NAME_FLOAT128: selected = BUSTER_TYPE_FLOAT128; break;
                case C_NAME_BF16: selected = BUSTER_TYPE_BF16; break;
                default: break;
            }
            valid &= base == 0 && selected != 0;
            base = selected;
        }
        else if (kind == PARSE_KIND_IDENTIFIER && (parser->tree.roles[i] == PARSE_ROLE_SPECIFIER || input->raw) && base == 0 && !integer_seen && !longs && !shorts && !signedness)
        {
            base = reader->declared_types[i];
            named_type = true;
            valid &= base != 0;
        }
        else
        {
            consumed = false;
            reading = false;
        }
        if (consumed)
        {
            saw = true;
            i += 1;
        }
    }
    valid &= saw && (base != 0 || integer_seen || longs || shorts || signedness || complex) && longs <= 2 && shorts <= 1 && !(longs && shorts);
    if (named_type && !bit_int_specifier)
    {
        valid &= !longs && !shorts && !signedness && !integer_seen && !complex;
    }
    else if (base == 0)
    {
        base = complex && !integer_seen && !longs && !shorts && !signedness ? BUSTER_TYPE_DOUBLE :
               shorts ? (signedness == 2 ? BUSTER_TYPE_USHORT : BUSTER_TYPE_SHORT) :
               longs == 2 ? (signedness == 2 ? BUSTER_TYPE_ULLONG : BUSTER_TYPE_LLONG) :
               longs == 1 ? (signedness == 2 ? BUSTER_TYPE_ULONG : BUSTER_TYPE_LONG) :
                            (signedness == 2 ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT);
    }
    else if (base == BUSTER_TYPE_CHAR)
    {
        valid &= !longs && !shorts && !integer_seen;
        base = signedness == 2 ? BUSTER_TYPE_UCHAR : signedness == 1 ? BUSTER_TYPE_SCHAR : base;
    }
    else if (base == BUSTER_TYPE_DOUBLE)
    {
        valid &= longs <= 1 && !shorts && !signedness && !integer_seen;
        base = longs ? BUSTER_TYPE_LONG_DOUBLE : base;
    }
    else if (base == BUSTER_TYPE_INT128)
    {
        valid &= !longs && !shorts && !integer_seen;
        base = signedness == 2 ? BUSTER_TYPE_UINT128 : base;
    }
    else if (type_kind(reader->types, base) == BUSTER_TYPE_KIND_BIT_INT)
    {
        valid &= !longs && !shorts && !integer_seen;
        const uint64_t width = reader->types->types.extent[type_index(base)];
        base = valid ? check_input_type_result(reader, type_bit_int(reader->types, (uint32_t)width, signedness == 2)) : 0;
    }
    else
    {
        valid &= !longs && !shorts && !signedness && !integer_seen;
    }
    if ((qualifiers & BUSTER_TYPE_RESTRICT) != 0)
    {
        valid &= type_kind(reader->types, base) == BUSTER_TYPE_KIND_POINTER &&
                 type_kind(reader->types, type_element(reader->types, base)) != BUSTER_TYPE_KIND_FUNCTION;
    }
    if (valid && complex)
    {
        const uint8_t code = type_arithmetic_code(reader->types, base);
        valid = code < 64 && ((BUSTER_TYPE_COMPLEX_SPELLABLE >> code) & 1) != 0;
        base = valid ? check_input_type_result(reader, type_complex(reader->types, base)) : 0;
    }
    if (valid && atomic)
    {
        base = check_input_type_result(reader, type_atomic(reader->types, base));
    }
    *cursor = i;
    return valid ? check_input_type_result(reader, type_qualify(reader->types, base, qualifiers)) : 0;
}

static bool check_constant_range_value(CheckConstantInput* input, uint32_t first, uint32_t end, CheckValue* value)
{
    const Parser* parser = input->reader->parser;
    uint32_t root = BUSTER_CHECK_NONE;
    uint32_t roots = 0;
    if (input->raw && first < end)
    {
        root = first;
        roots = 1;
    }
    for (uint32_t token = first; !input->raw && token < end; token += 1)
    {
        const uint32_t parent = parser->tree.parents[token];
        if (parent < first || parent >= end)
        {
            root = token;
            roots += 1;
        }
    }
    const bool valid = roots == 1 && root >= input->first && root < input->end && input->present[root - input->first];
    if (valid)
    {
        *value = input->values[root - input->first];
    }
    return valid;
}

static TypeId check_constant_type(CheckConstantInput* input, uint32_t first, uint32_t end)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    const uint64_t mark = reader->scratch->length;
    const uint32_t capacity = end - first + 1;
    CheckConstantTypeFrame* frames = arena_push_array(reader->scratch, capacity, sizeof(CheckConstantTypeFrame), 8);
    CheckConstantPart* parts = arena_push_array(reader->scratch, capacity, sizeof(CheckConstantPart), 8);
    CheckConstantParameter* parameters = arena_push_array(reader->scratch, capacity, sizeof(CheckConstantParameter), 4);
    reader->output->resource |= frames == 0 || parts == 0 || parameters == 0;
    uint32_t cursor = first;
    TypeId result = 0;
    const TypeId base = !reader->output->resource ? check_constant_specifiers(input, &cursor, end, false) : 0;
    bool valid = base != 0;
    uint32_t depth = 0;
    uint32_t part_count = 0;
    uint32_t parameter_count = 0;
    if (valid)
    {
        frames[depth++] = check_constant_type_frame(base, cursor, end, false, false);
    }
    while (valid && depth != 0)
    {
        CheckConstantTypeFrame* frame = &frames[depth - 1];
        uint32_t i = frame->cursor;
        if (frame->state == 0)
        {
            while (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_STAR)
            {
                const uint32_t token = i++;
                uint32_t qualifiers = 0;
                bool atomic = false;
                while (i < frame->end && (check_input_qualifier(parser->tokens.names[i], &qualifiers) || parser->tokens.names[i] == C_NAME_ATOMIC))
                {
                    atomic |= parser->tokens.names[i] == C_NAME_ATOMIC;
                    i += 1;
                }
                parts[part_count] = (CheckConstantPart)
                {
                    .next = frame->pointers,
                    .token = token,
                    .qualifiers = qualifiers,
                    .kind = BUSTER_TYPE_KIND_POINTER,
                    .flags = atomic,
                };
                frame->pointers = part_count++;
            }
            frame->state = 1;
            if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_IDENTIFIER && parser->tree.roles[i] == PARSE_ROLE_OPERAND)
            {
                frame->name = i++;
            }
            else if (i < frame->end && parser->tree.roles[i] == PARSE_ROLE_OPEN_GROUP)
            {
                const uint32_t close = parser->tree.parents[i];
                valid = close > i && close < frame->end;
                if (valid)
                {
                    frame->cursor = close + 1;
                    frames[depth++] = check_constant_type_frame(frame->base, i + 1, close, true, false);
                }
            }
            frame->cursor = frame == &frames[depth - 1] ? i : frame->cursor;
        }
        else if (frame->state == 2)
        {
            CheckConstantPart* function = &parts[frame->function];
            const uint32_t close = parser->tree.parents[function->token];
            if (i == close)
            {
                frame->cursor = close + 1;
                frame->state = 1;
            }
            else if (i < close && parser->tokens.kinds[i] == PARSE_KIND_ELLIPSIS)
            {
                valid = (function->parameter_count != 0 || parser->language.standard >= 2023) && i + 1 == close;
                function->flags |= BUSTER_TYPE_FLAG_VARIADIC;
                frame->cursor = i + 1;
            }
            else if (i < close)
            {
                const TypeId parameter = check_constant_specifiers(input, &i, close, true);
                valid = parameter != 0;
                if (valid)
                {
                    frames[depth++] = check_constant_type_frame(parameter, i, close, false, true);
                }
            }
            else
            {
                valid = false;
            }
        }
        else if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_L_SQUARE)
        {
            const uint32_t close = parser->tree.parents[i];
            valid = close > i && close < frame->end;
            CheckConstantPart part =
            {
                .next = BUSTER_CHECK_NONE,
                .token = i,
                .kind = BUSTER_TYPE_KIND_INCOMPLETE_ARRAY,
            };
            if (valid && i + 1 != close)
            {
                CheckValue bound = { 0 };
                int64_t extent = 0;
                valid = check_constant_range_value(input, i + 1, close, &bound) && check_action_is_integer(reader->types, bound.type);
                if (valid && (bound.flags & BUSTER_CHECK_ICE) != 0)
                {
                    valid = check_action_i64(reader->types, &bound, &extent) && extent >= 0 && (extent != 0 || parser->language.gnu);
                    part.kind = BUSTER_TYPE_KIND_ARRAY;
                    part.extent = (uint64_t)extent;
                }
                else if (valid)
                {
                    part.kind = BUSTER_TYPE_KIND_VARIABLE_ARRAY;
                    part.extent = (uint64_t)i + 2;
                }
            }
            if (valid)
            {
                if (frame->last != BUSTER_CHECK_NONE)
                {
                    parts[frame->last].next = part_count;
                }
                else
                {
                    frame->first = part_count;
                }
                parts[part_count] = part;
                frame->last = part_count++;
                frame->cursor = close + 1;
            }
        }
        else if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_L_PAREN &&
                 (parser->tree.roles[i] == PARSE_ROLE_OPEN_FUNCTION || parser->tree.roles[i] == PARSE_ROLE_OPEN_CALL))
        {
            const uint32_t close = parser->tree.parents[i];
            valid = close > i && close < frame->end;
            if (valid)
            {
                if (frame->last != BUSTER_CHECK_NONE)
                {
                    parts[frame->last].next = part_count;
                }
                else
                {
                    frame->first = part_count;
                }
                parts[part_count] = (CheckConstantPart)
                {
                    .next = BUSTER_CHECK_NONE,
                    .token = i,
                    .parameter_first = BUSTER_CHECK_NONE,
                    .parameter_last = BUSTER_CHECK_NONE,
                    .kind = BUSTER_TYPE_KIND_FUNCTION,
                    .flags = i + 1 != close || parser->language.standard >= 2023 ? BUSTER_TYPE_FLAG_PROTOTYPE : 0,
                };
                frame->function = part_count;
                frame->last = part_count++;
                frame->state = 2;
                frame->cursor = i + 1;
            }
        }
        else
        {
            valid = frame->parameter ? i <= frame->end && (i == frame->end || parser->tokens.kinds[i] == PARSE_KIND_COMMA) : i == frame->end;
            if (frame->last != BUSTER_CHECK_NONE)
            {
                parts[frame->last].next = frame->pointers;
            }
            else
            {
                frame->first = frame->pointers;
            }
            if (frame->pointers != BUSTER_CHECK_NONE)
            {
                frame->last = frame->pointers;
                while (parts[frame->last].next != BUSTER_CHECK_NONE)
                {
                    frame->last = parts[frame->last].next;
                }
            }
            if (valid && frame->group)
            {
                CheckConstantTypeFrame* parent = &frames[depth - 2];
                parent->name = frame->name;
                parent->first = frame->first;
                parent->last = frame->last;
                depth -= 1;
            }
            else if (valid)
            {
                uint32_t reverse = BUSTER_CHECK_NONE;
                for (uint32_t p = frame->first; p != BUSTER_CHECK_NONE;)
                {
                    const uint32_t next = parts[p].next;
                    parts[p].next = reverse;
                    reverse = p;
                    p = next;
                }
                TypeId type = frame->base;
                for (uint32_t p = reverse; type != 0 && p != BUSTER_CHECK_NONE; p = parts[p].next)
                {
                    const CheckConstantPart* part = &parts[p];
                    if (part->kind == BUSTER_TYPE_KIND_POINTER)
                    {
                        const bool restricted_function = (part->qualifiers & BUSTER_TYPE_RESTRICT) != 0 && type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION;
                        type = restricted_function ? 0 : check_input_type_result(reader, type_pointer(reader->types, type));
                        type = type != 0 && part->flags ? check_input_type_result(reader, type_atomic(reader->types, type)) : type;
                        type = type != 0 ? check_input_type_result(reader, type_qualify(reader->types, type, part->qualifiers)) : 0;
                    }
                    else if (part->kind == BUSTER_TYPE_KIND_FUNCTION)
                    {
                        const uint64_t parameter_mark = reader->scratch->length;
                        TypeId* types = arena_push_array(reader->scratch, (uint64_t)part->parameter_count + 1, sizeof(TypeId), 4);
                        reader->output->resource |= types == 0;
                        uint32_t count = 0;
                        for (uint32_t parameter = part->parameter_first; types != 0 && parameter != BUSTER_CHECK_NONE; parameter = parameters[parameter].next)
                        {
                            types[count++] = parameters[parameter].type;
                        }
                        type = types != 0 ? check_input_type_result(reader, type_function(reader->types, type, types, count, part->flags)) : 0;
                        arena_pop_to(reader->scratch, parameter_mark);
                    }
                    else
                    {
                        type = check_input_type_result(reader, part->kind == BUSTER_TYPE_KIND_ARRAY ? type_array(reader->types, type, part->extent) :
                               part->kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY ? type_variable_array(reader->types, type, (uint32_t)part->extent) :
                                                                              type_incomplete_array(reader->types, type));
                    }
                }
                valid = type != 0;
                if (valid && frame->parameter)
                {
                    CheckConstantTypeFrame* parent = &frames[depth - 2];
                    CheckConstantPart* function = &parts[parent->function];
                    const bool only_void = type == BUSTER_TYPE_VOID && frame->name == BUSTER_CHECK_NONE && function->parameter_count == 0 && i == frame->end;
                    if (!only_void)
                    {
                        const TypeId adjusted = check_input_type_result(reader, type_adjust_parameter(reader->types, type));
                        valid = adjusted != 0 && type_kind(reader->types, adjusted) != BUSTER_TYPE_KIND_VOID;
                        if (valid)
                        {
                            const uint32_t name = frame->name != BUSTER_CHECK_NONE ? parser->tokens.names[frame->name] : 0;
                            for (uint32_t previous = function->parameter_first; valid && name != 0 && previous != BUSTER_CHECK_NONE; previous = parameters[previous].next)
                            {
                                valid = parameters[previous].name != name;
                            }
                            parameters[parameter_count] = (CheckConstantParameter)
                            {
                                .type = adjusted,
                                .next = BUSTER_CHECK_NONE,
                                .name = name,
                            };
                            if (function->parameter_last != BUSTER_CHECK_NONE)
                            {
                                parameters[function->parameter_last].next = parameter_count;
                            }
                            else
                            {
                                function->parameter_first = parameter_count;
                            }
                            function->parameter_last = parameter_count++;
                            function->parameter_count += 1;
                        }
                    }
                    parent->cursor = i;
                    if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_COMMA)
                    {
                        parent->cursor = i + 1;
                        valid &= i + 1 < frame->end;
                    }
                    depth -= 1;
                }
                else if (valid)
                {
                    valid = frame->name == BUSTER_CHECK_NONE;
                    result = type;
                    depth -= 1;
                }
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
    return valid ? result : 0;
}

// Mark only declarator syntax. Array-bound expressions remain ordinary expression nodes and can themselves
// contain casts/type queries; the increasing source-order pass will refine each nested type range afterward.
static void check_constant_mark_type(CheckConstantInput* input, uint32_t first, uint32_t end)
{
    const Parser* parser = input->reader->parser;
    for (uint32_t token = first; token < end; token += 1)
    {
        input->syntax[token - input->first] = true;
    }
    for (uint32_t token = first; token < end; token += 1)
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_L_SQUARE)
        {
            const uint32_t close = parser->tree.parents[token];
            if (close > token && close < end)
            {
                for (uint32_t item = token + 1; item < close; item += 1)
                {
                    input->syntax[item - input->first] = false;
                }
                token = close;
            }
        }
    }
}

static uint32_t check_constant_punctuation(const Parser* parser, uint32_t first, uint32_t end, uint8_t punctuation)
{
    uint32_t token = first;
    while (token < end && parser->tokens.kinds[token] != punctuation)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
        {
            const uint32_t close = parser->tree.parents[token];
            token = close > token && close < end ? close + 1 : end;
        }
        else
        {
            token += 1;
        }
    }
    return token;
}

static uint32_t check_constant_separator(CheckConstantInput* input, uint32_t first, uint32_t end)
{
    return check_constant_punctuation(input->reader->parser, first, end, PARSE_KIND_COMMA);
}

static bool check_constant_type_start(const Parser* parser, uint32_t token)
{
    const uint8_t kind = parser->tokens.kinds[token];
    return parser->tree.roles[token] == PARSE_ROLE_SPECIFIER || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ||
           kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATOMIC;
}

static bool check_constant_raw_type_start(CheckConstantInput* input, uint32_t token);
static bool check_constant_raw_expression(CheckConstantInput* input, uint32_t first, uint32_t end);

// Attribute syntax is scanned once, without evaluating its operands. Each group is an explicit dependency job:
// normal expressions complete it at the group's postorder close; raw attribute tokens complete it inside out.
static bool check_constant_attribute_prepare(CheckConstantInput* input, uint32_t owner, uint32_t* cursor)
{
    CheckInputReader* reader = input->reader;
    const uint32_t first = *cursor;
    bool valid = true;
    if (check_input_attribute_starts(reader->parser, first, input->end))
    {
        CheckConstantAttributes* job = arena_push(reader->scratch, sizeof(CheckConstantAttributes), 8);
        reader->output->resource |= job == 0;
        valid = job != 0;
        if (valid)
        {
            *job = (CheckConstantAttributes)
            {
                .owner = owner,
                .publish = !input->cached && reader->output->attributes[owner] == 0,
            };
            valid = check_input_attributes(reader, cursor, input->end, &job->parsed);
        }
        if (valid)
        {
            input->attribute_starts[first - input->first] = job;
            input->attribute_ends[*cursor - 1 - input->first] = job;
            memset(input->attribute_syntax + first - input->first, 1, *cursor - first);
            memset(input->syntax + first - input->first, 1, *cursor - first);
            for (uint32_t i = 0; valid && i < job->parsed.count; i += 1)
            {
                const CheckInputAttribute* attribute = &job->parsed.records[i];
                const bool scalar = attribute->schema == CHECK_ATTRIBUTE_INTEGER;
                const bool list = attribute->schema == CHECK_ATTRIBUTE_INTEGER_LIST || attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS;
                for (uint32_t argument = attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS; valid && !attribute->resolved &&
                     (scalar || list) && argument < (scalar ? 1 : attribute->argument_count); argument += 1)
                {
                    uint32_t begin = attribute->argument_first;
                    uint32_t end = attribute->argument_end;
                    valid = scalar || check_input_attribute_argument(reader->parser, attribute, argument, &begin, &end);
                    if (valid)
                    {
                        memset(input->attribute_syntax + begin - input->first, 0, end - begin);
                        memset(input->syntax + begin - input->first, 0, end - begin);
                        const bool type = attribute->kind == CHECK_ATTRIBUTE_ALIGNAS &&
                                          (input->raw ? check_constant_raw_type_start(input, begin) : check_constant_type_start(reader->parser, begin));
                        if (type)
                        {
                            check_constant_mark_type(input, begin, end);
                        }
                    }
                }
            }
        }
    }
    return valid;
}

static bool check_constant_tag_prepare(CheckConstantInput* input, uint32_t token)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    uint32_t cursor = token + 1;
    bool valid = check_constant_attribute_prepare(input, token, &cursor);
    if (valid && cursor < input->end && parser->tokens.kinds[cursor] == PARSE_KIND_IDENTIFIER)
    {
        input->attribute_syntax[cursor - input->first] = true;
        cursor += 1;
    }
    valid = valid && check_constant_attribute_prepare(input, token, &cursor);
    if (valid)
    {
        input->tag_next[token - input->first] = input->tag_finishes[cursor - input->first];
        input->tag_finishes[cursor - input->first] = token + 1;
    }
    return valid;
}

// A tag activation follows all operands in its header. Deferring this small job preserves the binding stream's
// monotonically increasing source points even when an attribute introduces a nested, previously unseen tag.
static bool check_constant_tag_finish(CheckConstantInput* input, uint32_t position)
{
    bool valid = true;
    for (uint32_t next = input->tag_finishes[position - input->first]; valid && next != 0;)
    {
        const uint32_t token = next - 1;
        valid = (input->cached ? input->reader->tag_types[token] : check_input_tag_identity(input->reader, token, input->end, false)) != 0;
        next = input->tag_next[token - input->first];
    }
    return valid;
}

static bool check_constant_attribute_evaluate(CheckConstantInput* input, CheckConstantAttributes* job)
{
    CheckInputReader* reader = input->reader;
    bool valid = true;
    for (uint32_t i = 0; valid && i < job->parsed.count; i += 1)
    {
        const CheckInputAttribute* attribute = &job->parsed.records[i];
        const bool scalar = attribute->schema == CHECK_ATTRIBUTE_INTEGER;
        const bool list = attribute->schema == CHECK_ATTRIBUTE_INTEGER_LIST || attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS;
        for (uint32_t argument = attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS; valid && !attribute->resolved &&
             (scalar || list) && argument < (scalar ? 1 : attribute->argument_count); argument += 1)
        {
            uint32_t first = attribute->argument_first;
            uint32_t end = attribute->argument_end;
            valid = scalar || check_input_attribute_argument(reader->parser, attribute, argument, &first, &end);
            const bool type_argument = valid && attribute->kind == CHECK_ATTRIBUTE_ALIGNAS &&
                                       (input->raw ? check_constant_raw_type_start(input, first) : check_constant_type_start(reader->parser, first));
            int64_t integer = 0;
            if (valid && type_argument)
            {
                const TypeId type = check_constant_type(input, first, end);
                valid = type != 0 && type_is_complete(reader->types, type);
                integer = valid ? (int64_t)1 << type_alignment_log2(reader->types, type) : 0;
            }
            else if (valid)
            {
                CheckValue value = { 0 };
                valid = (!input->raw || check_constant_raw_expression(input, first, end)) &&
                        check_constant_range_value(input, first, end, &value) && (value.flags & BUSTER_CHECK_ICE) != 0 &&
                        check_action_i64(reader->types, &value, &integer);
            }
            valid = valid && (scalar ? check_input_attribute_value(reader, &job->parsed, i, integer) :
                                       check_input_attribute_argument_value(reader, &job->parsed, i, argument, integer));
        }
    }
    if (valid && job->publish)
    {
        CheckInputAttributes* combined = arena_push(reader->arena, sizeof(CheckInputAttributes), 8);
        reader->output->resource |= combined == 0;
        valid = combined != 0;
        if (valid)
        {
            const CheckInputAttributes empty = { 0 };
            CheckInputAttributes* previous = reader->output->attributes[job->owner];
            valid = check_input_attribute_merge(reader, previous != 0 ? previous : &empty, &job->parsed, combined);
            reader->output->attributes[job->owner] = valid ? combined : previous;
        }
    }
    return valid;
}

static uint16_t check_constant_integer_builtin(CheckConstantInput* input, uint32_t token, uint32_t handle)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    const uint32_t open = token + 1;
    const uint32_t close = open < input->end ? parser->tree.parents[open] : input->end;
    uint16_t builtin = CBUILTIN_NONE;
    if (handle == 0 && open < close && close < input->end && parser->tokens.kinds[open] == PARSE_KIND_L_PAREN &&
        (input->raw || parser->tree.roles[close] == PARSE_ROLE_CLOSE_CALL))
    {
        builtin = reader->builtins[close];
        if (!input->cached)
        {
            const uint32_t identifier = parser->tokens.names[token];
            builtin = reader->builtin_names[identifier];
            if (builtin == UINT16_MAX)
            {
                const InternSpan span = reader->pp->table->spans[identifier];
                const char* spelling = (const char*)reader->pp->table->records + span.offset;
                builtin = (uint16_t)(parser->options.scalar ? cbuiltin_lookup_scalar(spelling, span.length) : cbuiltin_lookup(spelling, span.length));
                reader->builtin_names[identifier] = builtin;
            }
            if (builtin > CBUILTIN_NONE && builtin < CBUILTIN_COUNT)
            {
                reader->builtins[close] = builtin;
            }
        }
    }
    return builtin < CBUILTIN_COUNT ? builtin : CBUILTIN_NONE;
}

static bool check_constant_bind(CheckConstantInput* input, bool type_name)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    bool valid = true;
    if (type_name)
    {
        check_constant_mark_type(input, input->first, input->end);
    }
    for (uint32_t token = input->first; valid && token < input->end; token += 1)
    {
        const uint32_t index = token - input->first;
        valid = check_constant_tag_finish(input, token);
        if (!valid || input->attribute_syntax[index])
        {
            continue;
        }
        const uint8_t role = parser->tree.roles[token];
        const uint8_t kind = parser->tokens.kinds[token];
        const uint32_t name = parser->tokens.names[token];
        if (role == PARSE_ROLE_OPEN_CAST || role == PARSE_ROLE_OPEN_SIZEOF)
        {
            const uint32_t close = parser->tree.parents[token];
            const uint32_t next = token + 1;
            valid = close > token && close < input->end;
            if (valid && next < close && check_constant_type_start(parser, next))
            {
                input->type_begin[close - input->first] = next;
                check_constant_mark_type(input, next, close);
            }
        }
        else if (role == PARSE_ROLE_OPEN_SPECIFIER && token > input->first)
        {
            const uint32_t close = parser->tree.parents[token];
            const uint32_t next = token + 1;
            valid = close > next && close < input->end;
            if (valid)
            {
                input->syntax[close - input->first] = false;
                if (check_constant_type_start(parser, next))
                {
                    input->type_begin[close - input->first] = next;
                    check_constant_mark_type(input, next, close);
                }
                else
                {
                    for (uint32_t item = next; item < close; item += 1)
                    {
                        input->syntax[item - input->first] = false;
                    }
                }
            }
        }
        else if (role == PARSE_ROLE_OPEN_KEYWORD && token > input->first)
        {
            const uint32_t keyword = parser->tokens.names[token - 1];
            const uint32_t close = parser->tree.parents[token];
            valid = close > token && close < input->end;
            if (valid && (keyword == C_NAME_BUILTIN_TYPES_COMPATIBLE_P || keyword == C_NAME_BUILTIN_OFFSETOF))
            {
                const uint32_t separator = check_constant_separator(input, token + 1, close);
                valid = separator < close;
                if (valid)
                {
                    check_constant_mark_type(input, token + 1, separator);
                    input->syntax[separator - input->first] = true;
                    check_constant_mark_type(input, separator + 1, close);
                }
            }
            else if (valid && keyword == C_NAME_GENERIC)
            {
                const uint32_t separator = check_constant_separator(input, token + 1, close);
                valid = separator < close;
                uint32_t association = separator + 1;
                while (valid && association < close)
                {
                    const uint32_t colon = check_constant_punctuation(parser, association, close, PARSE_KIND_COLON);
                    const uint32_t after = colon < close ? check_constant_separator(input, colon + 1, close) : close;
                    valid = colon < close && colon + 1 < after;
                    if (valid)
                    {
                        check_constant_mark_type(input, association, colon);
                        input->syntax[colon - input->first] = true;
                        association = after + 1;
                    }
                }
            }
        }
        if (valid && input->syntax[index] && (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM))
        {
            valid = check_constant_tag_prepare(input, token);
        }
        else if (valid && kind == PARSE_KIND_IDENTIFIER && role != PARSE_ROLE_MEMBER && role != PARSE_ROLE_TAG && role != PARSE_ROLE_LEAF_RIGHT &&
                 (!input->syntax[index] || role == PARSE_ROLE_SPECIFIER))
        {
            const uint32_t handle = input->cached ? 0 : c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name);
            const uint16_t builtin = !input->syntax[index] ? check_constant_integer_builtin(input, token, handle) : CBUILTIN_NONE;
            if (builtin != CBUILTIN_NONE)
            {
                input->syntax[index] = true;
            }
            else if (input->cached)
            {
                valid = reader->declared_types[token] != 0;
            }
            else if (handle != 0)
            {
                const uint32_t declaration = reader->bindings.declaration[handle - 1];
                const bool is_typedef = reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF;
                valid = is_typedef == (role == PARSE_ROLE_SPECIFIER);
                if (valid)
                {
                    reader->output->declarations[token] = declaration;
                    reader->declared_types[token] = reader->declared_types[declaration];
                    if (c_bindings_use(&reader->bindings, C_BINDING_ORDINARY, name, token, token) == 0)
                    {
                        reader->output->resource = true;
                        valid = false;
                    }
                }
            }
            else if (role == PARSE_ROLE_SPECIFIER)
            {
                reader->declared_types[token] = check_input_predefined_type(reader, name);
                valid = reader->declared_types[token] != 0;
            }
            else
            {
                valid = false;
            }
        }
    }
    return valid && check_constant_tag_finish(input, input->end);
}

static TypeId check_constant_control_type(CheckInputReader* reader, const CheckValue* value)
{
    TypeId type = type_conversion_operand(reader->types, value->type);
    const uint8_t kind = type_kind(reader->types, type);
    if (type_kind_is_array(kind))
    {
        type = check_input_type_result(reader, type_pointer(reader->types, type_element(reader->types, type)));
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        type = check_input_type_result(reader, type_pointer(reader->types, type));
    }
    return type;
}

static bool check_constant_selection(CheckConstantInput* input, uint32_t keyword, uint32_t first, uint32_t separator, uint32_t token, CheckValue* value)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    CheckValue control = { 0 };
    bool valid = check_constant_range_value(input, first, separator, &control);
    if (valid && keyword == C_NAME_BUILTIN_CHOOSE_EXPR)
    {
        const uint32_t next = check_constant_separator(input, separator + 1, token);
        bool condition = false;
        valid = next < token && (control.flags & BUSTER_CHECK_ICE) != 0 && check_action_truth(reader->types, &control, &condition);
        if (valid)
        {
            valid = check_constant_range_value(input, condition ? separator + 1 : next + 1, condition ? next : token, value);
        }
    }
    else if (valid)
    {
        const uint64_t mark = reader->scratch->length;
        const TypeId control_type = check_constant_control_type(reader, &control);
        TypeId* seen = arena_push_array(reader->scratch, token - separator, sizeof(TypeId), 4);
        reader->output->resource |= seen == 0;
        valid = seen != 0 && control_type != 0;
        uint32_t seen_count = 0;
        uint32_t chosen_first = BUSTER_CHECK_NONE;
        uint32_t chosen_end = 0;
        uint32_t default_first = BUSTER_CHECK_NONE;
        uint32_t default_end = 0;
        uint32_t association = separator + 1;
        while (valid && association < token)
        {
            const uint32_t colon = check_constant_punctuation(parser, association, token, PARSE_KIND_COLON);
            const uint32_t after = colon < token ? check_constant_separator(input, colon + 1, token) : token;
            valid = colon < token && colon + 1 < after;
            if (valid && parser->tokens.kinds[association] == PARSE_KIND_DEFAULT)
            {
                valid = association + 1 == colon && default_first == BUSTER_CHECK_NONE;
                default_first = colon + 1;
                default_end = after;
            }
            else if (valid)
            {
                const TypeId type = check_constant_type(input, association, colon);
                const uint8_t kind = type_kind(reader->types, type_strip_aligned(reader->types, type));
                valid = type != 0 && type_is_complete(reader->types, type) && kind != BUSTER_TYPE_KIND_FUNCTION &&
                        kind != BUSTER_TYPE_KIND_VOID && (type_inherited_flags(reader->types, type) & BUSTER_TYPE_FLAG_VARIABLY_MODIFIED) == 0;
                for (uint32_t prior = 0; valid && prior < seen_count; prior += 1)
                {
                    const TypeAnswer answer = type_compatible(reader->types, type, seen[prior]);
                    reader->output->resource |= answer == TYPE_ANSWER_FULL;
                    valid = answer == TYPE_ANSWER_NO;
                }
                if (valid)
                {
                    seen[seen_count++] = type;
                    const TypeAnswer answer = type_compatible(reader->types, type, control_type);
                    reader->output->resource |= answer == TYPE_ANSWER_FULL;
                    valid = answer != TYPE_ANSWER_FULL;
                    if (answer == TYPE_ANSWER_YES)
                    {
                        valid &= chosen_first == BUSTER_CHECK_NONE;
                        chosen_first = colon + 1;
                        chosen_end = after;
                    }
                }
            }
            association = after + 1;
        }
        if (chosen_first == BUSTER_CHECK_NONE)
        {
            chosen_first = default_first;
            chosen_end = default_end;
        }
        valid &= chosen_first != BUSTER_CHECK_NONE;
        if (valid)
        {
            valid = check_constant_range_value(input, chosen_first, chosen_end, value);
        }
        arena_pop_to(reader->scratch, mark);
    }
    return valid;
}

static bool check_constant_builtin_type(CheckConstantInput* input, uint32_t token, uint32_t name, uint32_t first, uint32_t separator, CheckValue* value)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    bool valid = separator < token && (name == C_NAME_BUILTIN_TYPES_COMPATIBLE_P || name == C_NAME_BUILTIN_OFFSETOF);
    TypeId type = valid ? check_constant_type(input, first, separator) : 0;
    valid &= type != 0;
    uint64_t number = 0;
    if (valid && name == C_NAME_BUILTIN_TYPES_COMPATIBLE_P)
    {
        const TypeId other = check_constant_type(input, separator + 1, token);
        valid = other != 0;
        if (valid)
        {
            const TypeAnswer answer = type_compatible(reader->types, type_unqualified(type), type_unqualified(other));
            reader->output->resource |= answer == TYPE_ANSWER_FULL;
            valid = answer != TYPE_ANSWER_FULL;
            number = answer == TYPE_ANSWER_YES;
        }
    }
    else if (valid)
    {
        uint32_t i = separator + 1;
        bool member_required = true;
        while (valid && i < token)
        {
            const uint8_t kind = parser->tokens.kinds[i];
            if (member_required && kind == PARSE_KIND_IDENTIFIER)
            {
                CheckAction action =
                {
                    .op = CHECK_OP_MEMBER,
                    .operands = { { .type = type, .category = CHECK_LVALUE, .constant_kind = CHECK_CONSTANT_ADDRESS, .object = 1 } },
                    .member = parser->tokens.names[i],
                    .node = i,
                    .tag_completions = reader->tag_completions,
                    .tag_completion_count = reader->types->tags.capacity,
                    .source_position = token + 1,
                    .extensions = parser->language.gnu ? BUSTER_CHECK_EXTENSION_GNU : 0,
                };
                CheckActionResult checked;
                check_action_scalar(reader->types, &action, &checked);
                reader->output->resource |= checked.diagnostic == CHECK_DIAG_RESOURCE;
                valid = checked.diagnostic == CHECK_DIAG_NONE && (checked.value.flags & BUSTER_CHECK_BIT_FIELD) == 0 && checked.value.offset >= 0;
                valid &= (uint64_t)checked.value.offset <= UINT64_MAX - number;
                number += (uint64_t)checked.value.offset;
                type = checked.value.type;
                member_required = false;
                i += 1;
            }
            else if (!member_required && kind == PARSE_KIND_PERIOD)
            {
                member_required = true;
                i += 1;
            }
            else if (!member_required && kind == PARSE_KIND_L_SQUARE)
            {
                const uint32_t close = parser->tree.parents[i];
                CheckValue bound = { 0 };
                int64_t index = 0;
                valid = close > i && close < token && type_kind_is_array(type_kind(reader->types, type)) &&
                        check_constant_range_value(input, i + 1, close, &bound) && (bound.flags & BUSTER_CHECK_ICE) != 0 &&
                        check_action_i64(reader->types, &bound, &index) && index >= 0;
                if (valid)
                {
                    type = type_element(reader->types, type);
                    const uint64_t size = type_size(reader->types, type);
                    valid = size == 0 || (uint64_t)index <= (UINT64_MAX - number) / size;
                    number += (uint64_t)index * size;
                }
                i = close + 1;
            }
            else
            {
                valid = false;
            }
        }
        valid &= !member_required;
    }
    if (valid)
    {
        *value = (CheckValue)
        {
            .type = name == C_NAME_BUILTIN_TYPES_COMPATIBLE_P ? BUSTER_TYPE_INT : BUSTER_TYPE_ULONG,
            .category = CHECK_RVALUE,
            .flags = BUSTER_CHECK_ICE,
            .constant_kind = CHECK_CONSTANT_INTEGER,
            .integer = number,
        };
    }
    return valid;
}

static bool check_constant_builtin(CheckConstantInput* input, uint32_t token, CheckValue* value)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    const uint32_t keyword = reader->first_child[token];
    bool valid = keyword >= input->first && keyword + 1 < token;
    const uint32_t name = valid ? parser->tokens.names[keyword] : 0;
    const uint32_t first = keyword + 2;
    const uint32_t separator = valid ? check_constant_separator(input, first, token) : token;
    const bool selection = name == C_NAME_GENERIC || name == C_NAME_BUILTIN_CHOOSE_EXPR;
    valid &= separator < token;
    if (valid && selection)
    {
        valid = check_constant_selection(input, name, first, separator, token, value);
    }
    else if (valid)
    {
        valid = check_constant_builtin_type(input, token, name, first, separator, value);
    }
    return valid;
}

static TypeId check_constant_object_alignment(CheckInputReader* reader, uint32_t query, TypeId type)
{
    const Parser* parser = reader->parser;
    uint32_t first = query + 1;
    uint32_t end = first + 1;
    if (first < parser->tokens.count && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN)
    {
        end = parser->tree.parents[first] + 1;
    }
    while (first < end && end <= parser->tokens.count && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN &&
           parser->tree.parents[first] == end - 1)
    {
        first += 1;
        end -= 1;
    }
    if (first + 1 == end && end <= parser->tokens.count && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER)
    {
        const uint32_t declaration = reader->output->declarations[first];
        const CheckInputAttributes* attributes = declaration < parser->tokens.count ? reader->output->attributes[declaration] : 0;
        if (attributes != 0 && attributes->alignment_log2 > type_alignment_log2(reader->types, type) + 1 &&
            type_kind(reader->types, type) != BUSTER_TYPE_KIND_FUNCTION)
        {
            type = check_input_type_result(reader, type_aligned(reader->types, type, attributes->alignment_log2 - 1));
        }
    }
    return type;
}

// Type-only declaration queries retain a runtime sizeof result. The source graph subsequently constructs the
// actual captured-extent dependencies; this pass must neither invent a constant size nor evaluate a bound twice.
static void check_constant_runtime_query(const CheckConstantInput* input, CheckAction* action)
{
    const TypeId type = action->type != 0 ? action->type : action->operands[0].type;
    if (action->op == CHECK_OP_SIZEOF && check_action_runtime_size(input->reader->types, type))
    {
        const CheckValue operand = action->operands[0];
        const bool expression = action->type == 0;
        action->op = CHECK_OP_SIZEOF_VLA;
        action->type = type;
        action->operands[0] = (CheckValue)
        {
            .type = BUSTER_TYPE_ULONG,
            .category = CHECK_RVALUE,
            .flags = expression ? operand.flags & BUSTER_CHECK_SIDE_EFFECT : 0,
            .fault = expression ? operand.fault : CHECK_FAULT_NONE,
        };
    }
}

static bool check_constant_evaluate(CheckConstantInput* input)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    uint32_t before = parser->tokens.count;
    uint32_t after = 0;
    for (uint32_t token = input->first; token < input->end; token += 1)
    {
        const uint32_t order = reader->postorder_positions[token];
        before = order < before ? order : before;
        after = order >= after ? order + 1 : after;
    }
    bool valid = true;
    for (uint32_t order = before; valid && order < after; order += 1)
    {
        const uint32_t token = parser->postorder[order];
        if (token < input->first || token >= input->end)
        {
            continue;
        }
        const uint32_t index = token - input->first;
        if (input->attribute_ends[index] != 0)
        {
            valid = check_constant_attribute_evaluate(input, input->attribute_ends[index]);
        }
        if (input->syntax[index])
        {
            continue;
        }
        const uint8_t role = parser->tree.roles[token];
        const uint8_t kind = parser->tokens.kinds[token];
        uint32_t first = BUSTER_CHECK_NONE;
        uint32_t last = BUSTER_CHECK_NONE;
        uint32_t count = 0;
        CheckAction action =
        {
            .op = CHECK_OP_COUNT,
            .node = token,
            .tag_completions = reader->tag_completions,
            .tag_completion_count = reader->types->tags.capacity,
            .source_position = reader->ends[token] + 1,
            .extensions = parser->language.gnu ? BUSTER_CHECK_EXTENSION_GNU : 0,
        };
        for (uint32_t child = reader->first_child[token]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
        {
            if (child >= input->first && child < input->end)
            {
                const uint32_t begin = input->first_value[child - input->first];
                if (begin != BUSTER_CHECK_NONE)
                {
                    if (last != BUSTER_CHECK_NONE)
                    {
                        input->next_value[last - input->first] = begin;
                    }
                    else
                    {
                        first = begin;
                    }
                    last = input->last_value[child - input->first];
                    for (uint32_t value = begin; value != BUSTER_CHECK_NONE; value = input->next_value[value - input->first])
                    {
                        if (count < 3)
                        {
                            action.operands[count] = input->values[value - input->first];
                        }
                        count += 1;
                    }
                }
            }
        }
        bool produced = false;
        bool list = false;
        if (role == PARSE_ROLE_OPERAND && (kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER))
        {
            const CheckLiteral literal = check_input_literal(reader, token);
            valid = literal.valid;
            input->values[index] = (CheckValue)
            {
                .type = literal.type,
                .category = CHECK_RVALUE,
                .flags = literal.real ? BUSTER_CHECK_FLOAT_LITERAL : literal.type == BUSTER_TYPE_NULLPTR ? 0 : BUSTER_CHECK_ICE,
                .constant_kind = literal.real ? CHECK_CONSTANT_FLOATING : literal.type == BUSTER_TYPE_NULLPTR ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_INTEGER,
                .integer = literal.integer,
                .floating = literal.floating,
            };
            produced = true;
        }
        else if (role == PARSE_ROLE_OPERAND && kind == PARSE_KIND_STRING)
        {
            const CheckInitString* string = check_input_string(reader, token);
            valid = string != 0;
            if (valid)
            {
                input->values[index] = (CheckValue)
                {
                    .type = string->type,
                    .category = CHECK_LVALUE,
                    .flags = BUSTER_CHECK_STATIC_OBJECT,
                    .constant_kind = CHECK_CONSTANT_ADDRESS,
                    .object = string->object,
                };
                produced = true;
            }
        }
        else if (role == PARSE_ROLE_OPERAND && kind == PARSE_KIND_IDENTIFIER)
        {
            const uint32_t declaration = reader->output->declarations[token];
            const TypeId type = reader->declared_types[token];
            valid = type != 0 && declaration < parser->tokens.count;
            if (valid)
            {
                const bool enumeration = reader->declaration_kinds[declaration] == C_BINDING_ENUMERATOR;
                input->values[index] = (CheckValue)
                {
                    .type = type,
                    .category = enumeration ? CHECK_RVALUE : type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE,
                    .flags = (enumeration ? BUSTER_CHECK_ICE : 0) | (reader->static_objects[declaration] && !reader->output->thread_local[declaration] ? BUSTER_CHECK_STATIC_OBJECT : 0) |
                             (reader->registers[declaration] ? BUSTER_CHECK_REGISTER : 0),
                    .constant_kind = enumeration ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_ADDRESS,
                    .integer = enumeration ? (uint64_t)reader->enum_values[declaration] : 0,
                    .object = reader->objects[declaration],
                };
                produced = true;
            }
        }
        else if (role == PARSE_ROLE_PREFIX)
        {
            switch (kind)
            {
                case PARSE_KIND_PLUS: action.op = CHECK_OP_POSITIVE; break;
                case PARSE_KIND_MINUS: action.op = CHECK_OP_NEGATIVE; break;
                case PARSE_KIND_EXCLAIM: action.op = CHECK_OP_NOT; break;
                case PARSE_KIND_TILDE: action.op = CHECK_OP_BIT_NOT; break;
                case PARSE_KIND_AMP: action.op = CHECK_OP_ADDRESS; break;
                case PARSE_KIND_STAR: action.op = CHECK_OP_DEREFERENCE; break;
                case PARSE_KIND_PLUSPLUS: action.op = CHECK_OP_PRE_INCREMENT; break;
                case PARSE_KIND_MINUSMINUS: action.op = CHECK_OP_PRE_DECREMENT; break;
                case PARSE_KIND_SIZEOF:
                    action.op = parser->tokens.names[token] == C_NAME_SIZEOF ? CHECK_OP_SIZEOF : CHECK_OP_ALIGNOF;
                    action.type = input->queries[index];
                    break;
                default: break;
            }
            valid = action.op != CHECK_OP_COUNT && (count == 1 || (kind == PARSE_KIND_SIZEOF && action.type != 0));
        }
        else if ((role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_ASSIGN) || role == PARSE_ROLE_COMMA)
        {
            action.op = check_input_binary(kind);
            valid = count == 2 && action.op != CHECK_OP_COUNT;
        }
        else if (role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_SIZEOF)
        {
            const uint32_t type_first = input->type_begin[index];
            if (role == PARSE_ROLE_CLOSE_SIZEOF && type_first != BUSTER_CHECK_NONE)
            {
                const TypeId type = check_constant_type(input, type_first, token);
                const uint32_t parent = parser->tree.parents[token];
                valid = type != 0 && parent >= input->first && parent < input->end;
                if (valid)
                {
                    input->queries[parent - input->first] = type;
                }
            }
            else
            {
                valid = count == 1;
                input->values[index] = action.operands[0];
                produced = true;
            }
        }
        else if (role == PARSE_ROLE_CLOSE_CAST)
        {
            const uint32_t type_first = input->type_begin[index];
            action.type = type_first != BUSTER_CHECK_NONE ? check_constant_type(input, type_first, token) : 0;
            action.op = CHECK_OP_CAST;
            valid = count == 1 && action.type != 0;
        }
        else if (role == PARSE_ROLE_CLOSE_SPECIFIER)
        {
            const uint32_t keyword = reader->first_child[token];
            const uint32_t name = keyword < token ? parser->tokens.names[keyword] : 0;
            const uint32_t type_first = input->type_begin[index];
            TypeId type = type_first != BUSTER_CHECK_NONE ? check_constant_type(input, type_first, token) : count == 1 ? action.operands[0].type : 0;
            if (name == C_NAME_BITINT || name == C_NAME_EXTINT)
            {
                int64_t width = 0;
                valid = count == 1 && (action.operands[0].flags & BUSTER_CHECK_ICE) != 0 &&
                        check_action_i64(reader->types, &action.operands[0], &width) && width > 0 && width <= UINT32_MAX;
                // Unsigned one-bit _BitInt is valid; the specifier pass applies the actual signedness.
                type = valid ? check_input_type_result(reader, type_bit_int(reader->types, (uint32_t)width, true)) : 0;
            }
            else if (name == C_NAME_ATOMIC)
            {
                valid = type_first != BUSTER_CHECK_NONE;
                type = valid && type != 0 ? check_input_type_result(reader, type_atomic(reader->types, type)) : 0;
            }
            else if (name == C_NAME_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL2)
            {
                type = type_unqualified(type);
                type = type_kind(reader->types, type) == BUSTER_TYPE_KIND_ATOMIC ? type_element(reader->types, type) : type;
            }
            else
            {
                valid = name == C_NAME_TYPEOF || name == C_NAME_GNU_TYPEOF || name == C_NAME_GNU_TYPEOF2;
            }
            valid &= type != 0 && (type_first != BUSTER_CHECK_NONE || (count == 1 && (action.operands[0].flags & BUSTER_CHECK_BIT_FIELD) == 0));
            input->queries[index] = type;
        }
        else if (role == PARSE_ROLE_CLOSE_TERNARY)
        {
            valid = count == 3 || (count == 2 && parser->language.gnu);
            if (count == 2)
            {
                action.operands[2] = action.operands[1];
                action.operands[1] = action.operands[0];
            }
            action.op = CHECK_OP_CONDITIONAL;
        }
        else if (role == PARSE_ROLE_CLOSE_SUBSCRIPT)
        {
            valid = count == 2;
            action.op = CHECK_OP_SUBSCRIPT;
        }
        else if (role == PARSE_ROLE_MEMBER_OPERATOR)
        {
            valid = count == 1 && token + 1 < input->end && parser->tree.roles[token + 1] == PARSE_ROLE_MEMBER;
            action.op = kind == PARSE_KIND_ARROW ? CHECK_OP_ARROW : CHECK_OP_MEMBER;
            action.member = valid ? parser->tokens.names[token + 1] : 0;
        }
        else if (role == PARSE_ROLE_POSTFIX)
        {
            valid = count == 1 && (kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS);
            action.op = kind == PARSE_KIND_PLUSPLUS ? CHECK_OP_POST_INCREMENT : CHECK_OP_POST_DECREMENT;
        }
        else if (role == PARSE_ROLE_CLOSE_KEYWORD)
        {
            valid = check_constant_builtin(input, token, &input->values[index]);
            produced = true;
        }
        else if (role == PARSE_ROLE_SEPARATOR)
        {
            uint32_t parent = parser->tree.parents[token];
            while (parent >= input->first && parent < input->end && parser->tree.roles[parent] == PARSE_ROLE_SEPARATOR)
            {
                parent = parser->tree.parents[parent];
            }
            list = parent >= input->first && parent < input->end &&
                   (parser->tree.roles[parent] == PARSE_ROLE_CLOSE_CALL || parser->tree.roles[parent] == PARSE_ROLE_CLOSE_FUNCTION ||
                    parser->tree.roles[parent] == PARSE_ROLE_CLOSE_KEYWORD);
            if (!list)
            {
                action.op = CHECK_OP_COMMA;
                valid = count == 2;
            }
        }
        else if (role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_FUNCTION)
        {
            const uint16_t builtin = reader->builtins[token];
            if (builtin != CBUILTIN_NONE && builtin < CBUILTIN_COUNT)
            {
                CheckBuiltinAction builtin_action =
                {
                    .id = (CBuiltinId)builtin,
                    .operands = { action.operands[0], action.operands[1], action.operands[2] },
                    .argument_count = count,
                    .node = token,
                };
                CheckBuiltinResult checked;
                check_builtins(reader->types, &builtin_action, &checked, 1, parser->options.scalar);
                reader->output->resource |= checked.action.diagnostic == CHECK_DIAG_RESOURCE;
                valid = checked.action.diagnostic == CHECK_DIAG_NONE;
                input->values[index] = checked.action.value;
                produced = true;
            }
            else
            {
                valid = count != 0;
                action.op = CHECK_OP_CALL;
            }
        }
        else
        {
            valid = role == PARSE_ROLE_OPEN_GROUP || role == PARSE_ROLE_OPEN_CAST || role == PARSE_ROLE_OPEN_SIZEOF ||
                    role == PARSE_ROLE_OPEN_CALL || role == PARSE_ROLE_OPEN_FUNCTION || role == PARSE_ROLE_OPEN_KEYWORD ||
                    role == PARSE_ROLE_OPEN_SUBSCRIPT || role == PARSE_ROLE_OPEN_TERNARY || role == PARSE_ROLE_MEMBER || role == PARSE_ROLE_LEAF_RIGHT ||
                    role == PARSE_ROLE_STRING_NEXT;
        }
        if (valid && action.op != CHECK_OP_COUNT)
        {
            if (action.op == CHECK_OP_ALIGNOF && action.type == 0 && count == 1)
            {
                const TypeId aligned = check_constant_object_alignment(reader, token, action.operands[0].type);
                action.type = aligned != action.operands[0].type ? aligned : 0;
                valid = aligned != 0;
            }
            const uint64_t action_mark = reader->scratch->length;
            if (action.op == CHECK_OP_CALL)
            {
                CheckValue* arguments = arena_push_array(reader->scratch, count, sizeof(CheckValue), 16);
                CheckConversion* conversions = arena_push_array(reader->scratch, count, sizeof(CheckConversion), 4);
                valid = arguments != 0 && conversions != 0;
                reader->output->resource |= !valid;
                uint32_t argument = 0;
                for (uint32_t value = valid ? input->next_value[first - input->first] : BUSTER_CHECK_NONE; value != BUSTER_CHECK_NONE; value = input->next_value[value - input->first])
                {
                    arguments[argument++] = input->values[value - input->first];
                }
                action.arguments = arguments;
                action.argument_count = argument;
                action.argument_conversions = conversions;
            }
            if (valid)
            {
                CheckActionResult checked;
                check_constant_runtime_query(input, &action);
                check_action_scalar(reader->types, &action, &checked);
                reader->output->resource |= checked.diagnostic == CHECK_DIAG_RESOURCE;
                valid = checked.diagnostic == CHECK_DIAG_NONE;
                input->values[index] = checked.value;
                produced = true;
            }
            arena_pop_to(reader->scratch, action_mark);
        }
        if (valid && produced)
        {
            input->present[index] = true;
            input->first_value[index] = token;
            input->last_value[index] = token;
            input->next_value[index] = BUSTER_CHECK_NONE;
        }
        else if (valid && list)
        {
            input->first_value[index] = first;
            input->last_value[index] = last;
        }
    }
    return valid;
}

typedef struct CheckConstantOperator CheckConstantOperator;
struct CheckConstantOperator
{
    CheckOp op;
    TypeId type;
    uint32_t token;
    uint8_t precedence;
    uint8_t arity;
    uint8_t marker;
    uint16_t builtin;
};

static uint8_t check_constant_precedence(CheckOp op)
{
    uint8_t precedence = 0;
    switch (op)
    {
        case CHECK_OP_MULTIPLY: case CHECK_OP_DIVIDE: case CHECK_OP_REMAINDER: precedence = 13; break;
        case CHECK_OP_ADD: case CHECK_OP_SUBTRACT: precedence = 12; break;
        case CHECK_OP_SHIFT_LEFT: case CHECK_OP_SHIFT_RIGHT: precedence = 11; break;
        case CHECK_OP_LESS: case CHECK_OP_LESS_EQUAL: case CHECK_OP_GREATER: case CHECK_OP_GREATER_EQUAL: precedence = 10; break;
        case CHECK_OP_EQUAL: case CHECK_OP_NOT_EQUAL: precedence = 9; break;
        case CHECK_OP_BIT_AND: precedence = 8; break;
        case CHECK_OP_BIT_XOR: precedence = 7; break;
        case CHECK_OP_BIT_OR: precedence = 6; break;
        case CHECK_OP_LOGICAL_AND: precedence = 5; break;
        case CHECK_OP_LOGICAL_OR: precedence = 4; break;
        case CHECK_OP_CONDITIONAL: precedence = 3; break;
        case CHECK_OP_ASSIGN: case CHECK_OP_ADD_ASSIGN: case CHECK_OP_SUBTRACT_ASSIGN: case CHECK_OP_MULTIPLY_ASSIGN:
        case CHECK_OP_DIVIDE_ASSIGN: case CHECK_OP_REMAINDER_ASSIGN: case CHECK_OP_SHIFT_LEFT_ASSIGN: case CHECK_OP_SHIFT_RIGHT_ASSIGN:
        case CHECK_OP_BIT_AND_ASSIGN: case CHECK_OP_BIT_OR_ASSIGN: case CHECK_OP_BIT_XOR_ASSIGN: precedence = 2; break;
        case CHECK_OP_COMMA: precedence = 1; break;
        default: break;
    }
    return precedence;
}

static bool check_constant_raw_reduce(CheckConstantInput* input, CheckConstantOperator* operators, uint32_t* operator_count, CheckValue* values, uint32_t* value_count)
{
    CheckInputReader* reader = input->reader;
    bool valid = *operator_count != 0;
    if (valid)
    {
        const CheckConstantOperator operation = operators[--*operator_count];
        valid = operation.marker == 0 && operation.arity <= *value_count;
        if (valid)
        {
            CheckAction action =
            {
                .op = operation.op,
                .type = operation.type,
                .node = operation.token,
                .tag_completions = reader->tag_completions,
                .tag_completion_count = reader->types->tags.capacity,
                .source_position = input->end,
                .extensions = reader->parser->language.gnu ? BUSTER_CHECK_EXTENSION_GNU : 0,
            };
            *value_count -= operation.arity;
            for (uint32_t i = 0; i < operation.arity; i += 1)
            {
                action.operands[i] = values[*value_count + i];
            }
            if (action.op == CHECK_OP_ALIGNOF && action.type == 0 && operation.arity == 1)
            {
                const TypeId aligned = check_constant_object_alignment(reader, operation.token, action.operands[0].type);
                action.type = aligned != action.operands[0].type ? aligned : 0;
                valid = aligned != 0;
            }
            CheckActionResult checked;
            if (operation.builtin != CBUILTIN_NONE)
            {
                CheckBuiltinAction builtin_action =
                {
                    .id = (CBuiltinId)operation.builtin,
                    .operands = { action.operands[0], action.operands[1], action.operands[2] },
                    .argument_count = operation.arity,
                    .node = operation.token,
                };
                CheckBuiltinResult builtin_result;
                check_builtins(reader->types, &builtin_action, &builtin_result, 1, reader->parser->options.scalar);
                checked = builtin_result.action;
            }
            else
            {
                check_constant_runtime_query(input, &action);
                check_action_scalar(reader->types, &action, &checked);
            }
            reader->output->resource |= checked.diagnostic == CHECK_DIAG_RESOURCE;
            valid &= checked.diagnostic == CHECK_DIAG_NONE;
            values[(*value_count)++] = checked.value;
        }
    }
    return valid;
}

// C23 attribute payloads are deliberately unstructured parser tokens. A bounded operator/value stack supplies
// their expression order without reparsing the unit, mutating parser output, or making the constant/type reader
// recursive. Type-query results have already been prepared inside out by check_constant_raw_prepare.
static bool check_constant_raw_expression(CheckConstantInput* input, uint32_t first, uint32_t end)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    const uint64_t mark = reader->scratch->length;
    const uint32_t capacity = end - first + 1;
    CheckConstantOperator* operators = arena_push_array(reader->scratch, capacity, sizeof(CheckConstantOperator), 8);
    CheckValue* values = arena_push_array(reader->scratch, capacity, sizeof(CheckValue), 16);
    reader->output->resource |= operators == 0 || values == 0;
    bool valid = first < end && operators != 0 && values != 0;
    bool operand = true;
    uint32_t operator_count = 0;
    uint32_t value_count = 0;
    uint32_t token = first;
    while (valid && token < end)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        if (operand && (kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER))
        {
            const CheckLiteral literal = check_input_literal(reader, token);
            valid = literal.valid;
            values[value_count++] = (CheckValue)
            {
                .type = literal.type,
                .category = CHECK_RVALUE,
                .flags = literal.real ? BUSTER_CHECK_FLOAT_LITERAL : literal.type == BUSTER_TYPE_NULLPTR ? 0 : BUSTER_CHECK_ICE,
                .constant_kind = literal.real ? CHECK_CONSTANT_FLOATING : literal.type == BUSTER_TYPE_NULLPTR ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_INTEGER,
                .integer = literal.integer,
                .floating = literal.floating,
            };
            operand = false;
            token += 1;
        }
        else if (operand && kind == PARSE_KIND_IDENTIFIER && input->syntax[token - input->first])
        {
            const uint32_t open = token + 1;
            const uint32_t close = open < end ? parser->tree.parents[open] : end;
            const uint16_t builtin = close < end ? reader->builtins[close] : CBUILTIN_NONE;
            valid = builtin != CBUILTIN_NONE && builtin < CBUILTIN_COUNT && !check_builtin_overflow((CBuiltinId)builtin) &&
                    check_constant_punctuation(parser, open + 1, close, PARSE_KIND_COMMA) == close;
            if (valid)
            {
                operators[operator_count++] = (CheckConstantOperator)
                {
                    .op = CHECK_OP_BUILTIN_UNARY,
                    .token = token,
                    .precedence = 15,
                    .arity = 1,
                    .builtin = builtin,
                };
            }
            token += 1;
        }
        else if (operand && kind == PARSE_KIND_IDENTIFIER)
        {
            const uint32_t declaration = reader->output->declarations[token];
            const TypeId type = reader->declared_types[token];
            valid = type != 0 && declaration < parser->tokens.count;
            if (valid)
            {
                const bool enumeration = reader->declaration_kinds[declaration] == C_BINDING_ENUMERATOR;
                values[value_count++] = (CheckValue)
                {
                    .type = type,
                    .category = enumeration ? CHECK_RVALUE : type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE,
                    .flags = (enumeration ? BUSTER_CHECK_ICE : 0) | (reader->static_objects[declaration] && !reader->output->thread_local[declaration] ? BUSTER_CHECK_STATIC_OBJECT : 0) |
                             (reader->registers[declaration] ? BUSTER_CHECK_REGISTER : 0),
                    .constant_kind = enumeration ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_ADDRESS,
                    .integer = enumeration ? (uint64_t)reader->enum_values[declaration] : 0,
                    .object = reader->objects[declaration],
                };
            }
            operand = false;
            token += 1;
        }
        else if (operand && kind == PARSE_KIND_STRING)
        {
            const CheckInitString* string = check_input_string(reader, token);
            valid = string != 0;
            if (valid)
            {
                values[value_count++] = (CheckValue)
                {
                    .type = string->type,
                    .category = CHECK_LVALUE,
                    .flags = BUSTER_CHECK_STATIC_OBJECT,
                    .constant_kind = CHECK_CONSTANT_ADDRESS,
                    .object = string->object,
                };
            }
            do
            {
                token += 1;
            }
            while (token < end && parser->tokens.kinds[token] == PARSE_KIND_STRING);
            operand = false;
        }
        else if (operand && (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_SIZEOF))
        {
            const bool query = kind == PARSE_KIND_SIZEOF;
            const uint32_t open = query ? token + 1 : token;
            const uint32_t close = open < end && parser->tokens.kinds[open] == PARSE_KIND_L_PAREN ? parser->tree.parents[open] : BUSTER_CHECK_NONE;
            const TypeId type = close > open && close < end ? input->queries[close - input->first] : 0;
            if (type != 0)
            {
                operators[operator_count++] = (CheckConstantOperator)
                {
                    .op = query ? (parser->tokens.names[token] == C_NAME_SIZEOF ? CHECK_OP_SIZEOF : CHECK_OP_ALIGNOF) : CHECK_OP_CAST,
                    .type = type,
                    .token = token,
                    .precedence = 14,
                    .arity = query ? 0 : 1,
                };
                if (query)
                {
                    valid = check_constant_raw_reduce(input, operators, &operator_count, values, &value_count);
                    operand = false;
                }
                token = close + 1;
            }
            else if (query)
            {
                operators[operator_count++] = (CheckConstantOperator)
                {
                    .op = parser->tokens.names[token] == C_NAME_SIZEOF ? CHECK_OP_SIZEOF : CHECK_OP_ALIGNOF,
                    .token = token,
                    .precedence = 14,
                    .arity = 1,
                };
                token += 1;
            }
            else
            {
                operators[operator_count++] = (CheckConstantOperator)
                {
                    .token = token,
                    .marker = 1,
                };
                token += 1;
            }
        }
        else if (!operand && kind == PARSE_KIND_R_PAREN)
        {
            while (valid && operator_count != 0 && operators[operator_count - 1].marker == 0)
            {
                valid = check_constant_raw_reduce(input, operators, &operator_count, values, &value_count);
            }
            valid &= operator_count != 0 && operators[operator_count - 1].marker == 1;
            operator_count -= valid;
            token += 1;
        }
        else if (!operand && kind == PARSE_KIND_QUESTION)
        {
            while (valid && operator_count != 0 && operators[operator_count - 1].marker == 0 && operators[operator_count - 1].precedence > 3)
            {
                valid = check_constant_raw_reduce(input, operators, &operator_count, values, &value_count);
            }
            operators[operator_count++] = (CheckConstantOperator)
            {
                .op = CHECK_OP_CONDITIONAL,
                .token = token,
                .precedence = 3,
                .arity = 3,
                .marker = 2,
            };
            operand = true;
            token += 1;
        }
        else if (kind == PARSE_KIND_COLON)
        {
            if (operand)
            {
                valid = parser->language.gnu && value_count != 0 && operator_count != 0 && operators[operator_count - 1].marker == 2;
                if (valid)
                {
                    values[value_count] = values[value_count - 1];
                    value_count += 1;
                }
            }
            while (valid && operator_count != 0 && operators[operator_count - 1].marker == 0)
            {
                valid = check_constant_raw_reduce(input, operators, &operator_count, values, &value_count);
            }
            valid &= operator_count != 0 && operators[operator_count - 1].marker == 2;
            if (valid)
            {
                operators[operator_count - 1].marker = 0;
            }
            operand = true;
            token += 1;
        }
        else
        {
            CheckOp op = CHECK_OP_COUNT;
            if (operand)
            {
                switch (kind)
                {
                    case PARSE_KIND_PLUS: op = CHECK_OP_POSITIVE; break;
                    case PARSE_KIND_MINUS: op = CHECK_OP_NEGATIVE; break;
                    case PARSE_KIND_EXCLAIM: op = CHECK_OP_NOT; break;
                    case PARSE_KIND_TILDE: op = CHECK_OP_BIT_NOT; break;
                    case PARSE_KIND_AMP: op = CHECK_OP_ADDRESS; break;
                    case PARSE_KIND_STAR: op = CHECK_OP_DEREFERENCE; break;
                    default: break;
                }
            }
            else
            {
                op = check_input_binary(kind);
            }
            const uint8_t precedence = operand ? 14 : check_constant_precedence(op);
            valid = op != CHECK_OP_COUNT && precedence != 0;
            const bool right = operand || precedence == 2;
            while (valid && operator_count != 0 && operators[operator_count - 1].marker == 0 &&
                   (operators[operator_count - 1].precedence > precedence || (!right && operators[operator_count - 1].precedence == precedence)))
            {
                valid = check_constant_raw_reduce(input, operators, &operator_count, values, &value_count);
            }
            if (valid)
            {
                operators[operator_count++] = (CheckConstantOperator)
                {
                    .op = op,
                    .token = token,
                    .precedence = precedence,
                    .arity = operand ? 1 : 2,
                };
            }
            operand = true;
            token += 1;
        }
    }
    valid &= !operand;
    while (valid && operator_count != 0)
    {
        valid = check_constant_raw_reduce(input, operators, &operator_count, values, &value_count);
    }
    valid &= value_count == 1;
    if (valid)
    {
        input->values[first - input->first] = values[0];
        input->present[first - input->first] = true;
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}

static bool check_constant_raw_type_start(CheckConstantInput* input, uint32_t token)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    const uint8_t kind = parser->tokens.kinds[token];
    bool type = kind == PARSE_KIND_TYPE || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ||
                kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATOMIC;
    if (kind == PARSE_KIND_IDENTIFIER)
    {
        const uint32_t handle = input->cached ? 0 : c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[token]);
        const uint32_t declaration = reader->output->declarations[token];
        type = input->cached ? declaration < parser->tokens.count && reader->declaration_kinds[declaration] == C_BINDING_TYPEDEF :
               handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF;
    }
    return type;
}

static bool check_constant_raw_bind(CheckConstantInput* input, bool type_name)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    bool valid = true;
    if (type_name)
    {
        check_constant_mark_type(input, input->first, input->end);
    }
    for (uint32_t token = input->first; valid && token < input->end; token += 1)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        const uint32_t index = token - input->first;
        valid = check_constant_tag_finish(input, token);
        if (!valid || input->attribute_syntax[index])
        {
            continue;
        }
        if (kind == PARSE_KIND_L_PAREN)
        {
            const uint32_t close = parser->tree.parents[token];
            valid = close > token && close < input->end;
            if (valid && token + 1 < close && check_constant_raw_type_start(input, token + 1))
            {
                input->type_begin[close - input->first] = token + 1;
                check_constant_mark_type(input, token + 1, close);
            }
            else if (valid && token > input->first && parser->tokens.kinds[token - 1] == PARSE_KIND_TYPEOF)
            {
                for (uint32_t item = token + 1; item < close; item += 1)
                {
                    input->syntax[item - input->first] = false;
                }
            }
        }
        if (valid && input->syntax[index] && (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM))
        {
            valid = check_constant_tag_prepare(input, token);
        }
        else if (valid && kind == PARSE_KIND_IDENTIFIER && (token == input->first ||
                 (parser->tokens.kinds[token - 1] != PARSE_KIND_STRUCT && parser->tokens.kinds[token - 1] != PARSE_KIND_ENUM)))
        {
            const uint32_t name = parser->tokens.names[token];
            const uint32_t handle = input->cached ? 0 : c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name);
            const bool type = handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF;
            const uint16_t builtin = !input->syntax[index] ? check_constant_integer_builtin(input, token, handle) : CBUILTIN_NONE;
            if (builtin != CBUILTIN_NONE)
            {
                input->syntax[index] = true;
            }
            else
            {
                valid = input->cached ? reader->declared_types[token] != 0 : handle != 0 && type == (input->syntax[index] != 0);
            }
            if (valid && !input->cached && builtin == CBUILTIN_NONE)
            {
                const uint32_t declaration = reader->bindings.declaration[handle - 1];
                reader->output->declarations[token] = declaration;
                reader->declared_types[token] = reader->declared_types[declaration];
                valid = c_bindings_use(&reader->bindings, C_BINDING_ORDINARY, name, token, token) != 0;
                reader->output->resource |= !valid;
            }
        }
    }
    return valid && check_constant_tag_finish(input, input->end);
}

static bool check_constant_raw_prepare(CheckConstantInput* input)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    bool valid = true;
    for (uint32_t reverse = input->end; valid && reverse != input->first;)
    {
        const uint32_t token = --reverse;
        if (input->attribute_starts[token - input->first] != 0)
        {
            valid = check_constant_attribute_evaluate(input, input->attribute_starts[token - input->first]);
        }
        if (!valid || input->attribute_syntax[token - input->first])
        {
            continue;
        }
        if (parser->tokens.kinds[token] == PARSE_KIND_L_PAREN)
        {
            const uint32_t close = parser->tree.parents[token];
            const uint32_t first = input->type_begin[close - input->first];
            const uint32_t previous_kind = token > input->first ? parser->tokens.kinds[token - 1] : 0;
            const uint32_t name = token > input->first ? parser->tokens.names[token - 1] : 0;
            const bool specifier = previous_kind == PARSE_KIND_TYPEOF || previous_kind == PARSE_KIND_ATOMIC;
            TypeId type = 0;
            if (first != BUSTER_CHECK_NONE)
            {
                for (uint32_t item = first; valid && item < close; item += 1)
                {
                    if (parser->tokens.kinds[item] == PARSE_KIND_L_SQUARE)
                    {
                        const uint32_t finish = parser->tree.parents[item];
                        valid = finish > item && finish < close;
                        if (valid && item + 1 != finish)
                        {
                            valid = check_constant_raw_expression(input, item + 1, finish);
                        }
                        item = finish;
                    }
                }
                type = valid ? check_constant_type(input, first, close) : 0;
                valid &= type != 0;
            }
            else if (specifier)
            {
                valid = check_constant_raw_expression(input, token + 1, close);
                type = valid ? input->values[token + 1 - input->first].type : 0;
            }
            if (valid && specifier)
            {
                if (name == C_NAME_ATOMIC)
                {
                    type = check_input_type_result(reader, type_atomic(reader->types, type));
                }
                else if (name == C_NAME_BITINT || name == C_NAME_EXTINT)
                {
                    const CheckValue* value = &input->values[token + 1 - input->first];
                    int64_t width = 0;
                    valid = first == BUSTER_CHECK_NONE && (value->flags & BUSTER_CHECK_ICE) != 0 &&
                            check_action_i64(reader->types, value, &width) && width > 0 && width <= UINT32_MAX;
                    type = valid ? check_input_type_result(reader, type_bit_int(reader->types, (uint32_t)width, true)) : 0;
                }
                else if (name == C_NAME_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL2)
                {
                    type = type_unqualified(type);
                    type = type_kind(reader->types, type) == BUSTER_TYPE_KIND_ATOMIC ? type_element(reader->types, type) : type;
                }
                valid &= type != 0;
            }
            input->queries[close - input->first] = type;
        }
    }
    return valid;
}

static bool check_input_constant_read(CheckInputReader* reader, uint32_t first, uint32_t end, bool type_name, bool cached, CheckValue* value, TypeId* type)
{
    const uint64_t mark = reader->scratch->length;
    const uint32_t count = end >= first ? end - first : 0;
    CheckConstantInput input = { .reader = reader, .first = first, .end = end, .cached = cached };
    bool valid = count != 0 && end <= reader->parser->tokens.count;
#define BUSTER_CHECK_CONSTANT_ARRAY(field, element) input.field = valid ? arena_push_array(reader->scratch, count, sizeof(element), 16) : 0; valid &= input.field != 0
    BUSTER_CHECK_CONSTANT_ARRAY(values, CheckValue);
    BUSTER_CHECK_CONSTANT_ARRAY(present, uint8_t);
    BUSTER_CHECK_CONSTANT_ARRAY(syntax, uint8_t);
    BUSTER_CHECK_CONSTANT_ARRAY(first_value, uint32_t);
    BUSTER_CHECK_CONSTANT_ARRAY(last_value, uint32_t);
    BUSTER_CHECK_CONSTANT_ARRAY(next_value, uint32_t);
    BUSTER_CHECK_CONSTANT_ARRAY(type_begin, uint32_t);
    BUSTER_CHECK_CONSTANT_ARRAY(queries, TypeId);
    BUSTER_CHECK_CONSTANT_ARRAY(attribute_syntax, uint8_t);
    BUSTER_CHECK_CONSTANT_ARRAY(attribute_starts, CheckConstantAttributes*);
    BUSTER_CHECK_CONSTANT_ARRAY(attribute_ends, CheckConstantAttributes*);
    BUSTER_CHECK_CONSTANT_ARRAY(tag_next, uint32_t);
    input.tag_finishes = valid ? arena_push_array(reader->scratch, (uint64_t)count + 1, sizeof(uint32_t), 4) : 0;
    valid &= input.tag_finishes != 0;
#undef BUSTER_CHECK_CONSTANT_ARRAY
    reader->output->resource |= count != 0 && end <= reader->parser->tokens.count && !valid;
    if (valid)
    {
        memset(input.present, 0, count);
        memset(input.syntax, 0, count);
        memset(input.first_value, 0xff, (uint64_t)count * sizeof(uint32_t));
        memset(input.last_value, 0xff, (uint64_t)count * sizeof(uint32_t));
        memset(input.next_value, 0xff, (uint64_t)count * sizeof(uint32_t));
        memset(input.type_begin, 0xff, (uint64_t)count * sizeof(uint32_t));
        memset(input.queries, 0, (uint64_t)count * sizeof(TypeId));
        memset(input.attribute_syntax, 0, count);
        memset(input.attribute_starts, 0, (uint64_t)count * sizeof(CheckConstantAttributes*));
        memset(input.attribute_ends, 0, (uint64_t)count * sizeof(CheckConstantAttributes*));
        memset(input.tag_finishes, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t token = first; token < end; token += 1)
        {
            const uint8_t kind = reader->parser->tokens.kinds[token];
            input.raw |= reader->parser->tree.roles[token] == PARSE_ROLE_OPERAND && kind != PARSE_KIND_NUMBER && kind != PARSE_KIND_CHARACTER &&
                         kind != PARSE_KIND_STRING && kind != PARSE_KIND_IDENTIFIER && kind != PARSE_KIND_DEFAULT;
        }
        if (input.raw)
        {
            valid = check_constant_raw_bind(&input, type_name) && check_constant_raw_prepare(&input) &&
                    (type_name || check_constant_raw_expression(&input, first, end));
        }
        else
        {
            valid = check_constant_bind(&input, type_name) && check_constant_evaluate(&input);
        }
    }
    if (valid && type_name)
    {
        *type = check_constant_type(&input, first, end);
        valid = *type != 0;
    }
    else if (valid)
    {
        valid = check_constant_range_value(&input, first, end, value);
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}

static bool check_input_evaluate(CheckInputReader* reader, uint32_t first, uint32_t end, CheckValue* value)
{
    TypeId unused = 0;
    return check_input_constant_read(reader, first, end, false, false, value, &unused);
}

static __attribute__((unused)) bool check_input_evaluate_cached(CheckInputReader* reader, uint32_t first, uint32_t end, CheckValue* value)
{
    TypeId unused = 0;
    return check_input_constant_read(reader, first, end, false, true, value, &unused);
}

static bool check_input_constant(CheckInputReader* reader, uint32_t first, uint32_t end, int64_t* number)
{
    CheckValue value = { 0 };
    return check_input_evaluate(reader, first, end, &value) && (value.flags & BUSTER_CHECK_ICE) != 0 &&
           check_action_i64(reader->types, &value, number);
}

static __attribute__((unused)) TypeId check_input_constant_type(CheckInputReader* reader, uint32_t first, uint32_t end)
{
    CheckValue unused = { 0 };
    TypeId type = 0;
    const bool valid = check_input_constant_read(reader, first, end, true, false, &unused, &type);
    return valid ? type : 0;
}
