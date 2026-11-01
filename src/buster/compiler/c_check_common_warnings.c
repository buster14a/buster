// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Clang's common semantic warnings that need the completed expression graph or the parser's operator tree, reported
// after checking and before publication: constant conversions and bounds, parenthesization, unused results and names,
// format calls and returned stack addresses (#823). Severity comes from the shared sparse policy, so command-line
// groups, source diagnostic pragmas and -Werror behave as the checker's other warnings do. Included by the driver
// after c_check_warning_render.c.

typedef struct CheckCommonNote CheckCommonNote;
struct CheckCommonNote
{
    uint32_t token;
    const char* message;
};

static CheckWarningSeverity check_common_severity(Preprocessor* pp, const Parser* parser, const CheckInput* input, uint32_t token, uint32_t id, bool default_ignored)
{
    const CheckWarningCommandLine command = check_warning_command_line(&pp->options, id, default_ignored);
    CheckWarningSeverity severity = command.quiet ? CHECK_WARNING_IGNORE :
        check_diagnostic_severity_id_from(input, token, BUSTER_CHECK_NONE, id, command.severity);
    bool system = check_graph_system_token(&input->graph, token);
    if (!system && pp->output.locations != NULL && parser != NULL && token < parser->tokens.count && parser->tokens.sources[token] < pp->output.count)
    {
        system = pp_system_level_at(pp, pp->output.locations[parser->tokens.sources[token]]) != BUSTER_PP_USER_FILE;
    }
    if (severity != CHECK_WARNING_IGNORE && !command.system_headers && system)
    {
        severity = CHECK_WARNING_IGNORE;
    }
    return severity;
}

// A warning at a parser token, with its optional notes. The suffix is clang's: -Werror's promotion names the group.
static void check_common_report_at(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t publication,
                                   uint32_t id, bool default_ignored, const char* message, const CheckCommonNote* notes, uint32_t note_count, Arena* scratch)
{
    if (token >= parser->tokens.count || pp->fatal)
    {
        return;
    }
    const CheckWarningSeverity severity = check_common_severity(pp, parser, input, token, id, default_ignored);
    if (severity != CHECK_WARNING_IGNORE)
    {
        const char* option = id < BUSTER_ARRAY_LENGTH(check_warning_descriptions) ? check_warning_descriptions[id].option : NULL;
        const bool error = severity >= CHECK_WARNING_ERROR;
        const char* suffix = option != NULL ? check_warning_text(scratch, " [%s%s]", error ? "-Werror," : "", option) : error ? " [-Werror]" : "";
        const PPSeverity disposition = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                       error ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
        PPOutputDiagnosticNote attached[8];
        const uint32_t used = note_count < BUSTER_ARRAY_LENGTH(attached) ? note_count : (uint32_t)BUSTER_ARRAY_LENGTH(attached);
        for (uint32_t i = 0; i < used; i += 1)
        {
            attached[i] = (PPOutputDiagnosticNote)
            {
                .token = notes[i].token < parser->tokens.count ? parser->tokens.sources[notes[i].token] : parser->tokens.sources[token],
                .message = notes[i].message,
            };
        }
        if (suffix != NULL)
        {
            pp_diagnose_output_notes_at(pp, disposition, parser->tokens.sources[token], parser->tokens.sources[publication],
                                        used != 0 ? attached : NULL, used, "%s%s", message, suffix);
        }
    }
}

static void check_common_report(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t id,
                                bool default_ignored, const char* message, const CheckCommonNote* notes, uint32_t note_count, Arena* scratch)
{
    check_common_report_at(parser, pp, input, token, token, id, default_ignored, message, notes, note_count, scratch);
}

// A type's spelling with clang's decayed type for a format argument: an array prints as a pointer to its element.
static String8 check_common_type_text(const Preprocessor* pp, TypeTable* types, Arena* scratch, TypeId type)
{
    String8 result = check_driver_warning_type(pp, types, scratch, type);
    const TypeId stripped = type != 0 ? type_strip_aligned(types, type) : 0;
    if (stripped != 0 && type_kind_is_array(type_kind(types, type_unqualified(stripped))) && result.length != 0)
    {
        const String8 element = check_driver_warning_type(pp, types, scratch, type_element(types, type_unqualified(stripped)));
        if (element.length != 0)
        {
            result = (String8){ .pointer = (char*)check_warning_text(scratch, "%.*s *", (int)element.length, element.pointer), .length = element.length + 2 };
        }
    }
    return result;
}

