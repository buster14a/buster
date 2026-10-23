// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The parser-to-checker reader. It retains parser token identity, elaborates supported declarations in visibility
// order, resolves uses through c_bindings, and emits expression nodes in the parser's postorder. Parser depths are
// bracket depths and are never used as semantic dependency levels. Unsupported declarators and statements fail
// explicitly; no unresolved name or unrecognized syntax acquires an invented int type.

typedef enum CheckInputMessage
{
    CHECK_INPUT_UNSUPPORTED,
    CHECK_INPUT_DECLARATION,
    CHECK_INPUT_UNDECLARED,
    CHECK_INPUT_LITERAL,
    CHECK_INPUT_RESOURCE,
    CHECK_INPUT_REDECLARATION,
    CHECK_INPUT_RETURN,
    CHECK_INPUT_TARGET,
} CheckInputMessage;

static const char* const check_input_messages[] =
{
    "semantic checking of this construct is not implemented",
    "invalid or unsupported declaration",
    "use of undeclared identifier",
    "invalid or unsupported literal",
    "out of memory during semantic checking",
    "conflicting or repeated declaration",
    "return statement does not match the function's result type",
    "unsupported checker language or target model",
};

// The type/action tables currently describe one C LP64 model: signed plain char, 32-bit wchar_t, x87 long double
// stored in 16 bytes, and arithmetic evaluated in its source format. Refuse preprocessing options that advertise
// a different model before applying those fixed tables to the source.
static bool check_input_target_supported(const PPOptions* options)
{
    const PredefinesOptions* target = &options->predefines;
    const bool source_evaluation = target->fp_eval_method == BUSTER_PREDEFINES_EVAL_SOURCE ||
                                   (target->fp_eval_method == BUSTER_PREDEFINES_EVAL_DEFAULT && predefines_eval_method(target) == 0);
    return !options->assembly && !target->char_unsigned && !target->short_wchar &&
           target->long_double == BUSTER_PREDEFINES_LONG_DOUBLE_80 && source_evaluation;
}

typedef struct CheckInputReport CheckInputReport;
struct CheckInputReport
{
    uint32_t token;
    CheckInputMessage message;
};

typedef struct CheckInput CheckInput;
struct CheckInput
{
    CheckGraph graph;
    CheckInputReport* reports;
    uint32_t report_count;
    uint32_t report_capacity;
    uint32_t* declarations;
    TypeId* declared_types;
    uint8_t* declaration_kinds;
    uint32_t* objects;
    uint8_t* static_objects;
    uint8_t* registers;
    uint8_t* linkage;
    uint8_t* defined;
    uint8_t* tentative;
    uint32_t* initializer_nodes;
    uint32_t token_count;
    CBindingStatistics binding_statistics;
    uint32_t binding_event_count;
    bool resource;
};

typedef struct CheckInputReader CheckInputReader;
struct CheckInputReader
{
    CheckInput* output;
    const Parser* parser;
    Preprocessor* pp;
    TypeTable* types;
    Arena* arena;
    Arena* scratch;
    CBindings bindings;
    TypeId* declared_types;
    TypeId* query_types;
    TypeId* return_types;
    uint8_t* declaration_kinds;
    uint8_t* defined;
    uint8_t* definition_seen;
    uint8_t* static_objects;
    uint8_t* registers;
    uint8_t* linkage;
    uint32_t* objects;
    uint8_t* ignored;
    uint8_t* initializers;
    uint8_t* static_initializers;
    uint32_t* initializer_declarations;
    uint8_t* expression_commas;
    uint8_t* conditions;
    TypeId* declaration_bases;
    uint32_t* declaration_storage;
    uint8_t* declaration_flags;
    uint32_t* activate;
    uint32_t* activation_next;
    uint32_t* parameters;
    uint32_t* parameter_next;
    uint32_t* parameter_seen;
    uint32_t* parameter_previous;
    uint32_t* member_seen;
    TypeId* tag_types;
    uint32_t* tag_ends;
    int64_t* enum_values;
    uint32_t* postorder_positions;
    uint32_t* tag_completions;
    uint32_t* ends;
    uint32_t* first_child;
    uint32_t* next_child;
    uint32_t* first_value;
    uint32_t* last_value;
    uint32_t* value_next;
    uint32_t* scope_marks;
    uint32_t* scope_ends;
    uint32_t scope_count;
    TypeId function_result;
    uint32_t function_end;
};

static void check_input_report(CheckInputReader* reader, uint32_t token, CheckInputMessage message)
{
    CheckInput* input = reader->output;
    if (input->resource)
    {
        message = CHECK_INPUT_RESOURCE;
    }
    if (input->report_count < input->report_capacity)
    {
        input->reports[input->report_count++] = (CheckInputReport)
        {
            .token = token,
            .message = message,
        };
    }
    else
    {
        input->resource = true;
    }
}

static TypeId check_input_type_result(CheckInputReader* reader, TypeId type)
{
    if (type == 0 && reader->types->last_error == TYPE_ERROR_FULL)
    {
        reader->output->resource = true;
    }
    return type;
}

static bool check_input_qualifier(uint32_t name, uint32_t* qualifiers)
{
    uint32_t bit = 0;
    switch (name)
    {
        case C_NAME_CONST: case C_NAME_GNU_CONST: case C_NAME_GNU_CONST2: bit = BUSTER_TYPE_CONST; break;
        case C_NAME_VOLATILE: case C_NAME_GNU_VOLATILE: case C_NAME_GNU_VOLATILE2: bit = BUSTER_TYPE_VOLATILE; break;
        case C_NAME_RESTRICT: case C_NAME_GNU_RESTRICT: case C_NAME_GNU_RESTRICT2: bit = BUSTER_TYPE_RESTRICT; break;
        default: break;
    }
    *qualifiers |= bit;
    return bit != 0;
}

// Tags have their own namespace. A definition or stand-alone forward declaration hides an outer tag; an ordinary
// reference finds the visible tag. The declaration token remains the original tag-name token when a definition
// completes an earlier forward declaration. Body completion is handled separately by the iterative tag reader.
static TypeId check_input_tag(CheckInputReader* reader, uint32_t token, uint32_t end, bool definition)
{
    const Parser* parser = reader->parser;
    TypeId type = reader->tag_types[token];
    if (type == 0)
    {
        uint32_t i = token + 1;
        const uint8_t kind = parser->tokens.kinds[token] == PARSE_KIND_ENUM ? BUSTER_TYPE_KIND_ENUM :
                             parser->tokens.names[token] == C_NAME_UNION ? BUSTER_TYPE_KIND_UNION : BUSTER_TYPE_KIND_STRUCT;
        const uint32_t name = i < end && parser->tokens.kinds[i] == PARSE_KIND_IDENTIFIER ? parser->tokens.names[i] : 0;
        const uint32_t declaration = name != 0 ? i++ : token;
        const bool body = i < end && parser->tokens.kinds[i] == PARSE_KIND_L_BRACE;
        const bool forward = i < end && parser->tokens.kinds[i] == PARSE_KIND_SEMI;
        const uint32_t previous = name != 0 ? c_bindings_lookup(&reader->bindings, C_BINDING_TAG, name) : 0;
        const bool same_scope = previous != 0 && reader->bindings.depth[previous - 1] == reader->scope_count;
        bool valid = name != 0 || body;
        valid &= !body || definition;
        if (valid && previous != 0 && (same_scope || (!body && !forward)))
        {
            const uint32_t old = reader->bindings.declaration[previous - 1];
            type = reader->declared_types[old];
            valid = type_kind(reader->types, type) == kind && (!body || !type_is_complete(reader->types, type));
            reader->output->declarations[declaration] = old;
            reader->declared_types[declaration] = type;
        }
        else if (valid)
        {
            type = check_input_type_result(reader, type_tag(reader->types, kind, name));
            valid = type != 0;
            if (valid)
            {
                reader->tag_completions[reader->types->types.index[type_index(type)]] = BUSTER_CHECK_NONE;
            }
            reader->declared_types[declaration] = type;
            reader->declaration_kinds[declaration] = C_BINDING_TAG_KIND;
            reader->output->declarations[declaration] = declaration;
            if (valid && name != 0 && c_bindings_declare(&reader->bindings, C_BINDING_TAG, name, C_BINDING_TAG_KIND, declaration, i, reader->scope_count) == 0)
            {
                reader->output->resource = true;
                valid = false;
            }
        }
        if (valid)
        {
            reader->ignored[token] = true;
            reader->ignored[declaration] = true;
            reader->tag_types[token] = type;
            reader->tag_ends[token] = i;
        }
        else
        {
            type = 0;
        }
    }
    return type;
}

