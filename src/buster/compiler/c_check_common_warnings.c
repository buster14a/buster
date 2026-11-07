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

// A test's hook, in every build, as BUSTER_TEST_LANE_FAIL_AT is: the common warnings' scratch allocation of
// this index, counted from 0 across one unit's passes, fails as an exhausted arena would, so a test reaches
// every failure path (UINT32_MAX for none). check_driver_common_warnings reads it once per unit; a lane
// checks its own units, so the count is the thread's.
typedef struct CheckCommonResources CheckCommonResources;
struct CheckCommonResources
{
    uint32_t fail_at;
    uint32_t pushes;
};
static _Thread_local CheckCommonResources check_common_resources = { .fail_at = UINT32_MAX };

static void* check_common_push_array(Arena* scratch, uint64_t count, uint64_t element_bytes, uint64_t alignment)
{
    const bool failing = check_common_resources.pushes == check_common_resources.fail_at;
    check_common_resources.pushes += check_common_resources.pushes != UINT32_MAX;
    void* result = failing ? NULL : arena_push_array(scratch, count, element_bytes, alignment);
    return result;
}
#define CHECK_COMMON_PUSH_ARRAY(arena, Type, count) ((Type*)check_common_push_array((arena), (count), sizeof(Type), _Alignof(Type)))

// A pass's scratch allocation failed: a fatal error that counts as one, so the unit fails instead of
// silently publishing fewer warnings. Diagnostic collection keeps it, and its replay reports it again,
// where a bare `pp->fatal` would be cleared at publication. The first failure alone is reported.
static void check_common_exhausted(Preprocessor* pp)
{
    if (!pp->fatal)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "common warning capacity exhausted");
    }
    pp->fatal = true;
}

// A sparse token column's entry, or BUSTER_CHECK_NONE where the input left the column absent.
static uint32_t check_common_sparse_get(CheckSparseColumn column, uint32_t token)
{
    const uint32_t result = check_sparse_present(column) ? check_sparse_get(column, token) : BUSTER_CHECK_NONE;
    return result;
}

static CheckWarningSeverity check_common_severity(Preprocessor* pp, const Parser* parser, const CheckInput* input, uint32_t token, uint32_t id, bool default_ignored)
{
    const CheckWarningCommandLine command = check_warning_command_line(&pp->options, id, default_ignored, false);
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

// Whether a warning may report anywhere in the unit: its command-line mapping, or a source pragma that maps it to
// anything but ignored, as check_common_severity resolves it at a token. Where it may not, its work is skipped, as
// Clang skips an analysis whose diagnostics are all ignored.
static bool check_common_enabled(const Preprocessor* pp, const CheckInput* input, uint32_t id, bool default_ignored)
{
    const CheckWarningCommandLine command = check_warning_command_line(&pp->options, id, default_ignored, false);
    bool enabled = !command.quiet && command.severity != CHECK_WARNING_IGNORE;
    const CheckInputDiagnosticPragmas* history = input->diagnostic_pragmas;
    for (uint32_t i = 0; !command.quiet && !enabled && history != NULL && i < history->count; i += 1)
    {
        const CheckInputDiagnosticPragma* record = history->records + i;
        enabled = record->operation != CHECK_DIAGNOSTIC_IGNORED && check_warning_option(id, record->option);
    }
    return enabled;
}

static void check_common_report_published(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t publication_source,
                                          uint32_t id, bool default_ignored, const char* message, const CheckCommonNote* notes, uint32_t note_count, Arena* scratch);
static void check_common_report_published_phase(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t publication_source,
                                                PPDiagnosticPhase phase, uint32_t id, bool default_ignored, const char* message,
                                                const CheckCommonNote* notes, uint32_t note_count, Arena* scratch);

// A warning at a parser token, with its optional notes. The suffix is clang's: -Werror's promotion names the group.
static void check_common_report_at(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t publication,
                                   uint32_t id, bool default_ignored, const char* message, const CheckCommonNote* notes, uint32_t note_count, Arena* scratch)
{
    const uint32_t publication_source = publication < parser->tokens.count ? parser->tokens.sources[publication] : pp->output.count;
    check_common_report_published(parser, pp, input, token, publication_source, id, default_ignored, message, notes, note_count, scratch);
}

static void check_common_report(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t id,
                                bool default_ignored, const char* message, const CheckCommonNote* notes, uint32_t note_count, Arena* scratch)
{
    check_common_report_at(parser, pp, input, token, token, id, default_ignored, message, notes, note_count, scratch);
}

// A warning that is not ignored at its token, `severity` its policy there, published at `publication_source`.
static void check_common_publish(const Parser* parser, Preprocessor* pp, uint32_t token, uint32_t publication_source, PPDiagnosticPhase phase,
                                 uint32_t id, CheckWarningSeverity severity, const char* message, const CheckCommonNote* notes, uint32_t note_count,
                                 Arena* scratch)
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
        pp_diagnose_output_notes_phase(pp, disposition, parser->tokens.sources[token], publication_source, phase,
                                       used != 0 ? attached : NULL, used, "%s%s", message, suffix);
    }
}

// `check_common_report_at` with the publication already a source position: a diagnostic published past its
// function's `}` needs the next boundary's source, or the past-end one when the `}` is the last token.
static void check_common_report_published_phase(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t publication_source,
                                                PPDiagnosticPhase phase, uint32_t id, bool default_ignored, const char* message,
                                                const CheckCommonNote* notes, uint32_t note_count, Arena* scratch)
{
    if (token >= parser->tokens.count || pp->fatal)
    {
        return;
    }
    const CheckWarningSeverity severity = check_common_severity(pp, parser, input, token, id, default_ignored);
    if (severity != CHECK_WARNING_IGNORE)
    {
        check_common_publish(parser, pp, token, publication_source, phase, id, severity, message, notes, note_count, scratch);
    }
}

static void check_common_report_published(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t publication_source,
                                          uint32_t id, bool default_ignored, const char* message, const CheckCommonNote* notes, uint32_t note_count, Arena* scratch)
{
    check_common_report_published_phase(parser, pp, input, token, publication_source, PP_DIAGNOSTIC_SEMANTIC, id, default_ignored, message, notes,
                                        note_count, scratch);
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
        else if (!target_signed && original < 0)
        {
            // Conversion to unsigned is defined modulo the target range: clang exempts every negative constant that
            // still names a value in the target width's signed half — `-2` to unsigned char is a defined 254 — and
            // reports only one whose magnitude the signed half cannot hold, like -4294967295 to unsigned int.
            changes = target_width < 128 && original < -((__int128)1 << (target_width - 1));
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
// A parenthesized expression's comparison or value climbs its group closers first. A GNU statement expression's last
// statement is its value, discarded only where the statement expression is: its `;` climbs to the expression's `)`, as
// clang's DiagnoseUnusedExprResult goes into a StmtExpr's last expression from the statement expression's own context.
static bool check_common_statement_terminator(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    bool result = false;
    bool climbing = true;
    uint32_t at = token;
    for (uint32_t steps = 0; climbing && at < count && steps < count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[at];
        const uint32_t owner = role == PARSE_ROLE_TERMINATOR ? parser->tree.parents[at] : count;
        const uint8_t owner_role = owner < count ? parser->tree.roles[owner] : PARSE_ROLE_NONE;
        // The block that owns the `;`, by its `}`: a statement expression's where a `)` closes the group right after it.
        const uint32_t close = owner_role == PARSE_ROLE_OPEN_BLOCK ? parser->tree.parents[owner] : owner_role == PARSE_ROLE_CLOSE_BLOCK ? owner : count;
        const bool last_value = close < count && at + 1 == close && close + 1 < count && parser->tokens.kinds[close + 1] == PARSE_KIND_R_PAREN &&
                                parser->tree.roles[close + 1] == PARSE_ROLE_CLOSE_GROUP;
        if (last_value)
        {
            at = close + 1;
        }
        else if (role == PARSE_ROLE_TERMINATOR)
        {
            // A block, a label or a control statement's un-braced head (`if (c) s;`, `else s;`, `do s;`)
            // owns the `;` that ends a discarded expression statement. A for's `)` owns both its head's
            // separators and its un-braced body's `;`: only the body's comes after the `)`.
            result = owner < count && (owner_role == PARSE_ROLE_OPEN_BLOCK || owner_role == PARSE_ROLE_CLOSE_BLOCK ||
                                       owner_role == PARSE_ROLE_OPEN_BODY || owner_role == PARSE_ROLE_CLOSE_BODY ||
                                       owner_role == PARSE_ROLE_LABEL || owner_role == PARSE_ROLE_OPEN_CASE ||
                                       owner_role == PARSE_ROLE_CLOSE_CASE || owner_role == PARSE_ROLE_CLOSE_CONDITION ||
                                       owner_role == PARSE_ROLE_ELSE || owner_role == PARSE_ROLE_DO ||
                                       (owner_role == PARSE_ROLE_CLOSE_FOR && at > owner));
            climbing = false;
        }
        else if (role == PARSE_ROLE_CLOSE_GROUP)
        {
            at = parser->tree.parents[at];
        }
        else
        {
            climbing = false;
        }
    }
    return result;
}

static bool check_common_effect_role(uint8_t role)
{
    const bool result = role == PARSE_ROLE_OPERAND || role == PARSE_ROLE_ADDITIVE || role == PARSE_ROLE_MULTIPLICATIVE ||
                        role == PARSE_ROLE_SHIFT || role == PARSE_ROLE_RELATIONAL || role == PARSE_ROLE_EQUALITY ||
                        role == PARSE_ROLE_BITWISE_AND || role == PARSE_ROLE_BITWISE_OR || role == PARSE_ROLE_BITWISE_XOR ||
                        role == PARSE_ROLE_LOGICAL_AND || role == PARSE_ROLE_LOGICAL_OR || role == PARSE_ROLE_PREFIX ||
                        role == PARSE_ROLE_POSTFIX || role == PARSE_ROLE_CLOSE_CAST || role == PARSE_ROLE_CLOSE_SUBSCRIPT ||
                        role == PARSE_ROLE_MEMBER || role == PARSE_ROLE_MEMBER_OPERATOR;
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
// Whether `token`'s parent chain reaches `root` — the token lies in root's subtree.
static bool check_common_token_descendant(const Parser* parser, uint32_t token, uint32_t root)
{
    uint32_t node = token;
    bool result = false;
    for (uint32_t steps = 0; node < parser->tokens.count && steps < parser->tokens.count; steps += 1)
    {
        if (node == root)
        {
            result = true;
            break;
        }
        const uint32_t parent = parser->tree.parents[node];
        if (parent == BUSTER_CHECK_NONE || parent >= parser->tokens.count || parent == node)
        {
            break;
        }
        node = parent;
    }
    return result;
}

static uint32_t check_common_subtree_start(const Parser* parser, uint32_t root)
{
    // A subtree's tokens are a contiguous span ending at `root`, so the descendant predicate is
    // monotone below it and binary search finds the span's first token in logarithmic climbs —
    // scanning the prefix linearly instead costs a whole translation unit per comma operand.
    uint32_t low = 0;
    uint32_t high = root;
    while (high - low > 1)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (check_common_token_descendant(parser, middle, root))
        {
            high = middle;
        }
        else
        {
            low = middle;
        }
    }
    const uint32_t result = check_common_token_descendant(parser, low, root) ? low : high;
    return result;
}

// The lookahead boundary after a token's enclosing `}`: clang replays a function body's queued diagnostics at its
// close, past the scope-pop warnings. A token outside a body publishes at itself.
static uint32_t check_common_function_boundary(const Parser* parser, uint32_t token)
{
    const uint32_t end = check_common_function_end(parser, token);
    return end != token && end < parser->tokens.count ? end + 1 : end;
}

// The parser contexts check_common_unevaluated reads: check_warning_graph_void_context's memo and climb path, each a column
// over the unit's tokens. They are pushed at the unit's first published warning that asks, and go with the passes'
// scratch, as check_driver_common_warnings resets them, so a unit that publishes none of those warnings pays nothing. No
// pass pops its own scratch across a report (the selections' ranges and a literal's spelling report nothing in theirs).
typedef struct CheckCommonContexts CheckCommonContexts;
struct CheckCommonContexts
{
    uint8_t* known;
    uint32_t* path;
    bool failed;
};
static _Thread_local CheckCommonContexts check_common_contexts;

// Whether `token` lies in an operand clang leaves unevaluated (#147): the expression operand of sizeof, _Alignof or
// __alignof__, parenthesized or not, typeof's, or _Generic's controlling expression. There clang drops its runtime-behavior
// warnings (DiagRuntimeBehavior: division by zero, array bounds, sign comparison, constant conversion) and its unused-result
// and self-assignment ones, which check isUnevaluatedContext. The graph's evaluation facts already keep an operand's own
// nodes unchecked, but a GNU statement expression's statements are graph roots of their own, which only the parse tree places
// in the operand. The contexts are -Wvoid-ptr-dereference's (check_warning_graph_void_context), as clang's parser enters
// them: an array declarator's bound, in the operand's type name or in a declaration inside the statement expression, is
// constant-evaluated, where clang keeps its unused-result warnings, and a sizeof of an expression of variable array type in
// an evaluated context evaluates its operand after all (TRANSFORMED), which warns again.
static bool check_common_unevaluated(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, Arena* scratch)
{
    CheckCommonContexts* contexts = &check_common_contexts;
    const uint32_t count = parser->tokens.count;
    if (contexts->known == NULL && !contexts->failed && token < count)
    {
        contexts->known = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, count);
        contexts->path = contexts->known != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
        contexts->failed = contexts->path == NULL;
        if (contexts->failed)
        {
            check_common_exhausted(pp);
        }
        else
        {
            memset(contexts->known, 0, count);
        }
    }
    const uint8_t context = !contexts->failed && contexts->known != NULL && token < count ?
                            check_warning_graph_void_context(input, parser, contexts->known, contexts->path, token) : CHECK_VOID_CONTEXT_KNOWN;
    const bool result = (context & CHECK_VOID_CONTEXT_UNEVALUATED) != 0 && (context & CHECK_VOID_CONTEXT_TRANSFORMED) == 0;
    return result;
}

// A warning clang issues only where its expression is evaluated (check_common_unevaluated), published at `publication`'s
// source in `phase`. Only a warning its policy would publish asks for its context.
static void check_common_report_evaluated(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t publication,
                                          PPDiagnosticPhase phase, uint32_t id, bool default_ignored, const char* message,
                                          const CheckCommonNote* notes, uint32_t note_count, Arena* scratch)
{
    if (token < parser->tokens.count && !pp->fatal)
    {
        const CheckWarningSeverity severity = check_common_severity(pp, parser, input, token, id, default_ignored);
        // (The contexts' allocation can fail, which is fatal: then nothing more is published.)
        if (severity != CHECK_WARNING_IGNORE && !check_common_unevaluated(parser, pp, input, token, scratch) && !pp->fatal)
        {
            const uint32_t publication_source = publication < parser->tokens.count ? parser->tokens.sources[publication] : pp->output.count;
            check_common_publish(parser, pp, token, publication_source, phase, id, severity, message, notes, note_count, scratch);
        }
    }
}

static void check_common_report_unused(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t id,
                                       const char* message, const CheckCommonNote* notes, uint32_t note_count, Arena* scratch)
{
    check_common_report_evaluated(parser, pp, input, token, check_common_function_boundary(parser, token), PP_DIAGNOSTIC_DEFERRED, id, false,
                                  message, notes, note_count, scratch);
}

// The deferred publication an expression warning gets inside a function: every unused-result, bounds,
// conversion, division and sign diagnostic lands at the body's end and keeps the order it was emitted in.
static void check_common_report_deferred(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t id,
                                         bool default_ignored, const char* message, const CheckCommonNote* notes, uint32_t note_count,
                                         Arena* scratch)
{
    check_common_report_evaluated(parser, pp, input, token, check_common_function_boundary(parser, token), PP_DIAGNOSTIC_DEFERRED, id,
                                  default_ignored, message, notes, note_count, scratch);
}

// The unused-result warnings clang reports where the expression stands rather than at the body's end: an unused
// comparison and the `void *` cast question.
static void check_common_report_discarded(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t id,
                                          const char* message, const CheckCommonNote* notes, uint32_t note_count, Arena* scratch)
{
    check_common_report_evaluated(parser, pp, input, token, token, PP_DIAGNOSTIC_SEMANTIC, id, false, message, notes, note_count, scratch);
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

// Clang judges a discarded expression by its own top-level operation: `a[i++]` and `f() + x` still report
// unused because the subscript and the add produce a value nobody reads, while assignments, calls and
// increments carry their own effect. A nested comma's contribution is the right operand it yields, so the
// caller hands in the unwrapped operand node.
static bool check_common_node_effect(const CheckGraph* graph, uint32_t node)
{
    const uint8_t op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    const bool result = op == CHECK_OP_ASSIGN || op == CHECK_OP_CALL || op == CHECK_OP_STATEMENT_VALUE || op == CHECK_OP_VARARGS ||
                        (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN) ||
                        op == CHECK_OP_BUILTIN_UNARY || op == CHECK_OP_BUILTIN_OVERFLOW || op == CHECK_OP_BUILTIN_MISC ||
                        op == CHECK_OP_BUILTIN_FLOAT || op == CHECK_OP_BUILTIN_ATOMIC || op == CHECK_OP_BUILTIN_VECTOR ||
                        op == CHECK_OP_BUILTIN_X86;
    return result;
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

// A cast to `void *` in a discarded position reads like a botched `(void)x`: clang asks whether the cast should
// have been to void rather than reporting the ordinary unused result. A qualified `void *` like `const void *`
// does not invite the question and reports the operand as an ordinary discarded value.
static bool check_common_void_pointer_cast(const CheckStream* stream, const TypeTable* types, uint32_t node)
{
    const TypeId type = node < stream->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, node))) : 0;
    const TypeId element = type != 0 && type_kind(types, type) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, type)) : 0;
    const bool result = element != 0 && type_kind(types, element) == BUSTER_TYPE_KIND_VOID && type_qualifiers(element) == 0;
    return result;
}

// The default-ignored warnings whose work a unit does (check_common_enabled): -Wsign-compare's comparison walk,
// -Wself-assign's plain assignments, and the -Wuninitialized pass, which reports -Wunused-but-set-parameter too.
typedef struct CheckCommonEnabled CheckCommonEnabled;
struct CheckCommonEnabled
{
    bool sign_compare;
    bool self_assign;
    bool uninitialized;
};

// What the expression walk and -Wuninitialized both read of the graph, made once a unit (check_common_facts).
typedef struct CheckCommonFacts CheckCommonFacts;
struct CheckCommonFacts
{
    // Each node's parent: the last node that reads it as an operand or an argument, BUSTER_CHECK_NONE at a root.
    uint32_t* parents;
    // Whether another node reads a node's value: not as a comma's left operand or a void cast's operand, which drop it.
    uint8_t* used;
    // The nodes the source evaluates (check_common_evaluated).
    uint8_t* evaluated;
    // What each node's subtree holds for the expression walk (BUSTER_CHECK_REACH_*).
    uint8_t* reach;
    // CheckCommonEmission.arm_effects.
    uint8_t* arm_effects;
};

static uint8_t* check_common_evaluated(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, const Parser* parser,
                                       const CheckProgram* program, const uint32_t* parents, Arena* scratch);
static bool check_common_unselected_roots(const CheckGraph* graph, const Parser* parser, const uint8_t* roots, uint8_t* marks, uint8_t value,
                                          Arena* scratch);
static void check_common_conversion_warning(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                            TypeTable* types, CheckWarningSpelling* spell, uint32_t source, const CheckValue* value,
                                            TypeId target, Arena* scratch);

// The deferred expression warnings — unused results, out-of-bounds subscripts, lossy conversions, division
// by zero and sign mismatches — publish at their enclosing function's end, so the order they print in is
// the order this traversal emits them. Clang emits a discarded expression's warnings in a value
// post-order: division by zero first, nested subexpressions next, the discard marker after them, each
// operand root's own warning after the marker, and sign comparisons last.
typedef struct CheckCommonEmission
{
    const Parser* parser;
    Preprocessor* pp;
    const CheckInput* input;
    const CheckStream* stream;
    TypeTable* types;
    CheckWarningSpelling* spell;
    const uint8_t* evaluated;
    const uint32_t* parents;
    uint8_t* emitted;
    // Whether a discarded arm is quiet: an effect or explicit void cast along its value path. CONDITIONAL answers
    // its arms', COMMA its result operand's, SELECT its chosen value's; others are checked directly (check_common_arm_of).
    // Children precede parents in the graph, so one ascending pass fills it and every query stays iterative.
    const uint8_t* arm_effects;
    // What each node's subtree holds for the walks (BUSTER_CHECK_REACH_*), so that a walk enters only the subtrees where
    // a warning can come from.
    const uint8_t* reach;
    // The deferred-emission worklist. Each (kind, node) frame stands in for a recursive call the
    // no-recursion rule forbids; push order is the calls' reverse so popping replays them exactly. A
    // full worklist fails the translation rather than dropping warnings.
    uint32_t* work_nodes;
    uint8_t* work_kinds;
    uint32_t work_capacity;
    uint32_t work_count;
    Arena* scratch;
    // Each token's subtree's first token (check_common_emission_subtree_start, #1350), built at the first query; null
    // before it, and after an allocation that failed (which fails the unit), the queries then left to
    // check_common_subtree_start.
    uint32_t* subtree_starts;
    bool subtree_starts_failed;
} CheckCommonEmission;

// check_common_subtree_start for the emission's queries, from a column of every token's subtree start built once
// (#1350): a comma chain's k-th left operand is the chain before it, so the binary search's climbs cost O(k log k) a
// comma. A subtree is a run of tokens and a node's children are in source order, so a node's run starts where its
// first child's does when that child is before it, and at the node otherwise: one ascending pass fills the column.
static uint32_t check_common_emission_subtree_start(CheckCommonEmission* emission, uint32_t root)
{
    const Parser* parser = emission->parser;
    const uint32_t count = parser->tokens.count;
    if (emission->subtree_starts == NULL && !emission->subtree_starts_failed && count != 0)
    {
        uint32_t* first = CHECK_COMMON_PUSH_ARRAY(emission->scratch, uint32_t, count);
        uint32_t* starts = first != NULL ? CHECK_COMMON_PUSH_ARRAY(emission->scratch, uint32_t, count) : NULL;
        emission->subtree_starts_failed = starts == NULL;
        // A failed allocation fails the unit, as every common warning's scratch does; the search stands in until it stops.
        if (starts == NULL)
        {
            check_common_exhausted(emission->pp);
        }
        if (starts != NULL)
        {
            memset(first, 0xff, (uint64_t)count * sizeof(uint32_t));
            for (uint32_t token = count; token-- > 0;)
            {
                const uint32_t parent = parser->tree.parents[token];
                if (parent < count && parent != token)
                {
                    first[parent] = token;
                }
            }
            for (uint32_t token = 0; token < count; token += 1)
            {
                starts[token] = first[token] < token ? starts[first[token]] : token;
            }
            emission->subtree_starts = starts;
        }
    }
    const uint32_t result = root < count && emission->subtree_starts != NULL ? emission->subtree_starts[root] : check_common_subtree_start(parser, root);
    return result;
}

// The worklist frame kinds.
#define BUSTER_CHECK_EMIT_TREE 0u
#define BUSTER_CHECK_EMIT_CHILDREN 1u
#define BUSTER_CHECK_EMIT_OWN 2u
#define BUSTER_CHECK_EMIT_COMMA 3u
#define BUSTER_CHECK_EMIT_COMMA_TAIL 4u
#define BUSTER_CHECK_EMIT_COMMA_END 5u
#define BUSTER_CHECK_EMIT_MARKER 6u
#define BUSTER_CHECK_EMIT_MARK 7u
#define BUSTER_CHECK_EMIT_DIV_SCAN 8u
#define BUSTER_CHECK_EMIT_DIV_OWN 9u
#define BUSTER_CHECK_EMIT_SIGN_SCAN 10u
#define BUSTER_CHECK_EMIT_SIGN_OWN 11u

// What a node's subtree holds for check_common_emit_root's walks, a bit each, in CheckCommonEmission.reach: a node whose
// own frame or comma stages may warn (check_common_own_candidate), a division by a constant, a comparison where
// -Wsign-compare may warn (check_common_enabled). Each counts evaluated or not: a comma's operands and a root's expression
// run their own frames either way, and the scans test evaluation themselves. A frame of a subtree without its walk's bit
// warns of nothing and marks emitted only nodes of such subtrees, which no other frame then reads to any effect, so the
// walks skip those subtrees and keep every warning's order.
#define BUSTER_CHECK_REACH_TREE 0x01u
#define BUSTER_CHECK_REACH_DIVISION 0x02u
#define BUSTER_CHECK_REACH_SIGN 0x04u
#define BUSTER_CHECK_REACH_ALL 0x07u

static void check_common_emit_push(CheckCommonEmission* emission, uint32_t node, uint8_t kind);
static void check_common_emit_run(CheckCommonEmission* emission);
static void check_common_own(CheckCommonEmission* emission, uint32_t node);

// The node a discard marker points at inside an operand: a comma resolves to its result and a conditional
// to its else arm, so `x, 0` and `x ? y : z` report at the `0` and the `z`.
static uint32_t check_common_result_root(const CheckGraph* graph, uint32_t node)
{
    uint32_t result = node;
    for (uint32_t steps = 0; result < graph->count && steps < graph->count; steps += 1)
    {
        const CheckOp op = graph->ops[result];
        const uint32_t next = op == CHECK_OP_COMMA ? graph->operand1[result] :
                              op == CHECK_OP_CONDITIONAL ? graph->operand2[result] : BUSTER_CHECK_NONE;
        if (next >= graph->count)
        {
            break;
        }
        result = next;
    }
    return result;
}

// The `expr, 0` idiom: a literal integer 0 result stays silent — it exists to sequence the left operand's
// effect. Only a bare numeric literal counts: `+0`, `0.0`, `'\0'` and enum zeros still report.
static bool check_common_literal_zero(const Parser* parser, const CheckStream* stream, const CheckGraph* graph, uint32_t node)
{
    const uint32_t token = node < graph->count ? graph->tokens[node] : BUSTER_CHECK_NONE;
    const bool result = node < graph->count && graph->ops[node] == CHECK_OP_VALUE && token < parser->tokens.count &&
                        parser->tokens.kinds[token] == PARSE_KIND_NUMBER && check_common_integer_constant(stream, node) &&
                        check_stream_value(stream, node).integer == 0;
    return result;
}

// Whether an arm's value path accepts discard: an effect or an explicit cast to void. Conditional, comma and
// selected-value results use postorder facts, so nested void casts stay quiet while inner diagnostics still run.
// An implicit conversion is not an explicit discard, and a side effect off the comma's result path is not enough.
static bool check_common_arm_of(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types,
                                 const uint8_t* arm_effects, uint32_t node)
{
    const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    const bool void_cast = op == CHECK_OP_CAST &&
                           type_kind(types, type_unqualified(type_strip_aligned(types, check_stream_type(stream, node)))) == BUSTER_TYPE_KIND_VOID;
    const bool result = node < graph->count && (void_cast ||
                        (op == CHECK_OP_CONDITIONAL || op == CHECK_OP_COMMA || op == CHECK_OP_SELECT ? arm_effects[node] != 0 : check_common_node_effect(graph, node)));
    return result;
}