// A 128-bit decimal spelling, the widest integer value a constant conversion can name.
static void check_common_number(char* buffer, uint64_t capacity, __int128 value)
{
    const bool negative = value < 0;
    unsigned __int128 magnitude = negative ? (unsigned __int128)(0 - (unsigned __int128)value) : (unsigned __int128)value;
    char digits[48];
    uint32_t count = 0;
    do
    {
        digits[count++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    }
    while (magnitude != 0 && count < sizeof(digits));
    uint32_t at = 0;
    if (negative && at + 1 < capacity)
    {
        buffer[at++] = '-';
    }
    while (count != 0 && at + 1 < capacity)
    {
        buffer[at++] = digits[--count];
    }
    buffer[at < capacity ? at : capacity - 1] = 0;
}

static __int128 check_common_integer_value(const CheckValue* value, uint32_t width, bool is_signed)
{
    const unsigned __int128 mask = check_action_mask(width);
    const unsigned __int128 bits = value->integer & mask;
    const bool negative = is_signed && width != 0 && ((bits >> (width - 1)) & 1) != 0;
    __int128 result = negative ? (__int128)(bits | ~mask) : (__int128)bits;
    return result;
}

static bool check_common_simple_operand(CheckOp op)
{
    const bool result = op == CHECK_OP_VALUE || op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW || op == CHECK_OP_DEREFERENCE || op == CHECK_OP_CONVERT ||
                        op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF;
    return result;
}

static bool check_common_write_op(CheckOp op)
{
    const bool result = op == CHECK_OP_ASSIGN || op == CHECK_OP_ADD_ASSIGN || op == CHECK_OP_SUBTRACT_ASSIGN || op == CHECK_OP_MULTIPLY_ASSIGN ||
                        op == CHECK_OP_DIVIDE_ASSIGN || op == CHECK_OP_REMAINDER_ASSIGN || op == CHECK_OP_SHIFT_LEFT_ASSIGN ||
                        op == CHECK_OP_SHIFT_RIGHT_ASSIGN || op == CHECK_OP_BIT_AND_ASSIGN || op == CHECK_OP_BIT_OR_ASSIGN ||
                        op == CHECK_OP_BIT_XOR_ASSIGN || op == CHECK_OP_PRE_INCREMENT || op == CHECK_OP_PRE_DECREMENT ||
                        op == CHECK_OP_POST_INCREMENT || op == CHECK_OP_POST_DECREMENT;
    return result;
}

static bool check_common_integer(const TypeTable* types, TypeId type, uint32_t* width, bool* is_signed)
{
    const bool result = check_action_integer_type(types, type, width, is_signed);
    return result;
}

// Whether a constant integer changes value in an implicit conversion to `target`. Unsigned sources warn only when
// their high bits are lost; a signed source warns unless the target represents its value, with clang's -1 exception
// for an unsigned target.
static bool check_common_conversion_changes(const TypeTable* types, TypeId source, TypeId target, const CheckValue* value, __int128* converted)
{
    uint32_t source_width = 0;
    uint32_t target_width = 0;
    bool source_signed = false;
    bool target_signed = false;
    bool changes = false;
    if (type_unqualified(type_strip_aligned(types, target)) == BUSTER_TYPE_BOOL)
    {
        return changes;
    }
    if (check_common_integer(types, source, &source_width, &source_signed) && check_common_integer(types, target, &target_width, &target_signed))
    {
        const __int128 original = check_common_integer_value(value, source_width, source_signed);
        const unsigned __int128 target_mask = check_action_mask(target_width);
        const unsigned __int128 bits = (unsigned __int128)original & target_mask;
        const __int128 target_value = target_signed && target_width != 0 && ((bits >> (target_width - 1)) & 1) != 0 ?
            (__int128)(bits | ~target_mask) : (__int128)bits;
        *converted = target_value;
        if (!source_signed)
        {
            changes = (unsigned __int128)original != bits;
        }
        else if (!target_signed && original == -1)
        {
            changes = false;
        }
        else
        {
            changes = target_width < 128 && source_width < 128 ? original != target_value : (unsigned __int128)original != bits;
        }
    }
    return changes;
}

static bool check_common_integer_constant(const CheckStream* stream, uint32_t node)
{
    const bool result = node < stream->count && stream->values[node].constant_kind == CHECK_CONSTANT_INTEGER;
    return result;
}

// The first token of the declaration whose name is `token`, for clang's "declared here" notes: the declaration begins
// after the previous statement or block delimiter.
static uint32_t check_common_declaration_start(const Parser* parser, uint32_t token)
{
    uint32_t at = token;
    uint32_t steps = 0;
    while (at != 0 && steps++ < parser->tokens.count)
    {
        const uint8_t role = parser->tree.roles[at - 1];
        const uint8_t kind = parser->tokens.kinds[at - 1];
        if (role == PARSE_ROLE_TERMINATOR || kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_R_BRACE || kind == PARSE_KIND_COMMA || kind == PARSE_KIND_L_PAREN)
        {
            break;
        }
        at -= 1;
    }
    return at;
}

static const char* check_common_operator_spelling(const Parser* parser, uint32_t token)
{
    const char* result = "?";
    switch (parser->tokens.kinds[token])
    {
        case PARSE_KIND_AMP: result = "&"; break;
        case PARSE_KIND_PIPE: result = "|"; break;
        case PARSE_KIND_CARET: result = "^"; break;
        case PARSE_KIND_EQUALEQUAL: result = "=="; break;
        case PARSE_KIND_EXCLAIMEQUAL: result = "!="; break;
        case PARSE_KIND_LESS: result = "<"; break;
        case PARSE_KIND_LESSEQUAL: result = "<="; break;
        case PARSE_KIND_GREATER: result = ">"; break;
        case PARSE_KIND_GREATEREQUAL: result = ">="; break;
        case PARSE_KIND_EQUAL: result = "="; break;
        default: break;
    }
    return result;
}

static bool check_common_comparison_role(uint8_t role)
{
    const bool result = role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_RELATIONAL;
    return result;
}

// An expression statement's terminator belongs to a block, body, label or case; a for header's semicolon doesn't.
static bool check_common_statement_terminator(const Parser* parser, uint32_t token)
{
    bool result = token < parser->tokens.count && parser->tree.roles[token] == PARSE_ROLE_TERMINATOR;
    if (result)
    {
        const uint32_t owner = parser->tree.parents[token];
        const uint8_t role = owner < parser->tokens.count ? parser->tree.roles[owner] : PARSE_ROLE_NONE;
        result = owner < parser->tokens.count && (role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_OPEN_BODY ||
                                                   role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_LABEL || role == PARSE_ROLE_OPEN_CASE ||
                                                   role == PARSE_ROLE_CLOSE_CASE);
    }
    return result;
}

static bool check_common_effect_role(uint8_t role)
{
    const bool result = role == PARSE_ROLE_ADDITIVE || role == PARSE_ROLE_MULTIPLICATIVE || role == PARSE_ROLE_SHIFT ||
                        role == PARSE_ROLE_RELATIONAL || role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_BITWISE_AND ||
                        role == PARSE_ROLE_BITWISE_OR || role == PARSE_ROLE_BITWISE_XOR || role == PARSE_ROLE_LOGICAL_AND ||
                        role == PARSE_ROLE_LOGICAL_OR || role == PARSE_ROLE_PREFIX || role == PARSE_ROLE_POSTFIX;
    return result;
}

// The parent of `token` is the operator whose right operand holds it; a parenthesized operand's parent is a group.
static uint32_t check_common_parent(const Parser* parser, uint32_t token)
{
    const uint32_t result = token < parser->tokens.count ? parser->tree.parents[token] : BUSTER_CHECK_NONE;
    return result;
}

// A closer's opener is the token whose parent it is; the closer's own parent is the enclosing construct.
static uint32_t check_common_opener(const Parser* parser, uint32_t closer)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t at = closer; at != 0 && result == BUSTER_CHECK_NONE;)
    {
        at -= 1;
        const uint8_t role = parser->tree.roles[at];
        if (parser->tree.parents[at] == closer && (role == PARSE_ROLE_OPEN_CONDITION || role == PARSE_ROLE_OPEN_FOR))
        {
            result = at;
        }
    }
    return result;
}

static void check_common_parentheses(const Parser* parser, Preprocessor* pp, const CheckInput* input, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    for (uint32_t token = 0; token < count; token += 1)
    {
        const uint8_t role = parser->tree.roles[token];
        const uint32_t parent = check_common_parent(parser, token);
        const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
        if (role == PARSE_ROLE_ASSIGN && parent_role == PARSE_ROLE_CLOSE_CONDITION && parser->tokens.kinds[token] == PARSE_KIND_EQUAL)
        {
            const CheckCommonNote notes[2] = {
                { token, "place parentheses around the assignment to silence this warning" },
                { token, "use '==' to turn this assignment into an equality comparison" },
            };
            check_common_report(parser, pp, input, token, CHECK_WARNING_ID_PARENTHESES, false,
                                "using the result of an assignment as a condition without parentheses", notes, 2, scratch);
        }
        else if (parent_role == PARSE_ROLE_BITWISE_AND || parent_role == PARSE_ROLE_BITWISE_OR || parent_role == PARSE_ROLE_BITWISE_XOR)
        {
            if (role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_RELATIONAL)
            {
                const char* lower = check_common_operator_spelling(parser, parent);
                const char* upper = check_common_operator_spelling(parser, token);
                const char* message = check_warning_text(scratch, "%s has lower precedence than %s; %s will be evaluated first", lower, upper, upper);
                const CheckCommonNote notes[2] = {
                    { parent, check_warning_text(scratch, "place parentheses around the '%s' expression to silence this warning", upper) },
                    { parent, check_warning_text(scratch, "place parentheses around the %s expression to evaluate it first", lower) },
                };
                check_common_report(parser, pp, input, parent, CHECK_WARNING_ID_PARENTHESES, false, message, notes, 2, scratch);
            }
        }
        else if (role == PARSE_ROLE_PREFIX && parser->tokens.kinds[token] == PARSE_KIND_EXCLAIM && check_common_comparison_role(parent_role) && token < parent)
        {
            const CheckCommonNote notes[2] = {
                { token, "add parentheses after the '!' to evaluate the comparison first" },
                { token, "add parentheses around left hand side expression to silence this warning" },
            };
            check_common_report(parser, pp, input, token, CHECK_WARNING_ID_LOGICAL_NOT_PARENTHESES, false,
                                "logical not is only applied to the left hand side of this comparison", notes, 2, scratch);
        }
        else if (role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_RELATIONAL)
        {
            if (check_common_statement_terminator(parser, parent))
            {
                const bool equality = role == PARSE_ROLE_EQUALITY;
                const char* message = equality ? (parser->tokens.kinds[token] == PARSE_KIND_EQUALEQUAL ? "equality comparison result unused" : "inequality comparison result unused") :
                                                "relational comparison result unused";
                const CheckCommonNote note = { token, parser->tokens.kinds[token] == PARSE_KIND_EQUALEQUAL ? "use '=' to turn this equality comparison into an assignment" :
                                                         parser->tokens.kinds[token] == PARSE_KIND_EXCLAIMEQUAL ? "use '|=' to turn this inequality comparison into an or-assignment" : "" };
                check_common_report(parser, pp, input, token, CHECK_WARNING_ID_UNUSED_COMPARISON, false, message,
                                    note.message[0] != 0 ? &note : NULL, note.message[0] != 0 ? 1u : 0u, scratch);
            }
        }
        else if (check_common_statement_terminator(parser, parent) && check_common_effect_role(role) &&
                 role != PARSE_ROLE_ASSIGN && role != PARSE_ROLE_EQUALITY && role != PARSE_ROLE_RELATIONAL &&
                 parser->tokens.kinds[token] != PARSE_KIND_L_PAREN && parser->tokens.kinds[token] != PARSE_KIND_R_PAREN &&
                 parser->tokens.kinds[token] != PARSE_KIND_PLUSPLUS && parser->tokens.kinds[token] != PARSE_KIND_MINUSMINUS)
        {
            check_common_report(parser, pp, input, token, CHECK_WARNING_ID_UNUSED_VALUE, false, "expression result unused", NULL, 0, scratch);
        }
    }
}