// Specifiers are retained in source order because their inherited base type precedes declarators semantically,
// although the parser's ordinary postorder puts their nodes after those declarators.
static TypeId check_input_specifiers(CheckInputReader* reader, uint32_t* cursor, uint32_t end, bool* is_typedef, uint32_t* storage_class, bool* is_inline)
{
    const Parser* parser = reader->parser;
    uint32_t i = *cursor;
    uint32_t qualifiers = 0;
    uint32_t longs = 0;
    uint32_t shorts = 0;
    uint32_t signedness = 0;
    uint32_t storage = 0;
    TypeId base = 0;
    bool integer_seen = false;
    bool valid = true;
    bool saw = false;
    bool reading = true;
    *is_typedef = false;
    *is_inline = false;
    while (reading && i < end)
    {
        const uint32_t name = parser->tokens.names[i];
        const uint8_t kind = parser->tokens.kinds[i];
        TypeId selected = 0;
        bool consumed = true;
        if (check_input_qualifier(name, &qualifiers))
        {
        }
        else if (kind == PARSE_KIND_TYPEDEF)
        {
            valid &= !*is_typedef;
            *is_typedef = true;
        }
        else if (kind == PARSE_KIND_STORAGE)
        {
            *is_inline |= name == C_NAME_INLINE || name == C_NAME_GNU_INLINE || name == C_NAME_GNU_INLINE2;
            valid &= name == C_NAME_STATIC || name == C_NAME_EXTERN || name == C_NAME_REGISTER || name == C_NAME_AUTO || name == C_NAME_INLINE ||
                     name == C_NAME_GNU_INLINE || name == C_NAME_GNU_INLINE2;
            if (name == C_NAME_STATIC || name == C_NAME_EXTERN || name == C_NAME_REGISTER || name == C_NAME_AUTO)
            {
                valid &= storage == 0;
                storage = name;
            }
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
            selected = check_input_tag(reader, i, end, false);
            valid &= selected != 0 && base == 0;
            base = selected;
            i = selected != 0 ? reader->tag_ends[i] - 1 : i;
        }
        else if (kind == PARSE_KIND_TYPE)
        {
            switch (name)
            {
                case C_NAME_CHAR: selected = BUSTER_TYPE_CHAR; break;
                case C_NAME_VOID: selected = BUSTER_TYPE_VOID; break;
                case C_NAME_BOOL: case C_NAME_BOOL_C99: selected = BUSTER_TYPE_BOOL; break;
                case C_NAME_FLOAT: selected = BUSTER_TYPE_FLOAT; break;
                case C_NAME_DOUBLE: selected = BUSTER_TYPE_DOUBLE; break;
                case C_NAME_INT128: selected = BUSTER_TYPE_INT128; break;
                default: valid = false; break;
            }
            valid &= base == 0;
            base = selected;
        }
        else if (kind == PARSE_KIND_IDENTIFIER && parser->tree.roles[i] == PARSE_ROLE_SPECIFIER && base == 0 && !integer_seen && !longs && !shorts && !signedness)
        {
            const uint32_t handle = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name);
            valid &= handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF;
            if (handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF)
            {
                const uint32_t declaration = reader->bindings.declaration[handle - 1];
                base = reader->declared_types[declaration];
                reader->output->declarations[i] = declaration;
                reader->declared_types[i] = base;
            }
        }
        else
        {
            consumed = false;
            reading = false;
        }
        if (consumed)
        {
            reader->ignored[i] = true;
            saw = true;
            i += 1;
        }
    }
    valid &= saw && (base != 0 || integer_seen || longs || shorts || signedness) && longs <= 2 && shorts <= 1 && !(longs && shorts);
    valid &= !*is_typedef || (storage == 0 && !*is_inline);
    if (base == 0)
    {
        base = shorts ? (signedness == 2 ? BUSTER_TYPE_USHORT : BUSTER_TYPE_SHORT) :
               longs == 2 ? (signedness == 2 ? BUSTER_TYPE_ULLONG : BUSTER_TYPE_LLONG) :
               longs == 1 ? (signedness == 2 ? BUSTER_TYPE_ULONG : BUSTER_TYPE_LONG) :
                            (signedness == 2 ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT);
    }
    else if (base == BUSTER_TYPE_CHAR)
    {
        valid &= !longs && !shorts && !integer_seen;
        base = signedness == 2 ? BUSTER_TYPE_UCHAR : signedness == 1 ? BUSTER_TYPE_SCHAR : BUSTER_TYPE_CHAR;
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
    else
    {
        valid &= !longs && !shorts && !signedness && !integer_seen;
    }
    if ((qualifiers & BUSTER_TYPE_RESTRICT) != 0)
    {
        const bool pointer = type_kind(reader->types, base) == BUSTER_TYPE_KIND_POINTER;
        valid &= pointer && type_kind(reader->types, reader->types->types.base[type_index(base)]) != BUSTER_TYPE_KIND_FUNCTION;
    }
    *cursor = i;
    *storage_class = storage;
    return valid ? type_qualify(reader->types, base, qualifiers) : 0;
}

static bool check_input_spelling(CheckInputReader* reader, uint32_t token, const uint8_t** bytes, uint32_t* length)
{
    const uint32_t source = reader->parser->tokens.sources[token];
    PPTokens* output = &reader->pp->output;
    const uint8_t kind = output->kinds[source];
    const uint64_t capacity = kind == BUSTER_PP_KIND_INTEGER ? 24 : (uint64_t)output->values[source] - output->locations[source] + 1;
    uint8_t* buffer = arena_push(reader->scratch, capacity, 1);
    if (buffer != 0)
    {
        *length = pp_spelling(reader->pp, kind, output->values[source], output->locations[source], buffer, bytes);
    }
    return buffer != 0;
}

static CheckLiteral check_input_literal(CheckInputReader* reader, uint32_t token)
{
    const uint64_t mark = reader->scratch->length;
    const uint8_t* bytes = 0;
    uint32_t length = 0;
    CheckLiteral result = { 0 };
    const uint32_t name = reader->parser->tokens.names[token];
    if (name == C_NAME_TRUE || name == C_NAME_FALSE)
    {
        result = (CheckLiteral)
        {
            .type = BUSTER_TYPE_BOOL,
            .integer = name == C_NAME_TRUE,
            .valid = true,
        };
    }
    else if (name == C_NAME_NULLPTR || name == C_NAME_GNU_NULLPTR)
    {
        result = (CheckLiteral)
        {
            .type = BUSTER_TYPE_NULLPTR,
            .valid = true,
        };
    }
    else if (name == 0 && check_input_spelling(reader, token, &bytes, &length))
    {
        result = reader->parser->tokens.kinds[token] == PARSE_KIND_CHARACTER ? check_literal_character(bytes, length, reader->pp->options.language.standard >= 2023) :
                 check_literal_number(bytes, length, reader->scratch);
    }
    arena_pop_to(reader->scratch, mark);
    return result;
}

static CheckOp check_input_binary(uint8_t kind);

// Declaration-time ICEs are evaluated through the same semantic actions as expression graphs. This bounded
// reader handles integer literals/enumerators and unary/binary arithmetic; other forms remain explicit gaps.
// Resolve names in source order before visiting the expression's postorder, preserving binding-event ordering.
static bool check_input_constant(CheckInputReader* reader, uint32_t first, uint32_t end, int64_t* number)
{
    const Parser* parser = reader->parser;
    const uint64_t mark = reader->scratch->length;
    const uint32_t count = end - first;
    CheckValue* values = arena_push_array(reader->scratch, (uint64_t)count + 1, sizeof(CheckValue), 16);
    uint8_t* present = arena_push_array(reader->scratch, (uint64_t)count + 1, sizeof(uint8_t), 1);
    bool valid = count != 0 && values != 0 && present != 0;
    reader->output->resource |= values == 0 || present == 0;
    uint32_t root = BUSTER_CHECK_NONE;
    uint32_t roots = 0;
    if (valid)
    {
        memset(present, 0, (uint64_t)count + 1);
    }
    for (uint32_t token = first; valid && token < end; token += 1)
    {
        const uint32_t parent = parser->tree.parents[token];
        if (parent < first || parent >= end)
        {
            root = token;
            roots += 1;
        }
        if (parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER)
        {
            const uint32_t handle = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[token]);
            valid = handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_ENUMERATOR;
            if (valid)
            {
                const uint32_t declaration = reader->bindings.declaration[handle - 1];
                reader->output->declarations[token] = declaration;
                reader->declared_types[token] = reader->declared_types[declaration];
                if (c_bindings_use(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[token], token, token) == 0)
                {
                    reader->output->resource = true;
                    valid = false;
                }
            }
        }
    }
    valid &= roots == 1;
    const uint32_t after = valid ? reader->postorder_positions[root] + 1 : 0;
    const uint32_t size = valid ? parser->sizes[after - 1] : 0;
    valid &= size != 0 && size <= after;
    const uint32_t before = valid ? after - size : after;
    for (uint32_t order = before; valid && order < after; order += 1)
    {
        const uint32_t token = parser->postorder[order];
        valid = token >= first && token < end;
        if (valid)
        {
            const uint8_t role = parser->tree.roles[token];
            const uint8_t kind = parser->tokens.kinds[token];
            CheckAction action = { .op = CHECK_OP_COUNT, .node = token };
            uint32_t operands = 0;
            for (uint32_t child = reader->first_child[token]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                if (child >= first && child < end && present[child - first])
                {
                    if (operands < 3)
                    {
                        action.operands[operands] = values[child - first];
                    }
                    operands += 1;
                }
            }
            if (role == PARSE_ROLE_OPERAND && (kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER))
            {
                const CheckLiteral literal = check_input_literal(reader, token);
                valid = literal.valid && !literal.real;
                values[token - first] = (CheckValue)
                {
                    .type = literal.type,
                    .category = CHECK_RVALUE,
                    .flags = BUSTER_CHECK_ICE,
                    .constant_kind = CHECK_CONSTANT_INTEGER,
                    .integer = literal.integer,
                };
                present[token - first] = true;
            }
            else if (role == PARSE_ROLE_OPERAND && kind == PARSE_KIND_IDENTIFIER)
            {
                values[token - first] = (CheckValue)
                {
                    .type = BUSTER_TYPE_INT,
                    .category = CHECK_RVALUE,
                    .flags = BUSTER_CHECK_ICE,
                    .constant_kind = CHECK_CONSTANT_INTEGER,
                    .integer = (uint32_t)reader->enum_values[reader->output->declarations[token]],
                };
                present[token - first] = true;
            }
            else if (role == PARSE_ROLE_PREFIX)
            {
                action.op = kind == PARSE_KIND_PLUS ? CHECK_OP_POSITIVE : kind == PARSE_KIND_MINUS ? CHECK_OP_NEGATIVE :
                            kind == PARSE_KIND_EXCLAIM ? CHECK_OP_NOT : kind == PARSE_KIND_TILDE ? CHECK_OP_BIT_NOT : CHECK_OP_COUNT;
                valid = operands == 1 && action.op != CHECK_OP_COUNT;
            }
            else if (role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_LOGICAL_OR)
            {
                action.op = check_input_binary(kind);
                valid = operands == 2 && action.op != CHECK_OP_COUNT;
            }
            else if (role == PARSE_ROLE_CLOSE_GROUP)
            {
                valid = operands == 1;
                values[token - first] = action.operands[0];
                present[token - first] = true;
            }
            else
            {
                valid = role == PARSE_ROLE_OPEN_GROUP;
            }
            if (valid && action.op != CHECK_OP_COUNT)
            {
                CheckActionResult checked;
                check_action_scalar(reader->types, &action, &checked);
                valid = checked.diagnostic == CHECK_DIAG_NONE;
                values[token - first] = checked.value;
                present[token - first] = true;
            }
        }
    }
    if (valid)
    {
        const CheckValue* value = &values[root - first];
        valid = present[root - first] && (value->flags & BUSTER_CHECK_ICE) != 0 && check_action_i64(reader->types, value, number);
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}

typedef struct CheckDeclaratorPart CheckDeclaratorPart;
struct CheckDeclaratorPart
{
    uint32_t next;
    uint32_t token;
    uint32_t qualifiers;
    uint32_t parameter_first;
    uint32_t parameter_last;
    uint32_t parameter_count;
    uint32_t names;
    uint32_t last_name;
    uint64_t extent;
    uint8_t kind;
    uint8_t flags;
};

typedef struct CheckDeclaratorParameter CheckDeclaratorParameter;
struct CheckDeclaratorParameter
{
    TypeId type;
    uint32_t next;
};

typedef struct CheckDeclaratorFrame CheckDeclaratorFrame;
struct CheckDeclaratorFrame
{
    TypeId base;
    uint32_t cursor;
    uint32_t end;
    uint32_t name;
    uint32_t first;
    uint32_t last;
    uint32_t pointers;
    uint32_t function;
    uint32_t binding_mark;
    uint32_t storage;
    uint8_t state;
    bool group;
    bool parameter;
};