static bool check_common_arm_at(const CheckCommonEmission* emission, uint32_t node)
{
    return check_common_arm_of(&emission->input->graph, emission->stream, emission->types, emission->arm_effects, node);
}

// One conditional, comma or selected-value entry, after every operand's entry.
static bool check_common_arm_effect(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types,
                                     const uint8_t* arm_effects, uint32_t node)
{
    const CheckOp op = graph->ops[node];
    const uint32_t first = graph->operand1[node];
    const uint32_t second = graph->operand2[node];
    const bool result = op == CHECK_OP_CONDITIONAL ? check_common_arm_of(graph, stream, types, arm_effects, first) || check_common_arm_of(graph, stream, types, arm_effects, second) :
                        op == CHECK_OP_COMMA ? check_common_arm_of(graph, stream, types, arm_effects, first) :
                        op == CHECK_OP_SELECT ? check_common_arm_of(graph, stream, types, arm_effects, graph->operand0[node]) : check_common_node_effect(graph, node);
    return result;
}

// A frame on the deferred-emission worklist, or the translation's failure when scratch is full.
static void check_common_emit_push(CheckCommonEmission* emission, uint32_t node, uint8_t kind)
{
    if (emission->work_count < emission->work_capacity)
    {
        emission->work_nodes[emission->work_count] = node;
        emission->work_kinds[emission->work_count] = kind;
        emission->work_count += 1;
    }
    else
    {
        check_common_exhausted(emission->pp);
    }
}

// A walk's frame of `kind` at `node`, where the node's subtree holds what the walk looks for, `bit` of its reach.
static void check_common_emit_reach(CheckCommonEmission* emission, uint32_t node, uint8_t kind, uint8_t bit)
{
    if (node < emission->input->graph.count && (emission->reach == NULL || (emission->reach[node] & bit) != 0))
    {
        check_common_emit_push(emission, node, kind);
    }
}

// A node's operand and argument children pushed in reverse so they pop in the order the subtree walks
// read them: operands left to right, then the call's arguments.
static void check_common_emit_kids(CheckCommonEmission* emission, uint32_t node, uint32_t slots, uint8_t kind)
{
    const CheckGraph* graph = &emission->input->graph;
    const uint8_t bit = kind == BUSTER_CHECK_EMIT_DIV_SCAN ? BUSTER_CHECK_REACH_DIVISION :
                        kind == BUSTER_CHECK_EMIT_SIGN_SCAN ? BUSTER_CHECK_REACH_SIGN : BUSTER_CHECK_REACH_TREE;
    if (check_graph_has_arguments(graph->ops[node]) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
    {
        for (uint32_t i = graph->argument_counts[node]; i-- > 0;)
        {
            check_common_emit_reach(emission, graph->argument_nodes[graph->argument_offsets[node] + i], kind, bit);
        }
    }
    for (uint32_t i = slots; i-- > 0;)
    {
        check_common_emit_reach(emission, i == 0 ? graph->operand0[node] : i == 1 ? graph->operand1[node] : graph->operand2[node], kind, bit);
    }
}

static void check_common_comma_marker(CheckCommonEmission* emission, uint32_t node);
static void check_common_comma_end(CheckCommonEmission* emission, uint32_t node);

// The division-warning visit one scan frame runs after its subtree: `emit_children`'s other half.
static void check_common_div_own(CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const CheckOp op = graph->ops[node];
    if (op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER)
    {
        const uint32_t divisor = graph->operand1[node];
        const uint32_t contexts = graph->contexts != NULL ? graph->contexts[node] : 0;
        const bool ice = (contexts & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT)) != 0;
        if (!ice && check_common_integer_constant(emission->stream, divisor) && check_stream_value(emission->stream, divisor).integer == 0)
        {
            uint32_t width = 0;
            bool sign = false;
            if (check_common_integer(emission->types, check_stream_type(emission->stream, graph->operand0[node]), &width, &sign))
            {
                check_common_report_deferred(emission->parser, emission->pp, emission->input, graph->tokens[node], CHECK_WARNING_ID_DIVISION_BY_ZERO,
                                             false, op == CHECK_OP_DIVIDE ? "division by zero is undefined" : "remainder by zero is undefined",
                                             NULL, 0, emission->scratch);
            }
        }
    }
}



// The sign-mismatch visit one scan frame runs after its subtree: these warnings follow every other
// warning the expression produces.
static void check_common_sign_own(CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const CheckOp op = graph->ops[node];
    const CheckStream* stream = emission->stream;
    const Parser* parser = emission->parser;
    TypeTable* types = emission->types;
    Arena* scratch = emission->scratch;
    if (op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL)
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
        // Keep the source operands' unsignedness before promotion: a narrow unsigned value still cannot
        // be negative after it becomes int (#1391). Explicit casts retain their own result type here.
        const bool unsigned_operands = integer && !left_signed && !right_signed;
        // Promotions still determine which signed values the common comparison type can represent.
        if (left_width != 0 && left_width < 32)
        {
            left_width = 32;
            left_signed = true;
        }
        if (right_width != 0 && right_width < 32)
        {
            right_width = 32;
            right_signed = true;
        }
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
                // Clang canonicalizes the signed operand to the left; a zero unsigned constant leaves the
                // result unchanged for `<` and `>=`, whose truth is already the same for every signed value.
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
        if (integer && !unsigned_operands && left_signed != right_signed && !constant_suppressed && (left_signed ? left_width <= right_width : right_width <= left_width))
        {
            const String8 left_name = check_driver_warning_type(parser, emission->pp, emission->input, types, emission->spell, scratch,
                                                                check_stream_type(stream, left), 0);
            const String8 right_name = check_driver_warning_type(parser, emission->pp, emission->input, types, emission->spell, scratch,
                                                                 check_stream_type(stream, right), 0);
            if (left_name.length != 0 && right_name.length != 0)
            {
                const char* message = check_warning_text(scratch, "comparison of integers of different signs: %.*s and %.*s",
                                                          (int)left_name.length, left_name.pointer, (int)right_name.length, right_name.pointer);
                check_common_report_deferred(parser, emission->pp, emission->input, graph->tokens[node], CHECK_WARNING_ID_SIGN_COMPARE, true,
                                             message, NULL, 0, scratch);
            }
        }
    }
}

// Clang's self-assignment check asks whether the operator or either bare identifier is a macro location.
// Body tokens and substituted arguments both retain this flag; no expansion-history replay is needed.
static bool check_common_from_macro(const Parser* parser, const Preprocessor* pp, uint32_t token)
{
    const uint32_t source = parser != NULL && parser->tokens.sources != NULL && token < parser->tokens.count ?
                               parser->tokens.sources[token] : BUSTER_PP_NONE;
    const bool result = pp != NULL && pp->output.flags != NULL && source < pp->output.count &&
                        (pp->output.flags[source] & BUSTER_PP_FROM_MACRO) != 0;
    return result;
}

// The warning a node carries at its own root: a lost value in an initializer, assignment, conversion or
// call argument, a self-assignment, or a constant subscript past its bound. A conditional has no warning
// of its own — its arms report at its slot instead.
static void check_common_own(CheckCommonEmission* emission, uint32_t node)
{
    const Parser* parser = emission->parser;
    Preprocessor* pp = emission->pp;
    const CheckInput* input = emission->input;
    const CheckStream* stream = emission->stream;
    TypeTable* types = emission->types;
    CheckWarningSpelling* spell = emission->spell;
    Arena* scratch = emission->scratch;
    const CheckGraph* graph = &input->graph;
    const CheckOp op = graph->ops[node];
    const uint32_t token = graph->tokens[node];
    if (op == CHECK_OP_INITIALIZE || op == CHECK_OP_ASSIGN || op == CHECK_OP_CONVERT)
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
            check_common_conversion_warning(parser, pp, input, stream, types, spell, source, value, target, scratch);
        }
        if (op == CHECK_OP_ASSIGN)
        {
            const uint32_t left = graph->operand0[node];
            const uint32_t right = graph->operand1[node];
            const CheckValue left_value = left < graph->count ? check_stream_value(stream, left) : (CheckValue){ 0 };
            const CheckValue right_value = right < graph->count ? check_stream_value(stream, right) : (CheckValue){ 0 };
            const bool volatile_object = left < graph->count &&
                (type_qualifiers(type_strip_aligned(types, left_value.type)) & BUSTER_TYPE_VOLATILE) != 0;
            // Clang's -Wself-assign only covers a variable's own name: `x = x` warns, while member,
            // subscript and indirection lvalues never do — so `u.a = u.b`, `s.m = s.m` and `a[i] = a[i]`
            // all stay silent regardless of the object and offset they resolve to.
            const bool plain_lvalues = left < graph->count && right < graph->count &&
                                       graph->ops[left] == CHECK_OP_VALUE && graph->ops[right] == CHECK_OP_VALUE;
            if (!volatile_object && plain_lvalues && left_value.category == CHECK_LVALUE &&
                right_value.category == CHECK_LVALUE && left_value.object != 0 && left_value.object == right_value.object &&
                left_value.offset == 0 && right_value.offset == 0 && !check_common_from_macro(parser, pp, token) &&
                !check_common_from_macro(parser, pp, graph->tokens[left]) && !check_common_from_macro(parser, pp, graph->tokens[right]))
            {
                const String8 type_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, left_value.type, 0);
                if (type_name.length != 0)
                {
                    const char* message = check_warning_text(scratch, "explicitly assigning value of variable of type %.*s to itself",
                                                              (int)type_name.length, type_name.pointer);
                    check_common_report_deferred(parser, pp, input, token, CHECK_WARNING_ID_SELF_ASSIGN, true, message, NULL, 0, scratch);
                }
            }
        }
    }
    else if (op == CHECK_OP_CALL && graph->argument_offsets != NULL && graph->argument_counts != NULL &&
             stream->argument_conversions != NULL)
    {
        // A call's converted arguments: a constant that loses value going into its parameter warns at the
        // argument, where an initializer or return conversion would already have been diagnosed. As clang
        // reports them, an unprototyped call's arguments are checked against each parameter's promoted type:
        // a K&R `short` parameter receives `int`, so a constant it holds warns only against the prototype's
        // declared type.
        const uint32_t callee = graph->operand0[node];
        TypeId function = callee < graph->count ? check_stream_type(stream, callee) : 0;
        function = function != 0 ? type_unqualified(type_strip_aligned(types, function)) : 0;
        if (function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_POINTER)
        {
            function = type_unqualified(type_strip_aligned(types, type_element(types, function)));
        }
        const bool prototype = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION &&
                               (types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
        const uint32_t offset = graph->argument_offsets[node];
        for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
        {
            const uint32_t argument = graph->argument_nodes[offset + i];
            const CheckConversion conversion = stream->argument_conversions[offset + i];
            if (argument < graph->count && (conversion.flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 &&
                (conversion.flags & BUSTER_CHECK_CONVERT_CAST) == 0 && check_common_integer_constant(stream, argument))
            {
                const TypeId destination = prototype ? conversion.type : type_default_argument_promote(types, conversion.type);
                const CheckValue value_storage = check_stream_value(stream, argument);
                check_common_conversion_warning(parser, pp, input, stream, types, spell, argument, &value_storage,
                                                destination, scratch);
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
                    // The lvalue's use decides the bound: `&a[i]` permits one past the end along the spine
                    // it takes the address of, a cast or a ternary condition discards the lvalue unchecked,
                    // and a nested index inside an address-of's operand goes unchecked as well.
                    bool checked = emission->evaluated == NULL || emission->evaluated[node] != 0;
                    bool relaxed = false;
                    if (checked && emission->parents != NULL)
                    {
                        uint32_t at = node;
                        uint32_t parent = emission->parents[at];
                        for (uint32_t steps = 0; parent < graph->count && steps < graph->count; steps += 1)
                        {
                            const CheckOp parent_op = graph->ops[parent];
                            const bool spine = (parent_op == CHECK_OP_SUBSCRIPT || parent_op == CHECK_OP_MEMBER || parent_op == CHECK_OP_ARROW) && graph->operand0[parent] == at;
                            const uint32_t parent_token = graph->tokens[parent];
                            // The implicit cast an expression statement wraps its value in is transparent
                            // here: `a[i];` still reads the lvalue. An explicit cast discards it unchecked.
                            const bool statement_cast = parent_op == CHECK_OP_CAST && parent_token < parser->tokens.count &&
                                                        (parser->tree.roles[parent_token] == PARSE_ROLE_TERMINATOR ||
                                                         parser->tree.roles[parent_token] == PARSE_ROLE_CLOSE_FOR);
                            const bool value_path = (parent_op == CHECK_OP_COMMA && graph->operand1[parent] == at) ||
                                                    (parent_op == CHECK_OP_CONDITIONAL && (graph->operand1[parent] == at || graph->operand2[parent] == at)) ||
                                                    parent_op == CHECK_OP_CONVERT || statement_cast;
                            if (parent_op == CHECK_OP_CONDITIONAL && graph->operand0[parent] == at)
                            {
                                checked = false;
                                break;
                            }
                            if (parent_op == CHECK_OP_CAST && !statement_cast)
                            {
                                checked = false;
                                break;
                            }
                            if (!spine && !value_path)
                            {
                                break;
                            }
                            at = parent;
                            parent = emission->parents[at];
                        }
                        if (checked && parent < graph->count && graph->ops[parent] == CHECK_OP_ADDRESS)
                        {
                            relaxed = true;
                        }
                        else if (checked)
                        {
                            for (uint32_t up = parent, steps = 0; up < graph->count && steps < graph->count; up = emission->parents[up], steps += 1)
                            {
                                if (graph->ops[up] == CHECK_OP_ADDRESS)
                                {
                                    checked = false;
                                    break;
                                }
                            }
                        }
                    }
                    const bool past = checked && value >= 0 && (relaxed ? (unsigned __int128)value > extent : (unsigned __int128)value >= extent);
                    const bool before = checked && value < 0;
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
                            // Array bounds queues in clang's function-end diagnostics where the body's walk
                            // emitted it — right after this expression's own discard warnings.
                            check_common_report_deferred(parser, pp, input, graph->tokens[root],
                                                         CHECK_WARNING_ID_ARRAY_BOUNDS, false, message,
                                                         note.message != NULL ? &note : NULL, note.message != NULL ? 1u : 0u, scratch);
                        }
                    }
                }
            }
        }
    }
}

// The diagnostics for one evaluated comma: every left operand is evaluated for effect only — a discarded
// comparison reports as a comparison when the operand's own root is the comparison, anything else as the
// operand's own value, and a nested comma's contribution is its right operand's. A chain ending a
// statement or for clause leaves its result unused.
// The discarded-left-operand analysis a comma node's frame runs once both operands' deep warnings are
// out: the marker between the children's warnings and the operand roots' own.
static void check_common_comma_marker(CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const Parser* parser = emission->parser;
    Preprocessor* pp = emission->pp;
    const CheckInput* input = emission->input;
    const CheckStream* stream = emission->stream;
    const TypeTable* types = emission->types;
    Arena* scratch = emission->scratch;
    const uint32_t count = parser->tokens.count;
    const uint32_t comma = graph->tokens[node];
    const uint32_t left_node = graph->operand0[node];
    uint32_t left = comma - 1;
    for (uint32_t steps = 0; steps < count; steps += 1)
    {
        const uint32_t up = parser->tree.parents[left];
        if (up == BUSTER_CHECK_NONE || up >= comma || up == left)
        {
            break;
        }
        left = up;
    }
    const uint32_t left_start = check_common_emission_subtree_start(emission, left);
    const uint32_t left_result = check_common_result_root(graph, left_node);
    const bool left_effect = left_node < graph->count ?
                             (graph->ops[left_node] == CHECK_OP_CONDITIONAL ? check_common_arm_at(emission, left_node) :
                              check_common_node_effect(graph, left_result)) :
                             check_common_expression_has_effect(parser, left_start, comma);
    // A left operand that resolves to a literal 0 through commas of its own stays quiet, like the
    // `expr, 0` result does — a bare `0, x` still reports the `0`, and so does a conditional's `0` arm.
    uint32_t left_zero_node = left_node;
    for (uint32_t steps = 0; left_zero_node < graph->count && graph->ops[left_zero_node] == CHECK_OP_COMMA && steps < graph->count; steps += 1)
    {
        left_zero_node = graph->operand1[left_zero_node];
    }
    const bool left_zero = left_zero_node != left_node && left_zero_node < graph->count &&
                           check_common_literal_zero(parser, stream, graph, left_zero_node);
    if (left_start < comma && !left_effect && !left_zero)
    {
        uint32_t report = left_result < graph->count && graph->tokens[left_result] < count ? graph->tokens[left_result] : left;
        uint8_t report_role = report < count ? parser->tree.roles[report] : PARSE_ROLE_NONE;
        const uint8_t left_role = left < count ? parser->tree.roles[left] : PARSE_ROLE_NONE;
        // Clang points a discarded member at the member's name.
        if (report_role == PARSE_ROLE_MEMBER_OPERATOR && report + 1 < count)
        {
            report += 1;
            report_role = parser->tree.roles[report];
        }
        if (left_role == PARSE_ROLE_EQUALITY || left_role == PARSE_ROLE_RELATIONAL)
        {
            const bool equality = left_role == PARSE_ROLE_EQUALITY;
            const char* message = equality ? (parser->tokens.kinds[left] == PARSE_KIND_EQUALEQUAL ? "equality comparison result unused" : "inequality comparison result unused") :
                                             "relational comparison result unused";
            const CheckCommonNote note = { report, parser->tokens.kinds[left] == PARSE_KIND_EQUALEQUAL ? "use '=' to turn this equality comparison into an assignment" :
                                                     parser->tokens.kinds[left] == PARSE_KIND_EXCLAIMEQUAL ? "use '|=' to turn this inequality comparison into an or-assignment" : "" };
            check_common_report_discarded(parser, pp, input, report, CHECK_WARNING_ID_UNUSED_COMPARISON, message,
                                          note.message[0] != 0 ? &note : NULL, note.message[0] != 0 ? 1u : 0u, scratch);
        }
        else if (report_role == PARSE_ROLE_CLOSE_CAST)
        {
            const bool void_cast = check_common_void_cast(parser, pp, input, stream, types, report);
            const bool void_pointer = !void_cast && left_result < graph->count && graph->ops[left_result] == CHECK_OP_CAST &&
                                      check_common_void_pointer_cast(stream, types, left_result);
            const uint32_t cast = check_common_cast_opener(parser, report);
            if (!void_cast)
            {
                if (void_pointer)
                {
                    // A `void *` question reports where the cast stands, not at the body's end.
                    check_common_report_discarded(parser, pp, input, cast != BUSTER_CHECK_NONE ? cast : report, CHECK_WARNING_ID_UNUSED_VALUE,
                                                  "expression result unused; should this cast be to 'void'?", NULL, 0, scratch);
                }
                else
                {
                    check_common_report_unused(parser, pp, input, cast != BUSTER_CHECK_NONE ? cast : report, CHECK_WARNING_ID_UNUSED_VALUE,
                                               "left operand of comma operator has no effect", NULL, 0, scratch);
                }
            }
        }
        else
        {
            check_common_report_unused(parser, pp, input, report, CHECK_WARNING_ID_UNUSED_VALUE,
                                       "left operand of comma operator has no effect", NULL, 0, scratch);
        }
    }
}

// The comma node's last stage: its operand roots marked emitted and, in a discarded position, the
// right operand's own marker after them.
static void check_common_comma_end(CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const Parser* parser = emission->parser;
    Preprocessor* pp = emission->pp;
    const CheckInput* input = emission->input;
    const CheckStream* stream = emission->stream;
    const TypeTable* types = emission->types;
    Arena* scratch = emission->scratch;
    const uint32_t count = parser->tokens.count;
    const uint32_t comma = graph->tokens[node];
    const uint32_t left_node = graph->operand0[node];
    const uint32_t right_node = graph->operand1[node];
    if (left_node < graph->count)
    {
        emission->emitted[left_node] = 1;
    }
    if (right_node < graph->count)
    {
        emission->emitted[right_node] = 1;
    }
    const uint32_t parent = check_common_parent(parser, comma);
    const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
    const uint32_t grandparent = parent < count ? check_common_parent(parser, parent) : BUSTER_CHECK_NONE;
    const bool for_init = parent_role == PARSE_ROLE_TERMINATOR && grandparent < count &&
                          parser->tree.roles[grandparent] == PARSE_ROLE_CLOSE_FOR &&
                          !check_common_for_condition(parser, comma, parent);
    const bool discarded = check_common_statement_terminator(parser, parent) || for_init || parent_role == PARSE_ROLE_CLOSE_FOR;
    if (discarded)
    {
        // The `expr, 0` idiom looks through nested comma results only: a conditional arm that happens to
        // be a literal 0 is still a discarded conditional, not the sequencing marker.
        uint32_t zero_node = right_node;
        for (uint32_t steps = 0; zero_node < graph->count && graph->ops[zero_node] == CHECK_OP_COMMA && steps < graph->count; steps += 1)
        {
            zero_node = graph->operand1[zero_node];
        }
        const uint32_t right_result = check_common_result_root(graph, right_node);
        const uint32_t right = right_result < graph->count && graph->tokens[right_result] < count ? graph->tokens[right_result] : comma + 1;
        const uint32_t right_start = right < count ? check_common_emission_subtree_start(emission, right) : right;
        const bool right_cast = right < count && parser->tree.roles[right] == PARSE_ROLE_CLOSE_CAST;
        const bool right_void = right_cast && check_common_void_cast(parser, pp, input, stream, types, right);
        const bool right_zero = check_common_literal_zero(parser, stream, graph, zero_node);
        const bool right_effect = right_node < graph->count ?
                                  (graph->ops[right_node] == CHECK_OP_CONDITIONAL ? check_common_arm_at(emission, right_node) :
                                   check_common_node_effect(graph, right_result)) :
                                  check_common_expression_has_effect(parser, right_start, parent);
        if (right_start < parent && !right_void && !right_zero && !right_effect)
        {
            const uint32_t cast = right_cast ? check_common_cast_opener(parser, right) : BUSTER_CHECK_NONE;
            const bool void_pointer = right_cast && right_result < graph->count && graph->ops[right_result] == CHECK_OP_CAST &&
                                      check_common_void_pointer_cast(stream, types, right_result);
            if (void_pointer)
            {
                check_common_report_discarded(parser, pp, input, cast != BUSTER_CHECK_NONE ? cast : right, CHECK_WARNING_ID_UNUSED_VALUE,
                                              "expression result unused; should this cast be to 'void'?", NULL, 0, scratch);
            }
            else
            {
                // A discarded member reports at the member's name, like the left operand does.
                const uint32_t spot = right < count && parser->tree.roles[right] == PARSE_ROLE_MEMBER_OPERATOR && right + 1 < count ?
                                      right + 1 : right;
                check_common_report_unused(parser, pp, input, cast != BUSTER_CHECK_NONE ? cast : spot, CHECK_WARNING_ID_UNUSED_VALUE,
                                           "expression result unused", NULL, 0, scratch);
            }
        }
    }
}

// The discard marker for an expression that ends a statement or a for clause: a bare comparison keeps its
// eager comparison warning and an arm that carries an effect keeps the conditional quiet.
static void check_common_stmt_marker(CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const Parser* parser = emission->parser;
    const uint32_t token = graph->tokens[node];
    const uint32_t count = parser->tokens.count;
    const uint32_t parent = check_common_parent(parser, token);
    const uint8_t role = token < count ? parser->tree.roles[token] : PARSE_ROLE_NONE;
    const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
    const uint32_t grandparent = parent < count ? check_common_parent(parser, parent) : BUSTER_CHECK_NONE;
    const bool for_init = parent_role == PARSE_ROLE_TERMINATOR && grandparent < count &&
                          parser->tree.roles[grandparent] == PARSE_ROLE_CLOSE_FOR &&
                          !check_common_for_condition(parser, token, parent);
    const bool discarded = check_common_statement_terminator(parser, parent) || for_init || parent_role == PARSE_ROLE_CLOSE_FOR;
    const CheckOp op = graph->ops[node];
    if (discarded && op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL)
    {
        // An indirect comparison takes the generic marker; a direct one already reported as a comparison.
        if (parent_role != PARSE_ROLE_TERMINATOR)
        {
            check_common_report_unused(parser, emission->pp, emission->input, token, CHECK_WARNING_ID_UNUSED_VALUE,
                                       "expression result unused", NULL, 0, emission->scratch);
        }
    }
    else if (discarded && op == CHECK_OP_CONDITIONAL)
    {
        if (!check_common_arm_at(emission, graph->operand1[node]) && !check_common_arm_at(emission, graph->operand2[node]))
        {
            const uint32_t root = check_common_result_root(graph, node);
            const uint32_t report = root < graph->count && graph->tokens[root] < count ? graph->tokens[root] : token;
            check_common_report_unused(parser, emission->pp, emission->input, report, CHECK_WARNING_ID_UNUSED_VALUE,
                                       "expression result unused", NULL, 0, emission->scratch);
        }
    }
    else if (discarded && op != CHECK_OP_INITIALIZE && op != CHECK_OP_CONVERT && op != CHECK_OP_CONDITION &&
             check_common_effect_role(role) && role != PARSE_ROLE_ASSIGN && role != PARSE_ROLE_EQUALITY &&
             role != PARSE_ROLE_RELATIONAL &&
             !(role == PARSE_ROLE_CLOSE_CAST && check_common_void_cast(parser, emission->pp, emission->input, emission->stream, emission->types, token)) &&
             parser->tokens.kinds[token] != PARSE_KIND_L_PAREN &&
             parser->tokens.kinds[token] != PARSE_KIND_PLUSPLUS && parser->tokens.kinds[token] != PARSE_KIND_MINUSMINUS &&
             !check_common_node_effect(graph, node))
    {
        // A discarded member read points at the member's name, like the comma's left operand does.
        const uint32_t report = role == PARSE_ROLE_CLOSE_CAST ? check_common_cast_opener(parser, token) :
                                role == PARSE_ROLE_MEMBER_OPERATOR && token + 1 < count ? token + 1 : token;
        const bool void_pointer = op == CHECK_OP_CAST && check_common_void_pointer_cast(emission->stream, emission->types, node);
        // A `void *` question reports with the cast itself; an ordinary unused result waits for the body's end.
        if (void_pointer)
        {
            check_common_report_discarded(parser, emission->pp, emission->input, report != BUSTER_CHECK_NONE ? report : token,
                                          CHECK_WARNING_ID_UNUSED_VALUE, "expression result unused; should this cast be to 'void'?", NULL, 0,
                                          emission->scratch);
        }
        else
        {
            check_common_report_unused(parser, emission->pp, emission->input, report != BUSTER_CHECK_NONE ? report : token,
                                       CHECK_WARNING_ID_UNUSED_VALUE, "expression result unused", NULL, 0, emission->scratch);
        }
    }
}