static void check_common_empty_bodies(const Parser* parser, Preprocessor* pp, const CheckInput* input, Arena* scratch)
{
    for (uint32_t token = 0; token < parser->tokens.count; token += 1)
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_SEMI && parser->tree.roles[token] == PARSE_ROLE_TERMINATOR)
        {
            const uint32_t parent = check_common_parent(parser, token);
            const uint8_t role = parent < parser->tokens.count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
            uint32_t keyword = BUSTER_CHECK_NONE;
            if ((role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR) && token == parent + 1)
            {
                const uint32_t opener = check_common_opener(parser, parent);
                keyword = opener != 0 && opener < parser->tokens.count ? opener - 1 : BUSTER_CHECK_NONE;
            }
            const uint8_t kind = keyword < parser->tokens.count ? parser->tokens.kinds[keyword] : 0;
            const char* subject = kind == PARSE_KIND_IF ? "if statement" : kind == PARSE_KIND_WHILE ? "while loop" :
                                  kind == PARSE_KIND_FOR ? "for loop" : kind == PARSE_KIND_SWITCH ? "switch statement" : NULL;
            if (subject != NULL)
            {
                const char* message = check_warning_text(scratch, "%s has empty body", subject);
                const CheckCommonNote note = { token, "put the semicolon on a separate line to silence this warning" };
                check_common_report(parser, pp, input, token, CHECK_WARNING_ID_EMPTY_BODY, false, message, &note, 1, scratch);
            }
        }
    }
}

static void check_common_dangling_else(const Parser* parser, Preprocessor* pp, const CheckInput* input, Arena* scratch)
{
    uint8_t* else_at = parser->tokens.count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, parser->tokens.count) : NULL;
    if (parser->tokens.count != 0 && else_at == NULL)
    {
        return;
    }
    memset(else_at, 0, parser->tokens.count);
    for (uint32_t token = 0; token < parser->tokens.count; token += 1)
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_ELSE && parser->tree.roles[token] == PARSE_ROLE_ELSE && parser->tree.parents[token] < parser->tokens.count)
        {
            else_at[parser->tree.parents[token]] = 1;
        }
    }
    for (uint32_t token = 0; token < parser->tokens.count; token += 1)
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_ELSE && parser->tree.roles[token] == PARSE_ROLE_ELSE)
        {
            const uint32_t inner_close = check_common_parent(parser, token);
            const uint32_t inner_open = check_common_opener(parser, inner_close);
            const uint32_t inner_if = inner_open != 0 && inner_open < parser->tokens.count ? inner_open - 1 : BUSTER_CHECK_NONE;
            const uint32_t outer_close = inner_close < parser->tokens.count ? check_common_parent(parser, inner_close) : BUSTER_CHECK_NONE;
            const uint8_t outer_role = outer_close < parser->tokens.count ? parser->tree.roles[outer_close] : PARSE_ROLE_NONE;
            if (outer_role == PARSE_ROLE_CLOSE_CONDITION && inner_if < parser->tokens.count && parser->tokens.kinds[inner_if] == PARSE_KIND_IF)
            {
                const uint32_t outer_open = check_common_opener(parser, outer_close);
                const uint32_t outer_if = outer_open != 0 && outer_open < parser->tokens.count ? outer_open - 1 : BUSTER_CHECK_NONE;
                const bool outer_else = outer_close < parser->tokens.count && else_at[outer_close] != 0;
                if (!outer_else && outer_if < parser->tokens.count && parser->tokens.kinds[outer_if] == PARSE_KIND_IF)
                {
                    check_common_report(parser, pp, input, token, CHECK_WARNING_ID_DANGLING_ELSE, false, "add explicit braces to avoid dangling else", NULL, 0, scratch);
                }
            }
        }
    }
}

static bool check_common_in_return(const Parser* parser, uint32_t token)
{
    bool result = false;
    uint32_t at = token;
    for (uint32_t steps = 0; at < parser->tokens.count && steps < parser->tokens.count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[at];
        if (role == PARSE_ROLE_RETURN)
        {
            result = true;
            break;
        }
        if (role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_OPEN_CALL || role == PARSE_ROLE_CLOSE_CALL ||
            role == PARSE_ROLE_OPEN_INITIALIZER || role == PARSE_ROLE_ASSIGN)
        {
            break;
        }
        at = parser->tree.parents[at];
    }
    return result;
}

// Whether `token` names a definition parameter, whose address clang names a parameter's in -Wreturn-stack-address.
static bool check_common_parameter_token(const CheckInput* input, const Parser* parser, uint32_t token)
{
    bool result = false;
    for (uint32_t opening = 0; !result && opening < parser->tokens.count; opening += 1)
    {
        for (uint32_t parameter = input->body_declarations[opening] != BUSTER_CHECK_NONE ? input->parameters[opening] : BUSTER_CHECK_NONE;
             !result && parameter != BUSTER_CHECK_NONE && parameter < parser->tokens.count; parameter = input->parameter_next[parameter])
        {
            result = parameter == token;
        }
    }
    return result;
}