static CheckDeclaratorFrame check_input_declarator_frame(TypeId base, uint32_t first, uint32_t end, bool group, bool parameter, uint32_t storage)
{
    return (CheckDeclaratorFrame)
    {
        .base = base,
        .cursor = first,
        .end = end,
        .name = BUSTER_CHECK_NONE,
        .first = BUSTER_CHECK_NONE,
        .last = BUSTER_CHECK_NONE,
        .pointers = BUSTER_CHECK_NONE,
        .function = BUSTER_CHECK_NONE,
        .storage = storage,
        .group = group,
        .parameter = parameter,
    };
}

// Declarator operators are collected outwards from the identifier, then applied in reverse to the base type.
// Group and prototype-parameter frames share one explicit stack, so neither parentheses nor nested callbacks use
// the C call stack. A prototype's bindings are restored at its closing parenthesis; its own names are retained only
// for a possible function body. Arrays accept the declaration ICE reader's integer arithmetic; VLA and qualified
// array syntax fail closed.
static TypeId check_input_declarator(CheckInputReader* reader, TypeId base, uint32_t* cursor, uint32_t end, uint32_t* name, uint32_t* names)
{
    const Parser* parser = reader->parser;
    uint32_t stop = *cursor;
    while (stop < end)
    {
        const uint8_t kind = parser->tokens.kinds[stop];
        if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE)
        {
            const uint32_t close = parser->tree.parents[stop];
            if (close <= stop || close >= end)
            {
                break;
            }
            stop = close + 1;
        }
        else if (kind == PARSE_KIND_COMMA || kind == PARSE_KIND_EQUAL || kind == PARSE_KIND_SEMI || kind == PARSE_KIND_L_BRACE ||
                 kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_COLON)
        {
            break;
        }
        else
        {
            stop += 1;
        }
    }
    const uint32_t capacity = stop - *cursor + 1;
    const uint64_t mark = reader->scratch->length;
    CheckDeclaratorFrame* frames = arena_push_array(reader->scratch, capacity, sizeof(CheckDeclaratorFrame), 8);
    CheckDeclaratorPart* parts = arena_push_array(reader->scratch, capacity, sizeof(CheckDeclaratorPart), 8);
    CheckDeclaratorParameter* parameters = arena_push_array(reader->scratch, capacity, sizeof(CheckDeclaratorParameter), 4);
    bool valid = frames != 0 && parts != 0 && parameters != 0 && base != 0;
    reader->output->resource |= frames == 0 || parts == 0 || parameters == 0;
    const uint32_t binding_mark = c_bindings_mark(&reader->bindings);
    uint32_t depth = 0;
    uint32_t part_count = 0;
    uint32_t parameter_count = 0;
    TypeId result = 0;
    *name = BUSTER_CHECK_NONE;
    *names = BUSTER_CHECK_NONE;
    if (valid)
    {
        frames[depth++] = check_input_declarator_frame(base, *cursor, stop, false, false, 0);
    }
    while (valid && depth != 0)
    {
        CheckDeclaratorFrame* frame = &frames[depth - 1];
        uint32_t i = frame->cursor;
        if (frame->state == 0)
        {
            while (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_STAR)
            {
                const uint32_t token = i;
                reader->ignored[i++] = true;
                uint32_t qualifiers = 0;
                while (i < frame->end && check_input_qualifier(parser->tokens.names[i], &qualifiers))
                {
                    reader->ignored[i++] = true;
                }
                parts[part_count] = (CheckDeclaratorPart)
                {
                    .next = frame->pointers,
                    .token = token,
                    .qualifiers = qualifiers,
                    .kind = BUSTER_TYPE_KIND_POINTER,
                };
                frame->pointers = part_count++;
            }
            frame->state = 1;
            if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_IDENTIFIER && parser->tree.roles[i] == PARSE_ROLE_OPERAND)
            {
                frame->name = i;
                reader->ignored[i++] = true;
            }
            else if (i < frame->end && parser->tree.roles[i] == PARSE_ROLE_OPEN_GROUP)
            {
                const uint32_t close = parser->tree.parents[i];
                valid = close > i && close < frame->end;
                if (valid)
                {
                    reader->ignored[i] = true;
                    reader->ignored[close] = true;
                    frame->cursor = close + 1;
                    frames[depth++] = check_input_declarator_frame(base, i + 1, close, true, false, 0);
                }
            }
            frame->cursor = depth != 0 && frame == &frames[depth - 1] ? i : frame->cursor;
        }
        else if (frame->state == 2)
        {
            CheckDeclaratorPart* function = &parts[frame->function];
            const uint32_t close = parser->tree.parents[function->token];
            if (i == close)
            {
                valid = c_bindings_restore(&reader->bindings, frame->binding_mark, close);
                reader->output->resource |= !valid;
                for (uint32_t parameter = function->names; parameter != BUSTER_CHECK_NONE; parameter = reader->parameter_next[parameter])
                {
                    reader->parameter_seen[parser->tokens.names[parameter]] = reader->parameter_previous[parameter];
                }
                reader->ignored[close] = true;
                frame->cursor = close + 1;
                frame->state = 1;
            }
            else if (i < close && parser->tokens.kinds[i] == PARSE_KIND_ELLIPSIS)
            {
                valid = (function->parameter_count != 0 || parser->language.standard >= 2023) && i + 1 == close;
                function->flags |= BUSTER_TYPE_FLAG_VARIADIC;
                reader->ignored[i] = true;
                frame->cursor = i + 1;
            }
            else if (i < close)
            {
                bool is_typedef;
                bool is_inline;
                uint32_t storage;
                const TypeId parameter = check_input_specifiers(reader, &i, close, &is_typedef, &storage, &is_inline);
                valid = parameter != 0 && !is_typedef && !is_inline && (storage == 0 || storage == C_NAME_REGISTER);
                if (valid)
                {
                    frames[depth++] = check_input_declarator_frame(parameter, i, close, false, true, storage);
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
            CheckDeclaratorPart part =
            {
                .next = BUSTER_CHECK_NONE,
                .token = i,
                .kind = BUSTER_TYPE_KIND_INCOMPLETE_ARRAY,
            };
            if (valid && i + 1 != close)
            {
                int64_t extent = 0;
                valid = check_input_constant(reader, i + 1, close, &extent) && extent >= 0 && (extent != 0 || parser->language.gnu);
                part.kind = BUSTER_TYPE_KIND_ARRAY;
                part.extent = (uint64_t)extent;
            }
            if (valid)
            {
                for (uint32_t token = i; token <= close; token += 1)
                {
                    reader->ignored[token] = true;
                }
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
                parts[part_count] = (CheckDeclaratorPart)
                {
                    .next = BUSTER_CHECK_NONE,
                    .token = i,
                    .parameter_first = BUSTER_CHECK_NONE,
                    .parameter_last = BUSTER_CHECK_NONE,
                    .names = BUSTER_CHECK_NONE,
                    .last_name = BUSTER_CHECK_NONE,
                    .kind = BUSTER_TYPE_KIND_FUNCTION,
                    .flags = i + 1 != close || parser->language.standard >= 2023 ? BUSTER_TYPE_FLAG_PROTOTYPE : 0,
                };
                frame->function = part_count;
                frame->last = part_count++;
                frame->binding_mark = c_bindings_mark(&reader->bindings);
                frame->state = 2;
                frame->cursor = i + 1;
                reader->ignored[i] = true;
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
                CheckDeclaratorFrame* parent = &frames[depth - 2];
                parent->name = frame->name;
                parent->first = frame->first;
                parent->last = frame->last;
                depth -= 1;
            }
            else if (valid)
            {
                const uint32_t first = frame->first;
                uint32_t reverse = BUSTER_CHECK_NONE;
                for (uint32_t part = first; part != BUSTER_CHECK_NONE;)
                {
                    const uint32_t next = parts[part].next;
                    parts[part].next = reverse;
                    reverse = part;
                    part = next;
                }
                TypeId type = frame->base;
                for (uint32_t p = reverse; type != 0 && p != BUSTER_CHECK_NONE; p = parts[p].next)
                {
                    const CheckDeclaratorPart* part = &parts[p];
                    if (part->kind == BUSTER_TYPE_KIND_POINTER)
                    {
                        const bool restrict_function = (part->qualifiers & BUSTER_TYPE_RESTRICT) != 0 && type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION;
                        type = restrict_function ? 0 : check_input_type_result(reader, type_pointer(reader->types, type));
                        type = type != 0 ? type_qualify(reader->types, type, part->qualifiers) : 0;
                    }
                    else if (part->kind == BUSTER_TYPE_KIND_FUNCTION)
                    {
                        const uint64_t parameter_mark = reader->scratch->length;
                        TypeId* parameter_types = arena_push_array(reader->scratch, (uint64_t)part->parameter_count + 1, sizeof(TypeId), 4);
                        reader->output->resource |= parameter_types == 0;
                        uint32_t count = 0;
                        for (uint32_t parameter = part->parameter_first; parameter_types != 0 && parameter != BUSTER_CHECK_NONE; parameter = parameters[parameter].next)
                        {
                            parameter_types[count++] = parameters[parameter].type;
                        }
                        type = parameter_types != 0 ? check_input_type_result(reader, type_function(reader->types, type, parameter_types, count, part->flags)) : 0;
                        arena_pop_to(reader->scratch, parameter_mark);
                    }
                    else
                    {
                        type = check_input_type_result(reader, part->kind == BUSTER_TYPE_KIND_ARRAY ? type_array(reader->types, type, part->extent) :
                                                                                                  type_incomplete_array(reader->types, type));
                    }
                }
                valid = type != 0;
                if (valid && frame->parameter)
                {
                    CheckDeclaratorFrame* parent = &frames[depth - 2];
                    CheckDeclaratorPart* function = &parts[parent->function];
                    const bool only_void = type == BUSTER_TYPE_VOID && frame->name == BUSTER_CHECK_NONE && function->parameter_count == 0 && i == frame->end;
                    if (!only_void)
                    {
                        const uint32_t qualifiers = type_qualifiers(type);
                        const uint8_t kind = type_kind(reader->types, type);
                        const TypeId adjusted = check_input_type_result(reader, type_adjust_parameter(reader->types, type));
                        valid = adjusted != 0 && type_kind(reader->types, adjusted) != BUSTER_TYPE_KIND_VOID;
                        if (valid)
                        {
                            parameters[parameter_count] = (CheckDeclaratorParameter)
                            {
                                .type = adjusted,
                                .next = BUSTER_CHECK_NONE,
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
                            if (frame->name != BUSTER_CHECK_NONE)
                            {
                                const uint32_t token = frame->name;
                                const uint32_t identifier = parser->tokens.names[token];
                                if (reader->parameter_seen[identifier] == function->token + 1)
                                {
                                    check_input_report(reader, token, CHECK_INPUT_REDECLARATION);
                                    valid = false;
                                }
                                reader->parameter_previous[token] = reader->parameter_seen[identifier];
                                reader->parameter_seen[identifier] = function->token + 1;
                                reader->declared_types[token] = type_kind_is_array(kind) ? adjusted : type_qualify(reader->types, adjusted, qualifiers);
                                reader->declaration_kinds[token] = C_BINDING_OBJECT;
                                reader->registers[token] = frame->storage == C_NAME_REGISTER;
                                reader->objects[token] = token + 1;
                                reader->output->declarations[token] = token;
                                reader->parameter_next[token] = BUSTER_CHECK_NONE;
                                if (function->last_name != BUSTER_CHECK_NONE)
                                {
                                    reader->parameter_next[function->last_name] = token;
                                }
                                else
                                {
                                    function->names = token;
                                }
                                function->last_name = token;
                                if (c_bindings_declare(&reader->bindings, C_BINDING_ORDINARY, identifier, C_BINDING_OBJECT, token, i, reader->scope_count + depth) == 0)
                                {
                                    reader->output->resource = true;
                                    valid = false;
                                }
                            }
                        }
                    }
                    parent->cursor = i;
                    if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_COMMA)
                    {
                        reader->ignored[i] = true;
                        parent->cursor = i + 1;
                        valid &= i + 1 < frame->end;
                    }
                    depth -= 1;
                }
                else if (valid)
                {
                    result = type;
                    *cursor = i;
                    *name = frame->name;
                    *names = first != BUSTER_CHECK_NONE && parts[first].kind == BUSTER_TYPE_KIND_FUNCTION ? parts[first].names : BUSTER_CHECK_NONE;
                    depth -= 1;
                }
            }
        }
    }
    if (!c_bindings_restore(&reader->bindings, binding_mark, stop))
    {
        reader->output->resource = true;
    }
    arena_pop_to(reader->scratch, mark);
    return valid ? result : 0;
}