// The worklist's frames replaying what recursion did: TREE gates and marks, CHILDREN pushes operands and
// arguments, OWN runs the node's checks or a conditional's arm emissions, COMMA/TAIL/END run a comma's
// stages, MARKER and MARK the deferred statements, and the scan pairs descend then visit each node.
static void check_common_emit_run(CheckCommonEmission* emission)
{
    const CheckGraph* graph = &emission->input->graph;
    while (emission->work_count != 0 && !emission->pp->fatal)
    {
        emission->work_count -= 1;
        const uint32_t node = emission->work_nodes[emission->work_count];
        const uint8_t kind = emission->work_kinds[emission->work_count];
        switch (kind)
        {
            case BUSTER_CHECK_EMIT_TREE:
                if (node >= graph->count || emission->emitted[node] != 0 ||
                    (emission->evaluated != NULL && emission->evaluated[node] == 0))
                {
                    break;
                }
                emission->emitted[node] = 1;
                // A comma the source writes; the reader's own (a variable length array's sizeof evaluating its operand,
                // c_check_vla_input.c) is at the sizeof and has no operand of the comma operator's to warn of.
                if (graph->ops[node] == CHECK_OP_COMMA && graph->tokens[node] < emission->parser->tokens.count &&
                    emission->parser->tokens.kinds[graph->tokens[node]] == PARSE_KIND_COMMA)
                {
                    check_common_emit_push(emission, node, BUSTER_CHECK_EMIT_COMMA);
                }
                else
                {
                    check_common_emit_push(emission, node, BUSTER_CHECK_EMIT_OWN);
                    check_common_emit_push(emission, node, BUSTER_CHECK_EMIT_CHILDREN);
                }
                break;
            case BUSTER_CHECK_EMIT_CHILDREN:
                if (node < graph->count)
                {
                    check_common_emit_kids(emission, node, graph->ops[node] == CHECK_OP_CONDITIONAL ? 1u : 3u, BUSTER_CHECK_EMIT_TREE);
                }
                break;
            case BUSTER_CHECK_EMIT_OWN:
                if (node < graph->count)
                {
                    if (graph->ops[node] == CHECK_OP_CONDITIONAL)
                    {
                        check_common_emit_reach(emission, graph->operand2[node], BUSTER_CHECK_EMIT_TREE, BUSTER_CHECK_REACH_TREE);
                        check_common_emit_reach(emission, graph->operand1[node], BUSTER_CHECK_EMIT_TREE, BUSTER_CHECK_REACH_TREE);
                    }
                    else
                    {
                        check_common_own(emission, node);
                    }
                }
                break;
            case BUSTER_CHECK_EMIT_COMMA:
            {
                const uint32_t left_node = node < graph->count ? graph->operand0[node] : BUSTER_CHECK_NONE;
                const uint32_t right_node = node < graph->count ? graph->operand1[node] : BUSTER_CHECK_NONE;
                check_common_emit_push(emission, node, BUSTER_CHECK_EMIT_COMMA_TAIL);
                check_common_emit_push(emission, right_node,
                                       right_node < graph->count && graph->ops[right_node] == CHECK_OP_COMMA ?
                                       BUSTER_CHECK_EMIT_COMMA : BUSTER_CHECK_EMIT_CHILDREN);
                check_common_emit_push(emission, left_node,
                                       left_node < graph->count && graph->ops[left_node] == CHECK_OP_COMMA ?
                                       BUSTER_CHECK_EMIT_COMMA : BUSTER_CHECK_EMIT_CHILDREN);
                break;
            }
            case BUSTER_CHECK_EMIT_COMMA_TAIL:
            {
                check_common_comma_marker(emission, node);
                const uint32_t left_node = node < graph->count ? graph->operand0[node] : BUSTER_CHECK_NONE;
                const uint32_t right_node = node < graph->count ? graph->operand1[node] : BUSTER_CHECK_NONE;
                check_common_emit_push(emission, node, BUSTER_CHECK_EMIT_COMMA_END);
                if (right_node < graph->count && graph->ops[right_node] != CHECK_OP_COMMA)
                {
                    check_common_emit_push(emission, right_node, BUSTER_CHECK_EMIT_OWN);
                }
                if (left_node < graph->count && graph->ops[left_node] != CHECK_OP_COMMA)
                {
                    check_common_emit_push(emission, left_node, BUSTER_CHECK_EMIT_OWN);
                }
                break;
            }
            case BUSTER_CHECK_EMIT_COMMA_END:
                check_common_comma_end(emission, node);
                break;
            case BUSTER_CHECK_EMIT_MARKER:
                check_common_stmt_marker(emission, node);
                break;
            case BUSTER_CHECK_EMIT_MARK:
                if (node < graph->count)
                {
                    emission->emitted[node] = 1;
                }
                break;
            case BUSTER_CHECK_EMIT_DIV_SCAN:
            case BUSTER_CHECK_EMIT_SIGN_SCAN:
                if (node < graph->count && (emission->evaluated == NULL || emission->evaluated[node] != 0))
                {
                    check_common_emit_push(emission, node,
                                           kind == BUSTER_CHECK_EMIT_DIV_SCAN ? BUSTER_CHECK_EMIT_DIV_OWN : BUSTER_CHECK_EMIT_SIGN_OWN);
                    check_common_emit_kids(emission, node, 3, kind);
                }
                break;
            case BUSTER_CHECK_EMIT_DIV_OWN:
                check_common_div_own(emission, node);
                break;
            case BUSTER_CHECK_EMIT_SIGN_OWN:
                check_common_sign_own(emission, node);
                break;
        }
    }
}

// The expression a root discards: an expression statement — and a for clause's init and increment — wraps its value
// in an implicit void cast at the `;` or the `)`, whose operand it is. Any other root is its own.
static uint32_t check_common_discarded(const Parser* parser, const CheckGraph* graph, uint32_t node)
{
    const uint32_t token = graph->tokens[node];
    const bool statement = graph->ops[node] == CHECK_OP_CAST && token < parser->tokens.count &&
                           (parser->tree.roles[token] == PARSE_ROLE_TERMINATOR || parser->tree.roles[token] == PARSE_ROLE_CLOSE_FOR);
    const uint32_t result = statement ? graph->operand0[node] : node;
    return result;
}

// Whether check_common_stmt_marker may warn at `expr`: a comparison, a conditional, or an expression without an effect
// that isn't an initialization, a conversion or a condition.
static bool check_common_marker_candidate(const CheckGraph* graph, uint32_t expr)
{
    const CheckOp op = expr < graph->count ? graph->ops[expr] : CHECK_OP_COUNT;
    const bool result = expr < graph->count &&
                        ((op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL) || op == CHECK_OP_CONDITIONAL ||
                         (op != CHECK_OP_INITIALIZE && op != CHECK_OP_CONVERT && op != CHECK_OP_CONDITION && !check_common_node_effect(graph, expr)));
    return result;
}

// The deferred warnings one evaluated expression root produces, in clang's order: divisions first, the
// deep subtree and the discard marker next, the root's own warning after them, and sign comparisons last.
// The worklist drains before returning, so each push is the calls it replaces in reverse.
static void check_common_emit_root(CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const uint32_t expr = check_common_discarded(emission->parser, graph, node);
    const uint8_t reach = emission->reach != NULL ? emission->reach[node] : BUSTER_CHECK_REACH_ALL;
    if ((reach & BUSTER_CHECK_REACH_SIGN) != 0)
    {
        check_common_emit_push(emission, node, BUSTER_CHECK_EMIT_SIGN_SCAN);
    }
    check_common_emit_push(emission, node, BUSTER_CHECK_EMIT_MARK);
    if (expr < graph->count)
    {
        check_common_emit_push(emission, expr, BUSTER_CHECK_EMIT_MARK);
    }
    if (expr < graph->count && graph->ops[expr] == CHECK_OP_COMMA)
    {
        check_common_emit_push(emission, expr, BUSTER_CHECK_EMIT_COMMA);
    }
    else if (expr < graph->count)
    {
        check_common_emit_push(emission, expr, BUSTER_CHECK_EMIT_OWN);
        check_common_emit_push(emission, expr, BUSTER_CHECK_EMIT_MARKER);
        check_common_emit_push(emission, expr, BUSTER_CHECK_EMIT_CHILDREN);
    }
    if ((reach & BUSTER_CHECK_REACH_DIVISION) != 0)
    {
        check_common_emit_push(emission, node, BUSTER_CHECK_EMIT_DIV_SCAN);
    }
    check_common_emit_run(emission);
}

// A pass's walk over the items its own test needs first (check_common_next_token, check_common_next_op and
// check_common_next_reach): a group of them at a time, each a bit of the group's mask until visited. Zeroed, it starts at 0.
typedef struct CheckCommonScan CheckCommonScan;
struct CheckCommonScan
{
    // The group's first item, and the next group's.
    uint32_t base;
    uint32_t next;
    // The group's items still to visit, a bit each.
    uint64_t pending;
};

// The scan's group's lowest pending item, which it then takes off, or `count` where the groups are done.
static uint32_t check_common_scan_take(CheckCommonScan* scan, uint32_t count)
{
    const uint32_t result = scan->pending != 0 ? scan->base + (uint32_t)__builtin_ctzll(scan->pending) : count;
    scan->pending &= scan->pending - 1;
    return result;
}

// The scan's next token below `count` whose role is one of the `role_count` `roles` and, unless `kind` is BUSTER_CHECK_NONE,
// whose kind is `kind`, or `count`: what a token pass's own test needs first. The vector path tests 64 tokens at a time;
// the scalar path, its twin, gives every token, so that its caller tests each.
static uint32_t check_common_next_token(const Parser* parser, uint32_t count, uint32_t kind, const uint8_t* roles, uint32_t role_count,
                                        CheckCommonScan* scan, bool vector)
{
    while (scan->pending == 0 && scan->next < count)
    {
        const uint32_t first = scan->next;
        const uint32_t live = count - first < 64 ? count - first : 64;
        const uint64_t lanes = live == 64 ? UINT64_MAX : (UINT64_C(1) << live) - 1;
        uint64_t selected = lanes;
        if (vector)
        {
            const __m512i role_bytes = _mm512_maskz_loadu_epi8((__mmask64)lanes, parser->tree.roles + first);
            selected = 0;
            for (uint32_t r = 0; r < role_count; r += 1)
            {
                selected |= _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, role_bytes, _mm512_set1_epi8((char)roles[r]));
            }
            if (kind != BUSTER_CHECK_NONE)
            {
                const __m512i kind_bytes = _mm512_maskz_loadu_epi8((__mmask64)lanes, parser->tokens.kinds + first);
                selected &= _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, kind_bytes, _mm512_set1_epi8((char)kind));
            }
        }
        scan->base = first;
        scan->next = first + live;
        scan->pending = selected;
    }
    const uint32_t result = check_common_scan_take(scan, count);
    return result;
}

// A set of graph operators, a bit each: what a node pass's own test needs first (check_common_next_op).
typedef struct CheckCommonOps CheckCommonOps;
struct CheckCommonOps
{
    uint32_t words[(CHECK_OP_COUNT + 31) / 32];
};
_Static_assert(CHECK_OP_COUNT <= 512, "check_common_next_op's permute holds the set's words in one register");

// The set of the `count` operators of `list`.
static CheckCommonOps check_common_ops(const CheckOp* list, uint32_t count)
{
    CheckCommonOps result = { 0 };
    for (uint32_t i = 0; i < count; i += 1)
    {
        result.words[list[i] >> 5] |= 1u << (list[i] & 31);
    }
    return result;
}

// The scan's next node below the graph's count whose operator is in `set`, or the count: the vector path tests 16 nodes at
// a time, each operator's word of the set a lane permute away; the scalar path, its twin, gives every node, so that its
// caller tests each.
static uint32_t check_common_next_op(const CheckGraph* graph, const CheckCommonOps* set, CheckCommonScan* scan, bool vector)
{
    while (scan->pending == 0 && scan->next < graph->count)
    {
        const uint32_t first = scan->next;
        const uint32_t live = graph->count - first < 16 ? graph->count - first : 16;
        const uint32_t lanes = live == 16 ? 0xffffu : (1u << live) - 1;
        uint32_t selected = lanes;
        if (vector)
        {
            const __m512i words = _mm512_maskz_loadu_epi32((__mmask16)((1u << BUSTER_ARRAY_LENGTH(set->words)) - 1), set->words);
            const __m512i ops = _mm512_maskz_loadu_epi32((__mmask16)lanes, graph->ops + first);
            const __m512i word = _mm512_permutexvar_epi32(_mm512_srli_epi32(ops, 5), words);
            const __m512i bit = _mm512_sllv_epi32(_mm512_set1_epi32(1), _mm512_and_si512(ops, _mm512_set1_epi32(31)));
            selected = _mm512_mask_test_epi32_mask((__mmask16)lanes, word, bit);
        }
        scan->base = first;
        scan->next = first + live;
        scan->pending = selected;
    }
    const uint32_t result = check_common_scan_take(scan, graph->count);
    return result;
}

// The comparison is already a direct child of the bitwise operator. Climb from the adjacent token on
// the other side to that operand's source root. Group/cast/generic wrappers remain explicit, and enum
// initializers need no retained runtime graph. A nested bitwise root ends the climb before its subtree.
static bool check_common_bitwise_precedence(const Parser* parser, uint32_t comparison, uint32_t bitwise)
{
    const uint32_t count = parser->tokens.count;
    uint32_t at = comparison < bitwise ? bitwise + 1 : bitwise != 0 ? bitwise - 1 : count;
    uint8_t role = PARSE_ROLE_NONE;
    bool found = false;
    for (uint32_t steps = 0; at < count && steps < count; steps += 1)
    {
        const uint32_t parent = parser->tree.parents[at];
        if (parent == bitwise)
        {
            role = parser->tree.roles[at];
            found = true;
            break;
        }
        if (parent >= count || parent == at)
        {
            break;
        }
        at = parent;
    }
    const bool other_bitwise = role == PARSE_ROLE_BITWISE_AND || role == PARSE_ROLE_BITWISE_OR || role == PARSE_ROLE_BITWISE_XOR;
    const bool result = found && !check_common_comparison_role(role) && !other_bitwise;
    return result;
}

static void check_common_parentheses(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                     Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    // Each warning's token is an assignment, a comparison or a `!`.
    static const uint8_t roles[] = { PARSE_ROLE_ASSIGN, PARSE_ROLE_EQUALITY, PARSE_ROLE_RELATIONAL, PARSE_ROLE_PREFIX };
    CheckCommonScan scan = { 0 };
    for (uint32_t token = check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector); token < count;
         token = check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector))
    {
        const uint8_t role = parser->tree.roles[token];
        const uint32_t parent = check_common_parent(parser, token);
        const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
        const bool for_condition = role == PARSE_ROLE_ASSIGN && check_common_for_condition(parser, token, parent);
        // A switch head's controlling expression is no truth test: `switch (x = 1)` stays silent while
        // if, while and for conditions report.
        const uint32_t opener = parent_role == PARSE_ROLE_CLOSE_CONDITION ? check_common_opener(parser, parent) : BUSTER_CHECK_NONE;
        const bool switch_condition = opener != BUSTER_CHECK_NONE && opener > 0 && parser->tokens.kinds[opener - 1] == PARSE_KIND_SWITCH;
        if (role == PARSE_ROLE_ASSIGN && (parent_role == PARSE_ROLE_CLOSE_CONDITION || for_condition) && !switch_condition &&
            parser->tokens.kinds[token] == PARSE_KIND_EQUAL)
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
            if (check_common_comparison_role(role) && check_common_bitwise_precedence(parser, token, parent))
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
        else if ((role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_RELATIONAL) &&
                 parent < parser->tokens.count && parser->tree.roles[parent] == PARSE_ROLE_TERMINATOR &&
                 check_common_statement_terminator(parser, parent))
        {
            const bool equality = role == PARSE_ROLE_EQUALITY;
            const char* message = equality ? (parser->tokens.kinds[token] == PARSE_KIND_EQUALEQUAL ? "equality comparison result unused" : "inequality comparison result unused") :
                                            "relational comparison result unused";
            const CheckCommonNote note = { token, parser->tokens.kinds[token] == PARSE_KIND_EQUALEQUAL ? "use '=' to turn this equality comparison into an assignment" :
                                                     parser->tokens.kinds[token] == PARSE_KIND_EXCLAIMEQUAL ? "use '|=' to turn this inequality comparison into an or-assignment" : "" };
            check_common_report_discarded(parser, pp, input, token, CHECK_WARNING_ID_UNUSED_COMPARISON, message,
                                          note.message[0] != 0 ? &note : NULL, note.message[0] != 0 ? 1u : 0u, scratch);
        }
    }
}

// `token`'s line in the file its text spells in — clang's getSpellingLineNumber: a `;` a macro produced
// spells on its definition's line, never the loop head's call-site one.
static uint32_t check_common_spelling_line(const Parser* parser, Preprocessor* pp, uint32_t token)
{
    uint32_t result = 0;
    if (pp->output.locations != NULL && token < parser->tokens.count)
    {
        const uint32_t source = parser->tokens.sources[token];
        if (source < pp->output.count && pp->output.locations[source] != 0)
        {
            const uint32_t location = pp->output.locations[source];
            const uint32_t range = pp_find_range(pp, location);
            result = range != BUSTER_PP_NONE ? pp_line_of(pp, range, location, 0) : 0;
        }
    }
    return result;
}

// `token`'s line and column at its expansion place — clang's presumed position of a statement's begin or
// a `)` (its column is its file column; a #line marker changes only the line).
static uint32_t check_common_expansion_point(const Parser* parser, Preprocessor* pp, uint32_t token, uint32_t* column)
{
    uint32_t result = 0;
    if (pp->output.locations != NULL && token < parser->tokens.count)
    {
        const uint32_t source = parser->tokens.sources[token];
        if (source < pp->output.count && pp->output.locations[source] != 0)
        {
            uint32_t location = pp->output.locations[source];
            if (pp->output.flags[source] & BUSTER_PP_FROM_MACRO)
            {
                // Only a candidate that needs a macro's invocation asks for the deferred history. Ordinary file
                // tokens keep their positions without replaying the unit, even when another token expands a macro;
                // and a token whose invocation the file alone places (pp_output_invocation_place: one invocation
                // between two file tokens) takes that place, but by --scalar, whose history is the twin (#628).
                const uint32_t place = BUSTER_SCALAR_PATH(parser->options.scalar) ? 0 : pp_output_invocation_place(pp, source);
                location = place != 0 ? place : pp_history_ensure(pp) ? pp_output_expansion_begin(pp, source, pp_history_at(pp, source)) : 0;
            }
            const uint32_t range = pp_find_range(pp, location);
            const char* file = NULL;
            uint32_t file_length = 0;
            result = range != BUSTER_PP_NONE ? pp_presumed(pp, location, &file, &file_length) : 0;
            if (column != NULL && range != BUSTER_PP_NONE)
            {
                (void)pp_line_of(pp, range, location, column);
            }
        }
    }
    return result;
}

// Clang emits an if's or switch's `;`-body warning when the statement parses, but a for's or while's
// only once its block's `}` is seen — there its elements are known and the next statement's shape or
// indentation decides. Publishing a loop's at its owner's position replays that later diagnostic order.
static void check_common_empty_bodies(const Parser* parser, Preprocessor* pp, const CheckInput* input, Arena* scratch)
{
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const uint8_t roles[] = { PARSE_ROLE_TERMINATOR };
    CheckCommonScan scan = { 0 };
    for (uint32_t token = check_common_next_token(parser, parser->tokens.count, PARSE_KIND_SEMI, roles, 1, &scan, vector); token < parser->tokens.count;
         token = check_common_next_token(parser, parser->tokens.count, PARSE_KIND_SEMI, roles, 1, &scan, vector))
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
            const bool loop = kind == PARSE_KIND_WHILE || kind == PARSE_KIND_FOR;
            const bool has_else = kind == PARSE_KIND_IF && token + 1 < parser->tokens.count &&
                                  parser->tokens.kinds[token + 1] == PARSE_KIND_ELSE && parser->tree.parents[token + 1] == parent;
            // The `;` must spell on the `)`'s line: `while (x)\n;` is quiet, as is a `;` a macro's body
            // provides (its spelling line is its definition's).
            bool warn = kind == PARSE_KIND_IF || kind == PARSE_KIND_WHILE || kind == PARSE_KIND_FOR ||
                        kind == PARSE_KIND_SWITCH;
            warn = warn && !has_else && check_common_severity(pp, parser, input, token, CHECK_WARNING_ID_EMPTY_BODY, false) != CHECK_WARNING_IGNORE &&
                   check_common_spelling_line(parser, pp, token) != 0 &&
                   check_common_spelling_line(parser, pp, token) == check_common_expansion_point(parser, pp, parent, 0);
            uint32_t publication = token;
            if (warn && loop)
            {
                // A for or while's `;` body warns only where it could be a mistaken block: the loop is a
                // block's own statement (a label's, a case's or a head's body is none) and a statement
                // follows it — a compound's, or one indented past the loop's keyword.
                const uint32_t owner = parent < parser->tokens.count ? parser->tree.parents[parent] : BUSTER_CHECK_NONE;
                const uint8_t owner_role = owner < parser->tokens.count ? parser->tree.roles[owner] : PARSE_ROLE_NONE;
                warn = owner_role == PARSE_ROLE_OPEN_BLOCK || owner_role == PARSE_ROLE_CLOSE_BLOCK ||
                       owner_role == PARSE_ROLE_OPEN_BODY || owner_role == PARSE_ROLE_CLOSE_BODY;
                uint32_t sibling = BUSTER_CHECK_NONE;
                for (uint32_t t = warn ? token + 1 : owner; t < owner; t += 1)
                {
                    if (parser->tree.parents[t] == owner)
                    {
                        sibling = t;
                        break;
                    }
                }
                if (warn)
                {
                    const uint8_t sibling_role = sibling < parser->tokens.count ? parser->tree.roles[sibling] : PARSE_ROLE_NONE;
                    warn = sibling_role == PARSE_ROLE_OPEN_BLOCK || sibling_role == PARSE_ROLE_CLOSE_BLOCK ||
                           sibling_role == PARSE_ROLE_OPEN_BODY || sibling_role == PARSE_ROLE_CLOSE_BODY;
                    if (!warn && sibling != BUSTER_CHECK_NONE)
                    {
                        uint32_t body_column = 0;
                        uint32_t head_column = 0;
                        (void)check_common_expansion_point(parser, pp, check_common_subtree_start(parser, sibling), &body_column);
                        (void)check_common_expansion_point(parser, pp, keyword, &head_column);
                        warn = body_column > head_column;
                    }
                    publication = warn ? owner : publication;
                }
            }
            if (warn)
            {
                const char* subject = kind == PARSE_KIND_IF ? "if statement" : kind == PARSE_KIND_WHILE ? "while loop" :
                                      kind == PARSE_KIND_FOR ? "for loop" : "switch statement";
                const char* message = check_warning_text(scratch, "%s has empty body", subject);
                const CheckCommonNote note = { token, "put the semicolon on a separate line to silence this warning" };
                check_common_report_at(parser, pp, input, token, publication, CHECK_WARNING_ID_EMPTY_BODY, false, message, &note, 1, scratch);
            }
        }
    }
}

static void check_common_dangling_else(const Parser* parser, Preprocessor* pp, const CheckInput* input, Arena* scratch)
{
    uint8_t* else_at = parser->tokens.count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, parser->tokens.count) : NULL;
    if (parser->tokens.count != 0 && else_at == NULL)
    {
        check_common_exhausted(pp);
    }
    if (parser->tokens.count == 0 || else_at == NULL)
    {
        return;
    }
    memset(else_at, 0, parser->tokens.count);
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const uint8_t roles[] = { PARSE_ROLE_ELSE };
    CheckCommonScan owners = { 0 };
    for (uint32_t token = check_common_next_token(parser, parser->tokens.count, PARSE_KIND_ELSE, roles, 1, &owners, vector); token < parser->tokens.count;
         token = check_common_next_token(parser, parser->tokens.count, PARSE_KIND_ELSE, roles, 1, &owners, vector))
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_ELSE && parser->tree.roles[token] == PARSE_ROLE_ELSE && parser->tree.parents[token] < parser->tokens.count)
        {
            else_at[parser->tree.parents[token]] = 1;
        }
    }
    CheckCommonScan elses = { 0 };
    for (uint32_t token = check_common_next_token(parser, parser->tokens.count, PARSE_KIND_ELSE, roles, 1, &elses, vector); token < parser->tokens.count;
         token = check_common_next_token(parser, parser->tokens.count, PARSE_KIND_ELSE, roles, 1, &elses, vector))
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

// Whether the separator `separator` is a comma operator, as the reader's expression_commas decides it: a separator
// takes what the first other token above it is, and a group, a sizeof, a condition, a subscript, a conditional or a
// for makes a comma operator; a call's, an initializer's or a selection's list does not.
static bool check_common_comma_operator(const Parser* parser, uint32_t separator)
{
    uint32_t at = separator;
    for (uint32_t steps = 0; at < parser->tokens.count && parser->tree.roles[at] == PARSE_ROLE_SEPARATOR && steps < parser->tokens.count; steps += 1)
    {
        at = parser->tree.parents[at];
    }
    const bool result = at < parser->tokens.count && check_input_expression_comma_seed(parser, at);
    return result;
}

// Whether the child `at` of the conditional whose `:` is `colon` is its condition: the conditional's `?`, a leaf child of
// its `:`, comes after it. The arms come after the `?`, a GNU `?:` included, whose condition is its true value too.
static bool check_common_ternary_condition(const Parser* parser, uint32_t at, uint32_t colon)
{
    bool result = false;
    for (uint32_t question = at + 1; at < colon && !result && question < colon; question += 1)
    {
        result = parser->tree.parents[question] == colon && parser->tree.roles[question] == PARSE_ROLE_OPEN_TERNARY;
    }
    return result;
}