static void check_common_return_stack(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        const CheckOp op = graph->ops[node];
        const uint32_t operand = op == CHECK_OP_ADDRESS || op == CHECK_OP_CONVERT ? graph->operand0[node] : BUSTER_CHECK_NONE;
        const bool arrays = op == CHECK_OP_CONVERT && (stream->conversions[node].operands[0].flags & BUSTER_CHECK_CONVERT_ARRAY) != 0;
        if ((op == CHECK_OP_ADDRESS || arrays) && operand < graph->count)
        {
            const CheckValue* value = stream->values + operand;
            const uint32_t declaration = value->object != 0 ? value->object - 1 : BUSTER_CHECK_NONE;
            const bool local = value->category == CHECK_LVALUE && declaration < input->token_count && input->declaration_bodies[declaration] < parser->tokens.count &&
                               !input->static_objects[declaration] && !input->thread_local[declaration];
            if (local && check_common_in_return(parser, graph->tokens[operand]))
            {
                const uint32_t name = parser->tokens.names[declaration];
                const char* subject = check_common_parameter_token(input, parser, declaration) ? "parameter" : "local variable";
                const char* message = check_warning_text(scratch, "address of stack memory associated with %s '%.*s' returned", subject,
                                                          (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                check_common_report(parser, pp, input, graph->tokens[operand], CHECK_WARNING_ID_RETURN_STACK_ADDRESS, false, message, NULL, 0, scratch);
            }
        }
    }
}

static void check_common_graph_warnings(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                        TypeTable* types, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        if (stream->diagnostics[node] != CHECK_DIAG_NONE)
        {
            continue;
        }
        const CheckOp op = graph->ops[node];
        const uint32_t token = graph->tokens[node];
        if (op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER)
        {
            const uint32_t divisor = graph->operand1[node];
            const uint32_t contexts = graph->contexts != NULL ? graph->contexts[node] : 0;
            const bool ice = (contexts & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT)) != 0;
            if (!ice && check_common_integer_constant(stream, divisor) && stream->values[divisor].integer == 0)
            {
                uint32_t width = 0;
                bool sign = false;
                if (check_common_integer(types, stream->values[graph->operand0[node]].type, &width, &sign))
                {
                    check_common_report(parser, pp, input, token, CHECK_WARNING_ID_DIVISION_BY_ZERO, false,
                                        op == CHECK_OP_DIVIDE ? "division by zero is undefined" : "remainder by zero is undefined", NULL, 0, scratch);
                }
            }
        }
        else if (op == CHECK_OP_INITIALIZE || op == CHECK_OP_ASSIGN || op == CHECK_OP_CONVERT)
        {
            const uint32_t source = op == CHECK_OP_ASSIGN ? graph->operand1[node] : graph->operand0[node];
            const uint32_t conversion = op == CHECK_OP_ASSIGN ? 1u : 0u;
            const uint32_t flags = stream->conversions[node].operands[conversion].flags;
            if (source < graph->count && (flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (flags & BUSTER_CHECK_CONVERT_CAST) == 0 &&
                check_common_integer_constant(stream, source))
            {
                const CheckValue* value = stream->values + source;
                const TypeId target = stream->values[node].type;
                __int128 converted = 0;
                if (check_common_conversion_changes(types, value->type, target, value, &converted))
                {
                    const String8 source_name = check_driver_warning_type(pp, types, scratch, value->type);
                    const String8 target_name = check_driver_warning_type(pp, types, scratch, target);
                    if (source_name.length != 0 && target_name.length != 0)
                    {
                        char original[48];
                        char changed[48];
                        uint32_t source_width = 0;
                        uint32_t target_width = 0;
                        bool source_signed = false;
                        bool target_signed = false;
                        check_common_integer(types, value->type, &source_width, &source_signed);
                        check_common_integer(types, target, &target_width, &target_signed);
                        check_common_number(original, sizeof(original), check_common_integer_value(value, source_width, source_signed));
                        check_common_number(changed, sizeof(changed), converted);
                        const char* message = check_warning_text(scratch, "implicit conversion from '%.*s' to '%.*s' changes value from %s to %s",
                                                                  (int)source_name.length, source_name.pointer, (int)target_name.length, target_name.pointer,
                                                                  original, changed);
                        check_common_report(parser, pp, input, graph->tokens[source], CHECK_WARNING_ID_CONSTANT_CONVERSION, false, message, NULL, 0, scratch);
                    }
                }
            }
            if (op == CHECK_OP_ASSIGN)
            {
                const uint32_t left = graph->operand0[node];
                const uint32_t right = graph->operand1[node];
                const bool volatile_object = left < graph->count &&
                    (type_qualifiers(type_strip_aligned(types, stream->values[left].type)) & BUSTER_TYPE_VOLATILE) != 0;
                if (!volatile_object && left < graph->count && right < graph->count && stream->values[left].category == CHECK_LVALUE &&
                    stream->values[right].category == CHECK_LVALUE && stream->values[left].object != 0 &&
                    stream->values[left].object == stream->values[right].object &&
                    stream->values[left].offset == 0 && stream->values[right].offset == 0)
                {
                    const String8 type_name = check_driver_warning_type(pp, types, scratch, stream->values[left].type);
                    if (type_name.length != 0)
                    {
                        const char* message = check_warning_text(scratch, "explicitly assigning value of variable of type '%.*s' to itself",
                                                                  (int)type_name.length, type_name.pointer);
                        check_common_report(parser, pp, input, token, CHECK_WARNING_ID_SELF_ASSIGN, true, message, NULL, 0, scratch);
                    }
                }
            }
        }
        else if (op == CHECK_OP_SUBSCRIPT)
        {
            const uint32_t array = graph->operand0[node];
            const uint32_t index = graph->operand1[node];
            if (array < graph->count && check_common_integer_constant(stream, index))
            {
                TypeId type = type_unqualified(type_strip_aligned(types, stream->values[array].type));
                if (type_kind(types, type) == BUSTER_TYPE_KIND_ARRAY)
                {
                    const uint64_t extent = types->types.extent[type_index(type)];
                    uint32_t width = 0;
                    bool sign = false;
                    if (check_common_integer(types, stream->values[index].type, &width, &sign))
                    {
                        const __int128 value = check_common_integer_value(stream->values + index, width, sign);
                        const bool past = value >= 0 && (unsigned __int128)value >= extent;
                        const bool before = value < 0;
                        if (past || before)
                        {
                            const String8 array_name = check_driver_warning_type(pp, types, scratch, stream->values[array].type);
                            char number[48];
                            check_common_number(number, sizeof(number), value);
                            const char* message = past && array_name.length != 0 ?
                                check_warning_text(scratch, "array index %s is past the end of the array (that has type '%.*s')", number,
                                                   (int)array_name.length, array_name.pointer) :
                                array_name.length != 0 ? check_warning_text(scratch, "array index %s is before the beginning of the array", number) : NULL;
                            CheckCommonNote note = { 0 };
                            const uint32_t declaration = stream->values[array].object != 0 ? stream->values[array].object - 1 : BUSTER_CHECK_NONE;
                            const uint32_t name = declaration < parser->tokens.count ? parser->tokens.names[declaration] : 0;
                            if (declaration < parser->tokens.count && name != 0)
                            {
                                note.token = check_common_declaration_start(parser, declaration);
                                note.message = check_warning_text(scratch, "array '%.*s' declared here",
                                                                  (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                            }
                            if (message != NULL)
                            {
                                check_common_report(parser, pp, input, graph->tokens[array], CHECK_WARNING_ID_ARRAY_BOUNDS, false, message,
                                                    note.message != NULL ? &note : NULL, note.message != NULL ? 1u : 0u, scratch);
                            }
                        }
                    }
                }
            }
        }
        else if (op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL)
        {
            const uint32_t left = graph->operand0[node];
            const uint32_t right = graph->operand1[node];
            uint32_t left_width = 0;
            uint32_t right_width = 0;
            bool left_signed = false;
            bool right_signed = false;
            bool integer = left < graph->count && right < graph->count &&
                           check_common_integer(types, stream->values[left].type, &left_width, &left_signed) &&
                           check_common_integer(types, stream->values[right].type, &right_width, &right_signed);
            bool constant_suppressed = false;
            if (integer && left_signed != right_signed)
            {
                const uint32_t signed_operand = left_signed ? left : right;
                const uint32_t unsigned_operand = left_signed ? right : left;
                const uint32_t signed_width = left_signed ? left_width : right_width;
                const uint32_t unsigned_width = left_signed ? right_width : left_width;
                if (check_common_integer_constant(stream, signed_operand) && unsigned_width != 0)
                {
                    const __int128 constant = check_common_integer_value(stream->values + signed_operand, signed_width, true);
                    constant_suppressed = constant >= 0 && (unsigned __int128)constant <= check_action_mask(unsigned_width);
                }
                else if ((op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL) && check_common_integer_constant(stream, unsigned_operand) && signed_width != 0)
                {
                    const __int128 constant = check_common_integer_value(stream->values + unsigned_operand, unsigned_width, false);
                    constant_suppressed = constant >= 0 && (unsigned __int128)constant <= check_action_mask(signed_width);
                }
                const CheckOp signed_op = graph->ops[signed_operand];
                constant_suppressed = constant_suppressed || signed_op == CHECK_OP_NOT || (signed_op >= CHECK_OP_LESS && signed_op <= CHECK_OP_NOT_EQUAL) ||
                                      signed_op == CHECK_OP_LOGICAL_AND || signed_op == CHECK_OP_LOGICAL_OR;
            }
            const bool simple = left < graph->count && right < graph->count &&
                                check_common_simple_operand(graph->ops[left]) && check_common_simple_operand(graph->ops[right]);
            const bool constant_operand = check_common_integer_constant(stream, left) || check_common_integer_constant(stream, right);
            if (integer && (simple || constant_operand) && left_signed != right_signed && !constant_suppressed && (left_signed ? left_width <= right_width : right_width <= left_width))
            {
                const String8 left_name = check_driver_warning_type(pp, types, scratch, stream->values[left].type);
                const String8 right_name = check_driver_warning_type(pp, types, scratch, stream->values[right].type);
                if (left_name.length != 0 && right_name.length != 0)
                {
                    const char* message = check_warning_text(scratch, "comparison of integers of different signs: '%.*s' and '%.*s'",
                                                              (int)left_name.length, left_name.pointer, (int)right_name.length, right_name.pointer);
                    check_common_report(parser, pp, input, token, CHECK_WARNING_ID_SIGN_COMPARE, true, message, NULL, 0, scratch);
                }
            }
        }
    }
}

// A printf-family call's format string: its name decides the format argument, and a literal's bytes decide each
// conversion's expected type.
static uint32_t check_common_format_index(String8 name)
{
    uint32_t result = UINT32_MAX;
    if (name.length == 6 && memcmp(name.pointer, "printf", 6) == 0) result = 0;
    else if (name.length == 7 && memcmp(name.pointer, "fprintf", 7) == 0) result = 1;
    else if (name.length == 7 && memcmp(name.pointer, "sprintf", 7) == 0) result = 1;
    else if (name.length == 8 && memcmp(name.pointer, "snprintf", 8) == 0) result = 2;
    else if (name.length == 7 && memcmp(name.pointer, "dprintf", 7) == 0) result = 1;
    return result;
}

static bool check_common_format_integer(TypeTable* types, TypeId type, uint32_t width_limit)
{
    uint32_t width = 0;
    bool sign = false;
    const bool result = check_action_integer_type(types, type, &width, &sign) && width <= width_limit;
    return result;
}

static bool check_common_format_pointer(TypeTable* types, TypeId type)
{
    const TypeId stripped = type_unqualified(type_strip_aligned(types, type));
    bool result = type_kind(types, stripped) == BUSTER_TYPE_KIND_POINTER;
    if (!result && type_kind_is_array(type_kind(types, stripped)))
    {
        result = type_kind(types, type_unqualified(type_element(types, stripped))) == BUSTER_TYPE_KIND_POINTER ||
                 type_kind(types, type_unqualified(type_element(types, stripped))) != BUSTER_TYPE_KIND_NONE;
    }
    return result;
}

static bool check_common_format_char_pointer(TypeTable* types, TypeId type)
{
    TypeId element = type_unqualified(type_strip_aligned(types, type));
    if (type_kind_is_array(type_kind(types, element)))
    {
        element = type_unqualified(type_element(types, element));
    }
    else if (type_kind(types, element) == BUSTER_TYPE_KIND_POINTER)
    {
        element = type_unqualified(type_element(types, element));
    }
    const bool result = type_kind(types, element) == BUSTER_TYPE_KIND_INTEGER && type_size(types, element) == 1;
    return result;
}

static bool check_common_format_floating(TypeTable* types, TypeId type, bool long_double)
{
    const TypeId stripped = type_unqualified(type_strip_aligned(types, type));
    const uint8_t kind = type_kind(types, stripped);
    const bool result = long_double ? kind == BUSTER_TYPE_KIND_FLOATING && stripped == BUSTER_TYPE_LONG_DOUBLE :
                                     kind == BUSTER_TYPE_KIND_FLOATING && stripped != BUSTER_TYPE_LONG_DOUBLE;
    return result;
}

static bool check_common_format_attribute(const CheckInputAttributes* attributes, uint32_t* format_index, uint32_t* first_argument)
{
    bool found = false;
    for (uint32_t a = 0; attributes != NULL && a < attributes->count && !found; a += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + a;
        if (attribute->kind == CHECK_ATTRIBUTE_FORMAT && !attribute->ignored && attribute->argument_count >= 3 && attribute->argument_values != NULL &&
            attribute->argument_values[1] > 0)
        {
            *format_index = (uint32_t)attribute->argument_values[1] - 1;
            *first_argument = attribute->argument_values[2] > 0 ? (uint32_t)attribute->argument_values[2] - 1 : *format_index + 1;
            found = true;
        }
    }
    return found;
}

static void check_common_format(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                TypeTable* types, const CheckProgram* program, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        if (graph->ops[node] != CHECK_OP_CALL || stream->diagnostics[node] != CHECK_DIAG_NONE)
        {
            continue;
        }
        const uint32_t callee = graph->operand0[node];
        const uint32_t callee_token = callee < graph->count ? graph->tokens[callee] : BUSTER_CHECK_NONE;
        if (callee_token >= parser->tokens.count || parser->tokens.kinds[callee_token] != PARSE_KIND_IDENTIFIER)
        {
            continue;
        }
        const uint32_t name_id = parser->tokens.names[callee_token];
        const String8 name = { .pointer = (char*)intern_bytes(pp->table, name_id), .length = intern_length(pp->table, name_id) };
        uint32_t format_index = check_common_format_index(name);
        uint32_t first_argument = format_index != UINT32_MAX ? format_index + 1 : UINT32_MAX;
        if (format_index == UINT32_MAX && program != NULL && stream->values[callee].object != 0 &&
            stream->values[callee].object <= program->object_count)
        {
            const uint32_t definition = program->object_declarations[stream->values[callee].object];
            bool found = definition < input->token_count && input->attributes != NULL &&
                         check_common_format_attribute(input->attributes[definition], &format_index, &first_argument);
            const uint32_t canonical = definition < input->token_count && input->declarations != NULL ? input->declarations[definition] : BUSTER_CHECK_NONE;
            found = found || (canonical < input->token_count && input->attributes != NULL &&
                              check_common_format_attribute(input->attributes[canonical], &format_index, &first_argument));
            for (uint32_t at = definition < input->token_count ? definition : 0; !found && at != 0 && input->declarations != NULL &&
                 input->declaration_kinds != NULL && input->attributes != NULL;)
            {
                at -= 1;
                if (input->declarations[at] == at && input->declaration_kinds[at] == C_BINDING_FUNCTION && parser->tokens.names[at] == name_id)
                {
                    found = check_common_format_attribute(input->attributes[at], &format_index, &first_argument);
                }
            }
        }
        const uint32_t offset = graph->argument_offsets[node];
        const uint32_t count = graph->argument_counts[node];
        if (format_index != UINT32_MAX && first_argument != UINT32_MAX && format_index < count && first_argument <= count && offset <= graph->argument_count &&
            count <= graph->argument_count - offset)
        {
            const uint32_t format_node = graph->argument_nodes[offset + format_index];
            const uint32_t format_token = format_node < graph->count ? graph->tokens[format_node] : BUSTER_CHECK_NONE;
            const uint32_t string = input->string_ids != NULL && format_token < input->token_count ? input->string_ids[format_token] : BUSTER_CHECK_NONE;
            if (string != BUSTER_CHECK_NONE && string < input->string_count && input->strings != NULL)
            {
                const uint8_t* bytes = input->strings[string].bytes;
                uint64_t length = input->strings[string].byte_count;
                uint32_t argument = first_argument;
                for (uint64_t at = 0; at < length && !pp->fatal; at += 1)
                {
                    if (bytes[at] != '%')
                    {
                        continue;
                    }
                    at += 1;
                    if (at >= length)
                    {
                        break;
                    }
                    if (bytes[at] == '%')
                    {
                        continue;
                    }
                    while (at < length && (bytes[at] == '-' || bytes[at] == '+' || bytes[at] == ' ' || bytes[at] == '#' ||
                                           bytes[at] == '0' || bytes[at] == '\''))
                    {
                        at += 1;
                    }
                    if (at < length && bytes[at] == '*')
                    {
                        argument += 1;
                        at += 1;
                    }
                    while (at < length && bytes[at] >= '0' && bytes[at] <= '9')
                    {
                        at += 1;
                    }
                    if (at < length && bytes[at] == '.')
                    {
                        at += 1;
                        if (at < length && bytes[at] == '*')
                        {
                            argument += 1;
                            at += 1;
                        }
                        while (at < length && bytes[at] >= '0' && bytes[at] <= '9')
                        {
                            at += 1;
                        }
                    }
                    char length_modifier = 0;
                    if (at < length && (bytes[at] == 'h' || bytes[at] == 'l' || bytes[at] == 'j' || bytes[at] == 'z' || bytes[at] == 't' || bytes[at] == 'L'))
                    {
                        length_modifier = (char)bytes[at];
                        at += 1;
                        if (at < length && bytes[at] == length_modifier && (length_modifier == 'h' || length_modifier == 'l'))
                        {
                            length_modifier = length_modifier == 'h' ? 'H' : 'L';
                            at += 1;
                        }
                    }
                    const uint8_t conversion = at < length ? bytes[at] : 0;
                    if (conversion == 0)
                    {
                        break;
                    }
                    const bool conversion_integer = conversion == 'd' || conversion == 'i' || conversion == 'u' || conversion == 'o' ||
                                                    conversion == 'x' || conversion == 'X' || conversion == 'c';
                    const bool conversion_string = conversion == 's';
                    const bool conversion_pointer = conversion == 'p';
                    const bool conversion_float = conversion == 'f' || conversion == 'F' || conversion == 'e' || conversion == 'E' ||
                                                  conversion == 'g' || conversion == 'G' || conversion == 'a' || conversion == 'A';
                    const bool conversion_number = conversion == 'n';
                    if (conversion_integer || conversion_string || conversion_pointer || conversion_float || conversion_number)
                    {
                        if (argument < count)
                        {
                            const uint32_t argument_node = graph->argument_nodes[offset + argument];
                            const TypeId type = argument_node < graph->count ? stream->values[argument_node].type : 0;
                            const bool long_double = length_modifier == 'L';
                            const uint32_t width_limit = length_modifier == 0 || length_modifier == 'h' || length_modifier == 'H' ? 32u : 64u;
                            const bool accepted = conversion_integer ? check_common_format_integer(types, type, conversion == 'c' || length_modifier == 0 || length_modifier == 'h' || length_modifier == 'H' ? 32u : width_limit) :
                                conversion_string ? check_common_format_char_pointer(types, type) :
                                conversion_pointer ? check_common_format_pointer(types, type) :
                                conversion_float ? check_common_format_floating(types, type, long_double) :
                                check_common_format_pointer(types, type);
                            if (!accepted)
                            {
                                const char* expected = conversion_string ? "char *" : conversion_pointer ? "void *" :
                                    conversion_float ? (long_double ? "long double" : "double") :
                                    conversion_number ? "int *" :
                                    (conversion == 'u' || conversion == 'o' || conversion == 'x' || conversion == 'X') ?
                                        (length_modifier == 'l' || length_modifier == 'L' ? "unsigned long" : length_modifier == 'z' ? "unsigned long" : "unsigned int") :
                                        (length_modifier == 'l' || length_modifier == 'L' ? "long" : "int");
                                const String8 actual = check_common_type_text(pp, types, scratch, type);
                                const uint32_t at_token = argument_node < graph->count ? graph->tokens[argument_node] : format_token;
                                if (actual.length != 0)
                                {
                                    const char* message = check_warning_text(scratch, "format specifies type '%s' but the argument has type '%.*s'",
                                                                              expected, (int)actual.length, actual.pointer);
                                    check_common_report(parser, pp, input, at_token, CHECK_WARNING_ID_FORMAT, false, message, NULL, 0, scratch);
                                }
                            }
                            argument += 1;
                        }
                    }
                }
            }
            else if (count == first_argument)
            {
                uint32_t base = format_node;
                for (uint32_t steps = 0; base < graph->count && steps < graph->count &&
                     (graph->ops[base] == CHECK_OP_CONVERT || graph->ops[base] == CHECK_OP_POSITIVE); steps += 1)
                {
                    const uint32_t operand = graph->operand0[base];
                    if (operand >= graph->count)
                    {
                        break;
                    }
                    base = operand;
                }
                if (base < graph->count && graph->ops[base] == CHECK_OP_VALUE)
                {
                    const CheckCommonNote note = { format_token, "treat the string as an argument to avoid this" };
                    check_common_report(parser, pp, input, format_token, CHECK_WARNING_ID_FORMAT_SECURITY, false,
                                        "format string is not a string literal (potentially insecure)", &note, 1, scratch);
                }
            }
        }
    }
}

// -Wswitch: an enum selector and case labels that name every handled enumerator, with no default. A case whose
// expression is not a single enumerator name leaves the answer to a full constant evaluator.
static void check_common_switches(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                  TypeTable* types, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    for (uint32_t token = 0; token < count; token += 1)
    {
        if (parser->tokens.kinds[token] != PARSE_KIND_SWITCH || parser->tree.roles[token] != PARSE_ROLE_LEAF_RIGHT)
        {
            continue;
        }
        const uint32_t closer = check_common_parent(parser, token);
        const uint32_t opener = check_common_opener(parser, closer);
        const uint32_t selector = opener != BUSTER_CHECK_NONE && opener + 1 < count ? opener + 1 : BUSTER_CHECK_NONE;
        uint32_t selector_node = input->token_nodes != NULL && selector < input->token_count ? input->token_nodes[selector] : BUSTER_CHECK_NONE;
        for (uint32_t steps = 0; selector_node < stream->count && steps < stream->count; steps += 1)
        {
            const CheckOp op = stream->graph->ops[selector_node];
            if (op != CHECK_OP_POSITIVE && op != CHECK_OP_NEGATIVE && op != CHECK_OP_BIT_NOT && op != CHECK_OP_CONVERT)
            {
                break;
            }
            const uint32_t operand = stream->graph->operand0[selector_node];
            if (operand >= stream->count)
            {
                break;
            }
            selector_node = operand;
        }
        const TypeId enum_type = selector_node < stream->count ? type_unqualified(type_strip_aligned(types, stream->values[selector_node].type)) : 0;
        if (enum_type == 0 || type_kind(types, enum_type) != BUSTER_TYPE_KIND_ENUM)
        {
            continue;
        }
        const uint32_t body = closer + 1 < count ? closer + 1 : count;
        const uint32_t body_end = body < count && parser->tokens.kinds[body] == PARSE_KIND_L_BRACE ? check_common_parent(parser, body) : count;
        bool has_default = false;
        bool unknown_case = false;
        uint32_t handled[64];
        uint32_t handled_count = 0;
        for (uint32_t at = body; at < body_end && at < count && !unknown_case; at += 1)
        {
            const uint8_t kind = parser->tokens.kinds[at];
            const bool owned = input->control_owners != NULL && input->control_owners[at] == closer;
            if (kind == PARSE_KIND_DEFAULT && owned)
            {
                has_default = true;
            }
            else if (kind == PARSE_KIND_CASE && owned)
            {
                uint32_t colon = at + 1 < count ? at + 1 : count;
                for (uint32_t find = at + 1; find < body_end; find += 1)
                {
                    if (parser->tokens.kinds[find] == PARSE_KIND_COLON)
                    {
                        colon = find;
                        break;
                    }
                }
                if (colon == at + 2 && parser->tokens.kinds[at + 1] == PARSE_KIND_IDENTIFIER)
                {
                    const uint32_t name = parser->tokens.names[at + 1];
                    if (handled_count < BUSTER_ARRAY_LENGTH(handled))
                    {
                        handled[handled_count++] = name;
                    }
                }
                else
                {
                    unknown_case = true;
                }
            }
        }
        if (!has_default && !unknown_case)
        {
            const uint32_t tag = type_index(enum_type) < types->types.count ? types->types.index[type_index(enum_type)] : UINT32_MAX;
            const uint32_t tag_name = tag < types->tags.count ? types->tags.name[tag] : 0;
            uint32_t source_names[64];
            uint32_t source_count = 0;
            for (uint32_t open = 2; tag_name != 0 && open < count && source_count < BUSTER_ARRAY_LENGTH(source_names); open += 1)
            {
                if (parser->tree.roles[open] != PARSE_ROLE_OPEN_ENUM || parser->tokens.kinds[open - 1] != PARSE_KIND_IDENTIFIER ||
                    parser->tokens.names[open - 1] != tag_name)
                {
                    continue;
                }
                const uint32_t close = check_common_parent(parser, open);
                for (uint32_t at = open + 1; at < close && at < count && source_count < BUSTER_ARRAY_LENGTH(source_names); at += 1)
                {
                    const uint8_t previous = parser->tokens.kinds[at - 1];
                    if (parser->tokens.kinds[at] == PARSE_KIND_IDENTIFIER && (previous == PARSE_KIND_L_BRACE || previous == PARSE_KIND_COMMA))
                    {
                        source_names[source_count++] = parser->tokens.names[at];
                    }
                }
                break;
            }
            uint32_t missing[64];
            uint32_t missing_count = 0;
            for (uint32_t n = 0; n < source_count && missing_count < BUSTER_ARRAY_LENGTH(missing); n += 1)
            {
                bool found = false;
                for (uint32_t h = 0; h < handled_count && !found; h += 1)
                {
                    found = handled[h] == source_names[n];
                }
                if (!found)
                {
                    missing[missing_count++] = source_names[n];
                }
            }
            char* list = missing_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, char, 512) : NULL;
            if (list != NULL)
            {
                uint64_t length = 0;
                list[0] = 0;
                for (uint32_t m = 0; m < missing_count && length < 480; m += 1)
                {
                    const String8 spelling = { .pointer = (char*)intern_bytes(pp->table, missing[m]), .length = intern_length(pp->table, missing[m]) };
                    const char* separator = m == 0 ? "" : m + 1 == missing_count ? (missing_count == 2 ? " and " : ", and ") : ", ";
                    const int written = snprintf(list + length, 512 - length, "%s'%.*s'", separator, (int)spelling.length, spelling.pointer);
                    if (written > 0 && (uint64_t)written < 512 - length)
                    {
                        length += (uint64_t)written;
                    }
                }
                const char* message = missing_count == 1 ? check_warning_text(scratch, "enumeration value %s not handled in switch", list) :
                                      check_warning_text(scratch, "enumeration values %s not handled in switch", list);
                check_common_report(parser, pp, input, selector, CHECK_WARNING_ID_SWITCH, false, message, NULL, 0, scratch);
            }
        }
    }
}