typedef struct CheckTagFrame CheckTagFrame;
struct CheckTagFrame
{
    TypeId type;
    uint32_t token;
    uint32_t cursor;
    uint32_t close;
    uint32_t first;
    uint32_t last;
    uint32_t count;
    int64_t enumerator;
    uint32_t negative_bits;
    uint32_t positive_bits;
};

typedef struct CheckTagMember CheckTagMember;
struct CheckTagMember
{
    TypeMember member;
    uint32_t next;
    uint32_t previous;
};

// Complete nested record definitions with explicit frames. Their tags have the enclosing declaration scope;
// members use an independent per-record namespace. Each frame retains a linked slice because nested records may
// append their members between two outer members; only completion packs that slice for the type-table consumer.
static bool check_input_tag_definition(CheckInputReader* reader, uint32_t token, uint32_t end)
{
    const Parser* parser = reader->parser;
    const uint64_t mark = reader->scratch->length;
    uint32_t open = token + 1;
    open += open < end && parser->tokens.kinds[open] == PARSE_KIND_IDENTIFIER;
    bool valid = open < end && parser->tokens.kinds[open] == PARSE_KIND_L_BRACE;
    const uint32_t close = valid ? parser->tree.parents[open] : open;
    valid &= close > open && close < end;
    const uint32_t capacity = valid ? close - token + 1 : 1;
    CheckTagFrame* frames = valid ? arena_push_array(reader->scratch, capacity, sizeof(CheckTagFrame), 8) : 0;
    CheckTagMember* members = valid ? arena_push_array(reader->scratch, capacity, sizeof(CheckTagMember), 8) : 0;
    reader->output->resource |= valid && (frames == 0 || members == 0);
    valid &= frames != 0 && members != 0;
    uint32_t depth = 0;
    uint32_t member_count = 0;
    if (valid)
    {
        const TypeId type = check_input_tag(reader, token, end, true);
        valid = type != 0;
        frames[depth++] = (CheckTagFrame)
        {
            .type = type,
            .token = token,
            .cursor = open + 1,
            .close = close,
            .first = BUSTER_CHECK_NONE,
            .last = BUSTER_CHECK_NONE,
        };
        reader->ignored[open] = true;
    }
    while (valid && depth != 0)
    {
        CheckTagFrame* frame = &frames[depth - 1];
        uint32_t i = frame->cursor;
        const uint8_t kind = type_kind(reader->types, frame->type);
        if (i == frame->close)
        {
            if (kind == BUSTER_TYPE_KIND_ENUM)
            {
                const TypeId underlying = type_enum_underlying(false, frame->negative_bits, frame->positive_bits);
                valid = frame->count != 0 && type_enum_complete(reader->types, frame->type, underlying, 0);
            }
            else
            {
                const uint64_t member_mark = reader->scratch->length;
                TypeMember* packed = arena_push_array(reader->scratch, (uint64_t)frame->count + 1, sizeof(TypeMember), 8);
                valid = packed != 0;
                reader->output->resource |= !valid;
                uint32_t written = 0;
                for (uint32_t member = frame->first; valid && member != BUSTER_CHECK_NONE; member = members[member].next)
                {
                    const TypeMember value = members[member].member;
                    packed[written++] = value;
                    if (value.name != 0)
                    {
                        reader->member_seen[value.name] = members[member].previous;
                    }
                    if (type_kind(reader->types, value.type) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
                    {
                        valid = kind == BUSTER_TYPE_KIND_STRUCT && written > 1 && written == frame->count;
                    }
                }
                const TypeRecordAttributes attributes = { 0 };
                valid = valid && type_complete_record(reader->types, frame->type, packed, frame->count, attributes);
                arena_pop_to(reader->scratch, member_mark);
            }
            reader->output->resource |= !valid && reader->types->last_error == TYPE_ERROR_FULL;
            if (valid)
            {
                reader->tag_completions[reader->types->types.index[type_index(frame->type)]] = i;
            }
            reader->ignored[i] = true;
            reader->tag_ends[frame->token] = i + 1;
            depth -= 1;
        }
        else if (i > frame->close)
        {
            valid = false;
        }
        else if (kind == BUSTER_TYPE_KIND_ENUM)
        {
            const uint32_t name = i;
            valid = parser->tokens.kinds[i] == PARSE_KIND_IDENTIFIER;
            int64_t value = frame->enumerator;
            i += 1;
            if (valid && i < frame->close && parser->tokens.kinds[i] == PARSE_KIND_EQUAL)
            {
                const uint32_t first = ++i;
                while (i < frame->close && parser->tokens.kinds[i] != PARSE_KIND_COMMA)
                {
                    if (parser->tokens.kinds[i] == PARSE_KIND_L_PAREN)
                    {
                        i = parser->tree.parents[i];
                    }
                    i += 1;
                }
                valid = check_input_constant(reader, first, i, &value);
            }
            // C17 enumerator identities have type int. Wider GNU/C23 enumeration values need mode-specific types.
            valid &= value >= INT32_MIN && value <= INT32_MAX;
            if (valid)
            {
                const uint32_t identifier = parser->tokens.names[name];
                const uint32_t old = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, identifier);
                valid = old == 0 || reader->bindings.depth[old - 1] != reader->scope_count;
                if (!valid)
                {
                    check_input_report(reader, name, CHECK_INPUT_REDECLARATION);
                }
                else
                {
                    reader->declared_types[name] = BUSTER_TYPE_INT;
                    reader->declaration_kinds[name] = C_BINDING_ENUMERATOR;
                    reader->enum_values[name] = value;
                    reader->output->declarations[name] = name;
                    valid = c_bindings_declare(&reader->bindings, C_BINDING_ORDINARY, identifier, C_BINDING_ENUMERATOR, name, i, reader->scope_count) != 0;
                    reader->output->resource |= !valid;
                    frame->enumerator = value + 1;
                    frame->count += 1;
                    type_enum_value_bits(value, false, &frame->negative_bits, &frame->positive_bits);
                }
            }
            for (uint32_t ignored = name; ignored < i; ignored += 1)
            {
                reader->ignored[ignored] = true;
            }
            if (i < frame->close && parser->tokens.kinds[i] == PARSE_KIND_COMMA)
            {
                reader->ignored[i++] = true;
            }
            else
            {
                valid &= i == frame->close;
            }
            frame->cursor = i;
        }
        else
        {
            uint32_t nested = i;
            while (nested < frame->close && (parser->tree.roles[nested] == PARSE_ROLE_SPECIFIER || parser->tokens.kinds[nested] == PARSE_KIND_QUALIFIER))
            {
                nested += 1;
            }
            uint32_t body = nested + 1;
            body += body < frame->close && parser->tokens.kinds[body] == PARSE_KIND_IDENTIFIER;
            if (nested < frame->close && reader->tag_types[nested] == 0 &&
                (parser->tokens.kinds[nested] == PARSE_KIND_STRUCT || parser->tokens.kinds[nested] == PARSE_KIND_ENUM) &&
                body < frame->close && parser->tokens.kinds[body] == PARSE_KIND_L_BRACE)
            {
                const uint32_t nested_close = parser->tree.parents[body];
                const TypeId type = check_input_tag(reader, nested, frame->close, true);
                valid = type != 0 && nested_close > body && nested_close < frame->close;
                if (valid)
                {
                    frames[depth++] = (CheckTagFrame)
                    {
                        .type = type,
                        .token = nested,
                        .cursor = body + 1,
                        .close = nested_close,
                        .first = BUSTER_CHECK_NONE,
                        .last = BUSTER_CHECK_NONE,
                    };
                    reader->ignored[body] = true;
                }
            }
            else
            {
                const uint32_t first = i;
                bool is_typedef;
                bool is_inline;
                uint32_t storage;
                const TypeId base = check_input_specifiers(reader, &i, frame->close, &is_typedef, &storage, &is_inline);
                valid = base != 0 && !is_typedef && !is_inline && storage == 0;
                bool more = valid;
                while (more)
                {
                    uint32_t name;
                    uint32_t parameters;
                    const TypeId type = check_input_declarator(reader, base, &i, frame->close, &name, &parameters);
                    uint32_t width = BUSTER_TYPE_NOT_BIT_FIELD;
                    if (i < frame->close && parser->tokens.kinds[i] == PARSE_KIND_COLON)
                    {
                        const uint32_t colon = i++;
                        const uint32_t begin = i;
                        while (i < frame->close && parser->tokens.kinds[i] != PARSE_KIND_COMMA && parser->tokens.kinds[i] != PARSE_KIND_SEMI)
                        {
                            if (parser->tokens.kinds[i] == PARSE_KIND_L_PAREN)
                            {
                                i = parser->tree.parents[i];
                            }
                            i += 1;
                        }
                        int64_t bits = 0;
                        valid &= check_input_constant(reader, begin, i, &bits) && bits >= 0 && bits <= UINT32_MAX;
                        width = (uint32_t)bits;
                        for (uint32_t ignored = colon; ignored < i; ignored += 1)
                        {
                            reader->ignored[ignored] = true;
                        }
                    }
                    valid &= type != 0 && i < frame->close && (parser->tokens.kinds[i] == PARSE_KIND_COMMA || parser->tokens.kinds[i] == PARSE_KIND_SEMI);
                    if (valid && name == BUSTER_CHECK_NONE && width == BUSTER_TYPE_NOT_BIT_FIELD)
                    {
                        // A tag-only declaration is not a named member. Anonymous record members need inherited-name
                        // conflict checking and are left explicitly unsupported rather than accepted without it.
                        check_input_report(reader, first, CHECK_INPUT_UNSUPPORTED);
                        valid = false;
                    }
                    if (valid)
                    {
                        const uint32_t identifier = name != BUSTER_CHECK_NONE ? parser->tokens.names[name] : 0;
                        valid = identifier == 0 || reader->member_seen[identifier] != frame->token + 1;
                        if (!valid)
                        {
                            check_input_report(reader, name, CHECK_INPUT_REDECLARATION);
                        }
                        else
                        {
                            members[member_count] = (CheckTagMember)
                            {
                                .member =
                                {
                                    .name = identifier,
                                    .type = type,
                                    .bit_width = width,
                                },
                                .next = BUSTER_CHECK_NONE,
                                .previous = reader->member_seen[identifier],
                            };
                            if (identifier != 0)
                            {
                                reader->member_seen[identifier] = frame->token + 1;
                                reader->declared_types[name] = type;
                                reader->output->declarations[name] = name;
                            }
                            if (frame->last != BUSTER_CHECK_NONE)
                            {
                                members[frame->last].next = member_count;
                            }
                            else
                            {
                                frame->first = member_count;
                            }
                            frame->last = member_count++;
                            frame->count += 1;
                        }
                    }
                    more = valid && parser->tokens.kinds[i] == PARSE_KIND_COMMA;
                    if (valid)
                    {
                        reader->ignored[i++] = true;
                    }
                }
                frame->cursor = i;
            }
        }
    }
    if (!valid && reader->output->report_count == 0)
    {
        check_input_report(reader, token, CHECK_INPUT_DECLARATION);
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}

static void check_input_schedule(CheckInputReader* reader, uint32_t token, uint32_t point, TypeId type, CBindingKind kind)
{
    reader->declared_types[token] = type;
    reader->declaration_kinds[token] = (uint8_t)kind;
    reader->activation_next[token] = reader->activate[point];
    reader->activate[point] = token;
    reader->ignored[token] = true;
    reader->output->declarations[token] = token;
    reader->objects[token] = token + 1;
}

// Read a declaration's specifiers and its declarators, using parser group boundaries and subtree ends to pass
// over each initializer. Identifier uses within them are resolved later at their own source positions.
static void check_input_declaration(CheckInputReader* reader, uint32_t first)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool continuation = reader->declaration_bases[first] != 0;
    uint32_t tag = first;
    while (!continuation && tag < count && parser->tree.roles[tag] == PARSE_ROLE_SPECIFIER)
    {
        tag += 1;
    }
    uint32_t tag_body = tag + 1;
    tag_body += tag_body < count && parser->tokens.kinds[tag_body] == PARSE_KIND_IDENTIFIER;
    bool prepared = true;
    if (!continuation && tag < count && (parser->tokens.kinds[tag] == PARSE_KIND_STRUCT || parser->tokens.kinds[tag] == PARSE_KIND_ENUM) &&
        tag_body < count && parser->tokens.kinds[tag_body] == PARSE_KIND_L_BRACE)
    {
        prepared = check_input_tag_definition(reader, tag, count);
    }
    uint32_t i = first + continuation;
    bool is_typedef = (reader->declaration_flags[first] & 1) != 0;
    bool is_inline = (reader->declaration_flags[first] & 2) != 0;
    uint32_t storage = reader->declaration_storage[first];
    const TypeId base = !prepared ? 0 : continuation ? reader->declaration_bases[first] : check_input_specifiers(reader, &i, count, &is_typedef, &storage, &is_inline);
    const bool is_static = storage == C_NAME_STATIC;
    bool valid = base != 0;
    const bool tag_only = valid && i < count && parser->tokens.kinds[i] == PARSE_KIND_SEMI && (type_kind(reader->types, base) == BUSTER_TYPE_KIND_STRUCT ||
                          type_kind(reader->types, base) == BUSTER_TYPE_KIND_UNION || type_kind(reader->types, base) == BUSTER_TYPE_KIND_ENUM);
    valid &= !tag_only || (!is_typedef && storage == 0 && !is_inline);
    bool more = valid && !tag_only;
    while (more && i < count)
    {
        uint32_t name;
        uint32_t parameters = BUSTER_CHECK_NONE;
        TypeId type = check_input_declarator(reader, base, &i, count, &name, &parameters);
        valid &= type != 0 && name != BUSTER_CHECK_NONE && i < count;
        if (valid)
        {
            const uint8_t kind = parser->tokens.kinds[i];
            const bool function = type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION;
            const bool file = reader->function_result == 0;
            valid &= is_typedef || function || type_kind(reader->types, type) != BUSTER_TYPE_KIND_VOID;
            valid &= !is_inline || function;
            valid &= !file || (storage != C_NAME_REGISTER && storage != C_NAME_AUTO);
            valid &= !function || (storage != C_NAME_REGISTER && storage != C_NAME_AUTO && (file || !is_static));
            valid &= is_typedef || function || file || type_is_complete(reader->types, type);
            valid &= kind == PARSE_KIND_EQUAL || kind == PARSE_KIND_COMMA || kind == PARSE_KIND_SEMI || (function && kind == PARSE_KIND_L_BRACE);
            if (!file && storage == C_NAME_EXTERN)
            {
                check_input_report(reader, first, CHECK_INPUT_UNSUPPORTED);
                valid = false;
            }
            if (valid)
            {
                check_input_schedule(reader, name, i, type, is_typedef ? C_BINDING_TYPEDEF : function ? C_BINDING_FUNCTION : C_BINDING_OBJECT);
                reader->defined[name] = kind == PARSE_KIND_EQUAL || (function && kind == PARSE_KIND_L_BRACE);
                reader->defined[name] |= !is_typedef && !function && !file && is_static;
                reader->definition_seen[name] = reader->defined[name];
                reader->output->tentative[name] = !is_typedef && !function && file && storage != C_NAME_EXTERN && kind != PARSE_KIND_EQUAL;
                reader->static_objects[name] = !is_typedef && (is_static || function || reader->function_result == 0);
                reader->registers[name] = storage == C_NAME_REGISTER;
                reader->linkage[name] = is_typedef || (!file && !function) ? 0 : is_static ? 1 : 2;
                if ((storage == C_NAME_EXTERN || (function && storage == 0)) && reader->linkage[name] != 0)
                {
                    const uint32_t previous = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[name]);
                    if (previous != 0)
                    {
                        const uint32_t declaration = reader->bindings.declaration[previous - 1];
                        reader->linkage[name] = reader->linkage[declaration] != 0 ? reader->linkage[declaration] : reader->linkage[name];
                    }
                }
                if (function && kind == PARSE_KIND_L_BRACE)
                {
                    reader->parameters[i] = parameters;
                    reader->query_types[i] = type_return(reader->types, type);
                    valid &= reader->query_types[i] == BUSTER_TYPE_VOID || type_is_complete(reader->types, reader->query_types[i]);
                    for (uint32_t parameter = parameters; parameter != BUSTER_CHECK_NONE; parameter = reader->parameter_next[parameter])
                    {
                        valid &= type_is_complete(reader->types, reader->declared_types[parameter]);
                    }
                }
                if (kind == PARSE_KIND_EQUAL)
                {
                    valid &= !is_typedef && !function;
                    reader->initializers[i] = true;
                    reader->initializer_declarations[i] = name;
                    reader->query_types[i] = type;
                    reader->static_initializers[i] = is_static || reader->function_result == 0;
                    uint32_t right = reader->first_child[i];
                    while (right != BUSTER_CHECK_NONE && right < i)
                    {
                        right = reader->next_child[right];
                    }
                    valid &= right != BUSTER_CHECK_NONE;
                    i = valid ? reader->ends[right] + 1 : i;
                }
                more = valid && i < count && parser->tokens.kinds[i] == PARSE_KIND_COMMA;
                if (more)
                {
                    // A later declarator sees earlier names and initializer uses in their exact source order.
                    // In particular, its prototype events must not precede the preceding declarator's activation.
                    reader->ignored[i] = true;
                    reader->declaration_bases[i] = base;
                    reader->declaration_storage[i] = storage;
                    reader->declaration_flags[i] = is_typedef | (is_inline << 1);
                    more = false;
                }
            }
        }
        if (!valid)
        {
            more = false;
        }
    }
    if (!valid && reader->output->report_count == 0)
    {
        check_input_report(reader, i < count ? i : first, CHECK_INPUT_DECLARATION);
    }
}