// Whether the address at `token` (an `&`, or an array that decays) is what a return returns: up the tree to the `return`,
// through what keeps the address (casts, groups, pointer arithmetic, a conditional's arms, a comma's right operand), as
// clang's checkExprLifetime follows a returned value. A dereference (`*`, `->`, a subscript), a comparison, a logical
// operator, a call, an assignment or an initializer consumes it, and a comma's left operand and a conditional's
// condition are not the value.
static bool check_common_in_return(const Parser* parser, uint32_t token)
{
    bool result = false;
    uint32_t at = token;
    for (uint32_t steps = 0; at < parser->tokens.count && steps < parser->tokens.count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[at];
        const uint8_t kind = parser->tokens.kinds[at];
        const uint32_t parent = parser->tree.parents[at];
        const uint8_t parent_role = parent < parser->tokens.count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
        if (role == PARSE_ROLE_RETURN)
        {
            result = true;
            break;
        }
        if (at != token && (role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_OPEN_CALL || role == PARSE_ROLE_CLOSE_CALL ||
                            role == PARSE_ROLE_OPEN_INITIALIZER || role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_MEMBER_OPERATOR ||
                            role == PARSE_ROLE_OPEN_SUBSCRIPT || role == PARSE_ROLE_CLOSE_SUBSCRIPT || role == PARSE_ROLE_EQUALITY ||
                            role == PARSE_ROLE_RELATIONAL || role == PARSE_ROLE_LOGICAL_AND || role == PARSE_ROLE_LOGICAL_OR ||
                            (role == PARSE_ROLE_PREFIX && (kind == PARSE_KIND_STAR || kind == PARSE_KIND_EXCLAIM))))
        {
            break;
        }
        const bool comma = parent_role == PARSE_ROLE_COMMA || (parent_role == PARSE_ROLE_SEPARATOR && check_common_comma_operator(parser, parent));
        if ((comma && at < parent) || (parent_role == PARSE_ROLE_CLOSE_TERNARY && check_common_ternary_condition(parser, at, parent)))
        {
            break;
        }
        at = parent;
    }
    return result;
}

// The graph nodes no static selection selects: a selection's controlling expression and its other arms, and what they
// read. clang's checkExprLifetime sees through a selection to the arm it selects alone.
static uint8_t* check_common_unselected(const CheckGraph* graph, const Parser* parser, Arena* scratch)
{
    uint8_t* roots = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    uint8_t* marks = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    uint8_t* result = roots != NULL && marks != NULL ? marks : NULL;
    if (result != NULL)
    {
        memset(roots, 1, graph->count);
        memset(marks, 0, graph->count);
        for (uint32_t node = 0; node < graph->count; node += 1)
        {
            const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
            {
                if (operands[i] < graph->count)
                {
                    roots[operands[i]] = 0;
                }
            }
            for (uint32_t i = 0; check_graph_has_arguments(graph->ops[node]) && graph->argument_offsets != NULL && graph->argument_counts != NULL &&
                                 i < graph->argument_counts[node]; i += 1)
            {
                const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
                if (argument < graph->count)
                {
                    roots[argument] = 0;
                }
            }
        }
        result = check_common_unselected_roots(graph, parser, roots, marks, 1, scratch) ? marks : NULL;
        // Operands come before what reads them, as check_common_evaluated's reverse pass has it.
        for (uint32_t node = result != NULL ? graph->count : 0; node-- > 0;)
        {
            const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            for (uint32_t i = 0; marks[node] != 0 && i < BUSTER_ARRAY_LENGTH(operands); i += 1)
            {
                if (operands[i] < graph->count)
                {
                    marks[operands[i]] = 1;
                }
            }
            for (uint32_t i = 0; marks[node] != 0 && check_graph_has_arguments(graph->ops[node]) && graph->argument_offsets != NULL &&
                                 graph->argument_counts != NULL && i < graph->argument_counts[node]; i += 1)
            {
                const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
                if (argument < graph->count)
                {
                    marks[argument] = 1;
                }
            }
        }
    }
    return result;
}

// Whether `token` names a definition parameter, whose address clang names a parameter's in -Wreturn-stack-address.
static bool check_common_parameter_token(const CheckInput* input, const Parser* parser, uint32_t token)
{
    bool result = false;
    for (uint32_t opening = 0; !result && opening < parser->tokens.count; opening += 1)
    {
        for (uint32_t parameter = check_common_sparse_get(input->body_declarations, opening) != BUSTER_CHECK_NONE ?
                                  check_common_sparse_get(input->parameters, opening) : BUSTER_CHECK_NONE;
             !result && parameter != BUSTER_CHECK_NONE && parameter < parser->tokens.count; parameter = check_common_sparse_get(input->parameter_next, parameter))
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
    // Made at the first returned local address: what no static selection selects.
    uint8_t* unselected = NULL;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const CheckOp returns[] = { CHECK_OP_ADDRESS, CHECK_OP_CONVERT };
    const CheckCommonOps set = check_common_ops(returns, BUSTER_ARRAY_LENGTH(returns));
    CheckCommonScan scan = { 0 };
    for (uint32_t node = check_common_next_op(graph, &set, &scan, vector); node < graph->count; node = check_common_next_op(graph, &set, &scan, vector))
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
                               check_common_sparse_get(input->declaration_bodies, declaration) < parser->tokens.count &&
                               !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) &&
                               !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL);
            // From the address itself (its `&`, or the array that decays), so that what is done to it counts.
            const bool returned = local && check_common_in_return(parser, graph->tokens[node]);
            if (returned && unselected == NULL)
            {
                unselected = check_common_unselected(graph, parser, scratch);
                if (unselected == NULL)
                {
                    check_common_exhausted(pp);
                }
            }
            if (returned && unselected != NULL && unselected[node] == 0)
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

// Clang treats an uncomputed non-decimal integer literal as a bit pattern when it fits the destination width.
// Parentheses do not make a graph node; unary operators, casts and computed expressions do.
static bool check_common_nondecimal_literal(const Parser* parser, Preprocessor* pp, const CheckGraph* graph,
                                            const TypeTable* types, uint32_t source, const CheckValue* value,
                                            TypeId target, Arena* scratch)
{
    bool result = false;
    uint32_t width = 0;
    bool is_signed = false;
    const uint32_t token = source < graph->count ? graph->tokens[source] : BUSTER_CHECK_NONE;
    if (source < graph->count && graph->ops[source] == CHECK_OP_VALUE && token < parser->tokens.count &&
        parser->tokens.kinds[token] == PARSE_KIND_NUMBER && check_common_integer(types, target, &width, &is_signed) &&
        width != 0 && (value->integer & ~check_action_mask(width)) == 0)
    {
        const uint64_t mark = scratch->length;
        const String8 spelling = check_semantic_token(parser, pp, token, scratch);
        result = spelling.length >= 2 && spelling.pointer[0] == '0' &&
                 (spelling.pointer[1] == 'x' || spelling.pointer[1] == 'X' || spelling.pointer[1] == 'b' ||
                  spelling.pointer[1] == 'B' || (spelling.pointer[1] >= '0' && spelling.pointer[1] <= '7'));
        arena_pop_to(scratch, mark);
    }
    return result;
}

// A value-changing implicit integer conversion of a constant: the warning clang spells at the constant's own
// token, shared by initializers, assignments, return conversions and a call's per-argument assignment.
static void check_common_conversion_warning(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                            TypeTable* types, CheckWarningSpelling* spell, uint32_t source, const CheckValue* value,
                                            TypeId target, Arena* scratch)
{
    __int128 converted = 0;
    if (check_common_conversion_changes(types, value->type, target, value, &converted) &&
        !check_common_nondecimal_literal(parser, pp, &input->graph, types, source, value, target, scratch))
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
            check_common_report_deferred(parser, pp, input, input->graph.tokens[source], CHECK_WARNING_ID_CONSTANT_CONVERSION, false, message,
                                         NULL, 0, scratch);
        }
    }
}

// Whether check_common_own may warn at `node`, or its comma stages run there, by what they test first: a constant that an
// initialization, an assignment, a conversion or a call's argument converts, a self-assignment's two names, a constant
// index, or a comma the source writes.
static bool check_common_own_candidate(const Parser* parser, const CheckGraph* graph, const CheckStream* stream, uint32_t node, bool self_assign)
{
    const CheckOp op = graph->ops[node];
    bool result = false;
    if (op == CHECK_OP_INITIALIZE || op == CHECK_OP_ASSIGN || op == CHECK_OP_CONVERT)
    {
        const uint32_t source = op == CHECK_OP_ASSIGN ? graph->operand1[node] : graph->operand0[node];
        const uint32_t flags = stream->conversions[node].operands[op == CHECK_OP_ASSIGN ? 1u : 0u].flags;
        const uint32_t left = graph->operand0[node];
        const uint32_t right = graph->operand1[node];
        result = (source < graph->count && check_common_integer_constant(stream, source) && (flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 &&
                  (flags & BUSTER_CHECK_CONVERT_CAST) == 0) ||
                 (self_assign && op == CHECK_OP_ASSIGN && left < graph->count && right < graph->count &&
                  graph->ops[left] == CHECK_OP_VALUE && graph->ops[right] == CHECK_OP_VALUE);
    }
    else if (op == CHECK_OP_CALL && graph->argument_offsets != NULL && graph->argument_counts != NULL && stream->argument_conversions != NULL)
    {
        const uint32_t offset = graph->argument_offsets[node];
        for (uint32_t i = 0; !result && i < graph->argument_counts[node]; i += 1)
        {
            const uint32_t argument = graph->argument_nodes[offset + i];
            const uint32_t flags = stream->argument_conversions[offset + i].flags;
            result = argument < graph->count && (flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (flags & BUSTER_CHECK_CONVERT_CAST) == 0 &&
                     check_common_integer_constant(stream, argument);
        }
    }
    else if (op == CHECK_OP_SUBSCRIPT)
    {
        result = check_common_integer_constant(stream, graph->operand1[node]);
    }
    else if (op == CHECK_OP_COMMA)
    {
        const uint32_t token = graph->tokens[node];
        result = token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_COMMA;
    }
    return result;
}

// A node's own bits of its reach (BUSTER_CHECK_REACH_*): its own frame's candidate, a division by a constant, a comparison.
// A plain `x = y` is a candidate for -Wself-assign alone, and a comparison's bit is -Wsign-compare's: each counts only where
// its warning may report.
static uint8_t check_common_own_reach(const Parser* parser, const CheckGraph* graph, const CheckStream* stream, uint32_t node,
                                      const CheckCommonEnabled* enabled)
{
    const CheckOp op = graph->ops[node];
    const bool division = (op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER) && check_common_integer_constant(stream, graph->operand1[node]);
    const bool comparison = enabled->sign_compare && op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
    const uint8_t result = (uint8_t)((check_common_own_candidate(parser, graph, stream, node, enabled->self_assign) ? BUSTER_CHECK_REACH_TREE : 0u) |
                                     (division ? BUSTER_CHECK_REACH_DIVISION : 0u) | (comparison ? BUSTER_CHECK_REACH_SIGN : 0u));
    return result;
}

// The scan's next node below `count` without a parent, or `count`: the vector path tests 16 nodes at a time; the scalar
// path, its twin, gives every node, so that its caller tests each.
static uint32_t check_common_next_root(const uint32_t* parents, uint32_t count, CheckCommonScan* scan, bool vector)
{
    while (scan->pending == 0 && scan->next < count)
    {
        const uint32_t first = scan->next;
        const uint32_t live = count - first < 16 ? count - first : 16;
        const uint32_t lanes = live == 16 ? 0xffffu : (1u << live) - 1;
        const uint32_t selected = vector ? _mm512_mask_cmpeq_epi32_mask((__mmask16)lanes, _mm512_maskz_loadu_epi32((__mmask16)lanes, parents + first),
                                                                         _mm512_set1_epi32((int)BUSTER_CHECK_NONE)) : lanes;
        scan->base = first;
        scan->next = first + live;
        scan->pending = selected;
    }
    const uint32_t result = check_common_scan_take(scan, count);
    return result;
}

// The scan's next node below `count` whose reach has one of `bits`, or `count`: the vector path tests 64 nodes at a time;
// the scalar path, its twin, gives every node, so that its caller tests each.
static uint32_t check_common_next_reach(const uint8_t* reach, uint32_t count, uint8_t bits, CheckCommonScan* scan, bool vector)
{
    while (scan->pending == 0 && scan->next < count)
    {
        const uint32_t first = scan->next;
        const uint32_t live = count - first < 64 ? count - first : 64;
        const uint64_t lanes = live == 64 ? UINT64_MAX : (UINT64_C(1) << live) - 1;
        const uint64_t selected = vector ? _mm512_mask_test_epi8_mask((__mmask64)lanes, _mm512_maskz_loadu_epi8((__mmask64)lanes, reach + first),
                                                                      _mm512_set1_epi8((char)bits)) : lanes;
        scan->base = first;
        scan->next = first + live;
        scan->pending = selected;
    }
    const uint32_t result = check_common_scan_take(scan, count);
    return result;
}

static void check_common_graph_warnings(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                        TypeTable* types, CheckWarningSpelling* spell, const CheckCommonFacts* facts, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    // Division and subscript warnings apply only to evaluated operands (facts->evaluated): sizeof and alignof operands,
    // unselected constant ternary arms, short-circuited sides and _Generic's other arms never run. Each node's parent
    // (facts->parents) is a subscript's use context: an `&a[i]` spine relaxes the bound to allow one past the end, an
    // explicit `(void)` cast discards the lvalue unchecked, and any other operand inside an address-of's subtree — a
    // nested index — is unchecked as well.
    uint8_t* emitted = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    if (graph->count != 0 && emitted == NULL)
    {
        check_common_exhausted(pp);
    }
    const uint8_t* reach = facts->reach;
    const bool ready = graph->count != 0 && facts->evaluated != NULL && emitted != NULL;
    // The emission worklist: a subtree's frames never outlive the pushes its nodes and edges already made,
    // so eight entries per node cover every walk with room to spare.
    const uint32_t work_capacity = graph->count * 8 + 16;
    uint32_t* work_nodes = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, work_capacity) : NULL;
    uint8_t* work_kinds = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, work_capacity) : NULL;
    if (graph->count != 0 && (work_nodes == NULL || work_kinds == NULL))
    {
        check_common_exhausted(pp);
    }
    if (ready && work_nodes != NULL && work_kinds != NULL)
    {
        memset(emitted, 0, graph->count);
        CheckCommonEmission emission =
        {
            .parser = parser,
            .pp = pp,
            .input = input,
            .stream = stream,
            .types = types,
            .spell = spell,
            .evaluated = facts->evaluated,
            .parents = facts->parents,
            .emitted = emitted,
            .arm_effects = facts->arm_effects,
            .reach = reach,
            .work_nodes = work_nodes,
            .work_kinds = work_kinds,
            .work_capacity = work_capacity,
            .work_count = 0,
            .scratch = scratch,
        };
        const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
        // Expression roots in graph order: each evaluated expression emits its subtree's deferred warnings
        // where clang's traversal puts them — the diagnostics' shared publication slot keeps this order. A root
        // whose subtree holds no reach and whose discarded expression is no comma and can't have a marker can't warn.
        CheckCommonScan roots = { 0 };
        for (uint32_t node = check_common_next_root(facts->parents, graph->count, &roots, vector); node < graph->count && !pp->fatal;
             node = check_common_next_root(facts->parents, graph->count, &roots, vector))
        {
            const uint32_t expr = check_common_discarded(parser, graph, node);
            if (facts->parents[node] == BUSTER_CHECK_NONE && facts->evaluated[node] != 0 && stream->diagnostics[node] == CHECK_DIAG_NONE &&
                emitted[node] == 0 &&
                (reach[node] != 0 || (expr < graph->count && graph->ops[expr] == CHECK_OP_COMMA) || check_common_marker_candidate(graph, expr)))
            {
                check_common_emit_root(&emission, node);
            }
        }
        // Anything a root walk did not reach still reports its own warning at its own place — through the
        // own frame, so a conditional's arms still emit ahead of it.
        CheckCommonScan rest = { 0 };
        for (uint32_t node = check_common_next_reach(reach, graph->count, BUSTER_CHECK_REACH_TREE, &rest, vector); node < graph->count && !pp->fatal;
             node = check_common_next_reach(reach, graph->count, BUSTER_CHECK_REACH_TREE, &rest, vector))
        {
            if ((reach[node] & BUSTER_CHECK_REACH_TREE) != 0 && facts->evaluated[node] != 0 && stream->diagnostics[node] == CHECK_DIAG_NONE &&
                emitted[node] == 0)
            {
                check_common_emit_push(&emission, node, BUSTER_CHECK_EMIT_OWN);
                check_common_emit_run(&emission);
            }
        }
    }
}

// A printf-family call's format string: its name decides the format argument, and a literal's bytes decide each
// conversion's expected type.
// Only infer a plain library name's format where the unit keeps it a builtin. Explicit __builtin_ spellings
// remain builtins under the same options; dprintf, absent from the builtin catalog, needs an explicit attribute.
static uint32_t check_common_format_index(String8 name, const PPOptions* options)
{
    const bool explicit_builtin = name.length > 10 && memcmp(name.pointer, "__builtin_", 10) == 0;
    if (explicit_builtin)
    {
        name.pointer += 10;
        name.length -= 10;
    }
    uint32_t result = UINT32_MAX;
    if (name.length == 6 && memcmp(name.pointer, "printf", 6) == 0) result = 0;
    else if (name.length == 7 && memcmp(name.pointer, "fprintf", 7) == 0) result = 1;
    else if (name.length == 7 && memcmp(name.pointer, "sprintf", 7) == 0) result = 1;
    else if (name.length == 8 && memcmp(name.pointer, "snprintf", 8) == 0) result = 2;
    if (result != UINT32_MAX && !explicit_builtin && !pp_library_builtin(options, (const uint8_t*)name.pointer, (uint32_t)name.length))
    {
        result = UINT32_MAX;
    }
    return result;
}

// Whether an integer argument has the class a length modifier names: an `int`-or-narrower value promoted for the
// unspecified and h/hh lengths, or the long or long long class (signed or unsigned) for the wider lengths. The two
// 64-bit classes share a width on x86-64, so the exact class, not the width, distinguishes them.
static bool check_common_format_integer_class(TypeTable* types, TypeId type, char length_modifier, uint32_t w_width, uint32_t i_width, bool pointer)
{
    uint32_t width = 0;
    bool sign = false;
    const TypeId stripped = type_unqualified(type_strip_aligned(types, type));
    // Equal widths do not make a _BitInt a printf builtin integer class, including through a %n pointer.
    bool result = type_kind(types, stripped) != BUSTER_TYPE_KIND_BIT_INT && check_action_integer_type(types, type, &width, &sign);
    if (result)
    {
        if (w_width != 0)
        {
            // An exact-width C23 modifier checks the intN_t it names: at 64 that is the long class and at 128
            // the __int128 one. Narrower names promote to `int`, so a value fitting the width or the promoted
            // `int` itself answers; a `%wn` pointer keeps the exact element width.
            result = w_width <= 32 ? (pointer ? width == w_width : width <= w_width || width == 32) :
                     w_width == 64 ? (stripped == BUSTER_TYPE_LONG || stripped == BUSTER_TYPE_ULONG) :
                                     (stripped == BUSTER_TYPE_INT128 || stripped == BUSTER_TYPE_UINT128);
        }
        // The MS `I`/`I64` names a __int64 — the long long class, not `l`'s long — while `I32` is the
        // plain int class.
        else if (i_width != 0)
        {
            result = i_width == 32 ? (pointer ? width == 32 : width <= 32) :
                                     (stripped == BUSTER_TYPE_LLONG || stripped == BUSTER_TYPE_ULLONG);
        }
        else switch (length_modifier)
        {
            case 'H': result = pointer ? width == 8 : width <= 32 && width != 16; break;
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
static bool check_common_format_integer_pointer(TypeTable* types, TypeId type, char length_modifier, uint32_t w_width, uint32_t i_width)
{
    TypeId element = type_unqualified(type_strip_aligned(types, type));
    if (type_kind(types, element) != BUSTER_TYPE_KIND_POINTER)
    {
        return false;
    }
    const TypeId target = type_strip_aligned(types, type_element(types, element));
    return (type_qualifiers(target) & BUSTER_TYPE_CONST) == 0 &&
           check_common_format_integer_class(types, target, length_modifier, w_width, i_width, true);
}

static bool check_common_format_width(TypeTable* types, TypeId type)
{
    uint32_t width = 0;
    bool sign = false;
    const TypeId stripped = type_unqualified(type_strip_aligned(types, type));
    const bool result = type_kind(types, stripped) != BUSTER_TYPE_KIND_BIT_INT && check_action_integer_type(types, type, &width, &sign) && width <= 32;
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
    const uint8_t kind = type_kind(types, element);
    // A scalar is never a string: only a pointer or array's element can answer `char *`/`wchar_t *`,
    // otherwise a bare `int` would pass the wide check on its own width.
    const bool indirect = type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_POINTER;
    if (indirect)
    {
        element = type_unqualified(type_element(types, element));
    }
    uint32_t width = 0;
    bool sign = false;
    const TypeId stripped = type_unqualified(type_strip_aligned(types, element));
    const bool integer = type_kind(types, stripped) != BUSTER_TYPE_KIND_BIT_INT && check_action_integer_type(types, element, &width, &sign);
    const bool result = indirect && integer && (wide ? width == 32 && sign : width == 8);
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

// Whether this source/parameter type belongs to a bit-precise format class, directly or through one pointer/array.
static bool check_common_format_bit_precise(const TypeTable* types, TypeId type)
{
    const TypeId stripped = type_unqualified(type_strip_aligned(types, type));
    const uint8_t kind = type_kind(types, stripped);
    const bool indirect = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind);
    const bool result = kind == BUSTER_TYPE_KIND_BIT_INT ||
                        (indirect && type_kind(types, type_unqualified(type_strip_aligned(types, type_element(types, stripped)))) == BUSTER_TYPE_KIND_BIT_INT);
    return result;
}

// A fixed parameter has already converted an argument before format checking. Honor that conversion when
// either class is bit-precise; variadic promotions retain the source class. Other existing classes stay unchanged.
static TypeId check_common_format_argument_type(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t offset, uint32_t argument)
{
    const uint32_t node = graph->argument_nodes[offset + argument];
    TypeId result = node < graph->count ? check_stream_type(stream, node) : 0;
    if (stream->argument_conversions != NULL)
    {
        const CheckConversion conversion = stream->argument_conversions[offset + argument];
        if ((conversion.flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && conversion.type != 0 &&
            (check_common_format_bit_precise(types, result) || check_common_format_bit_precise(types, conversion.type)))
        {
            result = conversion.type;
        }
    }
    return result;
}

static bool check_common_format_attribute(const Parser* parser, Preprocessor* pp, const CheckInputAttributes* attributes,
                                          uint32_t* format_index, uint32_t* first_argument)
{
    bool found = false;
    for (uint32_t a = 0; attributes != NULL && a < attributes->count && !found; a += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + a;
        if (attribute->kind == CHECK_ATTRIBUTE_FORMAT && !attribute->ignored && attribute->argument_count >= 3 &&
            attribute->argument_values != NULL && attribute->argument_values[1] > 0 && attribute->argument_first < parser->tokens.count)
        {
            // The archetype names the checking family: only printf-family semantics belong in this
            // scanner, so `format(scanf, ...)` calls stay unchecked rather than failing printf rules.
            const uint32_t archetype = parser->tokens.names[attribute->argument_first];
            String8 name = { .pointer = (char*)intern_bytes(pp->table, archetype), .length = intern_length(pp->table, archetype) };
            // `__printf__` names the same archetype: strip the reserved spelling as the attribute
            // checker does before it accepts the name.
            if (name.length >= 4 && name.pointer[0] == '_' && name.pointer[1] == '_' && name.pointer[name.length - 1] == '_' &&
                name.pointer[name.length - 2] == '_')
            {
                name.pointer += 2;
                name.length -= 4;
            }
            const bool printf_family = (name.length == 6 && memcmp(name.pointer, "printf", 6) == 0) ||
                                       (name.length == 10 && memcmp(name.pointer, "gnu_printf", 10) == 0) ||
                                       (name.length == 9 && memcmp(name.pointer, "ms_printf", 9) == 0);
            if (printf_family)
            {
                *format_index = (uint32_t)attribute->argument_values[1] - 1;
                // A zero first-argument index selects a va_list-taking callee: the literal is still
                // scanned, but no direct argument can be checked, which UINT32_MAX marks.
                *first_argument = attribute->argument_values[2] > 0 ? (uint32_t)attribute->argument_values[2] - 1 : UINT32_MAX;
                found = true;
            }
        }
    }
    return found;
}

static const char* check_common_format_integer_expected(uint8_t conversion, char length_modifier)
{
    // `%C` is the wide `c`: every modifier that leaves it checked still names `wchar_t`, while `%c`
    // only reaches `wint_t` under an accepted `l` (or a bare `w` lifted to it).
    if (conversion == 'C')
    {
        return "'wchar_t' (aka 'int')";
    }
    if (conversion == 'c')
    {
        return length_modifier == 'l' ? "'wint_t' (aka 'unsigned int')" : "'int'";
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

// The C99 names a `wN`/`wfN` modifier asks for: `int_fast16_t` spells through `int16_t`'s target type, and the
// unsigned conversions swap the sign word.
static const char* check_common_format_w_aka(uint32_t width, bool unsign, bool pointer)
{
    switch (width)
    {
        case 8: return unsign ? (pointer ? "'unsigned char *'" : "'unsigned char'") : (pointer ? "'signed char *'" : "'signed char'");
        case 16: return unsign ? (pointer ? "'unsigned short *'" : "'unsigned short'") : (pointer ? "'short *'" : "'short'");
        case 32: return unsign ? (pointer ? "'unsigned int *'" : "'unsigned int'") : (pointer ? "'int *'" : "'int'");
        case 64: return unsign ? (pointer ? "'unsigned long *'" : "'unsigned long'") : (pointer ? "'long *'" : "'long'");
        default: return unsign ? (pointer ? "'unsigned __int128 *'" : "'unsigned __int128'") : (pointer ? "'__int128 *'" : "'__int128'");
    }
}

static const char* check_common_format_w_expected(uint8_t conversion, uint32_t w_width, bool w_fast, bool pointer, Arena* scratch)
{
    const bool unsign = conversion == 'u' || conversion == 'o' || conversion == 'x' || conversion == 'X' ||
                        conversion == 'b' || conversion == 'B';
    const char* family = w_fast ? (unsign ? "uint_fast" : "int_fast") : (unsign ? "uint" : "int");
    const char* aka = check_common_format_w_aka(w_width, unsign, pointer);
    return check_warning_text(scratch, pointer ? "'%s%u_t *' (aka %s)" : "'%s%u_t' (aka %s)", family, w_width, aka);
}

// The MS `I`/`I32`/`I64` names keep their own spellings; unlike `wN` the `b`/`B` conversions stay signed.
static const char* check_common_format_i_expected(uint8_t conversion, uint32_t i_width)
{
    const bool unsign = conversion == 'u' || conversion == 'o' || conversion == 'x' || conversion == 'X';
    if (i_width == 32)
    {
        return unsign ? "'unsigned __int32' (aka 'unsigned int')" : "'__int32' (aka 'int')";
    }
    return unsign ? "'unsigned __int64' (aka 'unsigned long long')" : "'__int64' (aka 'long long')";
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
                                              uint64_t offset, uint32_t* order, uint32_t id, const char* message, Arena* scratch)
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
            // A raw source location keeps the literal's own column, while `order` keeps the collector's scan order
            // with the arguments' diagnostics: clang emits a literal's reports as it walks conversions, so an
            // in-string report after an argument's sorts behind it at semantic phase rather than back at the
            // literal ahead of it.
            *order = *order < source ? source : *order;
            pp_diagnose_output_location_at(pp, disposition, source, pp->output.locations[source] + 1 + (uint32_t)offset,
                                           *order, PP_DIAGNOSTIC_SEMANTIC, "%s%s", message, suffix);
        }
    }
}

// The same literal-column diagnostic, sorted after another token: a missing argument belongs to the end of a
// conversion sequence, after the earlier conversions' own argument diagnostics, not at the format token's spot.
static void check_common_format_report_offset_ordered(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t format_token,
                                                      uint64_t offset, uint32_t position_token, uint32_t* order, uint32_t id, const char* message, Arena* scratch)
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
            const uint32_t natural = position_token < parser->tokens.count ? parser->tokens.sources[position_token] : source;
            *order = natural > *order ? natural : *order;
            pp_diagnose_output_location_at(pp, disposition, source, pp->output.locations[source] + 1 + (uint32_t)offset,
                                           *order, PP_DIAGNOSTIC_SEMANTIC, "%s%s", message, suffix);
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
    // A position needs at least one digit: `%$d` leaves the `$` to clang's invalid-specifier diagnostic.
    const bool result = cursor > *at && cursor < length && bytes[cursor] == '$';
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
                                              uint8_t* covered, uint32_t* order, Arena* scratch)
{
    const bool result = position != 0 && position <= data_count;
    if (position == 0)
    {
        check_common_format_report_offset(parser, pp, input, format_token, percent_offset, order, CHECK_WARNING_ID_FORMAT,
                                          "position arguments in format strings start counting at 1 (not 0)", scratch);
    }
    else if (position > data_count)
    {
        const char* message = check_warning_text(scratch, "data argument position '%u' exceeds the number of data arguments (%u)",
                                                 position, data_count);
        check_common_format_report_offset(parser, pp, input, format_token, conversion_offset, order, CHECK_WARNING_ID_FORMAT, message, scratch);
    }
    if (result && covered != NULL)
    {
        covered[position - 1] = 1;
    }
    return result;
}

static void check_common_format_missing_width(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t format_token,
                                              uint64_t offset, bool precision, uint32_t* order, Arena* scratch)
{
    const char* message = precision ? "'.*' specified field precision is missing a matching 'int' argument" :
                                      "'*' specified field width is missing a matching 'int' argument";
    check_common_format_report_offset(parser, pp, input, format_token, offset, order, CHECK_WARNING_ID_FORMAT, message, scratch);
}

static void check_common_format_invalid_position(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t format_token,
                                                 uint64_t offset, bool precision, uint32_t* order, Arena* scratch)
{
    const char* message = precision ? "invalid position specified for field precision" : "invalid position specified for field width";
    check_common_format_report_offset(parser, pp, input, format_token, offset, order, CHECK_WARNING_ID_FORMAT, message, scratch);
}

static void check_common_format_report_type(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                            TypeTable* types, CheckWarningSpelling* spell, const CheckGraph* graph, uint32_t offset, uint32_t argument,
                                            uint32_t format_token, const char* expected, bool width, uint32_t* order, Arena* scratch)
{
    const uint32_t argument_node = graph->argument_nodes[offset + argument];
    const TypeId type = check_common_format_argument_type(graph, stream, types, offset, argument);
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
            // The literal's scan emits a conversion's diagnostics together: an argument report carries the order
            // forward so a later conversion's in-string report keeps sorting behind it.
            const uint32_t natural = at_token < parser->tokens.count ? parser->tokens.sources[at_token] : *order;
            *order = natural > *order ? natural : *order;
            check_common_report_published(parser, pp, input, at_token, *order, CHECK_WARNING_ID_FORMAT, false, message, NULL, 0, scratch);
        }
    }
}

