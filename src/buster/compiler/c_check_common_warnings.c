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

static uint32_t check_common_address_root(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node, bool members);

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

// A function-end warning: its location is the declaration, but it completes with the function's closing brace, after
// the body's own warnings. The return phase orders it before the semantic groups at the same boundary.
static void check_common_report_phase_at(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t publication,
                                         uint32_t id, bool default_ignored, PPDiagnosticPhase phase, const char* message, Arena* scratch)
{
    if (token >= parser->tokens.count || publication >= parser->tokens.count || pp->fatal)
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
        if (suffix != NULL)
        {
            pp_diagnose_output_phase(pp, disposition, parser->tokens.sources[token], parser->tokens.sources[publication], phase, "%s%s", message, suffix);
        }
    }
}

static void check_common_report(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t id,
                                bool default_ignored, const char* message, const CheckCommonNote* notes, uint32_t note_count, Arena* scratch)
{
    check_common_report_at(parser, pp, input, token, token, id, default_ignored, message, notes, note_count, scratch);
}

// A type's spelling with clang's decayed type for a format argument: an array prints as a pointer to its element.
static String8 check_common_type_text(const Parser* parser, Preprocessor* pp, const CheckInput* input, TypeTable* types, CheckWarningSpelling* spell,
                                      Arena* scratch, TypeId type)
{
    String8 result = check_driver_warning_type(parser, pp, input, types, spell, scratch, type, 0);
    const TypeId stripped = type != 0 ? type_strip_aligned(types, type) : 0;
    if (stripped != 0 && type_kind_is_array(type_kind(types, type_unqualified(stripped))) && result.length != 0)
    {
        const String8 element = check_driver_warning_type(parser, pp, input, types, spell, scratch,
                                                          type_element(types, type_unqualified(stripped)), 0);
        if (element.length != 0)
        {
            const char* text = element.pointer;
            uint64_t length = element.length;
            if (length >= 2 && text[0] == '\'' && text[length - 1] == '\'')
            {
                text += 1;
                length -= 2;
            }
            result = (String8){ .pointer = (char*)check_warning_text(scratch, "'%.*s *'", (int)length, text), .length = length + 4 };
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
    const bool result = node < stream->count && check_stream_constant_kind(stream, node) == CHECK_CONSTANT_INTEGER;
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
// A parenthesized expression's comparison or value climbs its group closers first.
static bool check_common_statement_terminator(const Parser* parser, uint32_t token)
{
    bool result = false;
    uint32_t at = token;
    for (uint32_t steps = 0; at < parser->tokens.count && steps < parser->tokens.count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[at];
        if (role == PARSE_ROLE_TERMINATOR)
        {
            const uint32_t owner = parser->tree.parents[at];
            const uint8_t owner_role = owner < parser->tokens.count ? parser->tree.roles[owner] : PARSE_ROLE_NONE;
            result = owner < parser->tokens.count && (owner_role == PARSE_ROLE_OPEN_BLOCK || owner_role == PARSE_ROLE_CLOSE_BLOCK ||
                                                       owner_role == PARSE_ROLE_OPEN_BODY || owner_role == PARSE_ROLE_CLOSE_BODY ||
                                                       owner_role == PARSE_ROLE_LABEL || owner_role == PARSE_ROLE_OPEN_CASE ||
                                                       owner_role == PARSE_ROLE_CLOSE_CASE);
            break;
        }
        if (role != PARSE_ROLE_CLOSE_GROUP)
        {
            break;
        }
        at = parser->tree.parents[at];
    }
    return result;
}

static bool check_common_effect_role(uint8_t role)
{
    const bool result = role == PARSE_ROLE_OPERAND || role == PARSE_ROLE_ADDITIVE || role == PARSE_ROLE_MULTIPLICATIVE ||
                        role == PARSE_ROLE_SHIFT || role == PARSE_ROLE_RELATIONAL || role == PARSE_ROLE_EQUALITY ||
                        role == PARSE_ROLE_BITWISE_AND || role == PARSE_ROLE_BITWISE_OR || role == PARSE_ROLE_BITWISE_XOR ||
                        role == PARSE_ROLE_LOGICAL_AND || role == PARSE_ROLE_LOGICAL_OR || role == PARSE_ROLE_PREFIX ||
                        role == PARSE_ROLE_POSTFIX || role == PARSE_ROLE_CLOSE_CAST;
    return result;
}

// The parent of `token` is the operator whose right operand holds it; a parenthesized operand's parent is a group.
static uint32_t check_common_opener(const Parser* parser, uint32_t closer);

// Whether `token` writes the condition clause of a for, whose value the loop reads.
static bool check_common_for_condition(const Parser* parser, uint32_t token, uint32_t parent)
{
    const uint32_t close = parser->tree.roles[parent] == PARSE_ROLE_CLOSE_FOR ? parent :
                           parent < parser->tokens.count && parser->tree.roles[parent] == PARSE_ROLE_TERMINATOR &&
                           parser->tree.parents[parent] < parser->tokens.count &&
                           parser->tree.roles[parser->tree.parents[parent]] == PARSE_ROLE_CLOSE_FOR ? parser->tree.parents[parent] : BUSTER_CHECK_NONE;
    bool result = false;
    if (close < parser->tokens.count)
    {
        const uint32_t opener = check_common_opener(parser, close);
        uint32_t semicolons = 0;
        for (uint32_t at = opener != BUSTER_CHECK_NONE ? opener + 1 : 0; at < token; at += 1)
        {
            semicolons += parser->tokens.kinds[at] == PARSE_KIND_SEMI;
        }
        result = semicolons == 1;
    }
    return result;
}

static uint32_t check_common_parent(const Parser* parser, uint32_t token)
{
    const uint32_t result = token < parser->tokens.count ? parser->tree.parents[token] : BUSTER_CHECK_NONE;
    return result;
}

// The closing brace of the function around `token`: Clang defers an unused result and an unused-but-set parameter to
// the end of the function, after the body's own warnings. A token outside a body keeps its own place.
static uint32_t check_common_function_end(const Parser* parser, uint32_t token)
{
    uint32_t result = token;
    uint32_t at = token;
    for (uint32_t steps = 0; at < parser->tokens.count && steps < parser->tokens.count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[at];
        if (role == PARSE_ROLE_OPEN_BODY)
        {
            result = parser->tree.parents[at];
        }
        else if (role == PARSE_ROLE_CLOSE_BODY)
        {
            result = at;
        }
        const uint32_t parent = parser->tree.parents[at];
        if (parent == BUSTER_CHECK_NONE || parent >= parser->tokens.count || parent == at)
        {
            break;
        }
        at = parent;
    }
    return result;
}

// The first token of the subtree rooted at `root`: the left operand of an unparenthesized comma has no group
// opener, so its start is the leftmost token whose parent chain reaches the operand's root.
static uint32_t check_common_subtree_start(const Parser* parser, uint32_t root)
{
    uint32_t result = root;
    bool found = false;
    for (uint32_t at = root; at-- > 0;)
    {
        uint32_t node = at;
        bool descendant = false;
        for (uint32_t steps = 0; node < parser->tokens.count && steps < parser->tokens.count; steps += 1)
        {
            if (node == root)
            {
                descendant = true;
                break;
            }
            const uint32_t parent = parser->tree.parents[node];
            if (parent == BUSTER_CHECK_NONE || parent >= parser->tokens.count || parent == node)
            {
                break;
            }
            node = parent;
        }
        if (descendant)
        {
            result = at;
            found = true;
        }
        else if (found)
        {
            break;
        }
    }
    return result;
}

static void check_common_report_unused(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t id,
                                       const char* message, const CheckCommonNote* notes, uint32_t note_count, Arena* scratch)
{
    check_common_report_at(parser, pp, input, token, check_common_function_end(parser, token), id, false, message, notes, note_count, scratch);
}

// A closer's opener is the token whose parent it is; the closer's own parent is the enclosing construct.
static uint32_t check_common_cast_opener(const Parser* parser, uint32_t closer)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t at = closer; closer < parser->tokens.count && at-- > 0;)
    {
        if (parser->tree.roles[at] == PARSE_ROLE_OPEN_CAST && parser->tree.parents[at] == closer)
        {
            result = at;
            break;
        }
    }
    return result;
}

static uint32_t check_common_group_opener(const Parser* parser, uint32_t closer)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t at = closer; closer < parser->tokens.count && at-- > 0;)
    {
        if (parser->tree.roles[at] == PARSE_ROLE_OPEN_GROUP && parser->tree.parents[at] == closer)
        {
            result = at;
            break;
        }
    }
    return result;
}

static uint32_t check_common_opener(const Parser* parser, uint32_t closer)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t at = closer; closer < parser->tokens.count && at != 0 && result == BUSTER_CHECK_NONE;)
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