static void check_input_bind(CheckInputReader* reader, uint32_t token, uint32_t point)
{
    const uint32_t name = reader->parser->tokens.names[token];
    const uint32_t old = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name);
    const CBindingKind kind = (CBindingKind)reader->declaration_kinds[token];
    if (old != 0 && reader->bindings.depth[old - 1] == reader->scope_count)
    {
        const uint32_t previous = reader->bindings.declaration[old - 1];
        const TypeAnswer compatible = type_compatible(reader->types, reader->declared_types[previous], reader->declared_types[token]);
        const bool repeatable = kind == C_BINDING_TYPEDEF || kind == C_BINDING_FUNCTION || reader->scope_count == 0;
        bool parameter_conflict = false;
        if (kind == C_BINDING_FUNCTION && reader->bindings.kind[old - 1] == kind && reader->defined[token])
        {
            uint32_t previous_count = 0;
            type_parameters(reader->types, reader->declared_types[previous], &previous_count);
            parameter_conflict = previous_count != 0 && (reader->types->types.flags[type_index(reader->declared_types[token])] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0;
        }
        if (!repeatable || compatible != TYPE_ANSWER_YES || reader->bindings.kind[old - 1] != kind || reader->linkage[token] != reader->linkage[previous] || parameter_conflict)
        {
            check_input_report(reader, token, CHECK_INPUT_REDECLARATION);
        }
        else
        {
            if (reader->defined[token] && reader->definition_seen[previous])
            {
                check_input_report(reader, token, CHECK_INPUT_REDECLARATION);
            }
            reader->definition_seen[token] |= reader->definition_seen[previous];
            reader->objects[token] = reader->objects[previous];
            const TypeId composite = check_input_type_result(reader, type_composite(reader->types, reader->declared_types[previous], reader->declared_types[token]));
            if (composite == 0)
            {
                check_input_report(reader, token, CHECK_INPUT_DECLARATION);
            }
            else
            {
                reader->declared_types[token] = composite;
            }
        }
    }
    if (c_bindings_declare(&reader->bindings, C_BINDING_ORDINARY, name, kind, token, point, reader->scope_count) == 0)
    {
        reader->output->resource = true;
    }
}