static void check_common_format_width_report(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                             TypeTable* types, CheckWarningSpelling* spell, const CheckGraph* graph, uint32_t offset, uint32_t argument,
                                             uint32_t format_token, uint64_t literal_offset, bool precision, uint32_t* order, Arena* scratch)
{
    const uint32_t argument_node = graph->argument_nodes[offset + argument];
    const TypeId type = check_common_format_argument_type(graph, stream, types, offset, argument);
    const String8 actual = check_common_type_text(parser, pp, input, types, spell, scratch, type);
    if (actual.length != 0)
    {
        const char* message = check_warning_text(scratch, "field %s should have type 'int', but argument has type %.*s",
                                                  precision ? "precision" : "width", (int)actual.length, actual.pointer);
        if (message != NULL)
        {
            const uint32_t at_token = argument_node < graph->count ? check_common_argument_start(parser, graph->tokens[argument_node]) : format_token;
            const uint32_t natural = at_token < parser->tokens.count ? parser->tokens.sources[at_token] : *order;
            *order = natural > *order ? natural : *order;
            check_common_format_report_offset(parser, pp, input, format_token, literal_offset, order, CHECK_WARNING_ID_FORMAT, message, scratch);
        }
    }
}

// A leaf-walk node pushed on the worklist, growing its arena column on demand: the walk's depth is
// the format argument's cast-and-conditional spine depth, small in practice but unbounded in the
// grammar, so a fixed bound would either truncate valid arguments or waste a graph-sized column.
static void check_common_format_leaf_push(Preprocessor* pp, const CheckGraph* graph, Arena* scratch, uint32_t** leaf_nodes,
                                          uint32_t* leaf_capacity, uint32_t* leaf_count, uint32_t node)
{
    if (node < graph->count && !pp->fatal)
    {
        if (*leaf_count == *leaf_capacity)
        {
            const uint32_t grown = *leaf_capacity * 2;
            uint32_t* next = CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, grown);
            if (next == NULL)
            {
                check_common_exhausted(pp);
            }
            else
            {
                memcpy(next, *leaf_nodes, (uint64_t)*leaf_count * sizeof(uint32_t));
                *leaf_nodes = next;
                *leaf_capacity = grown;
            }
        }
        if (!pp->fatal)
        {
            (*leaf_nodes)[(*leaf_count)++] = node;
        }
    }
}

// The conditional `node`'s condition reduced to a compile-time truth value, as clang's
// EvaluateAsBooleanCondition does before deciding which arm to check: when the condition folds to an
// integer constant through the same casts the spine passes, only that arm is checked.
static bool check_common_format_condition(const CheckGraph* graph, const CheckStream* stream, uint32_t node, bool* taken)
{
    // The outermost folded node holds the converted value: `(unsigned char)256` is zero, not 256, so
    // the walk stops at the first constant rather than stripping casts down to the literal.
    uint32_t condition = graph->operand0[node];
    for (uint32_t steps = 0; steps < graph->count && condition < graph->count && !check_common_integer_constant(stream, condition); steps += 1)
    {
        const CheckOp condition_op = graph->ops[condition];
        if (condition_op != CHECK_OP_CONVERT && condition_op != CHECK_OP_POSITIVE && condition_op != CHECK_OP_CAST)
        {
            break;
        }
        condition = graph->operand0[condition];
    }
    const bool constant = check_common_integer_constant(stream, condition);
    *taken = constant && check_stream_value(stream, condition).integer != 0;
    return constant;
}

static void check_common_format(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                TypeTable* types, CheckWarningSpelling* spell, const CheckProgram* program, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const CheckOp calls[] = { CHECK_OP_CALL, CHECK_OP_BUILTIN_X86 };
    const CheckCommonOps set = check_common_ops(calls, BUSTER_ARRAY_LENGTH(calls));
    CheckCommonScan scan = { 0 };
    for (uint32_t node = check_common_next_op(graph, &set, &scan, vector); node < graph->count; node = check_common_next_op(graph, &set, &scan, vector))
    {
        const bool intrinsic = graph->ops[node] == CHECK_OP_BUILTIN_X86;
        if ((graph->ops[node] != CHECK_OP_CALL && !intrinsic) || stream->diagnostics[node] != CHECK_DIAG_NONE)
        {
            continue;
        }
        const uint32_t callee = !intrinsic ? graph->operand0[node] : BUSTER_CHECK_NONE;
        uint32_t callee_token = callee < graph->count ? graph->tokens[callee] : BUSTER_CHECK_NONE;
        if (intrinsic)
        {
            // Explicit library builtins keep their source call receipt through deferred merging too. The graph
            // checks their prototype through BUILTIN_X86, not through an ordinary CALL node.
            const uint32_t closing = graph->tokens[node];
            const uint32_t id = input->intrinsic_ids != NULL && closing < input->token_count ? input->intrinsic_ids[closing] : 0;
            const CheckInputIntrinsicCall* call = input->intrinsic_calls != NULL && id != 0 && id <= input->intrinsic_call_count ?
                                                  input->intrinsic_calls + id - 1 : NULL;
            callee_token = call != NULL && call->closing == closing ? call->keyword : BUSTER_CHECK_NONE;
        }
        if (callee_token >= parser->tokens.count || parser->tokens.kinds[callee_token] != PARSE_KIND_IDENTIFIER)
        {
            continue;
        }
        const uint32_t name_id = parser->tokens.names[callee_token];
        const String8 name = { .pointer = (char*)intern_bytes(pp->table, name_id), .length = intern_length(pp->table, name_id) };
        const uint32_t callee_object = callee < graph->count ? check_stream_object(stream, callee) : 0;
        // A source use reads its visible declaration snapshot, not a later declaration of the same object.
        const uint32_t visible = input->declarations != NULL && callee_token < input->token_count ?
                                 input->declarations[callee_token] : BUSTER_CHECK_NONE;
        const uint32_t definition = visible < input->token_count ? visible :
                                    program != NULL && callee_object != 0 && callee_object <= program->object_count ?
                                    program->object_declarations[callee_object] : BUSTER_CHECK_NONE;
        // A spelled printf-family name only applies where the callee resolves to a function declaration
        // or stays undeclared: a local object — a function pointer named `printf` — can shadow the
        // library name without its semantics.
        const bool function_callee = callee_object == 0 ||
                                     (definition < input->token_count && input->declaration_kinds != NULL &&
                                      input->declaration_kinds[definition] == C_BINDING_FUNCTION);
        uint32_t format_index = function_callee ? check_common_format_index(name, &pp->options) : UINT32_MAX;
        uint32_t first_argument = format_index != UINT32_MAX ? format_index + 1 : UINT32_MAX;
        if (format_index == UINT32_MAX && definition < input->token_count)
        {
            bool found = check_common_format_attribute(parser, pp, check_input_attributes_at(input, definition), &format_index, &first_argument);
            const uint32_t canonical = input->declarations != NULL ? input->declarations[definition] : BUSTER_CHECK_NONE;
            found = found || (canonical < input->token_count &&
                              check_common_format_attribute(parser, pp, check_input_attributes_at(input, canonical), &format_index, &first_argument));
        }
        const uint32_t offset = graph->argument_offsets[node];
        const uint32_t count = graph->argument_counts[node];
        const bool args_checked = first_argument != UINT32_MAX;
        if (format_index != UINT32_MAX && format_index < count && (first_argument == UINT32_MAX || first_argument <= count) &&
            offset <= graph->argument_count && count <= graph->argument_count - offset)
        {
            const uint32_t format_node = graph->argument_nodes[offset + format_index];
            // The format expression's literal spine, walked depth-first in arm order the way clang's
            // checkFormatStringExpr recurses: casts, conversions and unary plus pass operand0 through,
            // and a conditional passes each arm in order — or only the taken arm when the condition is
            // a compile-time constant. Whatever else pops is a leaf; the first leaf that is not a
            // string literal ends the walk, like SLCT_NotALiteral's early return.
            uint32_t leaf_capacity = 16;
            uint32_t* leaf_nodes = CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, leaf_capacity);
            if (leaf_nodes == NULL)
            {
                check_common_exhausted(pp);
            }
            uint32_t leaf_count = 0;
            if (leaf_nodes != NULL && format_node < graph->count)
            {
                leaf_nodes[leaf_count++] = format_node;
            }
            // Each literal keeps its own argument coverage: the extra-arguments verdict is the
            // largest first-uncovered index any finished literal left, while a literal covering —
            // or needing — every argument suppresses it for the rest, like clang's
            // UncoveredArgHandler. Specifier, insufficiency and type diagnostics stay per literal.
            const uint32_t data_count = args_checked ? count - first_argument : 0;
            uint8_t* covered = data_count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, data_count) : NULL;
            if (data_count != 0 && covered == NULL)
            {
                check_common_exhausted(pp);
            }
            uint32_t order = 0;
            uint32_t max_uncovered = BUSTER_CHECK_NONE;
            bool all_covered = false;
            bool nonliteral_leaf = false;
            while (leaf_count != 0 && !nonliteral_leaf && !pp->fatal)
            {
                const uint32_t leaf = leaf_nodes[--leaf_count];
                const CheckOp leaf_op = graph->ops[leaf];
                if ((leaf_op == CHECK_OP_CONVERT || leaf_op == CHECK_OP_POSITIVE || leaf_op == CHECK_OP_CAST) && graph->operand0[leaf] < leaf)
                {
                    check_common_format_leaf_push(pp, graph, scratch, &leaf_nodes, &leaf_capacity, &leaf_count, graph->operand0[leaf]);
                    continue;
                }
                if (leaf_op == CHECK_OP_CONDITIONAL && (graph->operand1[leaf] < leaf || graph->operand2[leaf] < leaf))
                {
                    bool taken = false;
                    const bool constant = check_common_format_condition(graph, stream, leaf, &taken);
                    if (!constant || !taken)
                    {
                        check_common_format_leaf_push(pp, graph, scratch, &leaf_nodes, &leaf_capacity, &leaf_count, graph->operand2[leaf] < leaf ? graph->operand2[leaf] : graph->count);
                    }
                    if (!constant || taken)
                    {
                        check_common_format_leaf_push(pp, graph, scratch, &leaf_nodes, &leaf_capacity, &leaf_count, graph->operand1[leaf] < leaf ? graph->operand1[leaf] : graph->count);
                    }
                    continue;
                }
                const uint32_t format_token = graph->tokens[leaf];
                const uint32_t string = format_token < input->token_count ? check_common_sparse_get(input->string_ids, format_token) : BUSTER_CHECK_NONE;
                if (string == BUSTER_CHECK_NONE || string >= input->string_count || input->strings == NULL)
                {
                    nonliteral_leaf = true;
                    break;
                }
                if (covered != NULL)
                {
                    memset(covered, 0, data_count);
                }
                const uint8_t* bytes = input->strings[string].bytes;
                uint64_t length = input->strings[string].byte_count;
                uint32_t argument = first_argument;
                // Diagnostics publish in the literal's scan order: `order` is the furthest source any
                // conversion's reports have reached, so an in-string report after an argument's keeps
                // sorting behind it.
                if (order == 0)
                {
                    order = parser->tokens.sources[format_token];
                }
                bool insufficient = false;
                bool positional_seen = false;
                bool plain_seen = false;
                bool malformed = false;
                uint64_t insufficient_offset = 0;
                for (uint64_t at = 0; at < length && !pp->fatal; at += 1)
                {
                    // The literal's storage ends with a NUL terminator at `length - 1`; any earlier zero
                    // came from an escape in the body and ends the format string Clang-side too: warn at
                    // the NUL and stop, so trailing conversions and their arguments never register.
                    if (bytes[at] == 0 && at + 1 < length)
                    {
                        check_common_format_report_offset(parser, pp, input, format_token, at, &order, CHECK_WARNING_ID_FORMAT,
                                                          "format string contains '\\0' within the string body", scratch);
                        malformed = true;
                        break;
                    }
                    if (bytes[at] != '%')
                    {
                        continue;
                    }
                    const uint64_t percent = at;
                    at += 1;
                    if (at >= length)
                    {
                        check_common_format_report_offset(parser, pp, input, format_token, percent, &order, CHECK_WARNING_ID_FORMAT,
                                                          "incomplete format specifier", scratch);
                        malformed = true;
                        break;
                    }
                    if (bytes[at] == '%')
                    {
                        continue;
                    }
                    // The value's position comes before every flag: `%0$d` is positional 0, while `%+1$d` reads
                    // its `$` as the conversion after the `+` flag. A `*N$` width or precision is positional
                    // syntax only under a positional value; otherwise the digits reach the conversion, where clang
                    // reports them as the invalid specifier.
                    uint32_t value_position = 0;
                    const bool value_positioned = check_common_format_position(bytes, length, &at, &value_position);
                    while (at < length && (bytes[at] == '-' || bytes[at] == '+' || bytes[at] == ' ' || bytes[at] == '#' ||
                                           bytes[at] == '0' || bytes[at] == '\''))
                    {
                        at += 1;
                    }
                    bool width_argument = false;
                    bool width_positioned = false;
                    uint32_t width_position = 0;
                    uint64_t width_offset = at;
                    if (at < length && bytes[at] == '*')
                    {
                        width_argument = true;
                        width_offset = at;
                        at += 1;
                        width_positioned = value_positioned && check_common_format_position(bytes, length, &at, &width_position);
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
                            precision_positioned = value_positioned && check_common_format_position(bytes, length, &at, &precision_position);
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
                    const uint64_t length_offset = at;
                    const bool c23 = parser->language.standard >= 2023;
                    // C23's `H`/`D`/`DD` name decimal floating widths; they sit outside `length_modifier` so the
                    // `hh` collapse to 'H' below cannot alias them.
                    uint8_t decimal_bits = 0;
                    uint32_t i_width = 0;
                    bool length_doubled = false;
                    if (c23 && at < length && bytes[at] == 'H')
                    {
                        decimal_bits = 32;
                        at += 1;
                    }
                    else if (c23 && at < length && bytes[at] == 'D')
                    {
                        decimal_bits = 64;
                        at += 1;
                        if (at < length && bytes[at] == 'D')
                        {
                            decimal_bits = 128;
                            at += 1;
                        }
                    }
                    else if (at < length && (bytes[at] == 'h' || bytes[at] == 'l' || bytes[at] == 'j' || bytes[at] == 'z' || bytes[at] == 't' ||
                                            bytes[at] == 'L' || bytes[at] == 'w' || bytes[at] == 'q' || bytes[at] == 'I'))
                    {
                        length_modifier = (char)bytes[at];
                        at += 1;
                        if (at < length && bytes[at] == length_modifier && (length_modifier == 'h' || length_modifier == 'l'))
                        {
                            length_modifier = length_modifier == 'h' ? 'H' : 'L';
                            length_doubled = true;
                            at += 1;
                        }
                        // The MS `I` modifier defaults to a 64-bit width; `I32`/`I64` spell it longhand. Other
                        // digits stay the conversion, so `%I6d` reports `6` where `I6` never formed.
                        else if (length_modifier == 'I')
                        {
                            i_width = 64;
                            if (at + 1 < length && bytes[at] == '3' && bytes[at + 1] == '2')
                            {
                                i_width = 32;
                                at += 2;
                            }
                            else if (at + 1 < length && bytes[at] == '6' && bytes[at + 1] == '4')
                            {
                                at += 2;
                            }
                        }
                    }
                    // C23's `wN`/`wfN` widths follow the `w`: an `f` then digits picks an int_fastN_t, bare
                    // digits an intN_t. Without digits the lone `w` keeps the pre-C23 GNU meaning below.
                    uint32_t w_width = 0;
                    bool w_digits = false;
                    bool w_fast = false;
                    if (c23 && length_modifier == 'w' && at < length && bytes[at] == 'f' && at + 1 < length &&
                        bytes[at + 1] >= '1' && bytes[at + 1] <= '9')
                    {
                        w_fast = true;
                        at += 1;
                    }
                    // The width never leads with a zero: `%w0d` leaves `0` to be the conversion, which is invalid.
                    if (c23 && length_modifier == 'w' && at < length && bytes[at] >= '1' && bytes[at] <= '9')
                    {
                        while (at < length && bytes[at] >= '0' && bytes[at] <= '9')
                        {
                            w_digits = true;
                            w_width = w_width < 1000000 ? w_width * 10 + (uint32_t)(bytes[at] - '0') : w_width;
                            at += 1;
                        }
                    }
                    const uint8_t conversion = at < length ? bytes[at] : 0;
                    if (conversion == 0)
                    {
                        // A specifier cut off by the literal's end warns at its `%`; one that runs into an
                        // embedded NUL — a zero before the storage terminator — warns at the NUL. Either
                        // way the scan stops, so the trailing argument accounting is skipped.
                        const bool nul = at + 1 < length && bytes[at] == 0;
                        check_common_format_report_offset(parser, pp, input, format_token, nul ? at : percent, &order, CHECK_WARNING_ID_FORMAT,
                                                          nul ? "format string contains '\\0' within the string body" : "incomplete format specifier", scratch);
                        malformed = true;
                        break;
                    }
                    const bool conversion_integer = conversion == 'd' || conversion == 'i' || conversion == 'u' || conversion == 'o' ||
                                                    conversion == 'x' || conversion == 'X' || conversion == 'b' || conversion == 'B' ||
                                                    conversion == 'c' || conversion == 'C';
                    const bool conversion_string = conversion == 's' || conversion == 'S';
                    bool wide = length_modifier == 'l' || conversion == 'S' || conversion == 'C';
                    const bool conversion_pointer = conversion == 'p';
                    const bool conversion_float = conversion == 'f' || conversion == 'F' || conversion == 'e' || conversion == 'E' ||
                                                  conversion == 'g' || conversion == 'G' || conversion == 'a' || conversion == 'A';
                    const bool conversion_number = conversion == 'n';
                    const bool conversion_argument = conversion_integer || conversion_string || conversion_pointer || conversion_float || conversion_number;
                    // `%m` is a valid GNU conversion but takes no data argument; a `%` conversion — a modifier
                    // then a bare percent — keeps `%%`'s literal meaning and ends the specifier whole.
                    const bool conversion_valid = conversion_argument || conversion == 'm' || conversion == '%';
                    if (!conversion_valid)
                    {
                        // clang's invalid conversion never reaches its argument check: `*` or `.*` spent their
                        // running argument index while the specifier was parsed but their arguments go unchecked
                        // and uncovered. The conversion covers the argument its index names — `N$` seeds that
                        // index, so a positioned invalid specifier covers its own argument — and keeps scanning
                        // while that argument exists. Otherwise the literal is abandoned whole: `%Q%` never
                        // reaches the trailing `%`, and `%5$Q` reports the position nowhere.
                        if (width_argument && !width_positioned && argument < count)
                        {
                            argument += 1;
                        }
                        if (precision_argument && !precision_positioned && argument < count)
                        {
                            argument += 1;
                        }
                        if (value_positioned && value_position == 0)
                        {
                            check_common_format_report_offset(parser, pp, input, format_token, percent, &order, CHECK_WARNING_ID_FORMAT,
                                                              "position arguments in format strings start counting at 1 (not 0)", scratch);
                            malformed = true;
                            break;
                        }
                        const uint32_t cover = value_positioned ? value_position - 1 : 0;
                        if (cover < data_count)
                        {
                            covered[cover] = 1;
                        }
                        char spelling[6];
                        if (conversion >= 0x20 && conversion < 0x7f)
                        {
                            spelling[0] = (char)conversion;
                            spelling[1] = 0;
                        }
                        else
                        {
                            snprintf(spelling, sizeof(spelling), "\\x%02x", conversion);
                        }
                        const char* message = check_warning_text(scratch, "invalid conversion specifier '%s'", spelling);
                        check_common_format_report_offset(parser, pp, input, format_token, at, &order, CHECK_WARNING_ID_FORMAT_INVALID_SPECIFIER,
                                                          message, scratch);
                        // A va_list callee's data start at index 0 for clang, so the whole call's argument
                        // count bounds the index: every plain `%Q` keeps scanning, and so does a `%N$Q`
                        // whose N names an argument of the call — `render("%3$Q %Q", ap)` stops at the first.
                        const uint32_t invalid_bound = args_checked ? data_count : count;
                        if (cover >= invalid_bound)
                        {
                            break;
                        }
                        continue;
                    }
                    positional_seen |= value_positioned || width_positioned || precision_positioned;
                    plain_seen |= (width_argument && !width_positioned) || (precision_argument && !precision_positioned);
                    // Every length modifier owns a set of conversions; anywhere else it earns the no-effect
                    // diagnostic and clang still checks the plain type for `s`/`p`/`f` while the integer family
                    // (c included) goes unchecked. `h`,`hh`,`z`,`j`,`t`,`q` own the integer conversions and `n`;
                    // `l` owns everything but `p` (c/s lift to their wide forms, `f` is a silent no-op); `L` owns
                    // the integer conversions — as `ll` — and the floats, but not `c`, `n`, `s`, or `p`. A bare
                    // `w` owns only `c`/`s` (same lift); `q` acts as `ll`. C23's `wN`/`wfN` own the integer
                    // conversions and `n` at width N; `H`/`D`/`DD` own the floats where they name _Decimal types.
                    const bool int_family = conversion_integer || conversion_number;
                    const bool w_valid_conv = int_family && conversion != 'c' && conversion != 'C';
                    const bool w_standard_width = w_width == 8 || w_width == 16 || w_width == 32 || w_width == 64;
                    const bool w_valid_width = w_standard_width || (!w_fast && w_width == 128);
                    const bool decimal_modifier = decimal_bits != 0;
                    char modifier_text[8];
                    const uint64_t modifier_length = at - length_offset < 8 ? at - length_offset : 7;
                    memcpy(modifier_text, bytes + length_offset, modifier_length);
                    modifier_text[modifier_length] = 0;
                    bool value_checked = true;
                    bool decimal_unsupported = false;
                    bool modifier_valid = false;
                    if (decimal_modifier)
                    {
                        if (conversion_float)
                        {
                            // clang knows _Decimal32/64/128 here but has no target support: the check reports
                            // the type itself, ahead of any argument type verdict.
                            decimal_unsupported = true;
                            value_checked = false;
                            modifier_valid = true;
                        }
                    }
                    else if (length_modifier == 'w' && (w_digits || w_fast))
                    {
                        if (w_valid_conv && w_valid_width)
                        {
                            modifier_valid = true;
                            if (!w_standard_width)
                            {
                                // `w128` checks an int128_t but still earns the nonstandard-width diagnostic.
                                const char* message = check_warning_text(scratch, "length modifier '%s' results in undefined behavior or no effect with '%c' conversion specifier",
                                                                         modifier_text, conversion);
                                check_common_format_report_offset(parser, pp, input, format_token, length_offset, &order, CHECK_WARNING_ID_FORMAT, message, scratch);
                            }
                        }
                    }
                    else switch (length_modifier)
                    {
                        case 0: modifier_valid = true; break;
                        case 'h':
                        case 'H':
                        case 'z':
                        case 'j':
                        case 't':
                        case 'q': modifier_valid = w_valid_conv; break;
                        case 'l': modifier_valid = !(conversion_pointer || conversion == 'S' || conversion == 'C'); break;
                        case 'L': modifier_valid = length_doubled ? w_valid_conv :
                                                          (conversion_integer && conversion != 'c' && conversion != 'C') || conversion_float; break;
                        // A bare `w` and the MS `I` family are meaningless on every conversion, but the
                        // argument still checks: `w` lifts `c`/`s` to their wide forms, `I` checks its
                        // width on the integer conversions, and `C`/`S` keep their wide checks under any
                        // rejected modifier.
                        case 'w':
                        case 'I': modifier_valid = false; break;
                    }
                    if (!modifier_valid && conversion != 'm' && conversion != '%')
                    {
                        // `%m` and `%` take no argument and tolerate any modifier silently.
                        const char* message = check_warning_text(scratch, "length modifier '%s' results in undefined behavior or no effect with '%c' conversion specifier",
                                                                 modifier_text, conversion);
                        check_common_format_report_offset(parser, pp, input, format_token, length_offset, &order, CHECK_WARNING_ID_FORMAT, message, scratch);
                        value_checked = !int_family || conversion == 'C' ||
                                        (length_modifier == 'w' && !w_digits && conversion == 'c') ||
                                        (i_width != 0 && conversion_integer && conversion != 'c' && conversion != 'C');
                        if (length_modifier == 'w' && !w_digits)
                        {
                            // A bare `w` still checks `c`/`s` as the wide forms it lifts them to.
                            length_modifier = 'l';
                            wide = true;
                        }
                        else
                        {
                            length_modifier = 0;
                        }
                    }
                    else if (length_modifier == 'q')
                    {
                        // `q` checks as `ll`.
                        length_modifier = 'L';
                    }
                    // Resolve the width and precision first: they take their data arguments before the value.
                    if (width_argument && args_checked)
                    {
                        uint32_t width_index = first_argument;
                        if (width_positioned)
                        {
                            if (width_position == 0)
                            {
                                check_common_format_report_offset(parser, pp, input, format_token, width_offset, &order, CHECK_WARNING_ID_FORMAT,
                                                                  "position arguments in format strings start counting at 1 (not 0)", scratch);
                                malformed = true;
                            }
                            else if (width_position > data_count)
                            {
                                check_common_format_missing_width(parser, pp, input, format_token, width_offset, false, &order, scratch);
                                malformed = true;
                            }
                            else
                            {
                                width_index = first_argument + width_position - 1;
                                covered[width_position - 1] = 1;
                            }
                        }
                        else if (positional_seen)
                        {
                            check_common_format_invalid_position(parser, pp, input, format_token, width_offset, false, &order, scratch);
                            malformed = true;
                        }
                        else if (argument >= count)
                        {
                            // clang's field-width amount starts one past the `*`: the missing-argument
                            // diagnostic points at the conversion, not the star.
                            check_common_format_missing_width(parser, pp, input, format_token, width_offset + 1, false, &order, scratch);
                            malformed = true;
                        }
                        else
                        {
                            width_index = argument++;
                            covered[width_index - first_argument] = 1;
                        }
                        if (malformed)
                        {
                            break;
                        }
                        const TypeId width_type = check_common_format_argument_type(graph, stream, types, offset, width_index);
                        if (!check_common_format_width(types, width_type))
                        {
                            check_common_format_width_report(parser, pp, input, stream, types, spell, graph, offset, width_index, format_token,
                                                             width_positioned ? width_offset : width_offset + 1, false, &order, scratch);
                            malformed = true;
                            break;
                        }
                    }
                    if (precision_argument && args_checked)
                    {
                        uint32_t precision_index = first_argument;
                        if (precision_positioned)
                        {
                            if (precision_position == 0)
                            {
                                check_common_format_report_offset(parser, pp, input, format_token, precision_offset + 1, &order, CHECK_WARNING_ID_FORMAT,
                                                                  "position arguments in format strings start counting at 1 (not 0)", scratch);
                                malformed = true;
                            }
                            else if (precision_position > data_count)
                            {
                                check_common_format_missing_width(parser, pp, input, format_token, precision_offset, true, &order, scratch);
                                malformed = true;
                            }
                            else
                            {
                                precision_index = first_argument + precision_position - 1;
                                covered[precision_position - 1] = 1;
                            }
                        }
                        else if (positional_seen)
                        {
                            check_common_format_invalid_position(parser, pp, input, format_token, precision_offset + 1, true, &order, scratch);
                            malformed = true;
                        }
                        else if (argument >= count)
                        {
                            // The precision amount carries the `.` prefix into its start, so clang's
                            // missing-argument diagnostic lands on the `*` itself.
                            check_common_format_missing_width(parser, pp, input, format_token, precision_offset + 1, true, &order, scratch);
                            malformed = true;
                        }
                        else
                        {
                            precision_index = argument++;
                            covered[precision_index - first_argument] = 1;
                        }
                        if (malformed)
                        {
                            break;
                        }
                        const TypeId precision_type = check_common_format_argument_type(graph, stream, types, offset, precision_index);
                        if (!check_common_format_width(types, precision_type))
                        {
                            check_common_format_width_report(parser, pp, input, stream, types, spell, graph, offset, precision_index, format_token,
                                                             precision_positioned ? precision_offset : precision_offset + 1, true, &order, scratch);
                            malformed = true;
                            break;
                        }
                    }
                    bool value_valid = true;
                    uint32_t value_index = first_argument;
                    if (conversion_argument)
                    {
                        if (value_positioned)
                        {
                            if (value_position == 0)
                            {
                                // `0$` is a literal error clang reports even for a va_list callee.
                                check_common_format_report_offset(parser, pp, input, format_token, percent, &order, CHECK_WARNING_ID_FORMAT,
                                                                  "position arguments in format strings start counting at 1 (not 0)", scratch);
                                value_valid = false;
                            }
                            else if (args_checked)
                            {
                                value_valid = check_common_format_data_position(parser, pp, input, format_token, percent, at, value_position,
                                                                                data_count, covered, &order, scratch);
                                if (value_valid)
                                {
                                    value_index = first_argument + value_position - 1;
                                }
                            }
                        }
                        else if (positional_seen || !args_checked)
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
                            covered[value_index - first_argument] = 1;
                            plain_seen = true;
                        }
                    }
                    if (positional_seen && plain_seen)
                    {
                        check_common_format_report_offset(parser, pp, input, format_token, at, &order, CHECK_WARNING_ID_FORMAT,
                                                          "cannot mix positional and non-positional arguments in format string", scratch);
                        malformed = true;
                        break;
                    }
                    if (!value_valid)
                    {
                        malformed = true;
                        break;
                    }
                    if (conversion_argument && decimal_unsupported && args_checked)
                    {
                        // `_Decimal32/64/128` name real types the target lacks; clang reports the type at the
                        // argument's position rather than running a type verdict.
                        const uint32_t argument_node = graph->argument_nodes[offset + value_index];
                        const uint32_t argument_token = argument_node < graph->count ? graph->tokens[argument_node] : format_token;
                        const char* decimal_name = decimal_bits == 32 ? "_Decimal32" : decimal_bits == 64 ? "_Decimal64" : "_Decimal128";
                        const char* message = check_warning_text(scratch, "format specifies type '%s' which is not supported yet", decimal_name);
                        const uint32_t natural = argument_token < parser->tokens.count ? parser->tokens.sources[argument_token] : order;
                        order = natural > order ? natural : order;
                        check_common_report_published(parser, pp, input, argument_token, order, CHECK_WARNING_ID_FORMAT, false, message, NULL, 0, scratch);
                    }
                    else if (conversion_argument && value_checked && args_checked)
                    {
                        const uint32_t argument_node = graph->argument_nodes[offset + value_index];
                        const TypeId type = check_common_format_argument_type(graph, stream, types, offset, value_index);
                        const bool long_double = length_modifier == 'L';
                        const bool w_check = length_modifier == 'w' && w_digits;
                        // `I`+`c` never reaches this block and `I`+`C` keeps `C`'s own `wchar_t` check, so
                        // the MS width only answers for the remaining integer conversions.
                        const bool i_check = i_width != 0 && conversion != 'c' && conversion != 'C';
                        bool accepted = false;
                        const char* expected = NULL;
                        if (conversion_integer)
                        {
                            const bool character = conversion == 'c' || conversion == 'C';
                            accepted = check_common_format_integer_class(types, type, character ? 0 : length_modifier, w_check ? w_width : 0, i_check ? i_width : 0, false);
                            expected = w_check ? check_common_format_w_expected(conversion, w_width, w_fast, false, scratch) :
                                       i_check ? check_common_format_i_expected(conversion, i_width) :
                                                 check_common_format_integer_expected(conversion, length_modifier);
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
                            accepted = check_common_format_integer_pointer(types, type, length_modifier, w_check ? w_width : 0, i_check ? i_width : 0);
                            expected = w_check ? check_common_format_w_expected(conversion, w_width, w_fast, true, scratch) :
                                                 check_common_format_integer_pointer_expected(length_modifier);
                        }
                        if (!accepted)
                        {
                            check_common_format_report_type(parser, pp, input, stream, types, spell, graph, offset, value_index, format_token, expected, false, &order, scratch);
                        }
                        else if (conversion == 'c' && type_unqualified(type_strip_aligned(types, type)) == BUSTER_TYPE_BOOL)
                        {
                            const uint32_t at_token = argument_node < graph->count ? check_common_argument_start(parser, graph->tokens[argument_node]) : format_token;
                            const uint64_t specifier_length = at - percent + 1;
                            const int printed_length = specifier_length > INT_MAX ? INT_MAX : (int)specifier_length;
                            const char* message = check_warning_text(scratch, "using '%.*s' format specifier, but argument has boolean value",
                                                                     printed_length, bytes + percent);
                            const uint32_t natural = at_token < parser->tokens.count ? parser->tokens.sources[at_token] : order;
                            order = natural > order ? natural : order;
                            if (message != NULL)
                            {
                                check_common_report_published(parser, pp, input, at_token, order, CHECK_WARNING_ID_FORMAT, false, message, NULL, 0, scratch);
                            }
                        }
                    }
                }
                // The finished literal's argument verdict: needing every argument claims them all,
                // otherwise its first uncovered index joins the call's extra-arguments verdict. A
                // literal the scan abandoned contributes nothing to either.
                if (!malformed)
                {
                    if (insufficient)
                    {
                        if (!positional_seen)
                        {
                            const uint32_t last_node = count > first_argument ? graph->argument_nodes[offset + count - 1] : BUSTER_CHECK_NONE;
                            const uint32_t order_token = last_node < graph->count ? graph->tokens[last_node] : format_token;
                            check_common_format_report_offset_ordered(parser, pp, input, format_token, insufficient_offset, order_token,
                                                                      &order, CHECK_WARNING_ID_FORMAT_INSUFFICIENT_ARGS,
                                                                      "more '%' conversions than data arguments", scratch);
                        }
                        all_covered = true;
                    }
                    else
                    {
                        uint32_t first_uncovered = BUSTER_CHECK_NONE;
                        for (uint32_t i = 0; i < data_count; i += 1)
                        {
                            if (covered[i] == 0)
                            {
                                first_uncovered = i;
                                break;
                            }
                        }
                        if (first_uncovered == BUSTER_CHECK_NONE)
                        {
                            all_covered = true;
                        }
                        else if (max_uncovered == BUSTER_CHECK_NONE || first_uncovered > max_uncovered)
                        {
                            max_uncovered = first_uncovered;
                        }
                    }
                }
            }
            if (data_count != 0 && !all_covered && max_uncovered != BUSTER_CHECK_NONE)
            {
                // The extra argument is the first index no finished literal covered — an invalid
                // specifier may mark an index without advancing past it, so plain and positional
                // styles share the scan.
                const uint32_t extra_node = graph->argument_nodes[offset + first_argument + max_uncovered];
                const uint32_t extra_token = extra_node < graph->count ? graph->tokens[extra_node] : graph->tokens[format_node];
                const uint32_t natural = extra_token < parser->tokens.count ? parser->tokens.sources[extra_token] : order;
                order = natural > order ? natural : order;
                check_common_report_published(parser, pp, input, extra_token, order, CHECK_WARNING_ID_FORMAT_EXTRA_ARGS, false,
                                              "data argument not used by format string", NULL, 0, scratch);
            }
            // A format argument that never resolves to a string literal warns under -Wformat-security
            // when the call passes no data arguments.
            if (nonliteral_leaf && count == first_argument)
            {
                // The warning points where clang points — `Args[format_idx]->getBeginLoc()`, the
                // argument expression's leading token: the recorded first token of the argument,
                // extended over the grouping parens that belong to it but not to the call's list.
                uint32_t report_token = graph->argument_tokens[offset + format_index];
                for (uint32_t steps = 0; report_token != 0 && steps < parser->tokens.count; steps += 1)
                {
                    const uint32_t before = report_token - 1;
                    if (parser->tokens.kinds[before] != PARSE_KIND_L_PAREN ||
                        parser->tree.roles[before] == PARSE_ROLE_OPEN_CALL || parser->tree.roles[before] == PARSE_ROLE_OPEN_FUNCTION)
                    {
                        break;
                    }
                    report_token = before;
                }
                const CheckCommonNote note = { report_token, "treat the string as an argument to avoid this" };
                check_common_report(parser, pp, input, report_token, CHECK_WARNING_ID_FORMAT_SECURITY, false,
                                    "format string is not a string literal (potentially insecure)", &note, 1, scratch);
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
// The unit-wide indexes the enum switches read (#1348), built at the first one: the program's cases sorted by owner
// (stable, so each switch's cases keep the program's order) and each enumerator token's first retained value. A switch
// then finds its cases by a binary search and an enumerator's value by one load, not a scan of the unit's.
typedef struct CheckCommonSwitchIndex CheckCommonSwitchIndex;
struct CheckCommonSwitchIndex
{
    uint32_t* case_owners;
    uint32_t* case_order;
    uint32_t* enum_values;
    bool built;
};

static void check_common_switch_index(CheckCommonSwitchIndex* index, const Parser* parser, Preprocessor* pp, const CheckInput* input,
                                      const CheckProgram* program, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    index->built = true;
    const uint32_t cases = program != NULL ? program->case_count : 0;
    uint32_t* owners = cases != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, cases) : NULL;
    uint32_t* order = owners != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, cases) : NULL;
    uint32_t* owners_out = order != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, cases) : NULL;
    uint32_t* order_out = owners_out != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, cases) : NULL;
    uint32_t* digits = order_out != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, 2049) : NULL;
    // A failed allocation fails the unit, as every common warning's scratch does; the scans stand in until it stops.
    if (cases != 0 && digits == NULL)
    {
        check_common_exhausted(pp);
    }
    if (digits != NULL)
    {
        for (uint32_t c = 0; c < cases; c += 1)
        {
            owners[c] = program->cases[c].owner;
            order[c] = c;
        }
        // An LSD radix sort, 11 bits a pass: each pass is stable, so equal owners keep the program's order.
        for (uint32_t shift = 0; shift < 33; shift += 11)
        {
            memset(digits, 0, 2049 * sizeof(uint32_t));
            for (uint32_t c = 0; c < cases; c += 1)
            {
                digits[((owners[c] >> shift) & 0x7ff) + 1] += 1;
            }
            for (uint32_t d = 1; d < 2049; d += 1)
            {
                digits[d] += digits[d - 1];
            }
            for (uint32_t c = 0; c < cases; c += 1)
            {
                const uint32_t at = digits[(owners[c] >> shift) & 0x7ff]++;
                owners_out[at] = owners[c];
                order_out[at] = order[c];
            }
            uint32_t* swap = owners;
            owners = owners_out;
            owners_out = swap;
            swap = order;
            order = order_out;
            order_out = swap;
        }
        index->case_owners = owners;
        index->case_order = order;
    }
    uint32_t* values = input->enum_values != NULL && count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    if (input->enum_values != NULL && count != 0 && values == NULL)
    {
        check_common_exhausted(pp);
    }
    if (values != NULL)
    {
        memset(values, 0xff, (uint64_t)count * sizeof(uint32_t));
        for (uint32_t i = input->enum_value_count; i-- > 0;)
        {
            const uint32_t token = input->enum_values[i].token;
            if (token < count)
            {
                values[token] = i;
            }
        }
        index->enum_values = values;
    }
}