// Whether an expression range calls, assigns, or increments: a comma operand with one has an effect.
static bool check_common_expression_has_effect(const Parser* parser, uint32_t first, uint32_t end)
{
    bool effect = false;
    for (uint32_t at = first; at < end && at < parser->tokens.count; at += 1)
    {
        const uint8_t role = parser->tree.roles[at];
        const uint8_t kind = parser->tokens.kinds[at];
        effect = role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_OPEN_CALL || role == PARSE_ROLE_CLOSE_CALL ||
                 kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS || kind == PARSE_KIND_EQUAL;
        if (effect)
        {
            break;
        }
    }
    return effect;
}

static bool check_common_void_cast(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                   const TypeTable* types, uint32_t token)
{
    const uint32_t node = input->token_nodes != NULL && token < input->token_count ? input->token_nodes[token] : BUSTER_CHECK_NONE;
    const bool typed = node < input->graph.count && input->graph.ops[node] == CHECK_OP_CAST &&
                       type_kind(types, type_unqualified(type_strip_aligned(types, check_stream_type(stream, node)))) == BUSTER_TYPE_KIND_VOID;
    const bool spelled = token > 0 && token < parser->tokens.count && parser->tokens.kinds[token - 1] == PARSE_KIND_TYPE &&
                         intern_length(pp->table, parser->tokens.names[token - 1]) == 4 &&
                         memcmp(intern_bytes(pp->table, parser->tokens.names[token - 1]), "void", 4) == 0;
    return typed || spelled;
}