static void check_input_resolve(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    for (uint32_t i = 0; i <= parser->tokens.count && reader->output->report_count == 0 && !reader->output->resource; i += 1)
    {
        while (reader->scope_count != 0 && reader->scope_ends[reader->scope_count - 1] < i)
        {
            reader->scope_count -= 1;
            if (!c_bindings_restore(&reader->bindings, reader->scope_marks[reader->scope_count], i))
            {
                reader->output->resource = true;
            }
        }
        if (i > reader->function_end)
        {
            reader->function_result = 0;
        }
        if (i == parser->tokens.count)
        {
            break;
        }
        const uint8_t role = parser->tree.roles[i];
        const uint8_t kind = parser->tokens.kinds[i];
        for (uint32_t token = reader->activate[i]; token != BUSTER_CHECK_NONE; token = reader->activation_next[token])
        {
            check_input_bind(reader, token, i);
        }
        if ((!reader->ignored[i] && (role == PARSE_ROLE_SPECIFIER || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM)) || reader->declaration_bases[i] != 0)
        {
            check_input_declaration(reader, i);
        }
        if (role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_OPEN_FOR || role == PARSE_ROLE_OPEN_CONDITION)
        {
            const uint32_t parent = parser->tree.parents[i];
            reader->scope_marks[reader->scope_count] = c_bindings_mark(&reader->bindings);
            reader->scope_ends[reader->scope_count] = parent < parser->tokens.count ? reader->ends[parent] : parser->tokens.count;
            reader->scope_count += 1;
            if (role == PARSE_ROLE_OPEN_BODY)
            {
                reader->function_result = reader->query_types[i];
                reader->function_end = reader->scope_ends[reader->scope_count - 1];
                for (uint32_t p = reader->parameters[i]; p != BUSTER_CHECK_NONE; p = reader->parameter_next[p])
                {
                    check_input_bind(reader, p, i);
                }
            }
        }
        if (role == PARSE_ROLE_RETURN)
        {
            if (kind == PARSE_KIND_RETURN)
            {
                reader->return_types[i] = reader->function_result;
            }
            else if (kind == PARSE_KIND_BREAK || kind == PARSE_KIND_CONTINUE)
            {
                bool loop = false;
                for (uint32_t ancestor = parser->tree.parents[i]; ancestor < parser->tokens.count && !loop; ancestor = parser->tree.parents[ancestor])
                {
                    const uint8_t ancestor_role = parser->tree.roles[ancestor];
                    const uint32_t keyword = reader->first_child[ancestor];
                    loop = ancestor_role == PARSE_ROLE_CLOSE_FOR || ancestor_role == PARSE_ROLE_DO ||
                           (ancestor_role == PARSE_ROLE_CLOSE_CONDITION && keyword < parser->tokens.count && parser->tokens.kinds[keyword] == PARSE_KIND_WHILE);
                }
                if (!loop)
                {
                    check_input_report(reader, i, CHECK_INPUT_DECLARATION);
                }
                reader->ignored[i] = true;
            }
        }
        if (role == PARSE_ROLE_OPEN_FOR)
        {
            const uint32_t close = parser->tree.parents[i];
            uint32_t separators = 0;
            for (uint32_t child = reader->first_child[close]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                if (child > i && child < close && parser->tokens.kinds[child] == PARSE_KIND_SEMI)
                {
                    separators += 1;
                    reader->conditions[child] = separators == 2;
                }
            }
        }
        if (!reader->ignored[i] && (role == PARSE_ROLE_OPEN_CAST || role == PARSE_ROLE_OPEN_SIZEOF))
        {
            const uint32_t close = parser->tree.parents[i];
            const uint32_t next = i + 1;
            if (close < parser->tokens.count && next < close && (parser->tree.roles[next] == PARSE_ROLE_SPECIFIER ||
                parser->tokens.kinds[next] == PARSE_KIND_STRUCT || parser->tokens.kinds[next] == PARSE_KIND_ENUM))
            {
                uint32_t cursor = next;
                bool is_typedef;
                bool is_inline;
                uint32_t storage;
                TypeId type = check_input_specifiers(reader, &cursor, close, &is_typedef, &storage, &is_inline);
                uint32_t name;
                uint32_t parameters;
                type = check_input_declarator(reader, type, &cursor, close, &name, &parameters);
                if (type == 0 || cursor != close || name != BUSTER_CHECK_NONE || is_typedef || storage != 0 || is_inline)
                {
                    check_input_report(reader, i, CHECK_INPUT_DECLARATION);
                }
                reader->query_types[close] = type;
                if (role == PARSE_ROLE_OPEN_SIZEOF && type != 0 && !type_is_complete(reader->types, type))
                {
                    check_input_report(reader, i, CHECK_INPUT_DECLARATION);
                }
            }
        }
        if (!reader->ignored[i] && kind == PARSE_KIND_IDENTIFIER && role != PARSE_ROLE_MEMBER && role != PARSE_ROLE_TAG && role != PARSE_ROLE_LEAF_RIGHT)
        {
            if (c_bindings_use(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[i], i, i) == 0)
            {
                reader->output->resource = true;
            }
        }
    }
}

static uint32_t check_input_add(CheckInputReader* reader, CheckOp op, uint32_t token, uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t node = check_graph_add(&reader->output->graph, op, token, a, b, c);
    if (node == BUSTER_CHECK_NONE)
    {
        reader->output->resource = true;
    }
    else
    {
        reader->value_next[node] = BUSTER_CHECK_NONE;
    }
    return node;
}

static CheckOp check_input_binary(uint8_t kind)
{
    CheckOp result = CHECK_OP_COUNT;
    switch (kind)
    {
        case PARSE_KIND_PLUS: result = CHECK_OP_ADD; break;
        case PARSE_KIND_MINUS: result = CHECK_OP_SUBTRACT; break;
        case PARSE_KIND_STAR: result = CHECK_OP_MULTIPLY; break;
        case PARSE_KIND_SLASH: result = CHECK_OP_DIVIDE; break;
        case PARSE_KIND_PERCENT: result = CHECK_OP_REMAINDER; break;
        case PARSE_KIND_LESSLESS: result = CHECK_OP_SHIFT_LEFT; break;
        case PARSE_KIND_GREATERGREATER: result = CHECK_OP_SHIFT_RIGHT; break;
        case PARSE_KIND_LESS: result = CHECK_OP_LESS; break;
        case PARSE_KIND_LESSEQUAL: result = CHECK_OP_LESS_EQUAL; break;
        case PARSE_KIND_GREATER: result = CHECK_OP_GREATER; break;
        case PARSE_KIND_GREATEREQUAL: result = CHECK_OP_GREATER_EQUAL; break;
        case PARSE_KIND_EQUALEQUAL: result = CHECK_OP_EQUAL; break;
        case PARSE_KIND_EXCLAIMEQUAL: result = CHECK_OP_NOT_EQUAL; break;
        case PARSE_KIND_AMP: result = CHECK_OP_BIT_AND; break;
        case PARSE_KIND_PIPE: result = CHECK_OP_BIT_OR; break;
        case PARSE_KIND_CARET: result = CHECK_OP_BIT_XOR; break;
        case PARSE_KIND_AMPAMP: result = CHECK_OP_LOGICAL_AND; break;
        case PARSE_KIND_PIPEPIPE: result = CHECK_OP_LOGICAL_OR; break;
        case PARSE_KIND_EQUAL: result = CHECK_OP_ASSIGN; break;
        case PARSE_KIND_PLUSEQUAL: result = CHECK_OP_ADD_ASSIGN; break;
        case PARSE_KIND_MINUSEQUAL: result = CHECK_OP_SUBTRACT_ASSIGN; break;
        case PARSE_KIND_STAREQUAL: result = CHECK_OP_MULTIPLY_ASSIGN; break;
        case PARSE_KIND_SLASHEQUAL: result = CHECK_OP_DIVIDE_ASSIGN; break;
        case PARSE_KIND_PERCENTEQUAL: result = CHECK_OP_REMAINDER_ASSIGN; break;
        case PARSE_KIND_LESSLESSEQUAL: result = CHECK_OP_SHIFT_LEFT_ASSIGN; break;
        case PARSE_KIND_GREATERGREATEREQUAL: result = CHECK_OP_SHIFT_RIGHT_ASSIGN; break;
        case PARSE_KIND_AMPEQUAL: result = CHECK_OP_BIT_AND_ASSIGN; break;
        case PARSE_KIND_PIPEEQUAL: result = CHECK_OP_BIT_OR_ASSIGN; break;
        case PARSE_KIND_CARETEQUAL: result = CHECK_OP_BIT_XOR_ASSIGN; break;
        case PARSE_KIND_COMMA: result = CHECK_OP_COMMA; break;
        default: break;
    }
    return result;
}