static void check_common_switches(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                  TypeTable* types, CheckWarningSpelling* spell, const CheckProgram* program, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    CheckCommonSwitchIndex index = { 0 };
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const uint8_t roles[] = { PARSE_ROLE_LEAF_RIGHT };
    CheckCommonScan scan = { 0 };
    for (uint32_t token = check_common_next_token(parser, count, PARSE_KIND_SWITCH, roles, 1, &scan, vector); token < count;
         token = check_common_next_token(parser, count, PARSE_KIND_SWITCH, roles, 1, &scan, vector))
    {
        if (parser->tokens.kinds[token] != PARSE_KIND_SWITCH || parser->tree.roles[token] != PARSE_ROLE_LEAF_RIGHT)
        {
            continue;
        }
        const uint32_t closer = check_common_parent(parser, token);
        const uint32_t opener = check_common_opener(parser, closer);
        const uint32_t selector = opener != BUSTER_CHECK_NONE && opener + 1 < count ? opener + 1 : BUSTER_CHECK_NONE;
        uint32_t selector_node = input->token_nodes != NULL && selector < input->token_count ? input->token_nodes[selector] : BUSTER_CHECK_NONE;
        // Unary +, - and ~ apply the integer promotions, so their selector operand is never the
        // enum expression Clang analyzes: only an implicit enum-to-int conversion unwraps to it.
        for (uint32_t steps = 0; selector_node < stream->count && steps < stream->count; steps += 1)
        {
            if (stream->graph->ops[selector_node] != CHECK_OP_CONVERT ||
                (stream->conversions[selector_node].operands[0].flags & BUSTER_CHECK_CONVERT_CAST) != 0)
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
        if (!index.built)
        {
            check_common_switch_index(&index, parser, pp, input, program, scratch);
        }
        const uint32_t body = closer + 1 < count ? closer + 1 : count;
        const uint32_t body_end = body < count && parser->tokens.kinds[body] == PARSE_KIND_L_BRACE ? check_common_parent(parser, body) : count;
        bool has_default = false;
        bool unknown_case = false;
        // The control program resolves each case label's constant range at the selector's type: coverage,
        // range endpoints and the "not in enumerated type" check all compare in that ordered space, and a
        // label it could not evaluate was diagnosed there already.
        const uint32_t case_bound = body_end > body ? body_end - body : 0;
        uint32_t* found = case_bound != 0 && program != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, case_bound) : NULL;
        uint32_t found_count = 0;
        if (found != NULL && index.case_owners != NULL)
        {
            uint32_t low = 0;
            uint32_t high = program->case_count;
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2;
                low = index.case_owners[middle] < closer ? middle + 1 : low;
                high = index.case_owners[middle] < closer ? high : middle;
            }
            for (uint32_t k = low; k < program->case_count && index.case_owners[k] == closer && found_count < case_bound; k += 1)
            {
                found[found_count++] = index.case_order[k];
            }
        }
        else if (found != NULL)
        {
            for (uint32_t c = 0; c < program->case_count; c += 1)
            {
                if (program->cases[c].owner == closer && found_count < case_bound)
                {
                    found[found_count++] = c;
                }
            }
        }
        else if (program != NULL && program->case_count != 0 && case_bound != 0)
        {
            check_common_exhausted(pp);
            unknown_case = true;
        }
        // A case or default keyword without a resolved entry keeps the report silent, as before.
        for (uint32_t at = body; at < body_end && at < count && !unknown_case; at += 1)
        {
            const bool owned = check_common_sparse_get(input->control_owners, at) == closer;
            if (owned && (parser->tokens.kinds[at] == PARSE_KIND_CASE || parser->tokens.kinds[at] == PARSE_KIND_DEFAULT))
            {
                uint32_t colon = at + 1 < count ? at + 1 : count;
                for (uint32_t find = at + 1; find < body_end; find += 1)
                {
                    if (parser->tokens.kinds[find] == PARSE_KIND_COLON && parser->tree.roles[find] == PARSE_ROLE_CLOSE_CASE)
                    {
                        colon = find;
                        break;
                    }
                }
                bool resolved = false;
                for (uint32_t f = 0; !resolved && f < found_count; f += 1)
                {
                    resolved = program->cases[found[f]].token == colon && parser->tree.roles[colon] == PARSE_ROLE_CLOSE_CASE;
                }
                unknown_case = !resolved;
            }
        }
        // The label intervals' ordering space is the selector's promoted type, shared by every label.
        const CheckProgramCase* item_table = program != NULL ? program->cases : NULL;
        uint32_t case_width = 0;
        bool case_signed = false;
        for (uint32_t f = 0; !unknown_case && f < found_count; f += 1)
        {
            if (!item_table[found[f]].fallback)
            {
                check_action_integer_type(types, item_table[found[f]].type, &case_width, &case_signed);
                break;
            }
        }
        // Every enumerator, with its order key in its own declared type's space for the report's sort and
        // in the case space for coverage and endpoint membership. Values are stored masked at each
        // enumerator's declared type's width (int for narrow enums, its underlying for wide or fixed ones).
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
        bool values_known = enum_open != BUSTER_CHECK_NONE && enum_close != BUSTER_CHECK_NONE;
        const uint32_t enum_bound = enum_close > enum_open ? enum_close - enum_open : 0;
        uint32_t* all_names = values_known && enum_bound != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, enum_bound) : NULL;
        unsigned __int128* all_keys = values_known && enum_bound != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, unsigned __int128, enum_bound) : NULL;
        unsigned __int128* all_case_keys = values_known && enum_bound != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, unsigned __int128, enum_bound) : NULL;
        if (values_known && enum_bound != 0 && (all_names == NULL || all_keys == NULL || all_case_keys == NULL))
        {
            check_common_exhausted(pp);
        }
        values_known = values_known && all_names != NULL && all_keys != NULL && all_case_keys != NULL;
        uint32_t enum_count = 0;
        for (uint32_t at = enum_open + 1; values_known && at < enum_close && at < count; at += 1)
        {
            if (input->declaration_kinds[at] == C_BINDING_ENUMERATOR)
            {
                const uint32_t name = parser->tokens.names[at];
                unsigned __int128 value = 0;
                const uint32_t retained = index.enum_values != NULL ? index.enum_values[at] : BUSTER_CHECK_NONE;
                values_known = index.enum_values != NULL ? retained != BUSTER_CHECK_NONE : check_common_enum_value(input, parser, enum_open, enum_close, name, &value);
                value = retained != BUSTER_CHECK_NONE ? input->enum_values[retained].value : value;
                TypeId value_type = input->declared_types[at];
                value_type = value_type != 0 ? type_unqualified(type_strip_aligned(types, value_type)) : 0;
                if (value_type != 0 && types->types.kind[type_index(value_type)] == BUSTER_TYPE_KIND_ENUM)
                {
                    value_type = types->types.base[type_index(value_type)];
                }
                const uint8_t value_kind = value_type != 0 ? types->types.kind[type_index(value_type)] : 0;
                uint64_t value_rank = 0, value_width = 0;
                bool value_signed = false;
                if (value_kind == BUSTER_TYPE_KIND_INTEGER || value_kind == BUSTER_TYPE_KIND_BIT_INT)
                {
                    type_integer_properties(types, value_type, &value_rank, &value_width, &value_signed);
                }
                const unsigned __int128 dkey = value_signed && value_width != 0 && value_width < 128 ?
                                               value ^ ((unsigned __int128)1 << (value_width - 1)) : value;
                const unsigned __int128 dmask = check_action_mask((uint32_t)value_width);
                unsigned __int128 semantic = value & dmask;
                if (value_signed && value_width != 0 && value_width < 128 && (semantic >> (value_width - 1)) != 0)
                {
                    semantic |= ~dmask;
                }
                semantic &= check_action_mask(case_width);
                const unsigned __int128 ckey = case_signed && case_width != 0 && case_width < 128 ?
                                               semantic ^ ((unsigned __int128)1 << (case_width - 1)) : semantic;
                all_names[enum_count] = name;
                all_keys[enum_count] = dkey;
                all_case_keys[enum_count] = ckey;
                enum_count += 1;
            }
        }
        // An inverted range warns at its own label and covers nothing. The other labels join the value-sorted
        // pass clang runs at the switch's end — plain cases before ranges, each ordered by its lower value —
        // where each endpoint that names no enumerator warns before the missing list.
        uint32_t* intervals = values_known && found_count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, found_count) : NULL;
        uint32_t* bound_los = values_known && found_count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, found_count) : NULL;
        uint32_t* bound_his = values_known && found_count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, found_count) : NULL;
        if (values_known && found_count != 0 && (intervals == NULL || bound_los == NULL || bound_his == NULL))
        {
            check_common_exhausted(pp);
        }
        values_known = values_known && (found_count == 0 || (intervals != NULL && bound_los != NULL && bound_his != NULL));
        uint32_t interval_count = 0;
        for (uint32_t f = 0; values_known && !unknown_case && f < found_count; f += 1)
        {
            const CheckProgramCase* item = item_table + found[f];
            if (item->fallback)
            {
                has_default = true;
            }
            else
            {
                uint32_t keyword = item->token;
                while (keyword > body && parser->tokens.kinds[keyword] != PARSE_KIND_CASE)
                {
                    keyword -= 1;
                }
                const uint32_t lo_token = keyword + 1;
                uint32_t hi_token = item->token;
                if (item->range)
                {
                    for (uint32_t e = lo_token; e < item->token; e += 1)
                    {
                        if (parser->tokens.kinds[e] == PARSE_KIND_ELLIPSIS)
                        {
                            hi_token = e + 1;
                            break;
                        }
                    }
                }
                const unsigned __int128 lo_key = check_program_case_order(program, item, false);
                const unsigned __int128 hi_key = item->range ? check_program_case_order(program, item, true) : lo_key;
                if (item->range && lo_key > hi_key)
                {
                    check_common_report(parser, pp, input, lo_token, CHECK_WARNING_ID_EMPTY_CASE_RANGE, false, "empty case range specified", NULL, 0, scratch);
                }
                else
                {
                    intervals[interval_count] = found[f];
                    bound_los[interval_count] = lo_token;
                    bound_his[interval_count] = hi_token;
                    interval_count += 1;
                }
            }
        }
        for (uint32_t i = 1; i < interval_count; i += 1)
        {
            const uint32_t sorted = intervals[i];
            const uint32_t sorted_lo = bound_los[i];
            const uint32_t sorted_hi = bound_his[i];
            const uint8_t sorted_rank = item_table[sorted].range ? 2 : 1;
            const unsigned __int128 sorted_key = check_program_case_order(program, item_table + sorted, false);
            uint32_t j = i;
            while (j > 0)
            {
                const CheckProgramCase* left = item_table + intervals[j - 1];
                const uint8_t left_rank = left->range ? 2 : 1;
                const unsigned __int128 left_key = check_program_case_order(program, left, false);
                if (left_rank < sorted_rank || (left_rank == sorted_rank && (left_key < sorted_key ||
                    (left_key == sorted_key && left->token < item_table[sorted].token))))
                {
                    break;
                }
                intervals[j] = intervals[j - 1];
                bound_los[j] = bound_los[j - 1];
                bound_his[j] = bound_his[j - 1];
                j -= 1;
            }
            intervals[j] = sorted;
            bound_los[j] = sorted_lo;
            bound_his[j] = sorted_hi;
        }
        const String8 type_name = enum_count != 0 ? check_driver_warning_type(parser, pp, input, types, spell, scratch, enum_type, 0) : (String8){ 0 };
        for (uint32_t i = 0; values_known && type_name.length != 0 && i < interval_count; i += 1)
        {
            const CheckProgramCase* item = item_table + intervals[i];
            for (uint32_t bound = 0; bound < (item->range ? 2u : 1u); bound += 1)
            {
                const unsigned __int128 bound_key = check_program_case_order(program, item, bound != 0);
                bool in_enum = false;
                for (uint32_t e = 0; !in_enum && e < enum_count; e += 1)
                {
                    in_enum = all_case_keys[e] == bound_key;
                }
                if (!in_enum)
                {
                    const uint32_t bound_token = bound != 0 ? bound_his[i] : bound_los[i];
                    check_common_report_at(parser, pp, input, bound_token, body_end, CHECK_WARNING_ID_SWITCH, false,
                                           check_warning_text(scratch, "case value not in enumerated type %s", type_name.pointer), NULL, 0, scratch);
                }
            }
        }
        if (!has_default && !unknown_case && values_known && enum_count != 0)
        {
            uint32_t* missing = CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, enum_count);
            unsigned __int128* missing_values = missing != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, unsigned __int128, enum_count) : NULL;
            uint32_t* order = missing_values != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, enum_count) : NULL;
            uint32_t* merged = order != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, enum_count) : NULL;
            if (merged == NULL)
            {
                check_common_exhausted(pp);
            }
            values_known = merged != NULL;
            // The uncovered enumerators in declaration order, then stably merge-sorted by value (#1348): each run of
            // equal values lists its first-declared enumerator once, the list the value-deduplicated insertion it
            // replaces built, already in value order.
            uint32_t uncovered = 0;
            for (uint32_t e = 0; values_known && e < enum_count; e += 1)
            {
                bool covered = false;
                for (uint32_t i = 0; !covered && i < interval_count; i += 1)
                {
                    const CheckProgramCase* item = item_table + intervals[i];
                    const unsigned __int128 lo_key = check_program_case_order(program, item, false);
                    const unsigned __int128 hi_key = item->range ? check_program_case_order(program, item, true) : lo_key;
                    covered = all_case_keys[e] >= lo_key && all_case_keys[e] <= hi_key;
                }
                if (!covered)
                {
                    order[uncovered++] = e;
                }
            }
            for (uint32_t width = 1; values_known && width < uncovered; width *= 2)
            {
                for (uint32_t low = 0; low < uncovered; low += 2 * width)
                {
                    const uint32_t middle = low + width < uncovered ? low + width : uncovered;
                    const uint32_t high = middle + width < uncovered ? middle + width : uncovered;
                    uint32_t left = low;
                    uint32_t right = middle;
                    for (uint32_t k = low; k < high; k += 1)
                    {
                        const bool take_left = left < middle && (right >= high || all_keys[order[left]] <= all_keys[order[right]]);
                        merged[k] = take_left ? order[left++] : order[right++];
                    }
                }
                uint32_t* swap = order;
                order = merged;
                merged = swap;
            }
            uint32_t missing_count = 0;
            for (uint32_t k = 0; values_known && k < uncovered; k += 1)
            {
                const uint32_t e = order[k];
                if (missing_count == 0 || missing_values[missing_count - 1] != all_keys[e])
                {
                    missing[missing_count] = all_names[e];
                    missing_values[missing_count] = all_keys[e];
                    missing_count += 1;
                }
            }
            char* list = values_known && missing_count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, char, 512) : NULL;
            if (values_known && missing_count != 0 && list == NULL)
            {
                check_common_exhausted(pp);
            }
            if (list != NULL)
            {
                uint64_t length = 0;
                list[0] = 0;
                // Three or fewer missing names list in full; more print the count and the first three.
                const uint32_t shown = missing_count <= 3 ? missing_count : 3;
                for (uint32_t m = 0; m < shown && length < 480; m += 1)
                {
                    const String8 spelling = { .pointer = (char*)intern_bytes(pp->table, missing[m]), .length = intern_length(pp->table, missing[m]) };
                    const char* separator = m == 0 ? "" : m + 1 == shown && shown == missing_count ? (shown == 2 ? " and " : ", and ") : ", ";
                    const int written = snprintf(list + length, 512 - length, "%s'%.*s'", separator, (int)spelling.length, spelling.pointer);
                    if (written > 0 && (uint64_t)written < 512 - length)
                    {
                        length += (uint64_t)written;
                    }
                }
                const char* message = missing_count == 1 ? check_warning_text(scratch, "enumeration value %s not handled in switch", list) :
                                      missing_count <= 3 ? check_warning_text(scratch, "enumeration values %s not handled in switch", list) :
                                      check_warning_text(scratch, "%u enumeration values not handled in switch: %s...", missing_count, list);
                check_common_report_at(parser, pp, input, selector, body_end, CHECK_WARNING_ID_SWITCH, false, message, NULL, 0, scratch);
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
// The control entries a body's first reaches, by the walk check_program_return_flow's second pass makes for
// -Wreturn-type: a constant branch reaches only its taken edge, a case the arm its value selects, and an
// indirect jump or asm goto every label it may enter. `seen` and `queue` hold body->count elements each.
static void check_common_body_reached(const CheckProgram* program, const CheckProgramBody* body, uint8_t* seen, uint32_t* queue)
{
    const CheckControl* entries = program->controls + body->first;
    memset(seen, 0, body->count);
    uint32_t head = 0;
    uint32_t tail = 1;
    queue[0] = 0;
    seen[0] = 1;
    bool labels = false;
    while (head < tail)
    {
        const uint32_t index = queue[head++];
        const CheckControl* entry = entries + index;
        uint32_t targets[2] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        bool next = entry->kind != CHECK_CONTROL_RETURN && entry->kind != CHECK_CONTROL_END;
        if (entry->kind >= CHECK_CONTROL_BRANCH_FALSE)
        {
            bool truth = false;
            const bool jump = entry->kind == CHECK_CONTROL_JUMP;
            const bool known = !jump && check_program_runtime_truth(program, entry->node, entry->node, &truth);
            const bool taken = truth == (entry->kind == CHECK_CONTROL_BRANCH_TRUE);
            targets[0] = jump || !known || taken ? entry->target : BUSTER_CHECK_NONE;
            next = !jump && (!known || !taken);
        }
        else if (entry->kind == CHECK_CONTROL_CASE)
        {
            const CheckProgramCase* item = program->cases + entry->target;
            const uint32_t condition = program->input->token_nodes[item->owner];
            CheckProgramCase selected = { .type = item->type };
            const bool known = condition != BUSTER_CHECK_NONE &&
                               check_program_case_value(program, condition, item->type, &selected.lower);
            const unsigned __int128 ordered = known ? check_program_case_order(program, &selected, false) : 0;
            const bool taken = known && ordered >= check_program_case_order(program, item, false) &&
                                       ordered <= check_program_case_order(program, item, true);
            targets[0] = !known || taken ? item->target : BUSTER_CHECK_NONE;
            next = !known || !taken;
        }
        else if (entry->kind == CHECK_CONTROL_ASM || entry->kind == CHECK_CONTROL_JUMP_INDIRECT)
        {
            if (entry->kind == CHECK_CONTROL_ASM && program->asm_offsets != NULL && program->asm_targets != NULL)
            {
                for (uint32_t l = program->asm_offsets[entry->target]; l < program->asm_offsets[entry->target + 1]; l += 1)
                {
                    const uint32_t target = program->asm_targets[l];
                    if (target < body->count && seen[target] == 0)
                    {
                        seen[target] = 1;
                        queue[tail++] = target;
                    }
                }
            }
            else if (!labels)
            {
                for (uint32_t l = 0; l < body->count; l += 1)
                {
                    if (entries[l].kind == CHECK_CONTROL_LABEL && seen[l] == 0)
                    {
                        seen[l] = 1;
                        queue[tail++] = l;
                    }
                }
                labels = true;
            }
            next = entry->kind != CHECK_CONTROL_JUMP_INDIRECT;
        }
        targets[1] = next && index + 1 < body->count ? index + 1 : BUSTER_CHECK_NONE;
        for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(targets); t += 1)
        {
            const uint32_t target = targets[t];
            if (target < body->count && seen[target] == 0)
            {
                seen[target] = 1;
                queue[tail++] = target;
            }
        }
    }
}