// An object an inline assembly operand or a builtin may write: its lvalue's object is unmodeled, so no uninitialized
// warning names it. A conversion or promotion keeps the lvalue's object.
static void check_common_mark_written(const CheckGraph* graph, const CheckStream* stream, uint32_t node, uint8_t* addressed, uint32_t count)
{
    for (uint32_t steps = 0; node < graph->count && steps < graph->count; steps += 1)
    {
        const CheckValue* value = stream->values + node;
        if (value->category == CHECK_LVALUE)
        {
            if (value->object != 0 && value->object - 1 < count)
            {
                addressed[value->object - 1] = 1;
            }
            break;
        }
        const CheckOp op = graph->ops[node];
        if (op != CHECK_OP_CONVERT && op != CHECK_OP_POSITIVE && op != CHECK_OP_NEGATIVE && op != CHECK_OP_BIT_NOT)
        {
            break;
        }
        const uint32_t operand = graph->operand0[node];
        if (operand >= graph->count)
        {
            break;
        }
        node = operand;
    }
}

// A local object a source statement leaves uninitialized and a read precedes every write of: clang's
// -Wuninitialized at the first use. Address-taken objects and parameters are left to a full dataflow analysis.
static void check_common_uninitialized(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                       TypeTable* types, Arena* scratch)
{
    const uint32_t count = input->token_count;
    const CheckGraph* graph = &input->graph;
    uint32_t* first_write = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    uint8_t* addressed = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    uint32_t* first_read = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    uint8_t* nonread = graph->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    uint8_t* parameters = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    uint8_t* statement_write = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    bool valid = count == 0 || (first_write != NULL && addressed != NULL && first_read != NULL && parameters != NULL && statement_write != NULL &&
                                (graph->count == 0 || nonread != NULL));
    if (valid && count != 0)
    {
        memset(first_write, 0xff, (uint64_t)count * sizeof(uint32_t));
        memset(addressed, 0, count);
        memset(first_read, 0xff, (uint64_t)count * sizeof(uint32_t));
        memset(parameters, 0, count);
        memset(statement_write, 0, count);
        if (nonread != NULL)
        {
            memset(nonread, 0, graph->count);
        }
        for (uint32_t opening = 0; opening < count; opening += 1)
        {
            for (uint32_t parameter = input->body_declarations[opening] != BUSTER_CHECK_NONE ? input->parameters[opening] : BUSTER_CHECK_NONE;
                 parameter != BUSTER_CHECK_NONE && parameter < count; parameter = input->parameter_next[parameter])
            {
                parameters[parameter] = 1;
            }
        }
        for (uint32_t node = 0; node < graph->count; node += 1)
        {
            const CheckOp op = graph->ops[node];
            const uint32_t operand = op == CHECK_OP_ADDRESS ? graph->operand0[node] : BUSTER_CHECK_NONE;
            if (operand < graph->count && stream->values[operand].category == CHECK_LVALUE && stream->values[operand].object != 0 &&
                stream->values[operand].object - 1 < count)
            {
                addressed[stream->values[operand].object - 1] = 1;
            }
            const bool write = check_common_write_op(op);
            const uint32_t target = graph->operand0[node];
            if (write && target < graph->count && stream->values[target].category == CHECK_LVALUE && stream->values[target].object != 0 &&
                stream->values[target].object - 1 < count)
            {
                const uint32_t object = stream->values[target].object - 1;
                const uint32_t token = graph->tokens[node] < first_write[object] ? graph->tokens[node] : first_write[object];
                first_write[object] = token;
                const uint32_t parent = token < parser->tokens.count ? parser->tree.parents[token] : BUSTER_CHECK_NONE;
                if (parent < parser->tokens.count && parser->tree.roles[parent] == PARSE_ROLE_TERMINATOR)
                {
                    statement_write[object] = 1;
                }
            }
            if (target < graph->count && nonread != NULL && (write || op == CHECK_OP_ADDRESS || op == CHECK_OP_INITIALIZE))
            {
                nonread[target] = 1;
            }
            const bool builtin = op >= CHECK_OP_BUILTIN_UNARY && op <= CHECK_OP_BUILTIN_BIT_CAST;
            if (builtin)
            {
                for (uint32_t o = 0; o < 3; o += 1)
                {
                    const uint32_t operand_value = o == 0 ? graph->operand0[node] : o == 1 ? graph->operand1[node] : graph->operand2[node];
                    check_common_mark_written(graph, stream, operand_value, addressed, count);
                }
                const uint32_t first = graph->argument_offsets != NULL ? graph->argument_offsets[node] : 0;
                const uint32_t n = graph->argument_counts != NULL ? graph->argument_counts[node] : 0;
                for (uint32_t a = 0; a < n && first + a < graph->argument_count; a += 1)
                {
                    check_common_mark_written(graph, stream, graph->argument_nodes[first + a], addressed, count);
                }
            }
        }
        for (uint32_t asm_index = 0; asm_index < input->asm_count; asm_index += 1)
        {
            const CheckInputAsm* statement = input->asm_statements + asm_index;
            const uint32_t operands = statement->output_count + statement->input_count;
            for (uint32_t o = 0; o < operands; o += 1)
            {
                check_common_mark_written(graph, stream, statement->operands[o].node, addressed, count);
            }
        }
        for (uint32_t node = 0; node < graph->count; node += 1)
        {
            const CheckValue* value = stream->values + node;
            const uint32_t declaration = value->object != 0 ? value->object - 1 : BUSTER_CHECK_NONE;
            const uint32_t token = graph->tokens[node];
            if (declaration < count && value->category == CHECK_LVALUE && graph->ops[node] == CHECK_OP_VALUE &&
                (nonread == NULL || nonread[node] == 0) && token < first_read[declaration])
            {
                first_read[declaration] = token;
            }
        }
        for (uint32_t declaration = 0; valid && declaration < count; declaration += 1)
        {
            const bool local = input->declaration_kinds[declaration] == C_BINDING_OBJECT && input->declaration_bodies[declaration] < parser->tokens.count &&
                               !input->static_objects[declaration] && !input->thread_local[declaration];
            const bool parameter = local && parameters[declaration] != 0;
            const bool plain = local && !parameter && input->initializer_nodes[declaration] == BUSTER_CHECK_NONE && !addressed[declaration];
            const TypeId type = local ? input->declared_types[declaration] : 0;
            const uint8_t kind = type != 0 ? type_kind(types, type_unqualified(type_strip_aligned(types, type))) : 0;
            const bool scalar = kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_POINTER ||
                                kind == BUSTER_TYPE_KIND_ENUM || kind == BUSTER_TYPE_KIND_NULLPTR;
            const uint32_t name = parser->tokens.names[declaration];
            if (plain && scalar && first_read[declaration] != BUSTER_CHECK_NONE &&
                (first_write[declaration] == BUSTER_CHECK_NONE || first_read[declaration] < first_write[declaration]))
            {
                const char* message = check_warning_text(scratch, "variable '%.*s' is uninitialized when used here",
                                                          (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                const CheckCommonNote note = { declaration + 1 < parser->tokens.count ? declaration + 1 : declaration,
                    check_warning_text(scratch, "initialize the variable '%.*s' to silence this warning",
                                       (int)intern_length(pp->table, name), intern_bytes(pp->table, name)) };
                const uint32_t body = input->declaration_bodies[declaration];
                const uint32_t publication = body < parser->tokens.count ? parser->tree.parents[body] : first_read[declaration];
                check_common_report_at(parser, pp, input, first_read[declaration], publication, CHECK_WARNING_ID_UNINITIALIZED, true, message, &note, 1, scratch);
            }
            else if (parameter && scalar && !addressed[declaration] && first_read[declaration] == BUSTER_CHECK_NONE &&
                     first_write[declaration] != BUSTER_CHECK_NONE && statement_write[declaration] != 0)
            {
                const char* message = check_warning_text(scratch, "parameter '%.*s' set but not used",
                                                          (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                const uint32_t body = input->declaration_bodies[declaration];
                const uint32_t publication = body < parser->tokens.count ? parser->tree.parents[body] : declaration;
                check_common_report_at(parser, pp, input, declaration, publication, CHECK_WARNING_ID_UNUSED_BUT_SET_PARAMETER, true, message, NULL, 0, scratch);
            }
        }
    }
}

static void check_driver_common_warnings(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                         TypeTable* types, const CheckProgram* program, Arena* scratch)
{
    if (parser == NULL || pp == NULL || input == NULL || stream == NULL || pp->fatal)
    {
        return;
    }
    check_common_parentheses(parser, pp, input, scratch);
    check_common_empty_bodies(parser, pp, input, scratch);
    check_common_dangling_else(parser, pp, input, scratch);
    check_common_switches(parser, pp, input, stream, types, scratch);
    check_common_graph_warnings(parser, pp, input, stream, types, scratch);
    check_common_return_stack(parser, pp, input, stream, scratch);
    check_common_uninitialized(parser, pp, input, stream, types, scratch);
    check_common_format(parser, pp, input, stream, types, program, scratch);
}