static void check_common_parentheses(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                     Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    for (uint32_t token = 0; token < count; token += 1)
    {
        const uint8_t role = parser->tree.roles[token];
        const uint32_t parent = check_common_parent(parser, token);
        const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
        const bool for_condition = role == PARSE_ROLE_ASSIGN && check_common_for_condition(parser, token, parent);
        if (role == PARSE_ROLE_ASSIGN && (parent_role == PARSE_ROLE_CLOSE_CONDITION || for_condition) && parser->tokens.kinds[token] == PARSE_KIND_EQUAL)
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
                const bool direct = parent < parser->tokens.count && parser->tree.roles[parent] == PARSE_ROLE_TERMINATOR;
                if (!direct)
                {
                    check_common_report_unused(parser, pp, input, token, CHECK_WARNING_ID_UNUSED_VALUE, "expression result unused", NULL, 0, scratch);
                }
                else
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
        }
        else if ((role == PARSE_ROLE_SEPARATOR || role == PARSE_ROLE_COMMA) && parser->tokens.kinds[token] == PARSE_KIND_COMMA &&
                 check_common_statement_terminator(parser, parent))
        {
            const uint32_t opener = check_common_group_opener(parser, parent);
            bool nested = false;
            for (uint32_t at = token + 1; opener != BUSTER_CHECK_NONE && at < parent; at += 1)
            {
                nested = parser->tokens.kinds[at] == PARSE_KIND_COMMA;
                if (nested)
                {
                    break;
                }
            }
            uint32_t left = token > 0 ? token - 1 : token;
            if (opener != BUSTER_CHECK_NONE && opener + 1 < token)
            {
                left = opener + 1;
            }
            else
            {
                uint32_t root = left;
                for (uint32_t steps = 0; steps < parser->tokens.count; steps += 1)
                {
                    const uint32_t up = parser->tree.parents[root];
                    if (up == BUSTER_CHECK_NONE || up >= token || up == root)
                    {
                        break;
                    }
                    root = up;
                }
                left = check_common_subtree_start(parser, root);
            }
            const uint32_t right = token + 1 < parent ? token + 1 : token;
            if (!nested && left < token && right < parent)
            {
                if (!check_common_expression_has_effect(parser, left, token))
                {
                    check_common_report_unused(parser, pp, input, left, CHECK_WARNING_ID_UNUSED_VALUE,
                                               "left operand of comma operator has no effect", NULL, 0, scratch);
                }
                if (!check_common_expression_has_effect(parser, right, parent))
                {
                    check_common_report_unused(parser, pp, input, right, CHECK_WARNING_ID_UNUSED_VALUE,
                                               "expression result unused", NULL, 0, scratch);
                }
            }
        }
        else if (role == PARSE_ROLE_OPEN_TERNARY && parent_role == PARSE_ROLE_CLOSE_TERNARY &&
                 check_common_statement_terminator(parser, parser->tree.parents[parent]))
        {
            const uint32_t last = parent + 1 < parser->tokens.count ? parent + 1 : token;
            check_common_report_unused(parser, pp, input, last, CHECK_WARNING_ID_UNUSED_VALUE, "expression result unused", NULL, 0, scratch);
        }
        else if (check_common_statement_terminator(parser, parent) && check_common_effect_role(role) &&
                 role != PARSE_ROLE_ASSIGN && role != PARSE_ROLE_EQUALITY && role != PARSE_ROLE_RELATIONAL &&
                 !(role == PARSE_ROLE_CLOSE_CAST && check_common_void_cast(parser, pp, input, stream, types, token)) &&
                 parser->tokens.kinds[token] != PARSE_KIND_L_PAREN &&
                 parser->tokens.kinds[token] != PARSE_KIND_PLUSPLUS && parser->tokens.kinds[token] != PARSE_KIND_MINUSMINUS)
        {
            const uint32_t report = role == PARSE_ROLE_CLOSE_CAST ? check_common_cast_opener(parser, token) : token;
            check_common_report_unused(parser, pp, input, report != BUSTER_CHECK_NONE ? report : token, CHECK_WARNING_ID_UNUSED_VALUE,
                                       "expression result unused", NULL, 0, scratch);
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
            const bool has_else = kind == PARSE_KIND_IF && token + 1 < parser->tokens.count &&
                                  parser->tokens.kinds[token + 1] == PARSE_KIND_ELSE && parser->tree.parents[token + 1] == parent;
            if (subject != NULL && !has_else)
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
    if (parser->tokens.count == 0 || else_at == NULL)
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
            role == PARSE_ROLE_OPEN_INITIALIZER || role == PARSE_ROLE_ASSIGN ||
            (role == PARSE_ROLE_PREFIX && parser->tokens.kinds[at] == PARSE_KIND_STAR))
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

static void check_common_return_stack(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                      TypeTable* types, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        const CheckOp op = graph->ops[node];
        const uint32_t operand = op == CHECK_OP_ADDRESS || op == CHECK_OP_CONVERT ? graph->operand0[node] : BUSTER_CHECK_NONE;
        const bool arrays = op == CHECK_OP_CONVERT && (stream->conversions[node].operands[0].flags & BUSTER_CHECK_CONVERT_ARRAY) != 0;
        if ((op == CHECK_OP_ADDRESS || arrays) && operand < graph->count)
        {
            const uint32_t root = op == CHECK_OP_ADDRESS ? check_common_address_root(graph, stream, types, operand, true) :
                                  check_common_address_root(graph, stream, types, operand, false);
            CheckValue root_value = root < graph->count ? check_stream_value(stream, root) : (CheckValue){ 0 };
            const CheckValue* value = root < graph->count ? &root_value : NULL;
            const uint32_t declaration = value != NULL && value->object != 0 ? value->object - 1 : BUSTER_CHECK_NONE;
            const bool local = value != NULL && value->category == CHECK_LVALUE && declaration < input->token_count &&
                               input->declaration_bodies[declaration] < parser->tokens.count && !input->static_objects[declaration] &&
                               !input->thread_local[declaration];
            if (local && check_common_in_return(parser, graph->tokens[root]))
            {
                const uint32_t name = parser->tokens.names[declaration];
                const char* subject = check_common_parameter_token(input, parser, declaration) ? "parameter" : "local variable";
                const char* message = check_warning_text(scratch, "address of stack memory associated with %s '%.*s' returned", subject,
                                                          (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                check_common_report(parser, pp, input, graph->tokens[root], CHECK_WARNING_ID_RETURN_STACK_ADDRESS, false, message, NULL, 0, scratch);
            }
        }
    }
}

static void check_common_graph_warnings(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                        TypeTable* types, CheckWarningSpelling* spell, Arena* scratch)
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
            if (!ice && check_common_integer_constant(stream, divisor) && check_stream_value(stream, divisor).integer == 0)
            {
                uint32_t width = 0;
                bool sign = false;
                if (check_common_integer(types, check_stream_type(stream, graph->operand0[node]), &width, &sign))
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
                const CheckValue value_storage = check_stream_value(stream, source);
                const CheckValue* value = &value_storage;
                const TypeId target = check_stream_type(stream, node);
                __int128 converted = 0;
                if (check_common_conversion_changes(types, value->type, target, value, &converted))
                {
                    const String8 source_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, value->type, 0);
                    const String8 target_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, target, 0);
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
                        const char* message = check_warning_text(scratch, "implicit conversion from %.*s to %.*s changes value from %s to %s",
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
                const CheckValue left_value = left < graph->count ? check_stream_value(stream, left) : (CheckValue){ 0 };
                const CheckValue right_value = right < graph->count ? check_stream_value(stream, right) : (CheckValue){ 0 };
                const bool volatile_object = left < graph->count &&
                    (type_qualifiers(type_strip_aligned(types, left_value.type)) & BUSTER_TYPE_VOLATILE) != 0;
                if (!volatile_object && left < graph->count && right < graph->count && left_value.category == CHECK_LVALUE &&
                    right_value.category == CHECK_LVALUE && left_value.object != 0 && left_value.object == right_value.object &&
                    left_value.offset == 0 && right_value.offset == 0)
                {
                    const String8 type_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, left_value.type, 0);
                    if (type_name.length != 0)
                    {
                        const char* message = check_warning_text(scratch, "explicitly assigning value of variable of type %.*s to itself",
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
            const uint32_t root = check_common_address_root(graph, stream, types, array, false);
            if (array < graph->count && root < graph->count && check_common_integer_constant(stream, index))
            {
                const TypeId array_type = check_stream_type(stream, array);
                TypeId type = type_unqualified(type_strip_aligned(types, array_type));
                if (type_kind(types, type) == BUSTER_TYPE_KIND_ARRAY && types->types.extent[type_index(type)] != 0)
                {
                    const uint64_t extent = types->types.extent[type_index(type)];
                    uint32_t width = 0;
                    bool sign = false;
                    if (check_common_integer(types, check_stream_type(stream, index), &width, &sign))
                    {
                        const CheckValue index_value = check_stream_value(stream, index);
                        const __int128 value = check_common_integer_value(&index_value, width, sign);
                        const bool past = value >= 0 && (unsigned __int128)value >= extent;
                        const bool before = value < 0;
                        if (past || before)
                        {
                            const String8 array_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, array_type, 0);
                            char number[48];
                            check_common_number(number, sizeof(number), value);
                            const char* message = past && array_name.length != 0 ?
                                check_warning_text(scratch, "array index %s is past the end of the array (that has type %.*s)", number,
                                                   (int)array_name.length, array_name.pointer) :
                                array_name.length != 0 ? check_warning_text(scratch, "array index %s is before the beginning of the array", number) : NULL;
                            CheckCommonNote note = { 0 };
                            const uint32_t root_object = check_stream_object(stream, root);
                            const uint32_t declaration = root_object != 0 ? root_object - 1 : BUSTER_CHECK_NONE;
                            const uint32_t name = declaration < parser->tokens.count ? parser->tokens.names[declaration] : 0;
                            if (declaration < parser->tokens.count && name != 0)
                            {
                                note.token = check_common_declaration_start(parser, declaration);
                                note.message = check_warning_text(scratch, "array '%.*s' declared here",
                                                                  (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                            }
                            if (message != NULL)
                            {
                                check_common_report(parser, pp, input, graph->tokens[root], CHECK_WARNING_ID_ARRAY_BOUNDS, false, message,
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
                           check_common_integer(types, check_stream_type(stream, left), &left_width, &left_signed) &&
                           check_common_integer(types, check_stream_type(stream, right), &right_width, &right_signed);
            bool constant_suppressed = false;
            if (integer && left_signed != right_signed)
            {
                const uint32_t signed_operand = left_signed ? left : right;
                const uint32_t unsigned_operand = left_signed ? right : left;
                const uint32_t signed_width = left_signed ? left_width : right_width;
                const uint32_t unsigned_width = left_signed ? right_width : left_width;
                if (check_common_integer_constant(stream, signed_operand) && unsigned_width != 0)
                {
                    const CheckValue signed_value = check_stream_value(stream, signed_operand);
                    const __int128 constant = check_common_integer_value(&signed_value, signed_width, true);
                    constant_suppressed = constant >= 0 && (unsigned __int128)constant <= check_action_mask(unsigned_width);
                }
                else if ((op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL) && check_common_integer_constant(stream, unsigned_operand) && signed_width != 0)
                {
                    const CheckValue unsigned_value = check_stream_value(stream, unsigned_operand);
                    const __int128 constant = check_common_integer_value(&unsigned_value, unsigned_width, false);
                    constant_suppressed = constant >= 0 && (unsigned __int128)constant <= check_action_mask(signed_width);
                }
                else if (op >= CHECK_OP_LESS && op <= CHECK_OP_GREATER_EQUAL && check_common_integer_constant(stream, unsigned_operand) &&
                         unsigned_width != 0)
                {
                    // Clang canonicalizes the signed operand to the left; a zero unsigned constant leaves the result
                    // unchanged for `<` and `>=`, whose truth is already the same for every signed value.
                    const CheckValue unsigned_value = check_stream_value(stream, unsigned_operand);
                    const __int128 constant = check_common_integer_value(&unsigned_value, unsigned_width, false);
                    CheckOp normalized = op;
                    if (right_signed)
                    {
                        normalized = op == CHECK_OP_LESS ? CHECK_OP_GREATER : op == CHECK_OP_LESS_EQUAL ? CHECK_OP_GREATER_EQUAL :
                                     op == CHECK_OP_GREATER ? CHECK_OP_LESS : CHECK_OP_LESS_EQUAL;
                    }
                    constant_suppressed = constant == 0 && (normalized == CHECK_OP_LESS || normalized == CHECK_OP_GREATER_EQUAL);
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
                const String8 left_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, check_stream_type(stream, left), 0);
                const String8 right_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, check_stream_type(stream, right), 0);
                if (left_name.length != 0 && right_name.length != 0)
                {
                    const char* message = check_warning_text(scratch, "comparison of integers of different signs: %.*s and %.*s",
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

// Whether an integer argument has the class a length modifier names: an `int`-or-narrower value promoted for the
// unspecified and h/hh lengths, or the long or long long class (signed or unsigned) for the wider lengths. The two
// 64-bit classes share a width on x86-64, so the exact class, not the width, distinguishes them.
static bool check_common_format_integer_class(TypeTable* types, TypeId type, char length_modifier, bool pointer)
{
    uint32_t width = 0;
    bool sign = false;
    const TypeId stripped = type_unqualified(type_strip_aligned(types, type));
    bool result = check_action_integer_type(types, type, &width, &sign);
    if (result)
    {
        switch (length_modifier)
        {
            case 'H': result = pointer ? width == 8 : width <= 32; break;
            case 'h': result = pointer ? width == 16 : width <= 32; break;
            case 'l':
            case 'z':
            case 'j':
            case 't': result = stripped == BUSTER_TYPE_LONG || stripped == BUSTER_TYPE_ULONG; break;
            case 'L': result = stripped == BUSTER_TYPE_LLONG || stripped == BUSTER_TYPE_ULLONG; break;
            default: result = pointer ? width == 32 : width <= 32; break;
        }
    }
    return result;
}

// A pointer to an integer of the class a `%n` conversion names.
static bool check_common_format_integer_pointer(TypeTable* types, TypeId type, char length_modifier)
{
    TypeId element = type_unqualified(type_strip_aligned(types, type));
    if (type_kind(types, element) != BUSTER_TYPE_KIND_POINTER)
    {
        return false;
    }
    return check_common_format_integer_class(types, type_element(types, element), length_modifier, true);
}

static bool check_common_format_width(TypeTable* types, TypeId type)
{
    uint32_t width = 0;
    bool sign = false;
    const bool result = check_action_integer_type(types, type, &width, &sign) && width <= 32;
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

static bool check_common_format_char_pointer(TypeTable* types, TypeId type, bool wide)
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
    uint32_t width = 0;
    bool sign = false;
    const bool integer = check_action_integer_type(types, element, &width, &sign);
    const bool result = integer && (wide ? width == 32 && sign : width == 8);
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

static const char* check_common_format_integer_expected(uint8_t conversion, char length_modifier)
{
    if (conversion == 'c' || conversion == 'C')
    {
        return length_modifier == 'l' || conversion == 'C' ? "'wint_t' (aka 'unsigned int')" : "'int'";
    }
    // `%b` and `%B` take any 32-bit-or-narrower integer but name the signed type, as Clang's message does.
    const bool unsigned_conversion = conversion == 'u' || conversion == 'o' || conversion == 'x' || conversion == 'X';
    switch (length_modifier)
    {
        case 'H': return unsigned_conversion ? "'unsigned char'" : "'char'";
        case 'h': return unsigned_conversion ? "'unsigned short'" : "'short'";
        case 'l': return unsigned_conversion ? "'unsigned long'" : "'long'";
        case 'L': return unsigned_conversion ? "'unsigned long long'" : "'long long'";
        case 'z': return unsigned_conversion ? "'size_t' (aka 'unsigned long')" : "'signed size_t' (aka 'long')";
        case 'j': return unsigned_conversion ? "'uintmax_t' (aka 'unsigned long')" : "'intmax_t' (aka 'long')";
        case 't': return unsigned_conversion ? "'unsigned ptrdiff_t' (aka 'unsigned long')" : "'ptrdiff_t' (aka 'long')";
        default: return unsigned_conversion ? "'unsigned int'" : "'int'";
    }
}

static const char* check_common_format_integer_pointer_expected(char length_modifier)
{
    switch (length_modifier)
    {
        case 'H': return "'signed char *'";
        case 'h': return "'short *'";
        case 'l': return "'long *'";
        case 'L': return "'long long *'";
        case 'z': return "'signed size_t *' (aka 'long *')";
        case 'j': return "'intmax_t *' (aka 'long *')";
        case 't': return "'ptrdiff_t *' (aka 'long *')";
        default: return "'int *'";
    }
}

// The first token of an argument expression: back over nested parentheses to a cast or group opener, or to a call's
// callee.
static uint32_t check_common_argument_start(const Parser* parser, uint32_t token)
{
    uint32_t result = token;
    uint32_t depth = 0;
    for (uint32_t at = token; at < parser->tokens.count && at-- > 0;)
    {
        const uint8_t kind = parser->tokens.kinds[at];
        if (kind == PARSE_KIND_R_PAREN)
        {
            depth += 1;
        }
        else if (kind == PARSE_KIND_L_PAREN)
        {
            if (depth == 0)
            {
                result = (parser->tree.roles[at] == PARSE_ROLE_OPEN_CALL || parser->tree.roles[at] == PARSE_ROLE_OPEN_FUNCTION) && at > 0 ? at - 1 : at;
                break;
            }
            depth -= 1;
        }
        else if (depth == 0 && kind == PARSE_KIND_COMMA)
        {
            break;
        }
        result = at;
    }
    return result;
}

// A format diagnostic inside a literal: `offset` counts the literal's value from its opening quote, as Clang's
// column does. The policy and suffix are the token's.
static void check_common_format_report_offset(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t format_token,
                                              uint64_t offset, uint32_t id, const char* message, Arena* scratch)
{
    const uint32_t source = parser->tokens.sources[format_token];
    if (source >= pp->output.count || pp->output.locations == NULL || pp->output.locations[source] == 0)
    {
        return;
    }
    const CheckWarningSeverity severity = check_common_severity(pp, parser, input, format_token, id, false);
    if (severity != CHECK_WARNING_IGNORE)
    {
        const char* option = id < BUSTER_ARRAY_LENGTH(check_warning_descriptions) ? check_warning_descriptions[id].option : NULL;
        const bool error = severity >= CHECK_WARNING_ERROR;
        const char* suffix = option != NULL ? check_warning_text(scratch, " [%s%s]", error ? "-Werror," : "", option) : error ? " [-Werror]" : "";
        const PPSeverity disposition = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                       error ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
        if (suffix != NULL)
        {
            // A raw source location keeps the literal's own column, while diagnostic_position keeps the collector's
            // order with the token's other diagnostics.
            const uint32_t saved = pp->diagnostic_position;
            pp->diagnostic_position = source;
            pp_diagnose(pp, disposition, pp->output.locations[source] + 1 + (uint32_t)offset, "%s%s", message, suffix);
            pp->diagnostic_position = saved;
        }
    }
}

// Parses `N$` at `*at`, leaving it past the `$`; otherwise leaves it where it was.
static bool check_common_format_position(const uint8_t* bytes, uint64_t length, uint64_t* at, uint32_t* position)
{
    uint64_t cursor = *at;
    uint64_t value = 0;
    while (cursor < length && bytes[cursor] >= '0' && bytes[cursor] <= '9')
    {
        value = value * 10 + (uint64_t)(bytes[cursor] - '0');
        value = value > UINT32_MAX ? UINT32_MAX : value;
        cursor += 1;
    }
    const bool result = cursor < length && bytes[cursor] == '$';
    if (result)
    {
        *at = cursor + 1;
        *position = (uint32_t)value;
    }
    return result;
}

// Marks a positional conversion's data argument used, or reports its position as invalid.
static bool check_common_format_data_position(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t format_token,
                                              uint64_t percent_offset, uint64_t conversion_offset, uint32_t position, uint32_t data_count,
                                              uint8_t* used, Arena* scratch)
{
    const bool result = position != 0 && position <= data_count;
    if (position == 0)
    {
        check_common_format_report_offset(parser, pp, input, format_token, percent_offset, CHECK_WARNING_ID_FORMAT,
                                          "position arguments in format strings start counting at 1 (not 0)", scratch);
    }
    else if (position > data_count)
    {
        const char* message = check_warning_text(scratch, "data argument position '%u' exceeds the number of data arguments (%u)",
                                                 position, data_count);
        check_common_format_report_offset(parser, pp, input, format_token, conversion_offset, CHECK_WARNING_ID_FORMAT, message, scratch);
    }
    if (result && used != NULL)
    {
        used[position - 1] = 1;
    }
    return result;
}

static void check_common_format_missing_width(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t format_token,
                                              uint64_t offset, bool precision, Arena* scratch)
{
    const char* message = precision ? "'.*' specified field precision is missing a matching 'int' argument" :
                                      "'*' specified field width is missing a matching 'int' argument";
    check_common_format_report_offset(parser, pp, input, format_token, offset, CHECK_WARNING_ID_FORMAT, message, scratch);
}

static void check_common_format_invalid_position(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t format_token,
                                                 uint64_t offset, bool precision, Arena* scratch)
{
    const char* message = precision ? "invalid position specified for field precision" : "invalid position specified for field width";
    check_common_format_report_offset(parser, pp, input, format_token, offset, CHECK_WARNING_ID_FORMAT, message, scratch);
}

static void check_common_format_report_type(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                            TypeTable* types, CheckWarningSpelling* spell, const CheckGraph* graph, uint32_t offset, uint32_t argument,
                                            uint32_t format_token, const char* expected, bool width, Arena* scratch)
{
    const uint32_t argument_node = graph->argument_nodes[offset + argument];
    const TypeId type = argument_node < graph->count ? check_stream_type(stream, argument_node) : 0;
    const String8 actual = check_common_type_text(parser, pp, input, types, spell, scratch, type);
    const uint32_t at_token = argument_node < graph->count ? check_common_argument_start(parser, graph->tokens[argument_node]) : format_token;
    if (actual.length != 0)
    {
        const char* message = width ? check_warning_text(scratch, "field width should have type 'int', but argument has type '%.*s'",
                                                          (int)actual.length, actual.pointer) :
                                     check_warning_text(scratch, "format specifies type %s but the argument has type %.*s",
                                                        expected, (int)actual.length, actual.pointer);
        if (message != NULL)
        {
            check_common_report(parser, pp, input, at_token, CHECK_WARNING_ID_FORMAT, false, message, NULL, 0, scratch);
        }
    }
}

static void check_common_format_width_report(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                             TypeTable* types, CheckWarningSpelling* spell, const CheckGraph* graph, uint32_t offset, uint32_t argument,
                                             uint32_t format_token, uint64_t literal_offset, bool precision, Arena* scratch)
{
    const uint32_t argument_node = graph->argument_nodes[offset + argument];
    const TypeId type = argument_node < graph->count ? check_stream_type(stream, argument_node) : 0;
    const String8 actual = check_common_type_text(parser, pp, input, types, spell, scratch, type);
    if (actual.length != 0)
    {
        const char* message = check_warning_text(scratch, "field %s should have type 'int', but argument has type %.*s",
                                                  precision ? "precision" : "width", (int)actual.length, actual.pointer);
        if (message != NULL)
        {
            check_common_format_report_offset(parser, pp, input, format_token, literal_offset, CHECK_WARNING_ID_FORMAT, message, scratch);
        }
    }
}

static void check_common_format(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                TypeTable* types, CheckWarningSpelling* spell, const CheckProgram* program, Arena* scratch)
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
        const uint32_t callee_object = callee < graph->count ? check_stream_object(stream, callee) : 0;
        if (format_index == UINT32_MAX && program != NULL && callee_object != 0 && callee_object <= program->object_count)
        {
            const uint32_t definition = program->object_declarations[callee_object];
            bool found = definition < input->token_count && input->attributes != NULL &&
                         check_common_format_attribute(input->attributes[definition], &format_index, &first_argument);
            const uint32_t canonical = definition < input->token_count && input->declarations != NULL ? input->declarations[definition] : BUSTER_CHECK_NONE;
            found = found || (canonical < input->token_count && input->attributes != NULL &&
                              check_common_format_attribute(input->attributes[canonical], &format_index, &first_argument));
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
                const uint32_t data_count = count - first_argument;
                uint8_t* used = data_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, data_count) : NULL;
                const bool usable = data_count == 0 || used != NULL;
                if (used != NULL)
                {
                    memset(used, 0, data_count);
                }
                bool insufficient = false;
                bool positional_seen = false;
                bool plain_seen = false;
                bool malformed = false;
                uint64_t insufficient_offset = 0;
                for (uint64_t at = 0; usable && at < length && !pp->fatal; at += 1)
                {
                    if (bytes[at] != '%')
                    {
                        continue;
                    }
                    const uint64_t percent = at;
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
                    // The value's position comes before the width: `%2$*1$d` names value 2 and width 1.
                    uint32_t value_position = 0;
                    const bool value_positioned = check_common_format_position(bytes, length, &at, &value_position);
                    bool width_argument = false;
                    bool width_positioned = false;
                    uint32_t width_position = 0;
                    uint64_t width_offset = at;
                    if (at < length && bytes[at] == '*')
                    {
                        width_argument = true;
                        width_offset = at;
                        at += 1;
                        width_positioned = check_common_format_position(bytes, length, &at, &width_position);
                    }
                    else
                    {
                        while (at < length && bytes[at] >= '0' && bytes[at] <= '9')
                        {
                            at += 1;
                        }
                    }
                    bool precision_argument = false;
                    bool precision_positioned = false;
                    uint32_t precision_position = 0;
                    uint64_t precision_offset = at;
                    if (at < length && bytes[at] == '.')
                    {
                        precision_offset = at;
                        at += 1;
                        if (at < length && bytes[at] == '*')
                        {
                            precision_argument = true;
                            at += 1;
                            precision_positioned = check_common_format_position(bytes, length, &at, &precision_position);
                        }
                        else
                        {
                            while (at < length && bytes[at] >= '0' && bytes[at] <= '9')
                            {
                                at += 1;
                            }
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
                                                    conversion == 'x' || conversion == 'X' || conversion == 'b' || conversion == 'B' ||
                                                    conversion == 'c' || conversion == 'C';
                    const bool conversion_string = conversion == 's' || conversion == 'S';
                    const bool wide = length_modifier == 'l' || conversion == 'S' || conversion == 'C';
                    const bool conversion_pointer = conversion == 'p';
                    const bool conversion_float = conversion == 'f' || conversion == 'F' || conversion == 'e' || conversion == 'E' ||
                                                  conversion == 'g' || conversion == 'G' || conversion == 'a' || conversion == 'A';
                    const bool conversion_number = conversion == 'n';
                    const bool conversion_argument = conversion_integer || conversion_string || conversion_pointer || conversion_float || conversion_number;
                    positional_seen |= value_positioned || width_positioned || precision_positioned;
                    plain_seen |= (width_argument && !width_positioned) || (precision_argument && !precision_positioned);
                    // Resolve the width and precision first: they take their data arguments before the value.
                    if (width_argument)
                    {
                        uint32_t width_index = first_argument;
                        if (width_positioned)
                        {
                            if (width_position == 0 || width_position > data_count)
                            {
                                check_common_format_missing_width(parser, pp, input, format_token, width_offset, false, scratch);
                                malformed = true;
                            }
                            else
                            {
                                width_index = first_argument + width_position - 1;
                                used[width_position - 1] = 1;
                            }
                        }
                        else if (positional_seen)
                        {
                            check_common_format_invalid_position(parser, pp, input, format_token, width_offset, false, scratch);
                            malformed = true;
                        }
                        else if (argument >= count)
                        {
                            check_common_format_missing_width(parser, pp, input, format_token, width_offset, false, scratch);
                            malformed = true;
                        }
                        else
                        {
                            width_index = argument++;
                        }
                        if (malformed)
                        {
                            break;
                        }
                        const uint32_t width_node = graph->argument_nodes[offset + width_index];
                        const TypeId width_type = width_node < graph->count ? check_stream_type(stream, width_node) : 0;
                        if (!check_common_format_width(types, width_type))
                        {
                            check_common_format_width_report(parser, pp, input, stream, types, spell, graph, offset, width_index, format_token, width_offset + 1, false, scratch);
                        }
                    }
                    if (precision_argument)
                    {
                        uint32_t precision_index = first_argument;
                        if (precision_positioned)
                        {
                            if (precision_position == 0 || precision_position > data_count)
                            {
                                check_common_format_missing_width(parser, pp, input, format_token, precision_offset, true, scratch);
                                malformed = true;
                            }
                            else
                            {
                                precision_index = first_argument + precision_position - 1;
                                used[precision_position - 1] = 1;
                            }
                        }
                        else if (positional_seen)
                        {
                            check_common_format_invalid_position(parser, pp, input, format_token, precision_offset + 1, true, scratch);
                            malformed = true;
                        }
                        else if (argument >= count)
                        {
                            check_common_format_missing_width(parser, pp, input, format_token, precision_offset, true, scratch);
                            malformed = true;
                        }
                        else
                        {
                            precision_index = argument++;
                        }
                        if (malformed)
                        {
                            break;
                        }
                        const uint32_t precision_node = graph->argument_nodes[offset + precision_index];
                        const TypeId precision_type = precision_node < graph->count ? check_stream_type(stream, precision_node) : 0;
                        if (!check_common_format_width(types, precision_type))
                        {
                            check_common_format_width_report(parser, pp, input, stream, types, spell, graph, offset, precision_index, format_token, precision_offset + 1, true, scratch);
                        }
                    }
                    bool value_valid = true;
                    uint32_t value_index = first_argument;
                    if (conversion_argument)
                    {
                        if (value_positioned)
                        {
                            value_valid = check_common_format_data_position(parser, pp, input, format_token, percent, at, value_position,
                                                                            data_count, used, scratch);
                            if (value_valid)
                            {
                                value_index = first_argument + value_position - 1;
                            }
                        }
                        else if (positional_seen)
                        {
                            plain_seen = true;
                        }
                        else if (argument >= count)
                        {
                            insufficient = true;
                            insufficient_offset = at;
                            break;
                        }
                        else
                        {
                            value_index = argument++;
                            plain_seen = true;
                        }
                    }
                    if (positional_seen && plain_seen)
                    {
                        check_common_format_report_offset(parser, pp, input, format_token, at, CHECK_WARNING_ID_FORMAT,
                                                          "cannot mix positional and non-positional arguments in format string", scratch);
                        malformed = true;
                        break;
                    }
                    if (!value_valid)
                    {
                        malformed = true;
                        break;
                    }
                    if (conversion_argument)
                    {
                        const uint32_t argument_node = graph->argument_nodes[offset + value_index];
                        const TypeId type = argument_node < graph->count ? check_stream_type(stream, argument_node) : 0;
                        const bool long_double = length_modifier == 'L';
                        bool accepted = false;
                        const char* expected = NULL;
                        if (conversion_integer)
                        {
                            const bool character = conversion == 'c' || conversion == 'C';
                            accepted = check_common_format_integer_class(types, type, character ? 0 : length_modifier, false);
                            expected = check_common_format_integer_expected(conversion, length_modifier);
                        }
                        else if (conversion_string)
                        {
                            accepted = check_common_format_char_pointer(types, type, wide);
                            expected = wide ? "'wchar_t *' (aka 'int *')" : "'char *'";
                        }
                        else if (conversion_pointer)
                        {
                            accepted = check_common_format_pointer(types, type);
                            expected = "'void *'";
                        }
                        else if (conversion_float)
                        {
                            accepted = check_common_format_floating(types, type, long_double);
                            expected = long_double ? "'long double'" : "'double'";
                        }
                        else
                        {
                            accepted = check_common_format_integer_pointer(types, type, length_modifier);
                            expected = check_common_format_integer_pointer_expected(length_modifier);
                        }
                        if (!accepted)
                        {
                            check_common_format_report_type(parser, pp, input, stream, types, spell, graph, offset, value_index, format_token, expected, false, scratch);
                        }
                    }
                }
                if (usable && !malformed)
                {
                    if (!positional_seen)
                    {
                        if (insufficient)
                        {
                            check_common_format_report_offset(parser, pp, input, format_token, insufficient_offset, CHECK_WARNING_ID_FORMAT_INSUFFICIENT_ARGS,
                                                              "more '%' conversions than data arguments", scratch);
                        }
                        else if (argument < count)
                        {
                            const uint32_t extra_node = graph->argument_nodes[offset + argument];
                            const uint32_t extra_token = extra_node < graph->count ? graph->tokens[extra_node] : format_token;
                            check_common_report(parser, pp, input, extra_token, CHECK_WARNING_ID_FORMAT_EXTRA_ARGS, false,
                                                "data argument not used by format string", NULL, 0, scratch);
                        }
                    }
                    else
                    {
                        for (uint32_t i = 0; i < data_count; i += 1)
                        {
                            if (used[i] == 0)
                            {
                                const uint32_t extra_node = graph->argument_nodes[offset + first_argument + i];
                                const uint32_t extra_token = extra_node < graph->count ? graph->tokens[extra_node] : format_token;
                                check_common_report(parser, pp, input, extra_token, CHECK_WARNING_ID_FORMAT_EXTRA_ARGS, false,
                                                    "data argument not used by format string", NULL, 0, scratch);
                                break;
                            }
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

// The value the unit checked for the enumerator `name` declared inside [open, close), or false where no such
// enumerator's value was retained.
static bool check_common_enum_value(const CheckInput* input, const Parser* parser, uint32_t open, uint32_t close,
                                    uint32_t name, unsigned __int128* value)
{
    bool found = false;
    for (uint32_t i = 0; !found && input->enum_values != NULL && i < input->enum_value_count; i += 1)
    {
        const uint32_t token = input->enum_values[i].token;
        found = token > open && token < close && parser->tokens.names[token] == name;
        if (found)
        {
            *value = input->enum_values[i].value;
        }
    }
    return found;
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
        const TypeId enum_type = selector_node < stream->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, selector_node))) : 0;
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
            uint32_t source_names[64];
            uint32_t source_count = 0;
            uint32_t enum_open = BUSTER_CHECK_NONE;
            uint32_t enum_close = BUSTER_CHECK_NONE;
            for (uint32_t i = 0; input->enum_definitions != NULL && i < input->enum_definition_count; i += 1)
            {
                if (type_unqualified(input->enum_definitions[i].type) == enum_type)
                {
                    enum_open = input->enum_definitions[i].open;
                    enum_close = input->enum_definitions[i].close;
                    break;
                }
            }
            if (enum_open != BUSTER_CHECK_NONE)
            {
                for (uint32_t at = enum_open + 1; at < enum_close && at < count && source_count < BUSTER_ARRAY_LENGTH(source_names); at += 1)
                {
                    const uint8_t previous = parser->tokens.kinds[at - 1];
                    if (parser->tokens.kinds[at] == PARSE_KIND_IDENTIFIER && (previous == PARSE_KIND_L_BRACE || previous == PARSE_KIND_COMMA))
                    {
                        source_names[source_count++] = parser->tokens.names[at];
                    }
                }
            }
            bool values_known = enum_open != BUSTER_CHECK_NONE && enum_close != BUSTER_CHECK_NONE;
            unsigned __int128 source_values[64];
            unsigned __int128 handled_values[64];
            for (uint32_t h = 0; values_known && h < handled_count; h += 1)
            {
                values_known = check_common_enum_value(input, parser, enum_open, enum_close, handled[h], &handled_values[h]);
            }
            for (uint32_t n = 0; values_known && n < source_count; n += 1)
            {
                values_known = check_common_enum_value(input, parser, enum_open, enum_close, source_names[n], &source_values[n]);
            }
            uint32_t missing[64];
            uint32_t missing_count = 0;
            for (uint32_t n = 0; values_known && n < source_count && missing_count < BUSTER_ARRAY_LENGTH(missing); n += 1)
            {
                bool found = false;
                for (uint32_t h = 0; h < handled_count && !found; h += 1)
                {
                    found = source_values[n] == handled_values[h];
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
static bool check_common_builtin_op(CheckOp op)
{
    const bool result = op == CHECK_OP_BUILTIN_UNARY || op == CHECK_OP_BUILTIN_OVERFLOW || op == CHECK_OP_BUILTIN_MISC ||
                        op == CHECK_OP_BUILTIN_FLOAT || op == CHECK_OP_BUILTIN_QUERY || op == CHECK_OP_BUILTIN_ATOMIC ||
                        op == CHECK_OP_BUILTIN_VECTOR || op == CHECK_OP_BUILTIN_X86 || op == CHECK_OP_BUILTIN_BIT_CAST;
    return result;
}

static void check_common_mark_written(const CheckGraph* graph, const CheckStream* stream, uint32_t node, uint8_t* addressed, uint32_t count)
{
    for (uint32_t steps = 0; node < graph->count && steps < graph->count; steps += 1)
    {
        const CheckCategory category = check_stream_category(stream, node);
        const uint32_t object = check_stream_object(stream, node);
        if (category == CHECK_LVALUE)
        {
            if (object != 0 && object - 1 < count)
            {
                addressed[object - 1] = 1;
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
// Which graph nodes a source expression evaluates: `sizeof`/`alignof` operands and the unselected arms of a constant
// `&&`, `||` or `?:` are not evaluated. Roots are the nodes no other node reads; a reverse pass carries evaluation
// through operands and arguments. A node whose value nothing reads is a source root (an expression statement, a
// return, an initializer or a control condition).
static uint8_t* check_common_evaluated(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, const Parser* parser,
                                       Arena* scratch)
{
    uint8_t* evaluated = graph->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    uint8_t* referenced = graph->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    uint8_t* result = evaluated != NULL && referenced != NULL ? evaluated : NULL;
    if (evaluated != NULL && referenced != NULL)
    {
        memset(evaluated, 0, graph->count);
        memset(referenced, 0, graph->count);
        for (uint32_t node = 0; node < graph->count; node += 1)
        {
            const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
            {
                if (operands[i] < graph->count)
                {
                    referenced[operands[i]] = 1;
                }
            }
            if (check_graph_has_arguments(graph->ops[node]) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
            {
                for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                {
                    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
                    if (argument < graph->count)
                    {
                        referenced[argument] = 1;
                    }
                }
            }
        }
        for (uint32_t node = 0; node < graph->count; node += 1)
        {
            evaluated[node] = referenced[node] == 0;
        }
        // A static selection checks every arm but evaluates only the chosen one; its other arms are otherwise
        // unreferenced roots. The controlling expression is not evaluated either.
        for (uint32_t node = 0; node < graph->count; node += 1)
        {
            if (graph->ops[node] != CHECK_OP_SELECT)
            {
                continue;
            }
            const uint32_t close = graph->tokens[node];
            uint32_t token = close;
            for (uint32_t at = close; at-- > 0;)
            {
                if (parser->tree.parents[at] == close && (parser->tokens.kinds[at] == PARSE_KIND_GENERIC ||
                                                          parser->tokens.kinds[at] == PARSE_KIND_TYPE_ARGUMENT))
                {
                    token = at;
                    break;
                }
            }
            const uint32_t selected = graph->operand0[node];
            if (close < parser->tokens.count)
            {
                for (uint32_t candidate = 0; candidate < graph->count; candidate += 1)
                {
                    if (evaluated[candidate] != 0 && candidate != selected && graph->tokens[candidate] > token &&
                        graph->tokens[candidate] < close)
                    {
                        evaluated[candidate] = 0;
                    }
                }
            }
        }
        for (uint32_t node = graph->count; node-- > 0;)
        {
            if (evaluated[node] == 0)
            {
                continue;
            }
            const CheckOp op = graph->ops[node];
            uint32_t mask = UINT32_MAX;
            if (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF)
            {
                mask = 0;
                const uint32_t operand = graph->operand0[node];
                if (operand < graph->count && (graph->ops[operand] == CHECK_OP_VLA_BOUND || graph->ops[operand] == CHECK_OP_VLA_EXTENT))
                {
                    // A variably modified type's bound is evaluated even in sizeof.
                    mask = 1;
                }
            }
            else if (op == CHECK_OP_CONDITIONAL || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR)
            {
                const uint32_t condition = graph->operand0[node];
                const CheckValue condition_value = condition < graph->count ? check_stream_value(stream, condition) : (CheckValue){ 0 };
                bool truth = false;
                if (condition < graph->count && check_action_truth(types, &condition_value, &truth))
                {
                    mask = op == CHECK_OP_CONDITIONAL ? (truth ? 3u : 5u) :
                           ((op == CHECK_OP_LOGICAL_AND && !truth) || (op == CHECK_OP_LOGICAL_OR && truth)) ? 1u : 3u;
                }
            }
            const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
            {
                if (operands[i] < graph->count && (i >= 32 || (mask & (1u << i)) != 0))
                {
                    evaluated[operands[i]] = 1;
                }
            }
            if (check_graph_has_arguments(op) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
            {
                for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                {
                    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
                    if (argument < graph->count && (i >= 32 || (mask & (1u << i)) != 0))
                    {
                        evaluated[argument] = 1;
                    }
                }
            }
        }
    }
    return result;
}

// Mark an address computation's or a void cast's operand chain as no value read. A subscript or member's own base
// computes an address; the object's value is not read until a real value use elsewhere.
static void check_common_mark_nonread(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node, uint8_t* nonread)
{
    for (uint32_t steps = 0; node < graph->count && steps < graph->count; steps += 1)
    {
        nonread[node] = 1;
        const CheckOp op = graph->ops[node];
        if (op == CHECK_OP_ARROW)
        {
            // The pointer's own value computes the member's address; only the member value is not read.
            break;
        }
        if (op == CHECK_OP_SUBSCRIPT)
        {
            const uint32_t base = graph->operand0[node];
            const TypeId base_type = base < graph->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, base))) : 0;
            if (base_type == 0 || !type_kind_is_array(type_kind(types, base_type)))
            {
                break;
            }
            node = base;
            continue;
        }
        if (op != CHECK_OP_MEMBER)
        {
            break;
        }
        node = graph->operand0[node];
    }
}

// The VALUE node at the bottom of a conversion, promotion or unary arithmetic chain.
static uint32_t check_common_value_root(const CheckGraph* graph, const CheckStream* stream, uint32_t node)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t steps = 0; node < graph->count && steps < graph->count; steps += 1)
    {
        const CheckOp op = graph->ops[node];
        if (op == CHECK_OP_VALUE)
        {
            result = node;
            break;
        }
        if (op != CHECK_OP_CONVERT && op != CHECK_OP_POSITIVE && op != CHECK_OP_NEGATIVE && op != CHECK_OP_BIT_NOT)
        {
            break;
        }
        node = graph->operand0[node];
    }
    return result;
}

// The VALUE node holding the local object an address computation starts from: through subscripts and members for
// address-of, and through subscripts alone for an array subscript's own base.
static uint32_t check_common_address_root(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node, bool members)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t steps = 0; node < graph->count && steps < graph->count; steps += 1)
    {
        const CheckOp op = graph->ops[node];
        if (op == CHECK_OP_VALUE && check_stream_category(stream, node) == CHECK_LVALUE && check_stream_object(stream, node) != 0)
        {
            result = node;
            break;
        }
        if (op == CHECK_OP_SUBSCRIPT)
        {
            // Indexing a pointer reaches memory the local pointer may not own; only an array's own elements are
            // part of its stack object.
            const uint32_t base = graph->operand0[node];
            const TypeId base_type = base < graph->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, base))) : 0;
            if (base_type == 0 || !type_kind_is_array(type_kind(types, base_type)))
            {
                break;
            }
            node = base;
            continue;
        }
        if (!members || op != CHECK_OP_MEMBER)
        {
            break;
        }
        node = graph->operand0[node];
    }
    return result;
}

static void check_common_uninitialized(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                       TypeTable* types, Arena* scratch)
{
    const uint32_t count = input->token_count;
    const CheckGraph* graph = &input->graph;
    uint32_t* first_write = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    uint8_t* addressed = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    uint32_t* first_read = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    uint32_t* first_use_read = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    uint8_t* nonread = graph->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    uint8_t* self_read = graph->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    uint8_t* parameters = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    uint8_t* statement_write = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    uint8_t* evaluated = check_common_evaluated(graph, stream, types, parser, scratch);
    bool valid = count == 0 || (first_write != NULL && addressed != NULL && first_read != NULL && first_use_read != NULL && parameters != NULL &&
                                statement_write != NULL && (graph->count == 0 || (nonread != NULL && self_read != NULL && evaluated != NULL)));
    if (valid && count != 0)
    {
        memset(first_write, 0xff, (uint64_t)count * sizeof(uint32_t));
        memset(addressed, 0, count);
        memset(first_read, 0xff, (uint64_t)count * sizeof(uint32_t));
        memset(first_use_read, 0xff, (uint64_t)count * sizeof(uint32_t));
        memset(parameters, 0, count);
        memset(statement_write, 0, count);
        if (nonread != NULL)
        {
            memset(nonread, 0, graph->count);
        }
        if (self_read != NULL)
        {
            memset(self_read, 0, graph->count);
        }
        // A declaration's initializer writes the object after its own value is evaluated. A brace initializer has
        // no expression node, so its write is before every later read; a read inside it is still its own.
        for (uint32_t declaration = 0; declaration < count; declaration += 1)
        {
            const uint32_t node = input->initializer_nodes != NULL ? input->initializer_nodes[declaration] : BUSTER_CHECK_NONE;
            if (input->declaration_kinds[declaration] == C_BINDING_OBJECT)
            {
                if (node < graph->count)
                {
                    first_write[declaration] = node + 1;
                }
                else if (input->initializer_begins != NULL && input->initializer_ends != NULL &&
                         input->initializer_begins[declaration] != BUSTER_CHECK_NONE &&
                         input->initializer_ends[declaration] != BUSTER_CHECK_NONE)
                {
                    first_write[declaration] = 0;
                }
            }
        }
        // A variably modified type's bound is evaluated even where the graph's sizeof operand is not; read it from
        // the VLA record so a bound that is not a graph root still counts.
        for (uint32_t vla_index = 0; vla_index < input->vla_count; vla_index += 1)
        {
            const CheckInputVla* vla = input->vlas + vla_index;
            if (vla->bound_node < graph->count)
            {
                uint32_t bound = vla->bound_node;
                if (graph->ops[bound] == CHECK_OP_VLA_BOUND || graph->ops[bound] == CHECK_OP_VLA_EXTENT)
                {
                    bound = graph->operand0[bound];
                }
                const uint32_t root = check_common_value_root(graph, stream, bound);
                const uint32_t object = root < graph->count ? check_stream_object(stream, root) : 0;
                const uint32_t declaration = object != 0 ? object - 1 : BUSTER_CHECK_NONE;
                if (declaration < count && check_stream_category(stream, root) == CHECK_LVALUE)
                {
                    first_read[declaration] = bound < first_read[declaration] ? bound : first_read[declaration];
                    first_use_read[declaration] = bound < first_use_read[declaration] ? bound : first_use_read[declaration];
                }
            }
            for (uint32_t token = vla->bound_begin; token < vla->bound_end && token < count; token += 1)
            {
                const uint32_t declaration = input->declarations != NULL ? input->declarations[token] : BUSTER_CHECK_NONE;
                const uint32_t order = input->token_nodes != NULL ? input->token_nodes[token] : BUSTER_CHECK_NONE;
                if (declaration < count && order < graph->count && input->declaration_kinds[declaration] == C_BINDING_OBJECT &&
                    input->declaration_bodies[declaration] < parser->tokens.count && !input->static_objects[declaration] &&
                    !input->thread_local[declaration] && input->initializer_nodes[declaration] == BUSTER_CHECK_NONE && !addressed[declaration])
                {
                    first_read[declaration] = order < first_read[declaration] ? order : first_read[declaration];
                    first_use_read[declaration] = order < first_use_read[declaration] ? order : first_use_read[declaration];
                }
            }
        }
        for (uint32_t opening = 0; opening < count; opening += 1)
        {
            for (uint32_t parameter = input->body_declarations[opening] != BUSTER_CHECK_NONE ? input->parameters[opening] : BUSTER_CHECK_NONE;
                 parameter != BUSTER_CHECK_NONE && parameter < count; parameter = input->parameter_next[parameter])
            {
                parameters[parameter] = 1;
            }
        }
        // A target of an assignment is written, not read; a compound assignment or increment reads it too. An
        // address-of's operand and a void cast's are not value reads for the warning.
        for (uint32_t node = 0; node < graph->count; node += 1)
        {
            if (evaluated[node] == 0)
            {
                continue;
            }
            const CheckOp op = graph->ops[node];
            if (op == CHECK_OP_ADDRESS)
            {
                check_common_mark_nonread(graph, stream, types, graph->operand0[node], nonread);
                const uint32_t operand = check_common_address_root(graph, stream, types, graph->operand0[node], true);
                const uint32_t operand_object = operand < graph->count ? check_stream_object(stream, operand) : 0;
                if (operand_object != 0 && operand_object - 1 < count)
                {
                    addressed[operand_object - 1] = 1;
                }
            }
            else if (op == CHECK_OP_CAST && type_kind(types, type_unqualified(type_strip_aligned(types, check_stream_type(stream, node)))) == BUSTER_TYPE_KIND_VOID)
            {
                check_common_mark_nonread(graph, stream, types, graph->operand0[node], nonread);
            }
            else if (op == CHECK_OP_ASSIGN)
            {
                check_common_mark_nonread(graph, stream, types, graph->operand0[node], nonread);
            }
            else if (check_common_write_op(op))
            {
                // A compound assignment or an increment reads its target and writes it; the read is not an outside
                // use for -Wunused-but-set-parameter.
                const uint32_t target = graph->operand0[node];
                if (target < graph->count)
                {
                    self_read[target] = 1;
                }
            }
            const bool builtin = check_common_builtin_op(op);
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
        // Graph order is evaluation order: an assignment's right hand side is read before its target is written. A
        // compound assignment's write is one past its read, so an uninitialized read-modify-write still warns.
        for (uint32_t node = 0; node < graph->count; node += 1)
        {
            if (evaluated[node] == 0)
            {
                continue;
            }
            const CheckOp op = graph->ops[node];
            const uint32_t target = graph->operand0[node];
            const bool plain_write = op == CHECK_OP_ASSIGN;
            const bool read_write = check_common_write_op(op) && !plain_write;
            const CheckCategory target_category = target < graph->count ? check_stream_category(stream, target) : CHECK_RVALUE;
            const uint32_t target_object = target < graph->count ? check_stream_object(stream, target) : 0;
            if ((plain_write || read_write) && target < graph->count && target_category == CHECK_LVALUE &&
                target_object != 0 && target_object - 1 < count)
            {
                const uint32_t object = target_object - 1;
                const uint32_t order = read_write ? node + 1 : node;
                first_write[object] = order < first_write[object] ? order : first_write[object];
                const uint32_t token = graph->tokens[node];
                const uint32_t parent = token < parser->tokens.count ? parser->tree.parents[token] : BUSTER_CHECK_NONE;
                const uint8_t parent_role = parent < parser->tokens.count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
                if (parent < parser->tokens.count && (parent_role == PARSE_ROLE_TERMINATOR || parent_role == PARSE_ROLE_SEPARATOR ||
                                                      parent_role == PARSE_ROLE_COMMA) && !check_common_for_condition(parser, token, parent))
                {
                    statement_write[object] = 1;
                }
            }
            if (op == CHECK_OP_VALUE && nonread[node] == 0)
            {
                const uint32_t object = check_stream_object(stream, node);
                const uint32_t declaration = object != 0 ? object - 1 : BUSTER_CHECK_NONE;
                if (declaration < count && check_stream_category(stream, node) == CHECK_LVALUE)
                {
                    first_read[declaration] = node < first_read[declaration] ? node : first_read[declaration];
                    if (self_read[node] == 0)
                    {
                        first_use_read[declaration] = node < first_use_read[declaration] ? node : first_use_read[declaration];
                    }
                }
            }
        }
        for (uint32_t declaration = 0; valid && declaration < count; declaration += 1)
        {
            const bool local = input->declaration_kinds[declaration] == C_BINDING_OBJECT && input->declaration_bodies[declaration] < parser->tokens.count &&
                               !input->static_objects[declaration] && !input->thread_local[declaration];
            const bool parameter = local && parameters[declaration] != 0;
            const bool plain = local && !parameter && !addressed[declaration];
            const TypeId type = local ? input->declared_types[declaration] : 0;
            const uint8_t kind = type != 0 ? type_kind(types, type_unqualified(type_strip_aligned(types, type))) : 0;
            const bool scalar = kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_POINTER ||
                                kind == BUSTER_TYPE_KIND_ENUM || kind == BUSTER_TYPE_KIND_NULLPTR;
            const uint32_t name = parser->tokens.names[declaration];
            const uint32_t initializer_begin = input->initializer_begins != NULL ? input->initializer_begins[declaration] : BUSTER_CHECK_NONE;
            const uint32_t initializer_end = input->initializer_ends != NULL ? input->initializer_ends[declaration] : BUSTER_CHECK_NONE;
            uint32_t read_token = first_read[declaration] != BUSTER_CHECK_NONE ? graph->tokens[first_read[declaration]] : BUSTER_CHECK_NONE;
            // A lone identifier in a brace initializer (`int x = { x };`) is a self-read the graph's value column does
            // not retain; the declaration's own token range finds it. An expression initializer's reads are the
            // graph's, so only a braced range is scanned, and not through a sizeof operand.
            const bool braced = (initializer_begin < parser->tokens.count && parser->tokens.kinds[initializer_begin] == PARSE_KIND_L_BRACE) ||
                                (declaration + 2 < parser->tokens.count && parser->tree.roles[declaration + 1] == PARSE_ROLE_ASSIGN &&
                                 parser->tree.roles[declaration + 2] == PARSE_ROLE_OPEN_INITIALIZER);
            uint32_t scan_begin = initializer_begin;
            uint32_t scan_end = initializer_end;
            if (braced && (scan_begin == BUSTER_CHECK_NONE || scan_end == BUSTER_CHECK_NONE) && declaration + 2 < parser->tokens.count)
            {
                scan_begin = declaration + 2;
                const uint32_t close = parser->tree.parents[declaration + 2];
                scan_end = close < parser->tokens.count ? close + 1 : parser->tokens.count;
            }
            for (uint32_t at = scan_begin; braced && scan_end != BUSTER_CHECK_NONE &&
                 at < scan_end && at < parser->tokens.count; at += 1)
            {
                const bool member = at > 0 && (parser->tokens.kinds[at - 1] == PARSE_KIND_PERIOD ||
                                               parser->tokens.kinds[at - 1] == PARSE_KIND_ARROW);
                const uint32_t node = input->token_nodes != NULL && at < input->token_count ? input->token_nodes[at] : BUSTER_CHECK_NONE;
                bool query = false;
                for (uint32_t up = at, steps = 0; up < parser->tokens.count && steps < parser->tokens.count && !query; steps += 1)
                {
                    const uint8_t up_role = parser->tree.roles[up];
                    query = up_role == PARSE_ROLE_OPEN_SIZEOF || up_role == PARSE_ROLE_CLOSE_SIZEOF;
                    const uint32_t parent = parser->tree.parents[up];
                    if (parent == BUSTER_CHECK_NONE || parent >= parser->tokens.count || parent == up)
                    {
                        break;
                    }
                    up = parent;
                }
                // A token the graph leaves out is the brace initializer's read; one it marks non-read (an address or
                // void cast) is not, and a sizeof operand is not either.
                const bool read = node >= graph->count || nonread[node] == 0;
                if (read && !query && parser->tokens.kinds[at] == PARSE_KIND_IDENTIFIER && parser->tokens.names[at] == name && !member)
                {
                    read_token = at;
                }
            }
            const bool self_initialization = initializer_begin != BUSTER_CHECK_NONE && initializer_end != BUSTER_CHECK_NONE &&
                                             read_token != BUSTER_CHECK_NONE && read_token >= initializer_begin && read_token < initializer_end;
            const bool has_read = first_read[declaration] != BUSTER_CHECK_NONE || read_token != BUSTER_CHECK_NONE;
            if (plain && scalar && has_read &&
                (self_initialization || first_write[declaration] == BUSTER_CHECK_NONE ||
                 (first_read[declaration] != BUSTER_CHECK_NONE && first_read[declaration] < first_write[declaration])))
            {
                const char* message = self_initialization ?
                    check_warning_text(scratch, "variable '%.*s' is uninitialized when used within its own initialization",
                                       (int)intern_length(pp->table, name), intern_bytes(pp->table, name)) :
                    check_warning_text(scratch, "variable '%.*s' is uninitialized when used here",
                                       (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                const CheckCommonNote note = { declaration + 1 < parser->tokens.count ? declaration + 1 : declaration,
                    check_warning_text(scratch, "initialize the variable '%.*s' to silence this warning",
                                       (int)intern_length(pp->table, name), intern_bytes(pp->table, name)) };
                const uint32_t body = input->declaration_bodies[declaration];
                const uint32_t publication = body < parser->tokens.count ? parser->tree.parents[body] : read_token;
                check_common_report_at(parser, pp, input, read_token, publication, CHECK_WARNING_ID_UNINITIALIZED, true, message,
                                       self_initialization ? NULL : &note, self_initialization ? 0u : 1u, scratch);
            }
            else if (parameter && scalar && !addressed[declaration] && first_use_read[declaration] == BUSTER_CHECK_NONE &&
                     first_write[declaration] != BUSTER_CHECK_NONE && statement_write[declaration] != 0)
            {
                const char* message = check_warning_text(scratch, "parameter '%.*s' set but not used",
                                                          (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                const uint32_t body = input->declaration_bodies[declaration];
                const uint32_t publication = body < parser->tokens.count ? parser->tree.parents[body] : declaration;
                check_common_report_phase_at(parser, pp, input, declaration, publication, CHECK_WARNING_ID_UNUSED_BUT_SET_PARAMETER, true,
                                             PP_DIAGNOSTIC_RETURN, message, scratch);
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
    CheckWarningSpelling spell = { 0 };
    check_common_parentheses(parser, pp, input, stream, types, scratch);
    check_common_empty_bodies(parser, pp, input, scratch);
    check_common_dangling_else(parser, pp, input, scratch);
    check_common_switches(parser, pp, input, stream, types, scratch);
    check_common_graph_warnings(parser, pp, input, stream, types, &spell, scratch);
    check_common_return_stack(parser, pp, input, stream, types, scratch);
    check_common_uninitialized(parser, pp, input, stream, types, scratch);
    check_common_format(parser, pp, input, stream, types, &spell, program, scratch);
}