// An evaluation unreachable in the control walk never happens: a statement after a return, break or goto,
// or after a `for (;;)` that cannot fall through, evaluates nothing — clang's -Wuninitialized, unused-result,
// bounds and division checks all stay silent there. The entries whose `node` names a source expression's
// value — evaluates, returns, branch and switch conditions, indirect jumps, variable bounds and statement
// results — clear their root here, which keeps the node's whole operand chain unevaluated.
static bool check_common_unreachable(const CheckProgram* program, const CheckGraph* graph,
                                     uint8_t* evaluated, Arena* scratch)
{
    bool valid = true;
    for (uint32_t b = 0; valid && b < program->body_count; b += 1)
    {
        const CheckProgramBody* body = program->bodies + b;
        if (body->count == 0 || body->count != body->entry_count)
        {
            continue;
        }
        uint8_t* seen = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, body->count);
        uint32_t* queue = CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, body->count);
        if (seen == NULL || queue == NULL)
        {
            valid = false;
            break;
        }
        check_common_body_reached(program, body, seen, queue);
        const CheckControl* entries = program->controls + body->first;
        for (uint32_t i = 0; i < body->count; i += 1)
        {
            const uint8_t kind = entries[i].kind;
            const bool expression = kind == CHECK_CONTROL_EVALUATE || kind == CHECK_CONTROL_VLA || kind == CHECK_CONTROL_RETURN ||
                                    kind == CHECK_CONTROL_SWITCH || kind == CHECK_CONTROL_BRANCH_FALSE || kind == CHECK_CONTROL_BRANCH_TRUE ||
                                    kind == CHECK_CONTROL_JUMP_INDIRECT || kind == CHECK_CONTROL_RESULT;
            if (expression && seen[i] == 0 && entries[i].node < graph->count)
            {
                evaluated[entries[i].node] = 0;
            }
        }
    }
    return valid;
}

// check_common_unselected_roots by its definition, a selection at a time: each walks back from its token for its keyword,
// then visits every node. That is O(selections × (tokens + nodes)), seconds on a unit of many asserts or _Generic
// selections (#1341, #1360). The exact path for a selection whose selected node `roots` holds, which none of the callers'
// roots does (a selection's operand has a parent), and the oracle of check_common_unselected_roots' tests.
static void check_common_unselected_walk(const CheckGraph* graph, const Parser* parser, const uint8_t* roots, uint8_t* marks, uint8_t value)
{
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        const uint32_t close = graph->ops[node] == CHECK_OP_SELECT ? graph->tokens[node] : BUSTER_CHECK_NONE;
        uint32_t token = close;
        for (uint32_t at = close < parser->tokens.count ? close : 0; at-- > 0;)
        {
            if (parser->tree.parents[at] == close && (parser->tokens.kinds[at] == PARSE_KIND_GENERIC ||
                                                      parser->tokens.kinds[at] == PARSE_KIND_TYPE_ARGUMENT))
            {
                token = at;
                break;
            }
        }
        const uint32_t selected = graph->operand0[node];
        for (uint32_t candidate = 0; close < parser->tokens.count && candidate < graph->count; candidate += 1)
        {
            if (roots[candidate] != 0 && candidate != selected && graph->tokens[candidate] > token && graph->tokens[candidate] < close)
            {
                marks[candidate] = value;
            }
        }
    }
}

// A static selection's range: the tokens from just past its keyword up to its closing token, which it excludes.
typedef struct CheckCommonRange CheckCommonRange;
struct CheckCommonRange
{
    uint32_t first;
    uint32_t end;
};

// Sets the bits from `first` up to `end`, which it excludes, a word at a time.
static void check_common_set_bits(uint64_t* bits, uint32_t first, uint32_t end)
{
    for (uint32_t at = first; at < end;)
    {
        const uint32_t word = at >> 6;
        const uint32_t stop = end - (at & ~63u) < 64 ? end : (at & ~63u) + 64;
        const uint32_t width = stop - at;
        const uint64_t ones = width == 64 ? UINT64_MAX : ((UINT64_C(1) << width) - 1) << (at & 63);
        bits[word] |= ones;
        at = stop;
    }
}

// A static selection (_Generic, __builtin_choose_expr) checks every arm but evaluates only the chosen one; its other
// arms are otherwise unreferenced roots, and so is its controlling expression. Sets `marks` to `value` at each such
// root, the nodes `roots` has set inside a selection's parentheses that are not the one it selects (marks may be roots).
// A selection's range starts past its keyword, its token's last child before it of _Generic's kind or a type argument
// keyword's, and ends at its token; a selection without one, an `__extension__`'s, has none. In O(nodes + tokens / 64 +
// selections) rather than the walk's O(selections × (tokens + nodes)) (#1341, #1360): the selections' tokens, found 16
// nodes at a time by their operator, as bits; their keywords, from one pass down the tokens below the last of them that
// tests 64 tokens' kinds at a time, each token claimed once, by its last such child, as the walk finds it; the ranges,
// in order and merged, as bits; and the roots in them, 16 nodes at a time by a gather of their tokens' bits. The scalar
// path, the twin, visits every token and node and runs the same tests. A selection whose selected node `roots` holds,
// which the bits can't exclude from the other ranges, takes the walk. False where scratch can't hold the bits, leaving
// `marks` as it was.
static bool check_common_unselected_roots(const CheckGraph* graph, const Parser* parser, const uint8_t* roots, uint8_t* marks, uint8_t value,
                                          Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    const uint64_t mark = scratch->length;
    const uint64_t words = ((uint64_t)count + 63) / 64;
    static const CheckOp selects[] = { CHECK_OP_SELECT };
    const CheckCommonOps set = check_common_ops(selects, BUSTER_ARRAY_LENGTH(selects));
    uint64_t* bits = NULL;
    bool valid = true;
    bool walk = false;
    uint32_t selections = 0;
    uint32_t last = 0;
    CheckCommonScan scan = { 0 };
    for (uint32_t node = check_common_next_op(graph, &set, &scan, vector); valid && node < graph->count; node = check_common_next_op(graph, &set, &scan, vector))
    {
        const uint32_t close = graph->tokens[node];
        if (graph->ops[node] == CHECK_OP_SELECT && close < count)
        {
            if (bits == NULL)
            {
                bits = CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, words);
                valid = bits != NULL;
                if (valid)
                {
                    memset(bits, 0, words * sizeof(uint64_t));
                }
            }
            if (valid)
            {
                bits[close >> 6] |= UINT64_C(1) << (close & 63);
            }
            const uint32_t selected = graph->operand0[node];
            walk |= selected < graph->count && roots[selected] != 0;
            selections += 1;
            last = close >= last ? close + 1 : last;
        }
    }
    CheckCommonRange* ranges = valid && selections != 0 && !walk ? CHECK_COMMON_PUSH_ARRAY(scratch, CheckCommonRange, selections) : NULL;
    valid &= selections == 0 || walk || ranges != NULL;
    uint32_t used = 0;
    for (uint32_t end = ranges != NULL ? last : 0; end != 0;)
    {
        const uint32_t first = end > 64 ? end - 64 : 0;
        const uint32_t live = end - first;
        const uint64_t lanes = live == 64 ? UINT64_MAX : (UINT64_C(1) << live) - 1;
        uint64_t pending = lanes;
        if (vector)
        {
            const __m512i kinds = _mm512_maskz_loadu_epi8((__mmask64)lanes, parser->tokens.kinds + first);
            pending = _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, kinds, _mm512_set1_epi8((char)PARSE_KIND_GENERIC)) |
                      _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, kinds, _mm512_set1_epi8((char)PARSE_KIND_TYPE_ARGUMENT));
        }
        while (pending != 0)
        {
            const uint32_t lane = 63 - (uint32_t)__builtin_clzll(pending);
            pending &= ~(UINT64_C(1) << lane);
            const uint32_t at = first + lane;
            const uint8_t kind = parser->tokens.kinds[at];
            const uint32_t parent = parser->tree.parents[at];
            if ((kind == PARSE_KIND_GENERIC || kind == PARSE_KIND_TYPE_ARGUMENT) && at < parent && parent < last &&
                (bits[parent >> 6] & (UINT64_C(1) << (parent & 63))) != 0)
            {
                bits[parent >> 6] &= ~(UINT64_C(1) << (parent & 63));
                ranges[used] = (CheckCommonRange){ .first = at + 1, .end = parent };
                used += 1;
            }
        }
        end = first;
    }
    if (used != 0)
    {
        // The claims came in descending keyword order: the ranges in ascending order, each bit set once.
        memset(bits, 0, ((uint64_t)last + 63) / 64 * sizeof(uint64_t));
        uint32_t covered = 0;
        for (uint32_t i = used; i-- > 0;)
        {
            const uint32_t from = ranges[i].first > covered ? ranges[i].first : covered;
            if (ranges[i].end > from)
            {
                check_common_set_bits(bits, from, ranges[i].end);
                covered = ranges[i].end;
            }
        }
        for (uint32_t first = 0; first < graph->count; first += 16)
        {
            const uint32_t live = graph->count - first < 16 ? graph->count - first : 16;
            const uint32_t lanes = live == 16 ? 0xffffu : (1u << live) - 1;
            if (vector)
            {
                const __m512i root_bytes = _mm512_maskz_loadu_epi8((__mmask64)lanes, roots + first);
                const __m512i tokens = _mm512_maskz_loadu_epi32((__mmask16)lanes, graph->tokens + first);
                const __mmask16 candidates = (__mmask16)_mm512_mask_test_epi8_mask((__mmask64)lanes, root_bytes, root_bytes) &
                                             _mm512_mask_cmplt_epu32_mask((__mmask16)lanes, tokens, _mm512_set1_epi32((int)last));
                if (candidates != 0)
                {
                    const __m512i covers = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), candidates, _mm512_srli_epi32(tokens, 5), bits, 4);
                    const __m512i bit = _mm512_sllv_epi32(_mm512_set1_epi32(1), _mm512_and_si512(tokens, _mm512_set1_epi32(31)));
                    const __mmask16 marked = _mm512_mask_test_epi32_mask(candidates, covers, bit);
                    _mm512_mask_storeu_epi8(marks + first, (__mmask64)marked, _mm512_set1_epi8((char)value));
                }
            }
            else
            {
                for (uint32_t node = first; node < first + live; node += 1)
                {
                    const uint32_t token = graph->tokens[node];
                    if (roots[node] != 0 && token < last && (bits[token >> 6] & (UINT64_C(1) << (token & 63))) != 0)
                    {
                        marks[node] = value;
                    }
                }
            }
        }
    }
    if (valid && walk)
    {
        check_common_unselected_walk(graph, parser, roots, marks, value);
    }
    arena_pop_to(scratch, mark);
    return valid;
}

static uint8_t* check_common_evaluated(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, const Parser* parser,
                                       const CheckProgram* program, const uint32_t* parents, Arena* scratch)
{
    uint8_t* evaluated = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    uint8_t* result = evaluated;
    if (evaluated != NULL)
    {
        for (uint32_t node = 0; node < graph->count; node += 1)
        {
            evaluated[node] = parents[node] == BUSTER_CHECK_NONE;
        }
        if (program != NULL && program->controls != NULL && !check_common_unreachable(program, graph, evaluated, scratch))
        {
            result = NULL;
        }
        if (!check_common_unselected_roots(graph, parser, evaluated, evaluated, 0, scratch))
        {
            result = NULL;
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

// The graph's facts (CheckCommonFacts) into `facts`: each node's parent, whether its value is used, its reach and its arm
// effect, in one pass over the nodes in graph order, so that its operands' and arguments' already hold, and then which
// nodes are evaluated. An operand that doesn't precede its node, which the graph's validation rules out, gives it every
// bit of reach. False where scratch can't hold them.
static bool check_common_facts(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, const Parser* parser,
                               const CheckProgram* program, const CheckCommonEnabled* enabled, Arena* scratch, CheckCommonFacts* facts)
{
    uint32_t* parents = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, graph->count) : NULL;
    uint8_t* used = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    uint8_t* reach = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    uint8_t* arm_effects = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    uint8_t* evaluated = NULL;
    if (parents != NULL && used != NULL && reach != NULL && arm_effects != NULL)
    {
        memset(parents, 0xff, (uint64_t)graph->count * sizeof(uint32_t));
        memset(used, 0, graph->count);
        for (uint32_t node = 0; node < graph->count; node += 1)
        {
            const CheckOp op = graph->ops[node];
            // A comma's left operand and a cast to void's, statement-level or explicit, drop their operand's result.
            const TypeId result_type = op == CHECK_OP_CAST ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, node))) : 0;
            const bool discards = op == CHECK_OP_COMMA || (result_type != 0 && type_kind(types, result_type) == BUSTER_TYPE_KIND_VOID);
            uint8_t bits = check_common_own_reach(parser, graph, stream, node, enabled);
            const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
            {
                if (operands[i] < graph->count)
                {
                    parents[operands[i]] = node;
                    used[operands[i]] |= (uint8_t)!(i == 0 && discards);
                    bits |= (uint8_t)(operands[i] < node ? reach[operands[i]] : BUSTER_CHECK_REACH_ALL);
                }
            }
            if (check_graph_has_arguments(op) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
            {
                for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                {
                    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
                    if (argument < graph->count)
                    {
                        parents[argument] = node;
                        used[argument] = 1;
                        bits |= (uint8_t)(argument < node ? reach[argument] : BUSTER_CHECK_REACH_ALL);
                    }
                }
            }
            reach[node] = bits;
            if (op == CHECK_OP_CONDITIONAL || op == CHECK_OP_COMMA || op == CHECK_OP_SELECT)
            {
                arm_effects[node] = check_common_arm_effect(graph, stream, types, arm_effects, node);
            }
        }
        evaluated = check_common_evaluated(graph, stream, types, parser, program, parents, scratch);
    }
    *facts = (CheckCommonFacts){ .parents = parents, .used = used, .evaluated = evaluated, .reach = reach, .arm_effects = arm_effects };
    const bool result = graph->count == 0 || evaluated != NULL;
    return result;
}

// Mark an address computation's, a void cast's or a write target's operand chain in `mask`. A subscript or
// member's own base computes an address; the object's value is not read until a real value use elsewhere.
static void check_common_mark_chain(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node, uint8_t* mask)
{
    for (uint32_t steps = 0; node < graph->count && steps < graph->count; steps += 1)
    {
        mask[node] = 1;
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
        if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT)
        {
            // A conversion that keeps the operand's representation needs no load of it either: a discarded
            // `(int)d`, `(unsigned)d`, `(long)d`, `(int*)d` or `(enum E)d` leaves `d` unread exactly like a bare
            // `d` whenever source and destination share a size and a family — integers, pointers, enums and
            // _Bools are interchangeable bits. A size change or a move to or from floating reads the operand.
            const uint32_t operand = graph->operand0[node];
            const TypeId result_type = type_unqualified(type_strip_aligned(types, check_stream_type(stream, node)));
            const TypeId operand_type = operand < graph->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, operand))) : 0;
            const uint8_t result_kind = result_type != 0 ? type_kind(types, result_type) : BUSTER_TYPE_KIND_NONE;
            const uint8_t operand_kind = operand_type != 0 ? type_kind(types, operand_type) : BUSTER_TYPE_KIND_NONE;
            const bool result_floating = result_kind == BUSTER_TYPE_KIND_FLOATING || result_kind == BUSTER_TYPE_KIND_COMPLEX;
            const bool operand_floating = operand_kind == BUSTER_TYPE_KIND_FLOATING || operand_kind == BUSTER_TYPE_KIND_COMPLEX;
            if (operand_type == 0 || result_type == 0 || result_floating != operand_floating ||
                type_size(types, result_type) != type_size(types, operand_type))
            {
                break;
            }
            node = operand;
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

// check_sparse_next, or `count` where the input left the column absent.
static uint32_t check_common_next_entry(CheckSparseColumn column, uint32_t token, uint32_t count, bool vector)
{
    const uint32_t result = check_sparse_present(column) ? check_sparse_next(column, token, count, vector) : count;
    return result;
}

// The first token in [begin, end) that can be a brace initializer's read of its own object (#1349): an identifier
// spelled `name` that does not follow `.` or `->`, or `end` when there is none. Tokens before `begin` count as the
// previous token's kind, so the caller's range may start anywhere. The vector form classifies 64 kinds a block,
// shifts the member-operator mask one lane and carries its last lane into the next block, and compares the 32-bit
// names only in the quarters that hold an identifier. Every load is masked to [begin, end). The scalar loop is its
// twin under --scalar.
static uint32_t check_common_self_candidate(const uint8_t* kinds, const uint32_t* names, uint32_t name, uint32_t begin, uint32_t end,
                                            bool vector)
{
    uint32_t result = end;
    if (vector)
    {
        const uint8_t before = begin > 0 && begin < end ? kinds[begin - 1] : 0;
        uint64_t carry = before == PARSE_KIND_PERIOD || before == PARSE_KIND_ARROW ? 1 : 0;
        for (uint32_t first = begin; result == end && first < end; first += 64)
        {
            const uint32_t length = end - first < 64 ? end - first : 64;
            const uint64_t lanes = length == 64 ? UINT64_MAX : (UINT64_C(1) << length) - 1;
            const __m512i kind_bytes = _mm512_maskz_loadu_epi8((__mmask64)lanes, kinds + first);
            const uint64_t member = _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, kind_bytes, _mm512_set1_epi8((char)PARSE_KIND_PERIOD)) |
                                    _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, kind_bytes, _mm512_set1_epi8((char)PARSE_KIND_ARROW));
            const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, kind_bytes, _mm512_set1_epi8((char)PARSE_KIND_IDENTIFIER));
            const uint64_t candidates = identifiers & ~((member << 1) | carry);
            uint64_t named = 0;
            for (uint32_t quarter = 0; candidates != 0 && quarter < 4; quarter += 1)
            {
                const __mmask16 quarter_lanes = (__mmask16)(candidates >> (quarter * 16));
                if (quarter_lanes != 0)
                {
                    const __m512i spelled = _mm512_maskz_loadu_epi32(quarter_lanes, names + first + quarter * 16);
                    const uint64_t equal = _mm512_mask_cmpeq_epi32_mask(quarter_lanes, spelled, _mm512_set1_epi32((int)name));
                    named |= equal << (quarter * 16);
                }
            }
            carry = member >> 63;
            result = named != 0 ? first + (uint32_t)__builtin_ctzll(named) : end;
        }
    }
    else
    {
        for (uint32_t at = begin; result == end && at < end; at += 1)
        {
            const bool member = at > 0 && (kinds[at - 1] == PARSE_KIND_PERIOD || kinds[at - 1] == PARSE_KIND_ARROW);
            if (kinds[at] == PARSE_KIND_IDENTIFIER && names[at] == name && !member)
            {
                result = at;
            }
        }
    }
    return result;
}