static void check_input_expressions(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    CheckGraph* graph = &reader->output->graph;
    for (uint32_t order = 0; order < parser->tokens.count && !reader->output->resource; order += 1)
    {
        const uint32_t token = parser->postorder[order];
        const uint8_t role = parser->tree.roles[token];
        const uint8_t kind = parser->tokens.kinds[token];
        uint32_t first = BUSTER_CHECK_NONE;
        uint32_t last = BUSTER_CHECK_NONE;
        uint32_t count = 0;
        for (uint32_t child = reader->first_child[token]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
        {
            const uint32_t begin = reader->first_value[child];
            if (begin != BUSTER_CHECK_NONE)
            {
                if (last != BUSTER_CHECK_NONE)
                {
                    reader->value_next[last] = begin;
                }
                else
                {
                    first = begin;
                }
                last = reader->last_value[child];
                for (uint32_t n = begin; n != BUSTER_CHECK_NONE; n = reader->value_next[n])
                {
                    count += 1;
                }
            }
        }
        const uint32_t a = first;
        const uint32_t b = a != BUSTER_CHECK_NONE ? reader->value_next[a] : BUSTER_CHECK_NONE;
        const uint32_t c = b != BUSTER_CHECK_NONE ? reader->value_next[b] : BUSTER_CHECK_NONE;
        uint32_t node = BUSTER_CHECK_NONE;
        bool list = false;
        bool supported = true;
        CheckOp op = CHECK_OP_COUNT;
        if (reader->ignored[token])
        {
            // Specifier chains carry initializer nodes to their enclosing statement without creating expressions.
            list = role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_COMMA || role == PARSE_ROLE_SEPARATOR;
        }
        else if (role == PARSE_ROLE_OPERAND && kind == PARSE_KIND_IDENTIFIER)
        {
            if (reader->declared_types[token] != 0)
            {
                node = check_input_add(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                if (node != BUSTER_CHECK_NONE)
                {
                    const TypeId type = reader->declared_types[token];
                    const uint32_t declaration = reader->output->declarations[token];
                    const bool enumeration = reader->declaration_kinds[declaration] == C_BINDING_ENUMERATOR;
                    graph->leaves[node] = (CheckValue)
                    {
                        .type = type,
                        .category = enumeration ? CHECK_RVALUE : type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE,
                        .flags = (enumeration ? BUSTER_CHECK_ICE : 0) | (reader->static_objects[declaration] ? BUSTER_CHECK_STATIC_OBJECT : 0) | (reader->registers[declaration] ? BUSTER_CHECK_REGISTER : 0),
                        .constant_kind = enumeration ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_ADDRESS,
                        .integer = enumeration ? (uint32_t)reader->enum_values[declaration] : 0,
                        .object = reader->objects[declaration],
                    };
                }
            }
        }
        else if (role == PARSE_ROLE_OPERAND && (kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER))
        {
            const CheckLiteral literal = check_input_literal(reader, token);
            if (!literal.valid)
            {
                check_input_report(reader, token, CHECK_INPUT_LITERAL);
            }
            else
            {
                node = check_input_add(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                if (node != BUSTER_CHECK_NONE)
                {
                    graph->leaves[node] = (CheckValue)
                    {
                        .type = literal.type,
                        .category = CHECK_RVALUE,
                        .flags = literal.real ? BUSTER_CHECK_FLOAT_LITERAL : literal.type == BUSTER_TYPE_NULLPTR ? 0 : BUSTER_CHECK_ICE,
                        .constant_kind = literal.real ? CHECK_CONSTANT_FLOATING : literal.type == BUSTER_TYPE_NULLPTR ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_INTEGER,
                        .integer = literal.integer,
                        .floating = literal.floating,
                    };
                }
            }
        }
        else if (role == PARSE_ROLE_PREFIX)
        {
            supported = count == 1;
            switch (kind)
            {
                case PARSE_KIND_PLUS: op = CHECK_OP_POSITIVE; break;
                case PARSE_KIND_MINUS: op = CHECK_OP_NEGATIVE; break;
                case PARSE_KIND_EXCLAIM: op = CHECK_OP_NOT; break;
                case PARSE_KIND_TILDE: op = CHECK_OP_BIT_NOT; break;
                case PARSE_KIND_AMP: op = CHECK_OP_ADDRESS; break;
                case PARSE_KIND_STAR: op = CHECK_OP_DEREFERENCE; break;
                case PARSE_KIND_PLUSPLUS: op = CHECK_OP_PRE_INCREMENT; break;
                case PARSE_KIND_MINUSMINUS: op = CHECK_OP_PRE_DECREMENT; break;
                case PARSE_KIND_SIZEOF:
                    op = parser->tokens.names[token] == C_NAME_SIZEOF ? CHECK_OP_SIZEOF : CHECK_OP_ALIGNOF;
                    supported = count == 1 || reader->query_types[token] != 0;
                    break;
                default: supported = false; break;
            }
        }
        else if ((role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_ASSIGN) || role == PARSE_ROLE_COMMA || (role == PARSE_ROLE_SEPARATOR && reader->expression_commas[token]))
        {
            op = check_input_binary(kind);
            supported = count == 2 && op != CHECK_OP_COUNT;
            if (reader->initializers[token])
            {
                op = CHECK_OP_INITIALIZE;
                supported = count == 1;
            }
        }
        else if (role == PARSE_ROLE_CLOSE_GROUP)
        {
            supported = count == 1;
            list = true;
        }
        else if (role == PARSE_ROLE_CLOSE_CAST)
        {
            supported = count == 1 && reader->query_types[token] != 0;
            op = CHECK_OP_CAST;
        }
        else if (role == PARSE_ROLE_CLOSE_SIZEOF)
        {
            list = count == 1;
            const uint32_t parent = parser->tree.parents[token];
            if (reader->query_types[token] != 0 && parent < parser->tokens.count)
            {
                reader->query_types[parent] = reader->query_types[token];
            }
        }
        else if (role == PARSE_ROLE_OPEN_TERNARY || role == PARSE_ROLE_SEPARATOR)
        {
            list = true;
        }
        else if (role == PARSE_ROLE_CLOSE_TERNARY)
        {
            supported = count == 3;
            op = CHECK_OP_CONDITIONAL;
        }
        else if (role == PARSE_ROLE_CLOSE_SUBSCRIPT)
        {
            supported = count == 2;
            op = CHECK_OP_SUBSCRIPT;
        }
        else if (role == PARSE_ROLE_MEMBER_OPERATOR)
        {
            supported = count == 1 && token + 1 < parser->tokens.count && parser->tree.roles[token + 1] == PARSE_ROLE_MEMBER;
            op = kind == PARSE_KIND_ARROW ? CHECK_OP_ARROW : CHECK_OP_MEMBER;
        }
        else if (role == PARSE_ROLE_POSTFIX)
        {
            supported = count == 1 && (kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS);
            op = kind == PARSE_KIND_PLUSPLUS ? CHECK_OP_POST_INCREMENT : CHECK_OP_POST_DECREMENT;
        }
        else if (role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_FUNCTION)
        {
            supported = count != 0;
            op = CHECK_OP_CALL;
        }
        else if (role == PARSE_ROLE_RETURN)
        {
            supported = reader->return_types[token] != 0 && count <= 1;
            if (count == 1)
            {
                op = CHECK_OP_CONVERT;
                reader->query_types[token] = reader->return_types[token];
            }
            else if (reader->return_types[token] != BUSTER_TYPE_VOID)
            {
                check_input_report(reader, token, CHECK_INPUT_RETURN);
            }
        }
        else if (role == PARSE_ROLE_CLOSE_KEYWORD)
        {
            const uint32_t keyword = reader->first_child[token];
            supported = keyword < parser->tokens.count && parser->tokens.kinds[keyword] == PARSE_KIND_STATIC_ASSERT && count == 1;
            if (supported)
            {
                graph->contexts[a] |= BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT;
            }
        }
        else if (role == PARSE_ROLE_CLOSE_CONDITION)
        {
            // The condition itself is the first value; child statements have already been checked and dropped.
            supported = count == 1;
            op = CHECK_OP_CONDITION;
        }
        else if (role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_CLOSE_FOR)
        {
            // Discarding an expression still performs its lvalue/array/function conversions. In particular an
            // unused register array cannot decay, and a volatile lvalue remains an observable load.
            supported = !reader->conditions[token] || count <= 1;
            for (uint32_t value = first; value != BUSTER_CHECK_NONE; value = reader->value_next[value])
            {
                const uint32_t discard = check_input_add(reader, reader->conditions[token] ? CHECK_OP_CONDITION : CHECK_OP_CAST, token, value, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                if (discard != BUSTER_CHECK_NONE)
                {
                    graph->types[discard] = BUSTER_TYPE_VOID;
                }
            }
        }
        else if (role == PARSE_ROLE_CLOSE_INITIALIZER || role == PARSE_ROLE_CLOSE_STRUCT ||
                 role == PARSE_ROLE_CLOSE_ENUM || role == PARSE_ROLE_CLOSE_SPECIFIER || role == PARSE_ROLE_CLOSE_COMPOUND_BODY ||
                 role == PARSE_ROLE_CLOSE_ASM || role == PARSE_ROLE_CLOSE_CASE || role == PARSE_ROLE_LABEL || role == PARSE_ROLE_TAG || role == PARSE_ROLE_BITFIELD)
        {
            supported = false;
        }
        else if (kind == PARSE_KIND_STRING)
        {
            // Static assertions' messages do not contribute a value; other string objects need the string-pool adapter.
            uint32_t ancestor = parser->tree.parents[token];
            while (ancestor < parser->tokens.count && parser->tree.roles[ancestor] != PARSE_ROLE_CLOSE_KEYWORD && parser->tree.roles[ancestor] != PARSE_ROLE_TERMINATOR)
            {
                ancestor = parser->tree.parents[ancestor];
            }
            supported = ancestor < parser->tokens.count && parser->tree.roles[ancestor] == PARSE_ROLE_CLOSE_KEYWORD;
        }
        else if (kind == PARSE_KIND_GOTO || kind == PARSE_KIND_BREAK || kind == PARSE_KIND_CONTINUE || kind == PARSE_KIND_LABEL || kind == PARSE_KIND_SWITCH ||
                 kind == PARSE_KIND_CASE || kind == PARSE_KIND_DEFAULT || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ||
                 kind == PARSE_KIND_ASM || kind == PARSE_KIND_ATTRIBUTE || kind == PARSE_KIND_ALIGNAS || kind == PARSE_KIND_ATOMIC ||
                 kind == PARSE_KIND_CALLING_CONVENTION || kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_GENERIC || kind == PARSE_KIND_TYPE_ARGUMENT ||
                 kind == PARSE_KIND_REAL_IMAG || kind == PARSE_KIND_EXTENSION)
        {
            supported = false;
        }
        if (op != CHECK_OP_COUNT && supported)
        {
            const uint32_t left = op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF ? (count == 1 ? a : BUSTER_CHECK_NONE) : a;
            const bool one = op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT || op == CHECK_OP_CALL || op == CHECK_OP_CAST || op == CHECK_OP_CONDITION ||
                             op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW || role == PARSE_ROLE_PREFIX || role == PARSE_ROLE_POSTFIX;
            node = check_input_add(reader, op, token, left, one ? BUSTER_CHECK_NONE : b, op == CHECK_OP_CONDITIONAL ? c : BUSTER_CHECK_NONE);
            if (node != BUSTER_CHECK_NONE)
            {
                graph->types[node] = reader->query_types[token];
                if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
                {
                    graph->members[node] = parser->tokens.names[token + 1];
                }
                if (op == CHECK_OP_CALL)
                {
                    graph->argument_offsets[node] = graph->argument_count;
                    graph->argument_counts[node] = count - 1;
                    for (uint32_t argument = b; argument != BUSTER_CHECK_NONE; argument = reader->value_next[argument])
                    {
                        if (graph->argument_count < graph->argument_capacity)
                        {
                            graph->argument_nodes[graph->argument_count++] = argument;
                        }
                        else
                        {
                            reader->output->resource = true;
                        }
                    }
                }
                if (reader->static_initializers[token])
                {
                    graph->contexts[node] |= BUSTER_CHECK_CONTEXT_STATIC;
                }
                if (op == CHECK_OP_INITIALIZE)
                {
                    reader->output->initializer_nodes[reader->initializer_declarations[token]] = node;
                }
            }
        }
        if (!supported && reader->output->report_count == 0)
        {
            check_input_report(reader, token, CHECK_INPUT_UNSUPPORTED);
        }
        if (node != BUSTER_CHECK_NONE)
        {
            reader->first_value[token] = node;
            reader->last_value[token] = node;
        }
        else if (list)
        {
            reader->first_value[token] = first;
            reader->last_value[token] = last;
        }
    }
}

// Type tags complete in place, while graph actions run after source elaboration. Until the graph carries explicit
// completion barriers, refuse expression uses of a not-yet-complete tag (including through a pointer) rather than
// accidentally allowing a later record definition to authorize an earlier member/size/arithmetic operation.
static void check_input_complete_uses(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    for (uint32_t token = 0; token < parser->tokens.count; token += 1)
    {
        const bool use = !reader->ignored[token] && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && parser->tree.roles[token] == PARSE_ROLE_OPERAND;
        const bool cast = parser->tree.roles[token] == PARSE_ROLE_CLOSE_CAST;
        TypeId type = use ? reader->declared_types[token] : cast ? reader->query_types[token] : 0;
        while (type != 0)
        {
            type = type_strip_aligned(reader->types, type);
            const uint8_t kind = type_kind(reader->types, type);
            if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM)
            {
                const uint32_t tag = reader->types->types.index[type_index(type)];
                if (reader->tag_completions[tag] > token)
                {
                    check_input_report(reader, token, CHECK_INPUT_UNSUPPORTED);
                }
                type = 0;
            }
            else if (kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_FUNCTION)
            {
                type = reader->types->types.base[type_index(type)];
            }
            else
            {
                type = 0;
            }
        }
        if (reader->output->tentative[token] && !type_is_complete(reader->types, reader->declared_types[token]) &&
            type_kind(reader->types, reader->declared_types[token]) != BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
        {
            check_input_report(reader, token, CHECK_INPUT_DECLARATION);
        }
    }
}

#include "c_check_binding.c"

static bool check_input(CheckInput* input, const Parser* parser, Preprocessor* pp, TypeTable* types, Arena* arena, Arena* scratch)
{
    *input = (CheckInput){ 0 };
    const uint32_t count = parser->tokens.count;
    input->token_count = count;
    const uint64_t mark = scratch->length;
    CheckInputReader reader =
    {
        .output = input,
        .parser = parser,
        .pp = pp,
        .types = types,
        .arena = arena,
        .scratch = scratch,
    };
    bool valid = parser->errors == 0 && parser->fatal == PARSE_MESSAGE_NONE && (parser->postorder != 0 || count == 0) && count < UINT32_MAX / 4;
    input->report_capacity = count + 1;
    input->reports = valid ? arena_push_array(arena, input->report_capacity, sizeof(CheckInputReport), 4) : 0;
    input->declarations = input->reports != 0 ? arena_push_array(arena, (uint64_t)count + 1, sizeof(uint32_t), 4) : 0;
    input->declared_types = input->declarations != 0 ? arena_push_array(arena, (uint64_t)count + 1, sizeof(TypeId), 4) : 0;
    input->declaration_kinds = input->declared_types != 0 ? arena_push_array(arena, (uint64_t)count + 1, sizeof(uint8_t), 1) : 0;
    input->objects = input->declaration_kinds != 0 ? arena_push_array(arena, (uint64_t)count + 1, sizeof(uint32_t), 4) : 0;
    valid &= input->objects != 0;
#define BUSTER_CHECK_OUTPUT_ARRAY(field, type) input->field = valid ? arena_push_array(arena, (uint64_t)count + 1, sizeof(type), 8) : 0; valid &= input->field != 0
    BUSTER_CHECK_OUTPUT_ARRAY(static_objects, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(registers, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(linkage, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(defined, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(tentative, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(initializer_nodes, uint32_t);
#undef BUSTER_CHECK_OUTPUT_ARRAY
    reader.declared_types = input->declared_types;
    reader.declaration_kinds = input->declaration_kinds;
    reader.objects = input->objects;
    reader.static_objects = input->static_objects;
    reader.registers = input->registers;
    reader.linkage = input->linkage;
    reader.defined = input->defined;
    valid &= check_graph_initialize(&input->graph, arena, count + 1, count + 1);
#define BUSTER_CHECK_INPUT_ARRAY(field, type) reader.field = valid ? arena_push_array(scratch, (uint64_t)count + 1, sizeof(type), 64) : 0; valid &= reader.field != 0
    BUSTER_CHECK_INPUT_ARRAY(query_types, TypeId);
    BUSTER_CHECK_INPUT_ARRAY(return_types, TypeId);
    BUSTER_CHECK_INPUT_ARRAY(definition_seen, uint8_t);
    BUSTER_CHECK_INPUT_ARRAY(ignored, uint8_t);
    BUSTER_CHECK_INPUT_ARRAY(initializers, uint8_t);
    BUSTER_CHECK_INPUT_ARRAY(static_initializers, uint8_t);
    BUSTER_CHECK_INPUT_ARRAY(initializer_declarations, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(expression_commas, uint8_t);
    BUSTER_CHECK_INPUT_ARRAY(conditions, uint8_t);
    BUSTER_CHECK_INPUT_ARRAY(declaration_bases, TypeId);
    BUSTER_CHECK_INPUT_ARRAY(declaration_storage, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(declaration_flags, uint8_t);
    BUSTER_CHECK_INPUT_ARRAY(activate, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(activation_next, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(parameters, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(parameter_next, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(parameter_previous, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(tag_types, TypeId);
    BUSTER_CHECK_INPUT_ARRAY(tag_ends, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(enum_values, int64_t);
    BUSTER_CHECK_INPUT_ARRAY(postorder_positions, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(ends, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(first_child, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(next_child, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(first_value, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(last_value, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(value_next, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(scope_marks, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(scope_ends, uint32_t);
#undef BUSTER_CHECK_INPUT_ARRAY
    reader.parameter_seen = valid ? arena_push_array(scratch, pp->table->count, sizeof(uint32_t), 64) : 0;
    valid &= reader.parameter_seen != 0;
    reader.member_seen = valid ? arena_push_array(scratch, pp->table->count, sizeof(uint32_t), 64) : 0;
    valid &= reader.member_seen != 0;
    reader.tag_completions = valid ? arena_push_array(scratch, types->tags.capacity, sizeof(uint32_t), 4) : 0;
    valid &= reader.tag_completions != 0;
    valid &= c_bindings_initialize(&reader.bindings, scratch, pp->table->count, count * 4 + 1);
    if (valid)
    {
        memset(reader.declared_types, 0, ((uint64_t)count + 1) * sizeof(TypeId));
        memset(reader.query_types, 0, ((uint64_t)count + 1) * sizeof(TypeId));
        memset(reader.return_types, 0, ((uint64_t)count + 1) * sizeof(TypeId));
        memset(reader.declaration_kinds, 0, (uint64_t)count + 1);
        memset(reader.declaration_bases, 0, ((uint64_t)count + 1) * sizeof(TypeId));
        memset(reader.declaration_storage, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(reader.declaration_flags, 0, (uint64_t)count + 1);
        memset(reader.conditions, 0, (uint64_t)count + 1);
        memset(reader.ignored, 0, (uint64_t)count + 1);
        memset(reader.defined, 0, (uint64_t)count + 1);
        memset(reader.definition_seen, 0, (uint64_t)count + 1);
        memset(input->tentative, 0, (uint64_t)count + 1);
        memset(reader.static_objects, 0, (uint64_t)count + 1);
        memset(reader.registers, 0, (uint64_t)count + 1);
        memset(reader.linkage, 0, (uint64_t)count + 1);
        memset(reader.objects, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(reader.parameter_seen, 0, (uint64_t)pp->table->count * sizeof(uint32_t));
        memset(reader.member_seen, 0, (uint64_t)pp->table->count * sizeof(uint32_t));
        memset(reader.tag_types, 0, ((uint64_t)count + 1) * sizeof(TypeId));
        memset(reader.tag_completions, 0, (uint64_t)types->tags.capacity * sizeof(uint32_t));
        memset(reader.initializers, 0, (uint64_t)count + 1);
        memset(reader.static_initializers, 0, (uint64_t)count + 1);
        for (uint32_t i = 0; i <= count; i += 1)
        {
            input->declarations[i] = BUSTER_CHECK_NONE;
            input->initializer_nodes[i] = BUSTER_CHECK_NONE;
            reader.activate[i] = BUSTER_CHECK_NONE;
            reader.parameters[i] = BUSTER_CHECK_NONE;
            reader.first_child[i] = BUSTER_CHECK_NONE;
            reader.next_child[i] = BUSTER_CHECK_NONE;
            reader.first_value[i] = BUSTER_CHECK_NONE;
            reader.last_value[i] = BUSTER_CHECK_NONE;
            reader.ends[i] = i;
        }
        for (uint32_t i = count; i-- > 0;)
        {
            const uint32_t parent = parser->tree.parents[i];
            if (parent < count)
            {
                reader.next_child[i] = reader.first_child[parent];
                reader.first_child[parent] = i;
            }
        }
        for (uint32_t k = 0; k < count; k += 1)
        {
            const uint32_t token = parser->postorder[k];
            reader.postorder_positions[token] = k;
            const uint32_t parent = parser->tree.parents[token];
            if (parent < count && reader.ends[parent] < reader.ends[token])
            {
                reader.ends[parent] = reader.ends[token];
            }
        }
        // The parser uses SEPARATOR for commas inside any parentheses. The enclosing delimiter distinguishes
        // expression commas from call/declaration separators; reverse postorder passes that fact down once.
        for (uint32_t k = count; k-- > 0;)
        {
            const uint32_t token = parser->postorder[k];
            const uint32_t parent = parser->tree.parents[token];
            const uint8_t role = parser->tree.roles[token];
            reader.expression_commas[token] = role == PARSE_ROLE_SEPARATOR && parent < count ? reader.expression_commas[parent] :
                                              role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_SIZEOF || role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_SUBSCRIPT ||
                                              role == PARSE_ROLE_CLOSE_FOR || (role == PARSE_ROLE_TERMINATOR && parent < count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_FOR);
        }
        if (!check_input_target_supported(&pp->options))
        {
            check_input_report(&reader, 0, CHECK_INPUT_TARGET);
        }
        else if (parser->tokens.pragma_count != 0)
        {
            // In particular, silently ignoring a retained #pragma pack would give records the wrong layout.
            check_input_report(&reader, parser->tokens.pragma_positions[0], CHECK_INPUT_UNSUPPORTED);
        }
        else
        {
            check_input_resolve(&reader);
        }
        if (!input->resource && reader.bindings.valid && check_input_bindings(&reader, parser->options.scalar))
        {
            check_input_complete_uses(&reader);
            if (input->report_count == 0)
            {
                check_input_expressions(&reader);
            }
        }
        valid = !input->resource && input->report_count == 0 && reader.bindings.valid;
    }
    else
    {
        input->resource = true;
    }
    arena_pop_to(scratch, mark);
    return valid;
}