// Whether `token` is inside a sizeof or alignof operand: it, or an ancestor in the parse tree, is the operand's
// parenthesis (PARSE_ROLE_OPEN_SIZEOF or PARSE_ROLE_CLOSE_SIZEOF). The climb stops at a root or after `count` steps.
// `known`, when present, memoizes the answer per token (0 not yet, 1 outside, 2 inside): a climb stops at the first
// token whose answer is known, and a second climb writes the answer along the same path. Each token is then written
// once, so a unit's climbs cost O(tokens) in total however many reads ask (#1349). Without `known`, it is the plain
// climb, and the tests' reference.
static bool check_common_sizeof_operand(const uint8_t* roles, const uint32_t* parents, uint32_t count, uint8_t* known, uint32_t token)
{
    uint8_t answer = 1;
    uint32_t steps = 0;
    bool done = false;
    for (uint32_t up = token; !done && up < count && steps < count; steps += 1)
    {
        const uint32_t parent = parents[up];
        if (known != NULL && known[up] != 0)
        {
            answer = known[up];
            done = true;
        }
        else if (roles[up] == PARSE_ROLE_OPEN_SIZEOF || roles[up] == PARSE_ROLE_CLOSE_SIZEOF)
        {
            answer = 2;
            done = true;
        }
        else if (parent == BUSTER_CHECK_NONE || parent >= count || parent == up)
        {
            done = true;
        }
        else
        {
            up = parent;
        }
    }
    // A climb that ran out of steps found a cycle, which a parse tree doesn't have; its answer is the plain climb's,
    // and nothing is memoized from it.
    for (uint32_t up = token, written = 0; known != NULL && done && written < steps && up < count && known[up] == 0; written += 1)
    {
        known[up] = answer;
        const uint32_t parent = parents[up];
        up = parent == BUSTER_CHECK_NONE || parent >= count ? count : parent;
    }
    return answer == 2;
}

static void check_common_uninitialized(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                       TypeTable* types, const CheckCommonFacts* facts, Arena* scratch)
{
    const uint32_t count = input->token_count;
    const CheckGraph* graph = &input->graph;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    uint32_t* first_write = count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    uint8_t* addressed = count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    uint8_t* written = count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    uint32_t* first_read = count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    uint32_t* first_use_read = count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    uint8_t* nonread = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    uint8_t* self_read = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    uint8_t* target = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    // A node's value is consumed wherever another node reads it — except the discarded positions: a comma's left
    // operand and a cast to void, statement-level or explicit, drop their operand's result.
    const uint8_t* used_value = facts->used;
    uint8_t* parameters = count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    uint8_t* statement_write = count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    const uint8_t* evaluated = facts->evaluated;
    bool valid = count == 0 || (first_write != NULL && addressed != NULL && written != NULL && first_read != NULL && first_use_read != NULL &&
                                parameters != NULL && statement_write != NULL &&
                                (graph->count == 0 || (nonread != NULL && self_read != NULL && target != NULL && used_value != NULL && evaluated != NULL)));
    if (!valid)
    {
        // An empty token stream legitimately leaves every column null; anything else is a scratch
        // allocation failure, which the driver treats as a fatal resource error rather than silence.
        check_common_exhausted(pp);
    }
    if (valid && count != 0)
    {
        memset(first_write, 0xff, (uint64_t)count * sizeof(uint32_t));
        memset(addressed, 0, count);
        memset(written, 0, count);
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
        if (target != NULL)
        {
            memset(target, 0, graph->count);
        }
        // A declaration's initializer writes the object after its own value is evaluated. A brace initializer has
        // no expression node, so its write is before every later read; a read inside it is still its own. The
        // initialized write counts only where the declaration itself is reached — a goto can skip it. The
        // declarations with an initializer's node, and then those with its range alone, are the columns' entries.
        for (uint32_t declaration = check_common_next_entry(input->initializer_nodes, 0, count, vector); declaration < count;
             declaration = check_common_next_entry(input->initializer_nodes, declaration + 1, count, vector))
        {
            const uint32_t node = check_common_sparse_get(input->initializer_nodes, declaration);
            if (input->declaration_kinds[declaration] == C_BINDING_OBJECT && node < graph->count)
            {
                written[declaration] = 1;
                if (evaluated != NULL && evaluated[node] != 0)
                {
                    first_write[declaration] = node + 1;
                }
            }
        }
        for (uint32_t declaration = check_common_next_entry(input->initializer_begins, 0, count, vector); declaration < count;
             declaration = check_common_next_entry(input->initializer_begins, declaration + 1, count, vector))
        {
            if (input->declaration_kinds[declaration] == C_BINDING_OBJECT && check_common_sparse_get(input->initializer_nodes, declaration) >= graph->count &&
                check_common_sparse_get(input->initializer_begins, declaration) != BUSTER_CHECK_NONE &&
                check_common_sparse_get(input->initializer_ends, declaration) != BUSTER_CHECK_NONE)
            {
                first_write[declaration] = 0;
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
                    if (evaluated == NULL || evaluated[bound] != 0)
                    {
                        first_read[declaration] = bound < first_read[declaration] ? bound : first_read[declaration];
                    }
                    first_use_read[declaration] = bound < first_use_read[declaration] ? bound : first_use_read[declaration];
                }
            }
            for (uint32_t token = vla->bound_begin; token < vla->bound_end && token < count; token += 1)
            {
                const uint32_t declaration = input->declarations != NULL ? input->declarations[token] : BUSTER_CHECK_NONE;
                const uint32_t order = input->token_nodes != NULL ? input->token_nodes[token] : BUSTER_CHECK_NONE;
                if (declaration < count && order < graph->count && input->declaration_kinds[declaration] == C_BINDING_OBJECT &&
                    check_common_sparse_get(input->declaration_bodies, declaration) < parser->tokens.count &&
                    !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) &&
                    !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL) &&
                    check_common_sparse_get(input->initializer_nodes, declaration) == BUSTER_CHECK_NONE && !addressed[declaration])
                {
                    if (evaluated == NULL || evaluated[order] != 0)
                    {
                        first_read[declaration] = order < first_read[declaration] ? order : first_read[declaration];
                    }
                    first_use_read[declaration] = order < first_use_read[declaration] ? order : first_use_read[declaration];
                }
            }
        }
        for (uint32_t opening = check_common_next_entry(input->body_declarations, 0, count, vector); opening < count;
             opening = check_common_next_entry(input->body_declarations, opening + 1, count, vector))
        {
            for (uint32_t parameter = check_common_sparse_get(input->body_declarations, opening) != BUSTER_CHECK_NONE ?
                                      check_common_sparse_get(input->parameters, opening) : BUSTER_CHECK_NONE;
                 parameter != BUSTER_CHECK_NONE && parameter < count; parameter = check_common_sparse_get(input->parameter_next, parameter))
            {
                parameters[parameter] = 1;
            }
        }
        // A target of an assignment is written, not read; a compound assignment or increment reads it too. An
        // address-of's operand and a void cast's are not value reads for the warning. These marks are syntactic —
        // clang's set-but-not-used counts a discarded `(void)x` or a dead `&x` as a use — while only an
        // evaluation that is actually reached takes the object's address for -Wuninitialized.
        static const CheckOp marks[] = {
            CHECK_OP_ADDRESS, CHECK_OP_CAST, CHECK_OP_ASSIGN, CHECK_OP_COMMA,
            CHECK_OP_PRE_INCREMENT, CHECK_OP_PRE_DECREMENT, CHECK_OP_POST_INCREMENT, CHECK_OP_POST_DECREMENT,
            CHECK_OP_ADD_ASSIGN, CHECK_OP_SUBTRACT_ASSIGN, CHECK_OP_MULTIPLY_ASSIGN, CHECK_OP_DIVIDE_ASSIGN,
            CHECK_OP_REMAINDER_ASSIGN, CHECK_OP_SHIFT_LEFT_ASSIGN, CHECK_OP_SHIFT_RIGHT_ASSIGN, CHECK_OP_BIT_AND_ASSIGN,
            CHECK_OP_BIT_OR_ASSIGN, CHECK_OP_BIT_XOR_ASSIGN,
            CHECK_OP_BUILTIN_UNARY, CHECK_OP_BUILTIN_OVERFLOW, CHECK_OP_BUILTIN_MISC, CHECK_OP_BUILTIN_FLOAT,
            CHECK_OP_BUILTIN_QUERY, CHECK_OP_BUILTIN_ATOMIC, CHECK_OP_BUILTIN_VECTOR, CHECK_OP_BUILTIN_X86,
            CHECK_OP_BUILTIN_BIT_CAST,
        };
        // The operators the loop's own tests name (check_common_write_op's and check_common_builtin_op's among them).
        const CheckCommonOps mark_set = check_common_ops(marks, BUSTER_ARRAY_LENGTH(marks));
        CheckCommonScan mark_scan = { 0 };
        for (uint32_t node = check_common_next_op(graph, &mark_set, &mark_scan, vector); node < graph->count;
             node = check_common_next_op(graph, &mark_set, &mark_scan, vector))
        {
            const CheckOp op = graph->ops[node];
            if (op == CHECK_OP_ADDRESS)
            {
                check_common_mark_chain(graph, stream, types, graph->operand0[node], nonread);
                const uint32_t operand = check_common_address_root(graph, stream, types, graph->operand0[node], true);
                const uint32_t operand_object = operand < graph->count ? check_stream_object(stream, operand) : 0;
                if (evaluated[node] != 0 && operand_object != 0 && operand_object - 1 < count)
                {
                    addressed[operand_object - 1] = 1;
                }
            }
            else if (op == CHECK_OP_CAST && type_kind(types, type_unqualified(type_strip_aligned(types, check_stream_type(stream, node)))) == BUSTER_TYPE_KIND_VOID &&
                     graph->tokens[node] < parser->tokens.count && parser->tree.roles[graph->tokens[node]] == PARSE_ROLE_CLOSE_CAST)
            {
                // An explicit `(void)x` is a deliberate discard and reads nothing; an expression statement's
                // implicit void cast still reads a bare operand, as `g;` warns under -Wuninitialized.
                check_common_mark_chain(graph, stream, types, graph->operand0[node], nonread);
            }
            else if (op == CHECK_OP_ASSIGN)
            {
                check_common_mark_chain(graph, stream, types, graph->operand0[node], nonread);
                check_common_mark_chain(graph, stream, types, graph->operand0[node], target);
            }
            else if (op == CHECK_OP_COMMA)
            {
                // A comma's left operand is evaluated for effect only: the value it discards is not read. Through a
                // nested comma the discarded value is the left operand's own right operand, and through members,
                // array subscripts and representation-preserving conversions it is the object's own.
                uint32_t left = graph->operand0[node];
                for (uint32_t steps = 0; left < graph->count && graph->ops[left] == CHECK_OP_COMMA && steps < graph->count; steps += 1)
                {
                    left = graph->operand1[left];
                }
                if (left < graph->count)
                {
                    check_common_mark_chain(graph, stream, types, left, nonread);
                }
            }
            else if (check_common_write_op(op))
            {
                // A compound assignment or an increment reads its target and writes it; the read is not an outside
                // use for -Wunused-but-set-parameter.
                const uint32_t operand = graph->operand0[node];
                if (operand < graph->count)
                {
                    self_read[operand] = 1;
                }
            }
            if (evaluated[node] == 0)
            {
                continue;
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
        // Writes and outside uses are syntactic — a statement after `return` still sets its object and still uses
        // what it reads, as clang's set-but-not-used sees them; only -Wuninitialized's first read and write keep
        // to evaluations the control walk actually reaches. The loop's own tests name the writes' operators and VALUE.
        static const CheckOp accesses[] = {
            CHECK_OP_VALUE, CHECK_OP_ASSIGN,
            CHECK_OP_PRE_INCREMENT, CHECK_OP_PRE_DECREMENT, CHECK_OP_POST_INCREMENT, CHECK_OP_POST_DECREMENT,
            CHECK_OP_ADD_ASSIGN, CHECK_OP_SUBTRACT_ASSIGN, CHECK_OP_MULTIPLY_ASSIGN, CHECK_OP_DIVIDE_ASSIGN,
            CHECK_OP_REMAINDER_ASSIGN, CHECK_OP_SHIFT_LEFT_ASSIGN, CHECK_OP_SHIFT_RIGHT_ASSIGN, CHECK_OP_BIT_AND_ASSIGN,
            CHECK_OP_BIT_OR_ASSIGN, CHECK_OP_BIT_XOR_ASSIGN,
        };
        const CheckCommonOps access_set = check_common_ops(accesses, BUSTER_ARRAY_LENGTH(accesses));
        CheckCommonScan access_scan = { 0 };
        for (uint32_t node = check_common_next_op(graph, &access_set, &access_scan, vector); node < graph->count;
             node = check_common_next_op(graph, &access_set, &access_scan, vector))
        {
            const CheckOp op = graph->ops[node];
            const uint32_t operand = graph->operand0[node];
            const bool plain_write = op == CHECK_OP_ASSIGN;
            const bool read_write = check_common_write_op(op) && !plain_write;
            const CheckCategory target_category = operand < graph->count ? check_stream_category(stream, operand) : CHECK_RVALUE;
            const uint32_t target_object = operand < graph->count ? check_stream_object(stream, operand) : 0;
            if ((plain_write || read_write) && operand < graph->count && target_category == CHECK_LVALUE &&
                target_object != 0 && target_object - 1 < count)
            {
                const uint32_t object = target_object - 1;
                written[object] = 1;
                // A write whose stored value another node reads is also a use of the object: `(x = 1) + 1`, a
                // tested `if (x = 1)` or a comma's right operand keep it used; only a discarded value stays
                // set-but-not-used.
                if (used_value[node] != 0)
                {
                    first_use_read[object] = node < first_use_read[object] ? node : first_use_read[object];
                }
                const uint32_t order = read_write ? node + 1 : node;
                if (evaluated[node] != 0)
                {
                    first_write[object] = order < first_write[object] ? order : first_write[object];
                }
                const uint32_t token = graph->tokens[node];
                const uint32_t parent = token < parser->tokens.count ? parser->tree.parents[token] : BUSTER_CHECK_NONE;
                const uint8_t parent_role = parent < parser->tokens.count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
                if (parent < parser->tokens.count && (parent_role == PARSE_ROLE_TERMINATOR || parent_role == PARSE_ROLE_SEPARATOR ||
                                                      parent_role == PARSE_ROLE_COMMA || parent_role == PARSE_ROLE_CLOSE_FOR) &&
                    !check_common_for_condition(parser, token, parent))
                {
                    statement_write[object] = 1;
                }
            }
            if (op == CHECK_OP_VALUE)
            {
                const uint32_t object = check_stream_object(stream, node);
                const uint32_t declaration = object != 0 ? object - 1 : BUSTER_CHECK_NONE;
                if (declaration < count && check_stream_category(stream, node) == CHECK_LVALUE)
                {
                    if (evaluated[node] != 0 && nonread[node] == 0)
                    {
                        first_read[declaration] = node < first_read[declaration] ? node : first_read[declaration];
                    }
                    if (self_read[node] == 0 && target[node] == 0)
                    {
                        first_use_read[declaration] = node < first_use_read[declaration] ? node : first_use_read[declaration];
                    }
                }
            }
        }
        // The brace initializers' sizeof answers, per parser token (check_common_sizeof_operand): allocated at the
        // unit's first candidate read, so a unit without one pays nothing.
        uint8_t* sizeof_known = NULL;
        bool sizeof_failed = false;
        // A local declaration is one with a body, an entry of its column: no other warns.
        for (uint32_t declaration = check_common_next_entry(input->declaration_bodies, 0, count, vector); valid && declaration < count;
             declaration = check_common_next_entry(input->declaration_bodies, declaration + 1, count, vector))
        {
            const bool local = input->declaration_kinds[declaration] == C_BINDING_OBJECT && check_common_sparse_get(input->declaration_bodies, declaration) < parser->tokens.count &&
                               !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) &&
                               !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL);
            const bool parameter = local && parameters[declaration] != 0;
            const bool plain = local && !parameter && !addressed[declaration];
            const TypeId type = local ? input->declared_types[declaration] : 0;
            const uint8_t kind = type != 0 ? type_kind(types, type_unqualified(type_strip_aligned(types, type))) : 0;
            const bool scalar = kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_POINTER ||
                                kind == BUSTER_TYPE_KIND_ENUM || kind == BUSTER_TYPE_KIND_NULLPTR;
            const uint32_t name = parser->tokens.names[declaration];
            const uint32_t initializer_begin = check_common_sparse_get(input->initializer_begins, declaration);
            const uint32_t initializer_end = check_common_sparse_get(input->initializer_ends, declaration);
            uint32_t read_token = first_read[declaration] != BUSTER_CHECK_NONE ? graph->tokens[first_read[declaration]] : BUSTER_CHECK_NONE;
            // A lone identifier in a brace initializer (`int x = { x };`) is a self-read the graph's value column does
            // not retain; the declaration's own token range finds it. An expression initializer's reads are the
            // graph's, so only a braced range is scanned, and not through a sizeof operand.
            uint32_t brace_open = BUSTER_CHECK_NONE;
            if (local && !parameter)
            {
                for (uint32_t at = declaration + 1; at < parser->tokens.count; at += 1)
                {
                    const uint8_t kind = parser->tokens.kinds[at];
                    if (kind == PARSE_KIND_EQUAL)
                    {
                        if (at + 1 < parser->tokens.count && parser->tokens.kinds[at + 1] == PARSE_KIND_L_BRACE)
                        {
                            brace_open = at + 1;
                        }
                        break;
                    }
                    if (kind == PARSE_KIND_SEMI || kind == PARSE_KIND_COMMA || kind == PARSE_KIND_R_BRACE)
                    {
                        break;
                    }
                }
            }
            const bool braced = brace_open != BUSTER_CHECK_NONE;
            const uint32_t scan_begin = braced ? brace_open : initializer_begin;
            uint32_t scan_end = initializer_end;
            if (braced)
            {
                const uint32_t close = parser->tree.parents[brace_open];
                scan_end = close < parser->tokens.count ? close + 1 : parser->tokens.count;
            }
            // Only an identifier spelled as the object's name, and not a member after `.` or `->`, can be its read
            // (check_common_self_candidate), so only those climb for an enclosing sizeof, memoized across the unit.
            // The earliest read is the warning's position, so the scan ends at the earliest one found so far.
            const uint32_t candidate_end = scan_end < parser->tokens.count ? scan_end : parser->tokens.count;
            for (uint32_t at = braced ? check_common_self_candidate(parser->tokens.kinds, parser->tokens.names, name, scan_begin, candidate_end, vector)
                                      : candidate_end;
                 at < candidate_end && at < read_token;
                 at = check_common_self_candidate(parser->tokens.kinds, parser->tokens.names, name, at + 1, candidate_end, vector))
            {
                if (sizeof_known == NULL && !sizeof_failed)
                {
                    sizeof_known = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, parser->tokens.count);
                    sizeof_failed = sizeof_known == NULL;
                    if (sizeof_failed)
                    {
                        check_common_exhausted(pp);
                    }
                    else
                    {
                        memset(sizeof_known, 0, parser->tokens.count);
                    }
                }
                const bool query = check_common_sizeof_operand(parser->tree.roles, parser->tree.parents, parser->tokens.count, sizeof_known, at);
                // Only an evaluated value read of the declared object counts: address operands, assignment targets,
                // discarded comma operands and the unselected arms of constant folds are not reads. The earliest
                // such token is where clang's warning points.
                const uint32_t order = input->token_nodes != NULL && !query ? input->token_nodes[at] : BUSTER_CHECK_NONE;
                if (order < graph->count && graph->ops[order] == CHECK_OP_VALUE && evaluated[order] != 0 && nonread[order] == 0 &&
                    check_stream_category(stream, order) == CHECK_LVALUE && check_stream_object(stream, order) == declaration + 1 &&
                    at < read_token)
                {
                    read_token = at;
                }
            }
            const uint32_t self_begin = braced ? scan_begin : initializer_begin;
            const uint32_t self_end = braced ? scan_end : initializer_end;
            const bool self_initialization = self_begin != BUSTER_CHECK_NONE && self_end != BUSTER_CHECK_NONE &&
                                             read_token != BUSTER_CHECK_NONE && read_token >= self_begin && read_token < self_end;
            // A bare `x = x` warns only when x is read again later: the initializer's own read is the variable's
            // sole use otherwise, and then clang warns only about it being unused. `x = x + 1`, `x = a = a` and
            // `x = { x }` warn regardless, as does a parenthesized `x = (x)` once another read exists.
            bool self_bare_alone = false;
            if (self_initialization && !braced)
            {
                bool bare = true;
                for (uint32_t at = self_begin; at < self_end && at < parser->tokens.count; at += 1)
                {
                    const uint8_t kind = parser->tokens.kinds[at];
                    if (kind != PARSE_KIND_L_PAREN && kind != PARSE_KIND_R_PAREN && at != read_token)
                    {
                        bare = false;
                        break;
                    }
                }
                if (bare)
                {
                    bool later = false;
                    for (uint32_t node = 0; !later && node < graph->count; node += 1)
                    {
                        const uint32_t object = graph->ops[node] == CHECK_OP_VALUE ? check_stream_object(stream, node) : 0;
                        const uint32_t token = object != 0 ? graph->tokens[node] : BUSTER_CHECK_NONE;
                        later = object == declaration + 1 && evaluated[node] != 0 && nonread[node] == 0 &&
                                check_stream_category(stream, node) == CHECK_LVALUE &&
                                token < parser->tokens.count && (token < self_begin || token >= self_end);
                    }
                    self_bare_alone = !later;
                }
            }
            // A brace initializer's own read is reported directly: its object is initialized even though no value
            // column wrote it before the read, and the normal path's write ordering would hide the warning. A write
            // inside the braces before the read initializes it instead, as `{ (x = 2, x) }` reads the stored `x`.
            bool braced_written = false;
            for (uint32_t node = 0; braced && self_initialization && !braced_written && node < first_read[declaration]; node += 1)
            {
                const uint32_t target = graph->operand0[node];
                braced_written = (graph->ops[node] == CHECK_OP_ASSIGN || check_common_write_op(graph->ops[node])) &&
                                 target < graph->count && check_stream_category(stream, target) == CHECK_LVALUE &&
                                 check_stream_object(stream, target) == declaration + 1;
            }
            if (local && !parameter && scalar && braced && self_initialization)
            {
                if (!braced_written)
                {
                    const char* message = check_warning_text(scratch, "variable '%.*s' is uninitialized when used within its own initialization",
                                                              (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                    const uint32_t body = check_common_sparse_get(input->declaration_bodies, declaration);
                    const uint32_t brace = body < parser->tokens.count ? parser->tree.parents[body] : BUSTER_CHECK_NONE;
                    const uint32_t publication = brace < parser->tokens.count ? check_driver_publication_position(parser, pp, brace + 1, pp->output.count) : pp->output.count;
                    check_common_report_published(parser, pp, input, read_token, publication, CHECK_WARNING_ID_UNINITIALIZED, true, message, NULL, 0, scratch);
                }
                continue;
            }
            const bool has_read = first_read[declaration] != BUSTER_CHECK_NONE || read_token != BUSTER_CHECK_NONE;
            if (plain && scalar && has_read && !self_bare_alone &&
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
                const uint32_t body = check_common_sparse_get(input->declaration_bodies, declaration);
                const uint32_t brace = body < parser->tokens.count ? parser->tree.parents[body] : BUSTER_CHECK_NONE;
                const uint32_t publication = brace < parser->tokens.count ? check_driver_publication_position(parser, pp, brace + 1, pp->output.count) : pp->output.count;
                check_common_report_published(parser, pp, input, read_token, publication, CHECK_WARNING_ID_UNINITIALIZED, true, message,
                                              self_initialization ? NULL : &note, self_initialization ? 0u : 1u, scratch);
            }
            else if (parameter && scalar && !addressed[declaration] && first_use_read[declaration] == BUSTER_CHECK_NONE &&
                     written[declaration] != 0 && statement_write[declaration] != 0)
            {
                const char* message = check_warning_text(scratch, "parameter '%.*s' set but not used",
                                                          (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                // Clang pops the parameter's set-but-unused warning at the closing `}` itself, before the
                // scope's unused variables and ahead of every deferred or analysis warning inside it.
                const uint32_t body = check_common_sparse_get(input->declaration_bodies, declaration);
                const uint32_t brace = body < parser->tokens.count ? parser->tree.parents[body] : BUSTER_CHECK_NONE;
                const uint32_t publication = brace < parser->tokens.count ? check_driver_publication_position(parser, pp, brace, pp->output.count) : pp->output.count;
                check_common_report_published(parser, pp, input, declaration, publication, CHECK_WARNING_ID_UNUSED_BUT_SET_PARAMETER, true, message, NULL, 0, scratch);
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
    const char* fail_at = getenv("BUSTER_TEST_COMMON_WARNING_FAIL_AT");
    check_common_resources.fail_at = fail_at != NULL ? (uint32_t)strtoul(fail_at, NULL, 10) : UINT32_MAX;
    check_common_resources.pushes = 0;
    check_common_contexts = (CheckCommonContexts){ 0 };
    // What the passes push is theirs alone, and the scratch goes back to where it was after them, so that the backend's
    // pushes reuse its pages rather than take others past them (#628: 21.7 MB of sqlite3.c's, through the backend's peak).
    const uint64_t mark = scratch->length;
    CheckWarningSpelling spell = { 0 };
    const CheckCommonEnabled enabled =
    {
        .sign_compare = check_common_enabled(pp, input, CHECK_WARNING_ID_SIGN_COMPARE, true),
        .self_assign = check_common_enabled(pp, input, CHECK_WARNING_ID_SELF_ASSIGN, true),
        .uninitialized = check_common_enabled(pp, input, CHECK_WARNING_ID_UNINITIALIZED, true) ||
                         check_common_enabled(pp, input, CHECK_WARNING_ID_UNUSED_BUT_SET_PARAMETER, true),
    };
    check_common_parentheses(parser, pp, input, stream, types, scratch);
    check_common_empty_bodies(parser, pp, input, scratch);
    check_common_dangling_else(parser, pp, input, scratch);
    check_common_switches(parser, pp, input, stream, types, &spell, program, scratch);
    CheckCommonFacts facts = { 0 };
    if (!check_common_facts(&input->graph, stream, types, parser, program, &enabled, scratch, &facts))
    {
        check_common_exhausted(pp);
    }
    check_common_graph_warnings(parser, pp, input, stream, types, &spell, &facts, scratch);
    check_common_return_stack(parser, pp, input, stream, types, scratch);
    if (enabled.uninitialized)
    {
        check_common_uninitialized(parser, pp, input, stream, types, &facts, scratch);
    }
    check_common_format(parser, pp, input, stream, types, &spell, program, scratch);
    arena_pop_to(scratch, mark);
    check_common_contexts = (CheckCommonContexts){ 0 };
}
