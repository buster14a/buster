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

// A rejected unit's repaired view (check_repair_make, #2406, #2431): its original tokens [0, end) and the synthetic closers
// after them. A warning reports only at a token of a complete original statement (`complete`, CheckRepair.warning_complete,
// as the view's other warnings: check_repair_warning_token). The view's first closers may be the original's own, its
// tokens of the same kinds at the same places after the same tokens, which close the same braces: to `closed`. A warning
// publishes where the original's tokens put it, through `closed`; one past it, at a synthetic closer's lookahead, is the
// body's that the parse error ended, after every diagnostic of the unit's. A rejected unit's reading of the view
// (check_driver_rejected_read) edits its tokens: `to_unit` takes the reading's to the view's (BUSTER_CHECK_NONE at one the
// reading adds), `count` tokens. Null `complete` for any other unit: the driver sets it around a repaired view's common
// warnings alone.
typedef struct CheckCommonView CheckCommonView;
struct CheckCommonView
{
    const uint8_t* complete;
    const Parser* original;
    const uint32_t* to_unit;
    uint32_t count;
    uint32_t end;
    uint32_t closed;
};
static _Thread_local CheckCommonView check_common_view;

// The view's token that a reading's `token` is (itself where the reading edits nothing); past the reading's last, the view's
// end.
static uint32_t check_common_view_token(const CheckCommonView* view, const Parser* parser, uint32_t token)
{
    const uint32_t result = view->to_unit == NULL ? token : token < parser->tokens.count ? view->to_unit[token] : view->count;
    return result;
}

// Whether a warning may report at `token` (CheckCommonView): any in a unit that isn't a repaired view.
static bool check_common_view_keeps(const Parser* parser, uint32_t token)
{
    const CheckCommonView* view = &check_common_view;
    const uint32_t at = view->complete != NULL ? check_common_view_token(view, parser, token) : 0;
    const bool result = view->complete == NULL || (at < view->end && view->complete[at] != 0);
    return result;
}

// Whether the token `close` of a repaired view's reading is a synthetic closer (CheckCommonView): its body is the one the parse
// error ended.
static bool check_common_view_synthetic(const Parser* parser, uint32_t close)
{
    const CheckCommonView* view = &check_common_view;
    const bool result = view->complete != NULL && check_common_view_token(view, parser, close) >= view->closed;
    return result;
}

// The output position a warning publishes at, after the token `publication` (the unit's end past its last).
static uint32_t check_common_publication_source(const Parser* parser, const Preprocessor* pp, uint32_t publication)
{
    const CheckCommonView* view = &check_common_view;
    const uint32_t at = view->complete != NULL ? check_common_view_token(view, parser, publication) : publication;
    // (A token a reading adds publishes as the reading's sources put it.)
    const bool own = view->complete == NULL || at < view->end || (at == BUSTER_CHECK_NONE && publication < parser->tokens.count);
    const bool synthetic = !own && at > view->closed;
    const uint32_t result = own ? (publication < parser->tokens.count ? parser->tokens.sources[publication] : pp->output.count) :
                            !synthetic && at < view->original->tokens.count ? view->original->tokens.sources[at] : pp->output.count;
    return result;
}

// A sparse token column's entry, or BUSTER_CHECK_NONE where the input left the column absent.
static uint32_t check_common_sparse_get(CheckSparseColumn column, uint32_t token)
{
    const uint32_t result = check_sparse_present(column) ? check_sparse_get(column, token) : BUSTER_CHECK_NONE;
    return result;
}

// The common warnings clang makes errors by default (DefaultError): a chained comparison's (#2264).
static bool check_common_default_error(uint32_t id)
{
    return id == CHECK_WARNING_ID_CHAINED_COMPARISON;
}

static CheckWarningSeverity check_common_severity(Preprocessor* pp, const Parser* parser, const CheckInput* input, uint32_t token, uint32_t id, bool default_ignored)
{
    // One that is an error by default stays one under -w unless something demoted it (clang's -w spares errors).
    const bool default_error = check_common_default_error(id);
    const CheckWarningCommandLine command = check_warning_command_line(&pp->options, id, default_ignored, default_error);
    const CheckWarningSeverity mapped = default_error ? command.mapped : command.severity;
    CheckWarningSeverity severity = command.quiet && !default_error ? CHECK_WARNING_IGNORE :
        check_diagnostic_severity_id_from(input, token, BUSTER_CHECK_NONE, id, mapped);
    severity = command.quiet && severity < CHECK_WARNING_ERROR ? CHECK_WARNING_IGNORE : severity;
    bool system = check_graph_system_token(&input->graph, token);
    if (!system && pp->output.locations != NULL && parser != NULL && token < parser->tokens.count && parser->tokens.sources[token] < pp->output.count)
    {
        system = pp_system_level_at(pp, pp->output.locations[parser->tokens.sources[token]]) != BUSTER_PP_USER_FILE;
    }
    if (severity != CHECK_WARNING_IGNORE && !command.system_headers && system)
    {
        severity = CHECK_WARNING_IGNORE;
    }
    // None inside a direct call's full expression, of an interrupt service routine (#1447, check_driver_interrupt_kept).
    severity = input->interrupt_range_count != 0 && !check_driver_interrupt_kept(input, token) ? CHECK_WARNING_IGNORE : severity;
    // None in a repaired view's synthetic or incomplete statements (CheckCommonView).
    severity = !check_common_view_keeps(parser, token) ? CHECK_WARNING_IGNORE : severity;
    return severity;
}

// Whether a warning may report anywhere in the unit: its command-line mapping, or a source pragma that maps it to
// anything but ignored, as check_common_severity resolves it at a token. Where it may not, its work is skipped, as
// Clang skips an analysis whose diagnostics are all ignored.
static bool check_common_enabled(const Preprocessor* pp, const CheckInput* input, uint32_t id, bool default_ignored)
{
    const bool default_error = check_common_default_error(id);
    const CheckWarningCommandLine command = check_warning_command_line(&pp->options, id, default_ignored, default_error);
    bool enabled = default_error ? command.mapped != CHECK_WARNING_IGNORE : !command.quiet && command.severity != CHECK_WARNING_IGNORE;
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
    const uint32_t publication_source = check_common_publication_source(parser, pp, publication);
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
    // (An error by default names its group alone, as clang's does.)
    const bool promoted = error && !check_common_default_error(id);
    const char* suffix = option != NULL ? check_warning_text(scratch, " [%s%s]", promoted ? "-Werror," : "", option) : promoted ? " [-Werror]" : "";
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

// Whether two operands' spellings are one typedef's sugar (#1447): its uses (check_input_typedef_sugar) are its declaration.
static bool check_common_same_spelling(const CheckInput* input, const Parser* parser, uint32_t left, uint32_t right)
{
    uint32_t written = 0;
    return left == right || check_input_sugar_declaration(input, parser, left, &written) == check_input_sugar_declaration(input, parser, right, &written);
}

// Find an operand's typedef specifier without recursive calls. A bounded explicit stack follows the graph through
// casts, dereferences, subscripts, conversions and arithmetic; a bounded work budget leaves unusually large expressions
// in their canonical spelling. `sizeof` and `alignof` use the renderer's __size_t sentinel.
static uint32_t check_common_operand_spelling(const Parser* parser, const CheckInput* input, const CheckGraph* graph,
                                              const CheckStream* stream, const TypeTable* types, uint32_t node)
{
    typedef struct CheckCommonSpellingFrame
    {
        uint32_t node;
        uint32_t left;
        uint8_t phase;
    } CheckCommonSpellingFrame;
    CheckCommonSpellingFrame stack[17] = { { .node = node } };
    uint32_t count = 1;
    uint32_t result = 0;
    uint32_t work = 0;
    while (count != 0 && work < 256)
    {
        CheckCommonSpellingFrame* frame = &stack[count - 1];
        const uint32_t current = frame->node;
        if (frame->phase != 0)
        {
            if (frame->phase == 1)
            {
                const CheckOp op = graph->ops[current];
                const bool arithmetic = op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT;
                const uint32_t operand = graph->operand0[current];
                if (arithmetic && (operand >= graph->count || check_stream_type(stream, operand) != check_stream_type(stream, current)))
                {
                    result = 0;
                }
                count -= 1;
            }
            else if (frame->phase == 2)
            {
                frame->left = result;
                frame->phase = 3;
                if (count < 17)
                {
                    stack[count++] = (CheckCommonSpellingFrame){ .node = graph->operand1[current] };
                }
                else
                {
                    result = 0;
                }
            }
            else
            {
                const uint32_t left = frame->left;
                const uint32_t right = result;
                const uint32_t left_node = graph->operand0[current];
                const uint32_t right_node = graph->operand1[current];
                // (An operand's value is its lvalue's unqualified, #1447: `a + b` of two `const U` objects is a `U`.)
                const TypeId type = check_stream_type(stream, current);
                const TypeId left_type = left_node < graph->count ? type_unqualified(check_stream_type(stream, left_node)) : 0;
                const TypeId right_type = right_node < graph->count ? type_unqualified(check_stream_type(stream, right_node)) : 0;
                const CheckOp op = graph->ops[current];
                if (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT)
                {
                    result = left_type == type ? left : 0;
                }
                else if (left != 0 && check_common_same_spelling(input, parser, left, right) && left_type == type && right_type == type)
                {
                    // (Two uses of one typedef are the same sugar, #1447: arithmetic's value has no qualifiers to write.)
                    uint32_t written = 0;
                    result = check_input_sugar_declaration(input, parser, left, &written);
                }
                else if (left != 0 && left_type == type && right_type != type)
                {
                    result = left;
                }
                else
                {
                    result = right != 0 && right_type == type && left_type != type ? right : 0;
                }
                count -= 1;
            }
        }
        else if (current >= graph->count)
        {
            result = 0;
            count -= 1;
        }
        else
        {
            work += 1;
            const CheckOp op = graph->ops[current];
            const uint32_t token = graph->tokens[current];
            if (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF)
            {
                result = BUSTER_CHECK_NONE;
                count -= 1;
            }
            else if (op == CHECK_OP_CAST && token < parser->tokens.count && parser->tree.roles[token] == PARSE_ROLE_CLOSE_CAST)
            {
                result = 0;
                uint32_t open = token;
                uint32_t scanned = 0;
                while (open != 0 && scanned < 256 && !(parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == token))
                {
                    open -= 1;
                    scanned += 1;
                }
                const bool found = parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == token;
                for (uint32_t type_name = open + 1; found && result == 0 && type_name < token; type_name += 1)
                {
                    const uint32_t resolved = parser->tokens.kinds[type_name] == PARSE_KIND_IDENTIFIER && input->declarations != 0 ?
                                              input->declarations[type_name] : 0;
                    if (resolved != 0 && resolved < input->token_count && input->declaration_kinds != 0 &&
                        input->declaration_kinds[resolved] == C_BINDING_TYPEDEF)
                    {
                        result = resolved;
                    }
                }
                count -= 1;
            }
            else if (op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT || op == CHECK_OP_CONVERT ||
                     op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT ||
                     (op >= CHECK_OP_ADD && op <= CHECK_OP_SHIFT_RIGHT) || (op >= CHECK_OP_BIT_AND && op <= CHECK_OP_BIT_XOR))
            {
                const bool binary = (op >= CHECK_OP_ADD && op <= CHECK_OP_SHIFT_RIGHT) || (op >= CHECK_OP_BIT_AND && op <= CHECK_OP_BIT_XOR);
                frame->phase = binary ? 2 : 1;
                if (count < 17)
                {
                    stack[count++] = (CheckCommonSpellingFrame){ .node = graph->operand0[current] };
                }
                else
                {
                    result = 0;
                    count -= 1;
                }
            }
            else if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
            {
                // A member's own declarator's typedef (#1447), found in its record's body.
                const uint32_t member = check_semantic_member_of(parser, input, stream, types, current);
                const uint32_t declarator = member != 0 ? check_semantic_member_token(parser, input, types, member) : BUSTER_CHECK_NONE;
                result = declarator < input->token_count ? check_input_typedef_sugar(input, parser, declarator) : 0;
                count -= 1;
            }
            else
            {
                result = 0;
                if (token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && input->declarations != 0)
                {
                    const uint32_t declaration = input->declarations[token];
                    result = declaration != 0 && declaration != BUSTER_CHECK_NONE && declaration < input->token_count ?
                             check_input_typedef_sugar(input, parser, declaration) : 0;
                }
                else if (token < parser->tokens.count)
                {
                    // A compound literal's type name (#1812, check_input_token_type_spelling).
                    const uint8_t role = parser->tree.roles[token];
                    const bool compound = role == PARSE_ROLE_OPEN_COMPOUND || role == PARSE_ROLE_CLOSE_COMPOUND ||
                                          role == PARSE_ROLE_OPEN_COMPOUND_BODY || role == PARSE_ROLE_CLOSE_COMPOUND_BODY;
                    result = compound ? check_input_token_type_spelling(input, parser, token, false) : 0;
                }
                count -= 1;
            }
        }
    }
    return count == 0 ? result : 0;
}

// A type's spelling with clang's decayed type for a format argument: an array prints as a pointer to its element.
static String8 check_common_type_text(const Parser* parser, Preprocessor* pp, const CheckInput* input, TypeTable* types, CheckWarningSpelling* spell,
                                      Arena* scratch, TypeId type, uint32_t spelling)
{
    String8 result = check_driver_warning_type(parser, pp, input, types, spell, scratch, type, spelling);
    const TypeId stripped = type != 0 ? type_strip_aligned(types, type) : 0;
    if (stripped != 0 && type_kind_is_array(type_kind(types, type_unqualified(stripped))) && result.length != 0)
    {
        // The pointer's target keeps the array's qualifiers, as its element (`const int[2]`'s `'const int *'`, #1447).
        const TypeId element_type = type_qualify(types, type_element(types, type_unqualified(stripped)), type_qualifiers(stripped));
        const String8 element = check_driver_warning_type(parser, pp, input, types, spell, scratch, element_type, spelling);
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

#include "c_check_warning_number.c"

static __int128 check_common_integer_bits(unsigned __int128 integer, uint32_t width, bool is_signed)
{
    const unsigned __int128 mask = check_action_mask(width);
    const unsigned __int128 bits = integer & mask;
    const bool negative = is_signed && width != 0 && ((bits >> (width - 1)) & 1) != 0;
    __int128 result = negative ? (__int128)(bits | ~mask) : (__int128)bits;
    return result;
}

static __int128 check_common_integer_value(const CheckValue* value, uint32_t width, bool is_signed)
{
    return check_common_integer_bits(value->integer, width, is_signed);
}

// A node's type and integer as check_stream_value gives them, without copying its whole value: its dense value's, its
// payload row's, or the columns' type and no integer.
static unsigned __int128 check_common_stream_integer(const CheckStream* stream, uint32_t node, TypeId* type)
{
    unsigned __int128 result = 0;
    if (stream->dense_values != NULL)
    {
        *type = stream->dense_values[node].type;
        result = stream->dense_values[node].integer;
    }
    else
    {
        const uint32_t row = stream->payload_rows[node];
        const bool payload = row != 0 && row <= stream->payload_count;
        *type = payload ? stream->payloads[row - 1].type : stream->value_types[node];
        result = payload ? stream->payloads[row - 1].integer : 0;
    }
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
// Whether either of two types is a _BitInt past 128 bits (#1447), a cold path's: a kind check before its width's.
static bool check_common_integer_kind_wide(const TypeTable* types, TypeId source, TypeId target)
{
    const bool result = type_kind(types, type_unqualified(type_strip_aligned(types, source))) == BUSTER_TYPE_KIND_BIT_INT ||
                        type_kind(types, type_unqualified(type_strip_aligned(types, target))) == BUSTER_TYPE_KIND_BIT_INT;
    return result;
}

static bool check_common_conversion_changes(const TypeTable* types, TypeId source, TypeId target, unsigned __int128 integer, __int128* converted)
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
        const __int128 original = check_common_integer_bits(integer, source_width, source_signed);
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

static bool check_common_floating_literal(const Parser* parser, const CheckGraph* graph, uint32_t node)
{
    const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    const uint32_t literal = op == CHECK_OP_NEGATIVE || op == CHECK_OP_POSITIVE ? graph->operand0[node] : node;
    const uint32_t token = literal < graph->count ? graph->tokens[literal] : BUSTER_CHECK_NONE;
    const bool result = token < parser->tokens.count && graph->ops[literal] == CHECK_OP_VALUE && parser->tokens.kinds[token] == PARSE_KIND_NUMBER;
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
// Whether the `;` at `token` ends a statement expression's last statement, its value (a labeled one's through its labels),
// as check_common_statement_terminator's climb finds it.
static bool check_common_statement_value(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t owner = token < count && parser->tree.roles[token] == PARSE_ROLE_TERMINATOR ? parser->tree.parents[token] : count;
    uint32_t labeled = owner;
    for (uint32_t steps = 0; labeled < count && parser->tree.roles[labeled] == PARSE_ROLE_LABEL && steps < count; steps += 1)
    {
        labeled = parser->tree.parents[labeled];
    }
    const uint8_t block_role = labeled < count ? parser->tree.roles[labeled] : PARSE_ROLE_NONE;
    const uint32_t close = block_role == PARSE_ROLE_OPEN_BLOCK ? parser->tree.parents[labeled] : block_role == PARSE_ROLE_CLOSE_BLOCK ? labeled : count;
    const bool result = close < count && token + 1 == close && close + 1 < count && parser->tokens.kinds[close + 1] == PARSE_KIND_R_PAREN &&
                        parser->tree.roles[close + 1] == PARSE_ROLE_CLOSE_GROUP;
    return result;
}

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
        // A labeled last statement's `;` is the block's through its labels (#1447), as clang's StmtExpr value is.
        uint32_t labeled = owner;
        for (uint32_t label_steps = 0; labeled < count && parser->tree.roles[labeled] == PARSE_ROLE_LABEL && label_steps < count; label_steps += 1)
        {
            labeled = parser->tree.parents[labeled];
        }
        const uint8_t block_role = labeled < count ? parser->tree.roles[labeled] : PARSE_ROLE_NONE;
        const uint32_t close = block_role == PARSE_ROLE_OPEN_BLOCK ? parser->tree.parents[labeled] : block_role == PARSE_ROLE_CLOSE_BLOCK ? labeled : count;
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

// Whether the expression at `token` is discarded where it stands: its parent (past groups) is an expression statement's
// terminator, a for's init clause's `;` or its `)` (the increment's). An initializer's value, braced or not, a compound
// literal's, an argument and an operand are read, not discarded.
static bool check_common_discarded_place(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    // A parenthesized for clause's value (`for ((i, j); ...)`) is the clause's, as clang's IgnoreParens finds it.
    uint32_t parent = check_common_parent(parser, token);
    for (uint32_t steps = 0; parent < count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_GROUP && steps < count; steps += 1)
    {
        token = parent;
        parent = check_common_parent(parser, parent);
    }
    const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
    const uint32_t grandparent = parent < count ? check_common_parent(parser, parent) : BUSTER_CHECK_NONE;
    const bool for_init = parent_role == PARSE_ROLE_TERMINATOR && grandparent < count &&
                          parser->tree.roles[grandparent] == PARSE_ROLE_CLOSE_FOR &&
                          !check_common_for_condition(parser, token, parent);
    const bool result = check_common_statement_terminator(parser, parent) || for_init || parent_role == PARSE_ROLE_CLOSE_FOR;
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
// `token`'s parser context (check_warning_graph_void_context's), its memo pushed at the first ask.
static uint8_t check_common_context(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, Arena* scratch)
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
    return context;
}

static bool check_common_unevaluated(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, Arena* scratch)
{
    const uint8_t context = check_common_context(parser, pp, input, token, scratch);
    const bool result = (context & CHECK_VOID_CONTEXT_UNEVALUATED) != 0 && (context & CHECK_VOID_CONTEXT_TRANSFORMED) == 0;
    return result;
}

// Whether `token` lies in an array declarator's bound, clang's ConstantEvaluated context, where DiagRuntimeBehavior
// drops a runtime warning (#1447): a shift count's, a division's, a sign comparison's, a constant conversion's, an array
// index's, of a variable array's bound too; its unused-value warnings stay.
static bool check_common_bound(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, Arena* scratch)
{
    const uint8_t context = check_common_context(parser, pp, input, token, scratch);
    const bool result = (context & CHECK_VOID_CONTEXT_BOUND) != 0;
    return result;
}

// Whether warning `id` is one clang gives through DiagRuntimeBehavior, which drops it in an array bound
// (check_common_bound): a constant conversion's, a division's, an array index's, a sign comparison's, a shift's and a
// null dereference's (#1447).
static bool check_common_runtime_id(uint32_t id)
{
    const bool result = id == CHECK_WARNING_ID_CONSTANT_CONVERSION || id == CHECK_WARNING_ID_BITFIELD_CONSTANT_CONVERSION ||
                        id == CHECK_WARNING_ID_SINGLE_BIT_BITFIELD_CONSTANT_CONVERSION || id == CHECK_WARNING_ID_DIVISION_BY_ZERO ||
                        id == CHECK_WARNING_ID_ARRAY_BOUNDS || id == CHECK_WARNING_ID_SIGN_COMPARE || id == CHECK_WARNING_ID_SHIFT_COUNT_NEGATIVE ||
                        id == CHECK_WARNING_ID_SHIFT_COUNT_OVERFLOW || id == CHECK_WARNING_ID_SHIFT_NEGATIVE_VALUE ||
                        id == CHECK_WARNING_ID_NULL_DEREFERENCE || id == CHECK_WARNING_ID_TAUTOLOGICAL_OUT_OF_RANGE_COMPARE;
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
        // (The contexts' allocation can fail, which is fatal: then nothing more is published. A runtime warning in an array
        // bound is dropped, #1447.)
        if (severity != CHECK_WARNING_IGNORE && !check_common_unevaluated(parser, pp, input, token, scratch) &&
            !(check_common_runtime_id(id) && check_common_bound(parser, pp, input, token, scratch)) && !pp->fatal)
        {
            const uint32_t publication_source = check_common_publication_source(parser, pp, publication);
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

// A comma's left operand's "has no effect", check_common_report_unused's. In a file-scope initializer clang's
// DiagRuntimeBehavior queues it to the declarator's end, as it does a folded shift count's, in the order they are built
// (#1943, check_driver_file_initializer_end): both are published there, ordered by their nodes' ends (`comma`'s), so
// that a shift its operands hold comes first.
static void check_common_report_comma_left(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t comma,
                                           Arena* scratch)
{
    const uint32_t boundary = check_common_function_boundary(parser, token);
    const uint32_t declarator = boundary == token ? check_driver_file_initializer_end(parser, input, token) : BUSTER_CHECK_NONE;
    const PPDiagnosticRecord* previous = pp->diagnostic_last;
    check_common_report_evaluated(parser, pp, input, token, declarator != BUSTER_CHECK_NONE ? declarator : boundary,
                                  declarator != BUSTER_CHECK_NONE ? PP_DIAGNOSTIC_RUNTIME : PP_DIAGNOSTIC_DEFERRED, CHECK_WARNING_ID_UNUSED_VALUE, false,
                                  "left operand of comma operator has no effect", NULL, 0, scratch);
    if (declarator != BUSTER_CHECK_NONE && comma < input->graph.count && input->graph.source_positions != NULL)
    {
        pp_diagnostic_suborder(pp, previous, (uint64_t)input->graph.source_positions[comma] + 1);
    }
}

// The deferred publication an expression warning gets inside a function: every unused-result, bounds,
// conversion, division, shift count and sign diagnostic lands at the body's end and keeps the order it was emitted in.
static void check_common_report_deferred(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t id,
                                         bool default_ignored, const char* message, const CheckCommonNote* notes, uint32_t note_count,
                                         Arena* scratch)
{
    check_common_report_evaluated(parser, pp, input, token, check_common_function_boundary(parser, token), PP_DIAGNOSTIC_DEFERRED, id,
                                  default_ignored, message, notes, note_count, scratch);
}

// An array index's warning (#1447), check_common_report_deferred's. In a file-scope initializer clang checks the index once
// the full expression is complete and DiagRuntimeBehavior queues it to the declarator's end: after the initializer's own
// errors (a typo's correction of the array's name) and its operands' queued warnings (a shift count's, a comma's left
// operand's, which carry a suborder that this one, with none, follows).
static void check_common_report_bounds(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, const char* message,
                                       const CheckCommonNote* notes, uint32_t note_count, Arena* scratch)
{
    const uint32_t boundary = check_common_function_boundary(parser, token);
    const uint32_t declarator = boundary == token ? check_driver_file_initializer_end(parser, input, token) : BUSTER_CHECK_NONE;
    check_common_report_evaluated(parser, pp, input, token, declarator != BUSTER_CHECK_NONE ? declarator : boundary,
                                  declarator != BUSTER_CHECK_NONE ? PP_DIAGNOSTIC_RUNTIME : PP_DIAGNOSTIC_DEFERRED, CHECK_WARNING_ID_ARRAY_BOUNDS, false,
                                  message, notes, note_count, scratch);
}

// The unused-result warnings clang reports where the expression stands rather than at the body's end: an unused
// comparison and the `void *` cast question.
static void check_common_report_discarded(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t id,
                                          const char* message, const CheckCommonNote* notes, uint32_t note_count, Arena* scratch)
{
    check_common_report_evaluated(parser, pp, input, token, token, PP_DIAGNOSTIC_SEMANTIC, id, false, message, notes, note_count, scratch);
}

// check_common_report_discarded for a diagnostic whose highlighted range spans more than one token, a call's, which shows a
// macro's argument's own "expanded from macro" note (Preprocessor.diagnostic_spans).
static void check_common_report_spanning(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t id,
                                         const char* message, Arena* scratch)
{
    pp->diagnostic_spans = true;
    check_common_report_discarded(parser, pp, input, token, id, message, NULL, 0, scratch);
    pp->diagnostic_spans = false;
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
    // -Wtautological-constant-out-of-range-compare's, on by default, which the comparison walk serves too.
    bool out_of_range;
    bool self_assign;
    bool uninitialized;
    // Whether the unit names a tagged enumeration's enumerator or variable (CheckInput.enum_names), which an implicit
    // conversion between enumerations needs (check_common_enum_cast).
    bool enum_cast;
    // The const objects' initializer receipts a divisor's read may give zero from (check_common_zero_divisor), or null.
    const CheckConstValues* const_values;
};

// A folded shift's runtime warnings the legacy driver words (#1394): its counts and its negative left operand, clang's
// DiagRuntimeBehavior ones, in token order, each its report's text and place (the text in the preprocessor's arena,
// beyond the driver's scratch). The common walk reports each where it reaches the shift, among its own runtime warnings,
// and none it proves unevaluated (check_common_shift_unreached); whatever no walk reports is reported in the end
// (check_common_shift_finish), so that a failed program construction keeps them all. A warning is counted, or fails the
// unit as an error, only when it is reported.
typedef struct CheckCommonShiftWarning CheckCommonShiftWarning;
struct CheckCommonShiftWarning
{
    uint32_t token;
    uint32_t source;
    uint32_t publication;
    uint8_t severity;
    uint8_t phase;
    // In an unselected static-selection arm or controlling expression, which check_common_shift_span leaves to the
    // selection's walk (CheckCommonShiftExtras), in its order, or to the end (review on #1778).
    bool selection;
    // NULL once reported or dropped.
    const char* message;
};

typedef struct CheckCommonShiftWarnings CheckCommonShiftWarnings;
struct CheckCommonShiftWarnings
{
    CheckCommonShiftWarning* records;
    uint32_t count;
    uint32_t capacity;
};

// Detached static-selection arms have no CFG block in Clang, so their queued runtime count diagnostics are still
// published. These sparse edges are for that diagnostic traversal alone; actual evaluation keeps its own graph.
typedef struct CheckCommonShiftExtra CheckCommonShiftExtra;
struct CheckCommonShiftExtra
{
    uint32_t root;
    uint32_t selection;
    bool visited;
};

typedef struct CheckCommonShiftExtras CheckCommonShiftExtras;
struct CheckCommonShiftExtras
{
    CheckCommonShiftExtra* records;
    uint64_t* root_order;
    uint32_t count;
};

// A stashed warning's report, once.
static void check_common_shift_report(Preprocessor* pp, CheckCommonShiftWarning* warning)
{
    if (warning->message != NULL)
    {
        pp_diagnose_output_phase(pp, (PPSeverity)warning->severity, warning->source, warning->publication, (PPDiagnosticPhase)warning->phase, "%s",
                                 warning->message);
        warning->message = NULL;
    }
}

// The stashed warnings at this operator, reported where the arithmetic walk places them among its new runtime
// diagnostics: whether it has any.
static bool check_common_shift_replay(Preprocessor* pp, CheckCommonShiftWarnings* shifts, uint32_t token)
{
    uint32_t first = 0;
    uint32_t last = shifts != NULL ? shifts->count : 0;
    const uint32_t count = last;
    while (first < last)
    {
        const uint32_t middle = first + (last - first) / 2;
        const bool before = shifts->records[middle].token < token;
        first = before ? middle + 1 : first;
        last = before ? last : middle;
    }
    const bool result = first < count && shifts->records[first].token == token;
    for (uint32_t i = first; i < count && shifts->records[i].token == token; i += 1)
    {
        check_common_shift_report(pp, shifts->records + i);
    }
    return result;
}

static uint8_t* check_common_unselected(const CheckGraph* graph, const Parser* parser, Arena* scratch);
static void check_common_body_reached(const CheckProgram* program, const CheckProgramBody* body, uint8_t* seen, uint32_t* queue);
static uint8_t* check_common_evaluated(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, const Parser* parser,
                                       const CheckProgram* program, const uint32_t* parents, Arena* scratch);

// The node of the statement condition `token` is in, its closing parenthesis's (CHECK_OP_CONDITION, a switch's
// CHECK_OP_POSITIVE), or BUSTER_CHECK_NONE: the parse tree's climb from it to that parenthesis, short of a statement's or a
// block's end.
static uint32_t check_common_condition_root(const Parser* parser, const CheckInput* input, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t result = BUSTER_CHECK_NONE;
    bool climbing = true;
    for (uint32_t at = token, steps = 0; climbing && at < count && steps < count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[at];
        const bool condition = role == PARSE_ROLE_CLOSE_CONDITION && at < input->token_count;
        result = condition ? input->token_nodes[at] : result;
        climbing = !condition && role != PARSE_ROLE_TERMINATOR && role != PARSE_ROLE_OPEN_BLOCK && role != PARSE_ROLE_CLOSE_BLOCK &&
                   role != PARSE_ROLE_OPEN_BODY && role != PARSE_ROLE_CLOSE_BODY && parser->tree.parents[at] != at;
        at = parser->tree.parents[at];
    }
    return result;
}

// Whether the place of the statement at `keyword`, whose condition closes at `close`, is reached in its function's control
// program: a folded condition's statement whose arm has no expression has no program entry of its own (review on #1778).
// Within its enclosing block, its place follows the block's last entry before it: it is reached where that entry is and
// falls through, or where a jump lands there from a label before the statement — a goto's or a case's source label before
// it, or a preceding statement's own end, a jump from within the block. A label past it (a goto's, a case's, an enclosing
// statement's end) lands at the same entry but not at the place. The first statement of its block, whose place follows no
// entry of the block, is reached where the block's next entry is, unless a source label comes between. A place the program
// can't locate is taken as reached.
static bool check_common_place_reached(const Parser* parser, const CheckProgram* program, uint32_t keyword, uint32_t close, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    uint32_t scope = close < count ? parser->tree.parents[close] : BUSTER_CHECK_NONE;
    for (uint32_t steps = 0; scope < count && steps < count && parser->tree.roles[scope] != PARSE_ROLE_CLOSE_BLOCK &&
                             parser->tree.roles[scope] != PARSE_ROLE_CLOSE_BODY && parser->tree.parents[scope] != scope; steps += 1)
    {
        scope = parser->tree.parents[scope];
    }
    const bool closed = scope < count && (parser->tree.roles[scope] == PARSE_ROLE_CLOSE_BLOCK || parser->tree.roles[scope] == PARSE_ROLE_CLOSE_BODY);
    // (Its opening brace, its closing one's child.)
    uint32_t open = BUSTER_CHECK_NONE;
    for (uint32_t at = closed ? scope : 0; at != 0 && open == BUSTER_CHECK_NONE;)
    {
        at -= 1;
        const uint8_t role = parser->tree.roles[at];
        open = parser->tree.parents[at] == scope && (role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY) ? at : BUSTER_CHECK_NONE;
    }
    const CheckProgramBody* body = NULL;
    for (uint32_t b = 0; open < count && program != NULL && program->controls != NULL && program->input != NULL && b < program->body_count; b += 1)
    {
        const CheckProgramBody* candidate = program->bodies + b;
        body = candidate->opening <= open && (body == NULL || candidate->opening > body->opening) ? candidate : body;
    }
    bool result = true;
    if (body != NULL && body->count != 0 && body->count == body->entry_count)
    {
        const uint64_t mark = scratch->length;
        uint8_t* seen = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, body->count);
        uint32_t* queue = seen != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, body->count) : NULL;
        if (queue != NULL)
        {
            check_common_body_reached(program, body, seen, queue);
            const CheckControl* entries = program->controls + body->first;
            uint32_t before = BUSTER_CHECK_NONE;
            uint32_t after = BUSTER_CHECK_NONE;
            for (uint32_t i = 0; i < body->count; i += 1)
            {
                const uint32_t token = entries[i].token;
                before = token >= open && token < keyword ? i : before;
                after = after == BUSTER_CHECK_NONE && token >= keyword && token <= scope ? i : after;
            }
            if (before != BUSTER_CHECK_NONE)
            {
                const CheckControlKind kind = entries[before].kind;
                const uint32_t place = before + 1;
                result = seen[before] != 0 && kind != CHECK_CONTROL_RETURN && kind != CHECK_CONTROL_END && kind != CHECK_CONTROL_JUMP &&
                         kind != CHECK_CONTROL_JUMP_INDIRECT;
                for (uint32_t i = 0; !result && i < body->count; i += 1)
                {
                    const CheckControl* entry = entries + i;
                    const bool jump = entry->kind >= CHECK_CONTROL_BRANCH_FALSE;
                    const bool source = jump && entry->token < count && parser->tokens.kinds[entry->token] == PARSE_KIND_GOTO;
                    const uint32_t label = source ? check_common_sparse_get(program->input->label_targets, entry->token) : BUSTER_CHECK_NONE;
                    const CheckProgramCase* item = entry->kind == CHECK_CONTROL_CASE && program->cases != NULL ? program->cases + entry->target : NULL;
                    // (A branch whose condition the program knows is taken only one way, as check_common_body_reached's.)
                    bool truth = false;
                    const bool known = jump && entry->kind != CHECK_CONTROL_JUMP && check_program_branch_truth(program, entry->node, entry->node, &truth);
                    const bool taken = !known || truth == (entry->kind == CHECK_CONTROL_BRANCH_TRUE);
                    result = seen[i] != 0 &&
                             ((jump && taken && entry->target == place && (source ? label < keyword : entry->token >= open && entry->token < keyword)) ||
                              (item != NULL && item->target == place && item->token < keyword) ||
                              entry->kind == CHECK_CONTROL_ASM || (entry->kind == CHECK_CONTROL_JUMP_INDIRECT && place < body->count &&
                                                                   entries[place].kind == CHECK_CONTROL_LABEL));
                }
            }
            else if (after != BUSTER_CHECK_NONE)
            {
                bool labeled = false;
                for (uint32_t at = keyword; at < entries[after].token && at < count; at += 1)
                {
                    labeled |= parser->tree.roles[at] == PARSE_ROLE_LABEL || parser->tree.roles[at] == PARSE_ROLE_CLOSE_CASE;
                }
                result = labeled || seen[after] != 0;
            }
        }
        arena_pop_to(scratch, mark);
    }
    return result;
}

// Whether the statement whose condition's root is `condition` is reached, by `reached`, the evaluation with no range dropped.
// A statement the program drops whole, its keyword too (a constant condition's other branch, an `else if` past a true one),
// is not. Another's folded condition has no program entry of its own, so the first kept root of its executed arm stands
// for it (whose nodes come between the condition's operand and its root), reached where the statement is; a static
// selection's controlling expression and unselected arms (`unselected`), roots no evaluation reaches, never stand for it.
// A statement with no such root, an empty arm's, is reached where its place in the program is (check_common_place_reached;
// reviews on #1778).
static bool check_common_condition_reached(const Parser* parser, const CheckGraph* graph, const CheckProgram* program, const uint32_t* parents,
                                           const uint8_t* unselected, const uint8_t* reached, uint32_t condition, Arena* scratch)
{
    const uint32_t close = graph->tokens[condition];
    const uint32_t open = check_common_opener(parser, close);
    const bool kept = program == NULL || program->dropped_range_count == 0 || open == BUSTER_CHECK_NONE || open == 0 ||
                      !check_program_token_dropped(program, open - 1);
    const uint32_t operand = graph->operand0[condition];
    uint32_t found = BUSTER_CHECK_NONE;
    bool past = false;
    for (uint32_t node = operand < condition ? operand + 1 : condition; kept && !past && found == BUSTER_CHECK_NONE && node < graph->count; node += 1)
    {
        const uint32_t token = graph->tokens[node];
        const bool candidate = node != condition && parents[node] == BUSTER_CHECK_NONE && token > close && unselected[node] == 0 &&
                               (program == NULL || program->dropped_range_count == 0 || !check_program_token_dropped(program, token));
        // (A root within the statement: its token's parse-tree climb reaches the condition's closing parenthesis.)
        uint32_t at = token;
        for (uint32_t steps = 0; candidate && at < parser->tokens.count && at != close && steps < parser->tokens.count && parser->tree.parents[at] != at;
             steps += 1)
        {
            at = parser->tree.parents[at];
        }
        found = candidate && at == close ? node : BUSTER_CHECK_NONE;
        past = candidate && at != close;
    }
    const bool result = kept && (found < graph->count ? reached[found] != 0 :
                                 open == BUSTER_CHECK_NONE || open == 0 || check_common_place_reached(parser, program, open - 1, close, scratch));
    return result;
}

// The stashed warnings from `first` to `last`, the tokens of a root no walk enters, reported where that root stands in the
// roots' order (those check_common_shift_unreached kept).
static void check_common_shift_span(Preprocessor* pp, CheckCommonShiftWarnings* shifts, uint32_t first, uint32_t last)
{
    uint32_t low = 0;
    uint32_t high = shifts->count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = shifts->records[middle].token < first;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    for (uint32_t i = low; i < shifts->count && shifts->records[i].token <= last; i += 1)
    {
        if (!shifts->records[i].selection)
        {
            check_common_shift_report(pp, shifts->records + i);
        }
    }
}

// The stashed warnings the walk never reached whose shift it proved unevaluated, dropped: clang's runtime shift
// diagnostics drop themselves in code no path reaches — after a return, a goto or an endless loop, in a constant
// condition's other branch. Only a complete walk (`evaluated` known, no fatal diagnostic) proves a node unevaluated. Two
// unevaluated places still warn, as clang's Sema builds them where its CFG has them (review on #1778): an unselected
// _Generic association or __builtin_choose_expr arm (check_common_unselected), which has no CFG block, and a reachable
// statement's condition that folds to a constant, whose range the program drops (check_control_drop) though its condition's
// own root stays evaluated. Their marks are made only where a warning would otherwise drop.
static void check_common_shift_unreached(const Parser* parser, Preprocessor* pp, CheckCommonShiftWarnings* shifts, const CheckInput* input,
                                         const CheckStream* stream, const TypeTable* types, const CheckProgram* program, const uint32_t* parents,
                                         const uint8_t* evaluated, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const uint8_t* unselected = NULL;
    const uint8_t* reached = NULL;
    bool marked = false;
    for (uint32_t i = 0; shifts != NULL && evaluated != NULL && !pp->fatal && i < shifts->count; i += 1)
    {
        const uint32_t token = shifts->records[i].token;
        const uint32_t node = input->token_nodes != NULL && token < input->token_count ? input->token_nodes[token] : BUSTER_CHECK_NONE;
        const uint32_t condition = node < graph->count && evaluated[node] == 0 ? check_common_condition_root(parser, input, token) : BUSTER_CHECK_NONE;
        // (In an unevaluated operand's statement expression, and in an array bound, clang's DiagRuntimeBehavior drops it,
        // #1447.)
        const bool silent = check_common_unevaluated(parser, pp, input, token, scratch) || check_common_bound(parser, pp, input, token, scratch);
        bool drop = node < graph->count && evaluated[node] == 0;
        if (drop && !marked)
        {
            // The evaluation with no range dropped: the unreachable code's alone.
            CheckProgram whole = program != NULL ? *program : (CheckProgram){ 0 };
            whole.dropped_range_count = 0;
            unselected = check_common_unselected(graph, parser, scratch);
            reached = check_common_evaluated(graph, stream, types, parser, program != NULL ? &whole : NULL, parents, scratch);
            marked = true;
        }
        drop &= reached != NULL && unselected != NULL &&
                !(condition < graph->count && check_common_condition_reached(parser, graph, program, parents, unselected, reached, condition, scratch));
        uint32_t root = node;
        for (uint32_t steps = 0; node < graph->count && evaluated[node] == 0 && parents[root] < graph->count && steps < graph->count; steps += 1)
        {
            root = parents[root];
        }
        drop &= unselected != NULL && unselected[node] == 0 && unselected[root] == 0;
        drop |= silent;
        shifts->records[i].message = drop ? NULL : shifts->records[i].message;
        shifts->records[i].selection = !drop && unselected != NULL && node < graph->count && (unselected[node] != 0 || unselected[root] != 0);
    }
}

// A traversal can stop at a fatal diagnostic or fail for lack of scratch, and a unit with errors may run none. No
// stashed warning the walks didn't drop may disappear: report the rest, in their order.
static void check_common_shift_finish(Preprocessor* pp, CheckCommonShiftWarnings* shifts)
{
    for (uint32_t i = 0; shifts != NULL && i < shifts->count; i += 1)
    {
        check_common_shift_report(pp, shifts->records + i);
    }
}

// What the expression walk and -Wuninitialized both read of the graph, made once a unit (check_common_facts).
typedef struct CheckCommonFacts CheckCommonFacts;
struct CheckCommonFacts
{
    // Each node's parent: the last node that reads it as an operand or an argument, BUSTER_CHECK_NONE at a root.
    const uint32_t* parents;
    // Whether another node reads a node's value: not as a comma's left operand or a void cast's operand, which drop it.
    uint8_t* used;
    // The nodes the source evaluates (check_common_evaluated).
    uint8_t* evaluated;
    // What each node's subtree holds for the expression walk (BUSTER_CHECK_REACH_*).
    uint8_t* reach;
    // The tokens of the bodies clang ends after the unit's first error (check_common_flush), or null: none, in a unit without
    // one. 2 marks a body that holds an error, which a rejected unit's second reading left out of it (#1447,
    // check_driver_rejected_read): clang gives it no -Wunused-but-set-parameter, and the reading may lack its reads.
    const uint8_t* flushed;
    // The plain `x = y` assignments, a bit per node, -Wself-assign's candidates (check_common_self_assigns), or null where it
    // can't warn.
    uint64_t* self_assigns;
};

static uint8_t* check_common_evaluated(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, const Parser* parser,
                                       const CheckProgram* program, const uint32_t* parents, Arena* scratch);
static bool check_common_unselected_roots(const CheckGraph* graph, const Parser* parser, const uint8_t* roots, uint8_t* marks, uint8_t value,
                                          Arena* scratch);
// The leaves a conversion reports (check_common_conversion_warning_at, check_common_conversion_leaf): an integer
// constant's runtime one (clang's DiagRuntimeBehavior, past the target's width), a floating literal's, and an integer
// constant's that fills the target's width exactly (#1447, clang's isSameWidthConstantConversion: a non-negative signed
// value whose bits the signed target holds as a negative one), which clang's Diag reports at once, as a literal's. LISTED
// marks a brace initializer's own element conversion, whose character target clang exempts from the same-width warning.
#define BUSTER_CHECK_LEAVES_INTEGER 0x01u
#define BUSTER_CHECK_LEAVES_FLOATING 0x02u
#define BUSTER_CHECK_LEAVES_WIDTH 0x04u
#define BUSTER_CHECK_LEAVES_LISTED 0x08u
// A _Bool target's DiagnoseIntInBoolContext (check_common_int_in_bool), at once as a literal's.
#define BUSTER_CHECK_LEAVES_BOOL 0x10u
#define BUSTER_CHECK_LEAVES_ALL 0x17u

static void check_common_conversion_warning(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                            TypeTable* types, CheckWarningSpelling* spell, uint32_t source, const CheckValue* value,
                                            TypeId target, bool evaluated, const uint32_t* parents, uint32_t at_once, Arena* scratch);
static void check_common_conversion_leaf(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                         TypeTable* types, CheckWarningSpelling* spell, uint32_t source, const CheckValue* value,
                                         TypeId target, const uint32_t* parents, uint32_t at_once, uint8_t leaves, Arena* scratch);
static void check_common_int_in_bool(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                     uint32_t node, const uint32_t* parents, uint32_t at_once, Arena* scratch);
static bool check_common_bool_candidate(const CheckGraph* graph, uint32_t node);
static bool check_common_bool_target(const TypeTable* types, TypeId target);

// Query preparation suppresses every unevaluated constant-warning family before graph construction; runtime
// selections suppress overflow alone. The retained full mask also identifies detached initializer roots inside
// sizeof/typeof operands, whose plan parts have no operand edge from the query. Independent VLA bounds keep it clear.
static bool check_common_query_suppressed(const CheckGraph* graph, uint32_t node)
{
    const uint32_t mask = check_constant_unevaluated_warnings();
    const bool result = graph->warning_suppressed != NULL && (graph->warning_suppressed[node] & mask) == mask;
    return result;
}

// The deferred expression warnings — unused results, out-of-bounds subscripts, lossy conversions, division
// by zero, shift counts and sign mismatches — publish at their enclosing function's end, so the order they print in is
// the order this traversal emits them. Clang emits a discarded expression's warnings in a value
// post-order: arithmetic warnings first, nested subexpressions next, the discard marker after them, each
// operand root's own warning after the marker, and sign comparisons last.
typedef struct CheckCommonIgnored CheckCommonIgnored;
typedef struct CheckCommonEmission
{
    const Parser* parser;
    Preprocessor* pp;
    const CheckInput* input;
    const CheckStream* stream;
    const CheckProgram* program;
    TypeTable* types;
    CheckWarningSpelling* spell;
    CheckCommonShiftWarnings* shifts;
    CheckCommonShiftExtras* shift_extras;
    const uint8_t* evaluated;
    const uint32_t* parents;
    // The flushed bodies' tokens (CheckCommonFacts.flushed), or null.
    const uint8_t* flushed;
    // Separate ownership for the ordinary tree, detached count scans, count publication, the conversion visit and the
    // literal-only source context (BUSTER_CHECK_EMITTED_*).
    uint8_t* emitted;
    // The full expression's end a brace initializer's conversions all complete at (check_common_initializer_conversions),
    // or BUSTER_CHECK_NONE, where each conversion's root gives it (check_common_full_expression_end).
    uint32_t at_once;
    // What each node's subtree holds for the walks (BUSTER_CHECK_REACH_*), so that a walk enters only the subtrees where
    // a warning can come from.
    const uint8_t* reach;
    // The deferred-emission worklist. Each (kind, node) frame stands in for a recursive call the
    // no-recursion rule forbids; push order is the calls' reverse so popping replays them exactly. A
    // full worklist fails the translation rather than dropping warnings.
    uint32_t* work_nodes;
    uint8_t* work_kinds;
    // The conversion a frame's arm belongs to (BUSTER_CHECK_EMIT_ARM, BUSTER_CHECK_EMIT_ARM_LEAF), else BUSTER_CHECK_NONE.
    uint32_t* work_owners;
    uint32_t work_capacity;
    uint32_t work_count;
    Arena* scratch;
    // The unit's marks of discarded calls' results (c_check_ignored_result.c), or null.
    CheckCommonIgnored* ignored;
    // The statement expressions' inner roots the arithmetic scan takes where it enters them (check_common_inner_roots): keys
    // of each one's statement expression's node and its place, sorted, `inner_count` of them.
    uint64_t* inner;
    uint32_t inner_count;
} CheckCommonEmission;

// Whether `token` is in the subtree of `root`, whose node depth is `depth`: its parent chain reaches `root` before a token
// shallower than `root`, which a parent is never deeper than, so that the chain's tokens past it can't be `root` either.
static bool check_common_token_below(const Parser* parser, uint32_t token, uint32_t root, uint32_t depth)
{
    const uint32_t count = parser->tokens.count;
    uint32_t node = token;
    bool result = false;
    for (uint32_t steps = 0; node < count && steps < count && parser->tree.depths[node] >= depth; steps += 1)
    {
        const uint32_t parent = parser->tree.parents[node];
        result = node == root;
        node = result || parent == node ? BUSTER_CHECK_NONE : parent;
    }
    return result;
}

// check_common_subtree_start for the emission's queries (a comma's operands'): a subtree is a run of tokens, so the tokens
// of `root`'s before it are those from its start, which a search from `root` back by doubling steps brackets and a binary
// search then finds. Each probe's climb stops at a token shallower than `root` (check_common_token_below). A comma chain's
// k-th left operand is the chain before it (#1350), O(log k) probes, where the search from the unit's first token took
// climbs from the whole unit's tokens, and a column of every token's start, which a few hundred queries a unit don't pay
// for, the whole unit's.
static uint32_t check_common_emission_subtree_start(CheckCommonEmission* emission, uint32_t root)
{
    const Parser* parser = emission->parser;
    uint32_t result = root;
    if (root < parser->tokens.count && parser->tree.depths != NULL)
    {
        const uint32_t depth = parser->tree.depths[root];
        uint32_t inside = root;
        uint32_t outside = BUSTER_CHECK_NONE;
        for (uint32_t step = 1; outside == BUSTER_CHECK_NONE && inside != 0; step = step < UINT32_MAX / 2 ? step * 2 : step)
        {
            const uint32_t at = inside > step ? inside - step : 0;
            if (check_common_token_below(parser, at, root, depth))
            {
                inside = at;
            }
            else
            {
                outside = at;
            }
        }
        while (outside != BUSTER_CHECK_NONE && inside - outside > 1)
        {
            const uint32_t middle = outside + (inside - outside) / 2;
            if (check_common_token_below(parser, middle, root, depth))
            {
                inside = middle;
            }
            else
            {
                outside = middle;
            }
        }
        result = inside;
    }
    else
    {
        result = check_common_subtree_start(parser, root);
    }
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
#define BUSTER_CHECK_EMIT_ARITHMETIC_SCAN 8u
#define BUSTER_CHECK_EMIT_ARITHMETIC_OWN 9u
#define BUSTER_CHECK_EMIT_SIGN_SCAN 10u
#define BUSTER_CHECK_EMIT_SIGN_OWN 11u
#define BUSTER_CHECK_EMIT_SHIFT_SCAN 12u
#define BUSTER_CHECK_EMIT_SHIFT_OWN 13u
#define BUSTER_CHECK_EMIT_DETACHED_SCAN 14u
#define BUSTER_CHECK_EMIT_ARGUMENT 15u
#define BUSTER_CHECK_EMIT_PROMOTED_ARGUMENT 16u
#define BUSTER_CHECK_EMIT_CONVERSIONS 17u
// A root's whole visit (check_common_root_frames), for a statement expression's inner root (check_common_inner_roots).
#define BUSTER_CHECK_EMIT_ROOT 18u
// A conditional arm of a conversion whose source is a conditional (#1895, clang's CheckConditionalOperand): a conditional's
// condition, then its arms; a leaf's own conversions, then its BUSTER_CHECK_EMIT_ARM_LEAF report. The frame's owner is the
// conversion node.
#define BUSTER_CHECK_EMIT_ARM 19u
#define BUSTER_CHECK_EMIT_ARM_LEAF 20u
// The same in an arm a known condition doesn't select, where the constant conversions' runtime warnings are pruned.
#define BUSTER_CHECK_EMIT_ARM_PRUNED 21u
#define BUSTER_CHECK_EMIT_ARM_LEAF_PRUNED 22u
// An arm frame's owner (BUSTER_CHECK_EMIT_ARM): a conversion node, or a call argument's slot with this bit, and the
// prototype bit where its call has one (#1895).
#define BUSTER_CHECK_ARM_ARGUMENT 0x80000000u
#define BUSTER_CHECK_ARM_PROTOTYPE 0x40000000u

// A count scan must not claim ordinary tree ownership: the latter also publishes unrelated warning families.
// Keep these facts in the existing per-node byte, with publication claimed only after an eligible count visit.
// The separate conversion visit and the cold literal-only source context take their own bits of the same byte.
#define BUSTER_CHECK_EMITTED_TREE 0x01u
#define BUSTER_CHECK_EMITTED_SHIFT_SCAN 0x02u
#define BUSTER_CHECK_EMITTED_SHIFT_COUNT 0x04u
#define BUSTER_CHECK_EMITTED_CONVERSIONS 0x08u
#define BUSTER_CHECK_EMITTED_LITERAL_CONTEXT 0x10u
// A source comma's left-operand marker, already run by the arithmetic scan (check_common_arithmetic_own).
#define BUSTER_CHECK_EMITTED_COMMA_MARKER 0x20u
// A root the walks take where its operand stands (check_common_displaced_roots), or where its statement expression stands
// (check_common_inner_roots), skipped where it stands.
#define BUSTER_CHECK_EMITTED_DISPLACED 0x40u
// A node the arithmetic scan has entered: a statement expression's last value is its statement's root's and its
// enclosing root's too, and is scanned once.
#define BUSTER_CHECK_EMITTED_ARITHMETIC_SCAN 0x80u

// What a node's subtree holds for check_common_root_push's walks, a bit each, in CheckCommonEmission.reach: a node whose
// own frame or comma stages may warn (check_common_candidate_bits), a division by zero or shift by a constant, a comparison where
// -Wsign-compare may warn (check_common_enabled). Each counts evaluated or not: a comma's operands and a root's expression
// run their own frames either way, and the scans test evaluation themselves. A frame of a subtree without its walk's bit
// warns of nothing and marks emitted only nodes of such subtrees, which no other frame then reads to any effect, so the
// walks skip those subtrees and keep every warning's order.
#define BUSTER_CHECK_REACH_TREE 0x01u
#define BUSTER_CHECK_REACH_ARITHMETIC 0x02u
#define BUSTER_CHECK_REACH_SIGN 0x04u
#define BUSTER_CHECK_REACH_SHIFT 0x08u
#define BUSTER_CHECK_REACH_DETACHED 0x10u
#define BUSTER_CHECK_REACH_CONVERSION 0x20u
// An index's bound the tree walk may warn of, which orders a comma's marker after it (check_common_comma_bound_operands).
#define BUSTER_CHECK_REACH_BOUND 0x40u
#define BUSTER_CHECK_REACH_ALL 0x7fu

static void check_common_emit_push(CheckCommonEmission* emission, uint32_t node, uint8_t kind);
static void check_common_emit_run(CheckCommonEmission* emission);
static inline __attribute__((always_inline)) void check_common_root_frames(CheckCommonEmission* emission, uint32_t node);
static void check_common_own(CheckCommonEmission* emission, uint32_t node);

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

// A frame on the deferred-emission worklist, or the translation's failure when scratch is full.
static void check_common_emit_push(CheckCommonEmission* emission, uint32_t node, uint8_t kind)
{
    if (emission->work_count < emission->work_capacity)
    {
        emission->work_nodes[emission->work_count] = node;
        emission->work_kinds[emission->work_count] = kind;
        emission->work_owners[emission->work_count] = BUSTER_CHECK_NONE;
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

static uint32_t check_common_shift_extra_first(const CheckCommonShiftExtras* extras, uint32_t selection)
{
    uint32_t first = 0;
    uint32_t last = extras != NULL ? extras->count : 0;
    while (first < last)
    {
        const uint32_t middle = first + (last - first) / 2;
        const bool before = extras->records[middle].selection < selection;
        first = before ? middle + 1 : first;
        last = before ? last : middle;
    }
    return first;
}

static const CheckCommonShiftExtra* check_common_shift_extra_root(const CheckCommonShiftExtras* extras, uint32_t root)
{
    uint32_t first = 0;
    uint32_t last = extras != NULL ? extras->count : 0;
    const uint32_t count = last;
    while (first < last)
    {
        const uint32_t middle = first + (last - first) / 2;
        const bool before = (extras->root_order[middle] >> 32) < root;
        first = before ? middle + 1 : first;
        last = before ? last : middle;
    }
    const CheckCommonShiftExtra* result = first < count && (extras->root_order[first] >> 32) == root ?
                                         extras->records + (uint32_t)extras->root_order[first] : NULL;
    return result;
}

// A node's operand and argument children pushed in reverse so they pop in the order the subtree walks
// read them: operands left to right, then the call's arguments.
static void check_common_emit_kids(CheckCommonEmission* emission, uint32_t node, uint32_t slots, uint8_t kind)
{
    const CheckGraph* graph = &emission->input->graph;
    const uint8_t bit = kind == BUSTER_CHECK_EMIT_ARITHMETIC_SCAN ? BUSTER_CHECK_REACH_ARITHMETIC :
                        kind == BUSTER_CHECK_EMIT_SHIFT_SCAN ? BUSTER_CHECK_REACH_SHIFT | BUSTER_CHECK_REACH_DETACHED :
                        kind == BUSTER_CHECK_EMIT_DETACHED_SCAN ? BUSTER_CHECK_REACH_DETACHED :
                        kind == BUSTER_CHECK_EMIT_SIGN_SCAN ? BUSTER_CHECK_REACH_SIGN :
                        kind == BUSTER_CHECK_EMIT_CONVERSIONS ? BUSTER_CHECK_REACH_CONVERSION : BUSTER_CHECK_REACH_TREE;
    const bool counts = kind == BUSTER_CHECK_EMIT_ARITHMETIC_SCAN || kind == BUSTER_CHECK_EMIT_SHIFT_SCAN || kind == BUSTER_CHECK_EMIT_DETACHED_SCAN;
    CheckCommonShiftExtras* extras = emission->shift_extras;
    const uint32_t first = counts && graph->ops[node] == CHECK_OP_SELECT ? check_common_shift_extra_first(extras, node) :
                           extras != NULL ? extras->count : 0;
    const bool selection = extras != NULL && first < extras->count && extras->records[first].selection == node;
    if (selection)
    {
        uint32_t end = first;
        while (end < extras->count && extras->records[end].selection == node)
        {
            end += 1;
        }
        // Children precede parents in source order. Merge the selected operand with the detached arms, pushing
        // them backwards, so a warning in either kind of arm keeps its position among the other arithmetic visits.
        uint32_t selected = graph->operand0[node];
        while (end != first || selected < graph->count)
        {
            if (selected < graph->count && (end == first || selected > extras->records[end - 1].root))
            {
                check_common_emit_reach(emission, selected, kind, bit);
                selected = BUSTER_CHECK_NONE;
            }
            else
            {
                CheckCommonShiftExtra* extra = extras->records + --end;
                if (!extra->visited)
                {
                    extra->visited = true;
                    check_common_emit_reach(emission, extra->root, BUSTER_CHECK_EMIT_SHIFT_SCAN,
                                            BUSTER_CHECK_REACH_SHIFT | BUSTER_CHECK_REACH_DETACHED);
                }
            }
        }
    }
    else
    {
        if (check_graph_has_arguments(graph->ops[node]) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
        {
            for (uint32_t i = graph->argument_counts[node]; i-- > 0;)
            {
                check_common_emit_reach(emission, graph->argument_nodes[graph->argument_offsets[node] + i], kind, bit);
            }
        }
        for (uint32_t i = slots; i-- > 0;)
        {
            // GNU's omitted-middle conditional reads its condition once, although both graph slots name it.
            const bool repeated = counts && graph->ops[node] == CHECK_OP_CONDITIONAL && i == 1 && graph->operand1[node] == graph->operand0[node];
            if (!repeated)
            {
                check_common_emit_reach(emission, i == 0 ? graph->operand0[node] : i == 1 ? graph->operand1[node] : graph->operand2[node], kind, bit);
            }
        }
    }
}

// The comparison walk's frames of `node`, in the order clang's AnalyzeImplicitConversions visits a full expression: a
// comparison is analyzed before its operands (AnalyzeComparison), which are then visited left to right, as an
// assignment's, a compound assignment's and a conditional's are (AnalyzeAssignment, CheckConditionalOperator); any other
// node's children go on its work list in order and so are visited right to left, a call's last argument first.
static void check_common_sign_scan(CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const CheckOp op = graph->ops[node];
    const bool ordered = (op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL) || op == CHECK_OP_ASSIGN || op == CHECK_OP_CONDITIONAL ||
                         (op >= CHECK_OP_ADD_ASSIGN && op <= CHECK_OP_BIT_XOR_ASSIGN);
    if (ordered)
    {
        // Pushed in reverse, its children pop left to right, and the comparison's own frame first. GNU's omitted-middle
        // conditional reads its condition once, although both graph slots name it.
        const bool repeated = op == CHECK_OP_CONDITIONAL && graph->operand1[node] == graph->operand0[node];
        check_common_emit_reach(emission, graph->operand2[node], BUSTER_CHECK_EMIT_SIGN_SCAN, BUSTER_CHECK_REACH_SIGN);
        check_common_emit_reach(emission, repeated ? BUSTER_CHECK_NONE : graph->operand1[node], BUSTER_CHECK_EMIT_SIGN_SCAN, BUSTER_CHECK_REACH_SIGN);
        check_common_emit_reach(emission, graph->operand0[node], BUSTER_CHECK_EMIT_SIGN_SCAN, BUSTER_CHECK_REACH_SIGN);
        if (op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL)
        {
            check_common_emit_push(emission, node, BUSTER_CHECK_EMIT_SIGN_OWN);
        }
    }
    else
    {
        for (uint32_t i = 0; i < 3; i += 1)
        {
            check_common_emit_reach(emission, i == 0 ? graph->operand0[node] : i == 1 ? graph->operand1[node] : graph->operand2[node],
                                    BUSTER_CHECK_EMIT_SIGN_SCAN, BUSTER_CHECK_REACH_SIGN);
        }
        if (check_graph_has_arguments(op) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
        {
            for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
            {
                check_common_emit_reach(emission, graph->argument_nodes[graph->argument_offsets[node] + i], BUSTER_CHECK_EMIT_SIGN_SCAN,
                                        BUSTER_CHECK_REACH_SIGN);
            }
        }
    }
}

static void check_common_comma_marker(CheckCommonEmission* emission, uint32_t node);
static void check_common_comma_end(CheckCommonEmission* emission, uint32_t node);

static bool check_common_emit_evaluated(const CheckCommonEmission* emission, uint32_t node)
{
    return emission->evaluated == NULL || emission->evaluated[node] != 0;
}

// Conditional arms and a comma's result inherit an enclosing implicit conversion's target. Their value need not
// be constant for an individual arm to contain a literal conversion.
static bool check_common_conversion_arms(const CheckGraph* graph, uint32_t node)
{
    return node < graph->count && (graph->ops[node] == CHECK_OP_CONDITIONAL || graph->ops[node] == CHECK_OP_COMMA);
}

// A source whose implicit conversion the literal warnings can report: an integer or floating constant
// (check_common_conversion_leaf), or arms that carry the conversion to theirs. An address or complex constant
// never warns there, so a call that passes a string literal schedules no conversion walk.
static bool check_common_conversion_source(CheckConstantKind constant, bool arms)
{
    return constant == CHECK_CONSTANT_INTEGER || constant == CHECK_CONSTANT_FLOATING || arms;
}

// The operands a truth context converts to _Bool (#147, clang's CheckBoolLikeConversion), `count` of them in `operands`:
// a static assertion's own (BUSTER_CHECK_CONTEXT_ASSERT), then `!`'s, `&&`'s and `||`'s, a statement's condition
// (CHECK_OP_CONDITION) and a conditional's first. Only a floating literal's conversion there warns, -Wliteral-conversion's
// "changes value from 1.5 to true"; clang carries it into no conditional's arms or comma's result.
static uint32_t check_common_truth_operands(const CheckGraph* graph, uint32_t node, uint32_t operands[3])
{
    const CheckOp op = graph->ops[node];
    const uint32_t assertion = graph->contexts != NULL && (graph->contexts[node] & BUSTER_CHECK_CONTEXT_ASSERT) != 0 ? 1u : 0u;
    const uint32_t count = op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR ? 2u :
                           op == CHECK_OP_NOT || op == CHECK_OP_CONDITION || op == CHECK_OP_CONDITIONAL ? 1u : 0u;
    operands[0] = node;
    operands[assertion] = graph->operand0[node];
    operands[assertion + 1] = graph->operand1[node];
    return assertion + count;
}

// Whether a truth context's operand may warn of its conversion to _Bool: a floating constant's literal conversion, or a
// `<<`'s or a conditional's DiagnoseIntInBoolContext (#1447): check_common_truth_floating's test and
// check_common_bool_candidate's in one pass over the operands, for the walks' candidates.
static bool check_common_truth_candidate(const CheckGraph* graph, const CheckStream* stream, uint32_t node)
{
    uint32_t operands[3];
    const uint32_t count = check_common_truth_operands(graph, node, operands);
    bool result = false;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t operand = operands[i];
        result |= operand < graph->count && (check_common_bool_candidate(graph, operand) ||
                                             (operand < stream->count && check_stream_constant_kind(stream, operand) == CHECK_CONSTANT_FLOATING));
    }
    return result;
}

// Whether a truth context's operand is a floating constant, the one source its conversion to _Bool can warn of.
static bool check_common_truth_floating(const CheckGraph* graph, const CheckStream* stream, uint32_t node)
{
    uint32_t operands[3];
    const uint32_t count = check_common_truth_operands(graph, node, operands);
    bool result = false;
    for (uint32_t i = 0; i < count; i += 1)
    {
        result |= operands[i] < graph->count && operands[i] < stream->count && check_stream_constant_kind(stream, operands[i]) == CHECK_CONSTANT_FLOATING;
    }
    return result;
}

static bool check_common_shift_op(CheckOp op)
{
    const bool result = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT ||
                        op == CHECK_OP_SHIFT_LEFT_ASSIGN || op == CHECK_OP_SHIFT_RIGHT_ASSIGN;
    return result;
}

static uint32_t check_common_shift_warning(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node)
{
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    uint32_t warning = 0;
    if (left < graph->count && check_common_integer_constant(stream, right))
    {
        const CheckValue count = check_stream_value(stream, right);
        const CheckOp op = graph->ops[node];
        const bool compound = op == CHECK_OP_SHIFT_LEFT_ASSIGN || op == CHECK_OP_SHIFT_RIGHT_ASSIGN;
        const TypeId left_type = compound ? type_conversion_operand(types, check_stream_type(stream, left)) :
                                           stream->conversions[node].operands[0].type;
        uint32_t left_width = 0;
        uint32_t right_width = 0;
        bool left_signed = false;
        bool right_signed = false;
        if (check_value_evaluated_constant(&count) && check_common_integer(types, left_type, &left_width, &left_signed) &&
            check_common_integer(types, count.type, &right_width, &right_signed))
        {
            // Clang checks a compound Boolean shift against its storage, unlike the value width used for _BitInt.
            // The ordinary shift above already uses its promoted type; no other integer width changes here.
            if (compound && type_unqualified(type_strip_aligned(types, left_type)) == BUSTER_TYPE_BOOL)
            {
                left_width = (uint32_t)(type_size(types, left_type) * 8);
            }
            // Integer promotion preserves the right operand's mathematical value. Mask its own width before
            // testing its sign and magnitude, so an unsigned 128-bit count never narrows to a signed host value.
            const unsigned __int128 bits = count.integer & check_action_mask(right_width);
            const bool negative = right_signed && ((bits >> (right_width - 1)) & 1) != 0;
            warning = negative ? CHECK_WARNING_SHIFT_COUNT_NEGATIVE : bits >= left_width ? CHECK_WARNING_SHIFT_COUNT_OVERFLOW : 0;
        }
    }
    return warning;
}

// Runtime shift counts do not need a constant left operand (#1394). Use the checked operand conversion for an
// ordinary shift's promoted width; Clang diagnoses compound shifts against the original lvalue type instead.
// Constant folding retains its own warnings, including their required-constant and source-suppression rules.
static void check_common_shift_count(CheckCommonEmission* emission, uint32_t node, bool detached)
{
    const CheckGraph* graph = &emission->input->graph;
    const CheckStream* stream = emission->stream;
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    const uint32_t token = graph->tokens[node];
    const uint32_t counts = CHECK_WARNING_SHIFT_COUNT_NEGATIVE | CHECK_WARNING_SHIFT_COUNT_OVERFLOW;
    const uint32_t retained = stream->warnings[node] |
                              (emission->input->constant_warnings != NULL && token < emission->input->token_count ?
                               emission->input->constant_warnings[token] : 0);
    const uint32_t suppressed = graph->warning_suppressed != NULL ? graph->warning_suppressed[node] : 0;
    const uint32_t contexts = graph->contexts != NULL ? graph->contexts[node] : 0;
    const bool ice = (contexts & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT)) != 0;
    const bool emitted = (emission->emitted[node] & BUSTER_CHECK_EMITTED_SHIFT_COUNT) != 0;
    const bool replayed = !emitted && check_common_shift_replay(emission->pp, emission->shifts, token);
    if (replayed)
    {
        emission->emitted[node] |= BUSTER_CHECK_EMITTED_SHIFT_COUNT;
    }
    // Constant if/switch folding removes dead arms before the control walk can mark their roots unreachable.
    // Their ordinary counts stay quiet; detached static-selection arms still have no CFG block in Clang.
    // A flushed body's arms are evaluated whatever its folds (check_common_flush).
    const bool dropped = !detached && emission->program != NULL && (emission->flushed == NULL || token >= emission->parser->tokens.count || emission->flushed[token] == 0) &&
                         check_program_token_dropped(emission->program, token);
    if (!emitted && !replayed && !ice && ((retained | suppressed) & counts) == 0 && left < graph->count && right < graph->count &&
        !check_common_integer_constant(stream, left) && check_common_integer_constant(stream, right) && !dropped)
    {
        const uint32_t warning = check_common_shift_warning(graph, stream, emission->types, node);
        if (warning != 0)
        {
            const bool negative = warning == CHECK_WARNING_SHIFT_COUNT_NEGATIVE;
            check_common_report_deferred(emission->parser, emission->pp, emission->input, token,
                                         negative ? CHECK_WARNING_ID_SHIFT_COUNT_NEGATIVE : CHECK_WARNING_ID_SHIFT_COUNT_OVERFLOW,
                                         false, negative ? "shift count is negative" : "shift count >= width of type",
                                         NULL, 0, emission->scratch);
            // An ineligible ordinary visit (notably a dropped arm) cannot suppress a later detached visit.
            emission->emitted[node] |= BUSTER_CHECK_EMITTED_SHIFT_COUNT;
        }
    }
}

// A dereference clang warns its optimizer deletes (#1447, -Wnull-dereference, clang's CheckForNullPointerDereference):
// `*` of a null pointer constant through casts (its operand an integer constant 0 once the casts are passed, or any
// expression of type nullptr_t once the conversions are passed too, as IgnoreParenCasts and isNullPointerConstant find
// it), of a type that is neither volatile nor an array's or a function's, whose value an lvalue conversion would read.
// Whether its value is read is check_common_arithmetic_own's.
static bool check_common_null_dereference(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node)
{
    bool result = false;
    if (node < graph->count && graph->ops[node] == CHECK_OP_DEREFERENCE)
    {
        uint32_t operand = graph->operand0[node];
        for (uint32_t steps = 0; operand < graph->count && graph->ops[operand] == CHECK_OP_CAST && steps < graph->count; steps += 1)
        {
            operand = graph->operand0[operand];
        }
        uint32_t inner = operand;
        for (uint32_t steps = 0; inner < graph->count && (graph->ops[inner] == CHECK_OP_CAST || graph->ops[inner] == CHECK_OP_CONVERT) && steps < graph->count;
             steps += 1)
        {
            inner = graph->operand0[inner];
        }
        const TypeId inner_type = inner < graph->count ? check_stream_type(stream, inner) : 0;
        const TypeId inner_bare = inner_type != 0 ? type_unqualified(type_strip_aligned(types, inner_type)) : 0;
        // (Every dereference asks, as the reach is made: the constant first, which nearly none has.)
        if ((check_common_integer_constant(stream, operand) && check_stream_value(stream, operand).integer == 0) ||
            (inner_bare != 0 && type_kind(types, inner_bare) == BUSTER_TYPE_KIND_NULLPTR))
        {
            const TypeId type = check_stream_type(stream, node);
            const TypeId bare = type != 0 ? type_unqualified(type_strip_aligned(types, type)) : 0;
            const uint8_t kind = bare != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_VOID;
            result = (type_qualifiers(type) & BUSTER_TYPE_VOLATILE) == 0 && !type_kind_is_array(kind) && kind != BUSTER_TYPE_KIND_FUNCTION &&
                     kind != BUSTER_TYPE_KIND_VOID;
        }
    }
    return result;
}

// Whether a comma's operands' subtrees hold an index's bound the tree walk may warn of (BUSTER_CHECK_REACH_BOUND).
static bool check_common_comma_bound_operands(const CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    const uint8_t reach = emission->reach == NULL ? BUSTER_CHECK_REACH_BOUND :
                          (uint8_t)((left < graph->count ? emission->reach[left] : 0u) | (right < graph->count ? emission->reach[right] : 0u));
    return (reach & BUSTER_CHECK_REACH_BOUND) != 0;
}

// Whether a division's divisor is zero as clang's EvaluateAsInt finds it (DiagnoseBadDivideOrRemainderValues): an integer
// constant, or, through integer casts, a read of a const non-volatile object whose completed initializer gives it zero
// (its receipt, CheckInput.const_values: `const int k = 0; 1 / k`, an element's or a member's too, #1447).
static bool check_common_zero_divisor(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, const CheckConstValues* const_values,
                                      uint32_t division, uint32_t divisor)
{
    uint32_t at = divisor;
    bool result = false;
    bool going = true;
    for (uint32_t steps = 0; going && at < graph->count && steps < graph->count; steps += 1)
    {
        if (check_common_integer_constant(stream, at))
        {
            result = check_stream_value(stream, at).integer == 0;
            going = false;
        }
        else if (graph->ops[at] == CHECK_OP_CAST || graph->ops[at] == CHECK_OP_CONVERT)
        {
            uint32_t width = 0;
            bool sign = false;
            going = check_common_integer(types, check_stream_type(stream, at), &width, &sign);
            at = going ? graph->operand0[at] : at;
        }
        else
        {
            const CheckValue value = check_stream_value(stream, at);
            const uint32_t qualifiers = type_qualifiers(value.type);
            CheckValue loaded = { 0 };
            uint32_t width = 0;
            bool sign = false;
            result = value.category == CHECK_LVALUE && value.constant_kind == CHECK_CONSTANT_ADDRESS && value.fault == CHECK_FAULT_NONE &&
                     (qualifiers & BUSTER_TYPE_CONST) != 0 && (qualifiers & BUSTER_TYPE_VOLATILE) == 0 &&
                     (value.flags & (BUSTER_CHECK_VOLATILE_PART | BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_CONST_READ_RESTRICTED)) == 0 &&
                     check_common_integer(types, value.type, &width, &sign) &&
                     const_values != NULL && check_const_load(types, const_values, value.object, value.offset, value.object_bit_offset, value.bit_width,
                                      value.type, graph->source_positions[division], &loaded) &&
                     loaded.constant_kind == CHECK_CONSTANT_INTEGER && loaded.integer == 0;
            going = false;
        }
    }
    return result;
}

// The arithmetic-warning visit one scan frame runs after its subtree: `emit_children`'s other half. A null dereference's
// too (check_common_null_dereference), which clang's DiagRuntimeBehavior defers to the body's end with them: reachable,
// evaluated and read, as an lvalue conversion or an assignment's (or a compound one's) destination reads it, not where its
// address is taken, a member of it named or it is stepped, with clang's note at the same `*`.
static void check_common_arithmetic_own(CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const CheckOp op = graph->ops[node];
    if (op == CHECK_OP_DEREFERENCE && check_common_null_dereference(graph, emission->stream, emission->types, node))
    {
        const uint32_t parent = emission->parents != NULL ? emission->parents[node] : BUSTER_CHECK_NONE;
        const CheckOp parent_op = parent < graph->count ? graph->ops[parent] : CHECK_OP_COUNT;
        const bool unread = parent_op == CHECK_OP_ADDRESS || (parent_op == CHECK_OP_MEMBER && graph->operand0[parent] == node) ||
                            (parent_op >= CHECK_OP_PRE_INCREMENT && parent_op <= CHECK_OP_POST_DECREMENT);
        if (!unread)
        {
            const uint32_t star = graph->tokens[node];
            const CheckCommonNote note = { star, "consider using __builtin_trap() or qualifying pointer with 'volatile'" };
            check_common_report_deferred(emission->parser, emission->pp, emission->input, star, CHECK_WARNING_ID_NULL_DEREFERENCE, false,
                                         "indirection of non-volatile null pointer will be deleted, not trap", &note, 1, emission->scratch);
        }
    }
    else if (op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER)
    {
        const uint32_t divisor = graph->operand1[node];
        const uint32_t contexts = graph->contexts != NULL ? graph->contexts[node] : 0;
        const bool ice = (contexts & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT)) != 0;
        if (!ice && check_common_zero_divisor(graph, emission->stream, emission->types, &emission->input->const_values, node, divisor))
        {
            uint32_t width = 0;
            bool sign = false;
            if (check_common_integer(emission->types, check_stream_type(emission->stream, graph->operand0[node]), &width, &sign))
            {
                // In a file-scope initializer clang's DiagRuntimeBehavior queues it to the declarator's end, after the
                // initializer's own errors (`const int d = 1 / 0;`'s "not a compile-time constant", #1447), as a comma's left
                // operand (check_common_report_comma_left).
                const Parser* parser = emission->parser;
                const uint32_t token = graph->tokens[node];
                const uint32_t boundary = check_common_function_boundary(parser, token);
                const uint32_t declarator = boundary == token ? check_driver_file_initializer_end(parser, emission->input, token) : BUSTER_CHECK_NONE;
                const PPDiagnosticRecord* previous = emission->pp->diagnostic_last;
                check_common_report_evaluated(parser, emission->pp, emission->input, token, declarator != BUSTER_CHECK_NONE ? declarator : boundary,
                                              declarator != BUSTER_CHECK_NONE ? PP_DIAGNOSTIC_RUNTIME : PP_DIAGNOSTIC_DEFERRED, CHECK_WARNING_ID_DIVISION_BY_ZERO,
                                              false, op == CHECK_OP_DIVIDE ? "division by zero is undefined" : "remainder by zero is undefined", NULL, 0,
                                              emission->scratch);
                if (declarator != BUSTER_CHECK_NONE && node < emission->input->graph.count && emission->input->graph.source_positions != NULL)
                {
                    pp_diagnostic_suborder(emission->pp, previous, (uint64_t)emission->input->graph.source_positions[node] + 1);
                }
            }
        }
    }
    else if (check_common_shift_op(op))
    {
        check_common_shift_count(emission, node, false);
    }
    else if (op == CHECK_OP_COMMA && check_common_emit_evaluated(emission, node) && graph->tokens[node] < emission->parser->tokens.count &&
             emission->parser->tokens.kinds[graph->tokens[node]] == PARSE_KIND_COMMA &&
             (emission->emitted[node] & BUSTER_CHECK_EMITTED_COMMA_MARKER) == 0 && !check_common_comma_bound_operands(emission, node))
    {
        // A source comma's left operand of no effect: clang's CheckCommaOperands queues its DiagIfReachable as it builds
        // the comma, among the divisions and shifts built around it, so the tree walk's comma stage leaves it out. A
        // statement expression's comma is its statement's root's and its enclosing root's too, and warns once. A comma
        // whose operands hold an index's bound, which clang checks as it converts the operand (a discarded left one's after
        // the marker), keeps its marker in the tree walk's comma stage, which orders them.
        emission->emitted[node] |= BUSTER_CHECK_EMITTED_COMMA_MARKER;
        check_common_comma_marker(emission, node);
    }
}



static bool check_common_from_macro(const Parser* parser, const Preprocessor* pp, uint32_t token);

// clang's PromotedRange::compare (SemaChecking.cpp) of a constant against an operand's range promoted to the comparison's
// type: Less, Greater and InHole (a wrapped signed range's gap, NE alone) lie out of the range, Min, Max, OnlyValue and
// InRange in it.
#define BUSTER_CHECK_RANGE_LT 0x01u
#define BUSTER_CHECK_RANGE_LE 0x02u
#define BUSTER_CHECK_RANGE_GT 0x04u
#define BUSTER_CHECK_RANGE_GE 0x08u
#define BUSTER_CHECK_RANGE_EQ 0x10u
#define BUSTER_CHECK_RANGE_NE 0x20u
#define BUSTER_CHECK_RANGE_IN 0x40u
#define BUSTER_CHECK_RANGE_LESS (BUSTER_CHECK_RANGE_LE | BUSTER_CHECK_RANGE_LT | BUSTER_CHECK_RANGE_NE)
#define BUSTER_CHECK_RANGE_MIN (BUSTER_CHECK_RANGE_LE | BUSTER_CHECK_RANGE_IN)
#define BUSTER_CHECK_RANGE_MAX (BUSTER_CHECK_RANGE_GE | BUSTER_CHECK_RANGE_IN)
#define BUSTER_CHECK_RANGE_GREATER (BUSTER_CHECK_RANGE_GE | BUSTER_CHECK_RANGE_GT | BUSTER_CHECK_RANGE_NE)
#define BUSTER_CHECK_RANGE_ONLY (BUSTER_CHECK_RANGE_LE | BUSTER_CHECK_RANGE_GE | BUSTER_CHECK_RANGE_EQ | BUSTER_CHECK_RANGE_IN)

// Whether two bit patterns of `width` bits order a < b, read as `is_signed` says.
static bool check_common_range_below(unsigned __int128 a, unsigned __int128 b, uint32_t width, bool is_signed)
{
    const bool result = is_signed ? check_common_integer_bits(a, width, true) < check_common_integer_bits(b, width, true) :
                                    (a & check_action_mask(width)) < (b & check_action_mask(width));
    return result;
}

// A constant `value` of the comparison's type (`width` bits, up to 128, `is_signed`) against the range of an operand of
// `range_width` bits, nonnegative or not, promoted to that type, as PromotedRange's extOrTrunc builds it.
static uint32_t check_common_range_compare(unsigned __int128 value, uint32_t width, bool is_signed, uint32_t range_width, bool nonnegative)
{
    const unsigned __int128 mask = check_action_mask(width);
    unsigned __int128 minimum = 0;
    unsigned __int128 maximum = 0;
    if (range_width >= width && is_signed)
    {
        // Promotion made the type narrower: every value of the signed type is in range.
        minimum = (unsigned __int128)1 << (width - 1);
        maximum = minimum - 1;
    }
    else
    {
        // getMinValue and getMaxValue of the range's own width, extended (sign-extended when signed) or truncated.
        const uint32_t own = range_width < width ? range_width : width;
        minimum = nonnegative ? 0 : (~(unsigned __int128)0 << (own - 1)) & mask;
        maximum = nonnegative ? check_action_mask(own) : check_action_mask(own - 1);
    }
    value &= mask;
    uint32_t result = 0;
    if (check_common_range_below(maximum, minimum, width, is_signed))
    {
        // A discontiguous range, which only an unsigned comparison of a signed operand has.
        result = value == 0 ? BUSTER_CHECK_RANGE_MIN : value == mask ? BUSTER_CHECK_RANGE_MAX :
                 value >= minimum || value <= maximum ? BUSTER_CHECK_RANGE_IN : BUSTER_CHECK_RANGE_NE;
    }
    else if (check_common_range_below(value, minimum, width, is_signed))
    {
        result = BUSTER_CHECK_RANGE_LESS;
    }
    else if (value == minimum)
    {
        result = minimum == maximum ? BUSTER_CHECK_RANGE_ONLY : BUSTER_CHECK_RANGE_MIN;
    }
    else
    {
        result = check_common_range_below(value, maximum, width, is_signed) ? BUSTER_CHECK_RANGE_IN :
                 value == maximum ? BUSTER_CHECK_RANGE_MAX : BUSTER_CHECK_RANGE_GREATER;
    }
    return result;
}

// PromotedRange::constantValue: whether comparison `op` is always true (1), always false (0), or neither (-1) for a
// constant whose place against the other operand's range is `place`.
static int32_t check_common_range_truth(CheckOp op, uint32_t place, bool constant_right)
{
    uint32_t true_flag = BUSTER_CHECK_RANGE_EQ;
    uint32_t false_flag = BUSTER_CHECK_RANGE_NE;
    if (op == CHECK_OP_NOT_EQUAL)
    {
        true_flag = BUSTER_CHECK_RANGE_NE;
        false_flag = BUSTER_CHECK_RANGE_EQ;
    }
    else if (op != CHECK_OP_EQUAL)
    {
        const bool lower = (op == CHECK_OP_LESS || op == CHECK_OP_GREATER_EQUAL) != constant_right;
        true_flag = lower ? BUSTER_CHECK_RANGE_LT : BUSTER_CHECK_RANGE_GT;
        false_flag = lower ? BUSTER_CHECK_RANGE_GE : BUSTER_CHECK_RANGE_LE;
        const bool swap = op == CHECK_OP_GREATER_EQUAL || op == CHECK_OP_LESS_EQUAL;
        const uint32_t held = true_flag;
        true_flag = swap ? false_flag : true_flag;
        false_flag = swap ? held : false_flag;
    }
    const int32_t result = (place & true_flag) != 0 ? 1 : (place & false_flag) != 0 ? 0 : -1;
    return result;
}

// A comparison's range facts for -Wtautological-constant-out-of-range-compare (#2218): which side is the constant, the
// comparison's type of the usual arithmetic conversions (`width`, up to 128 bits here, `is_signed`), the other operand's
// own range (its type's, or a boolean's one bit), and the constant converted to the comparison's type.
typedef struct CheckCommonValueRange CheckCommonValueRange;
struct CheckCommonValueRange
{
    unsigned __int128 converted;
    TypeId other_type;
    uint32_t constant;
    uint32_t other;
    uint32_t width;
    uint32_t range_width;
    uint32_t place;
    bool constant_right;
    bool is_signed;
    bool range_signed;
    bool known_boolean;
    // Whether the facts hold: a candidate outside a required constant whose types are integers.
    bool ranged;
};

// The range facts of comparison `node` (CheckCommonValueRange), and the constant's place against the other operand's range
// where the comparison's type is 128 bits or narrower (`place`, BUSTER_CHECK_RANGE_IN otherwise).
static CheckCommonValueRange check_common_range_facts(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node)
{
    CheckCommonValueRange range = { .place = BUSTER_CHECK_RANGE_IN };
    // A candidate (clang's CheckTautologicalComparison may find it out of range): outside a required constant, an integer
    // constant expression on exactly one side, and on the other an operand whose range is narrower than the comparison's
    // type, a narrower type's or a boolean's. An operand of the comparison's width covers all of its values, wrapped or not.
    // Each operand's type is asked once: its own width, then its promotion's.
    const uint32_t contexts = graph->contexts != NULL ? graph->contexts[node] : 0;
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    const bool left_constant = check_common_integer_constant(stream, left) && (check_stream_flags(stream, left) & BUSTER_CHECK_ICE) != 0;
    const bool right_constant = check_common_integer_constant(stream, right) && (check_stream_flags(stream, right) & BUSTER_CHECK_ICE) != 0;
    const bool one = (contexts & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT)) == 0 && left < graph->count && right < graph->count &&
                     left_constant != right_constant;
    range.constant_right = one && right_constant;
    range.constant = range.constant_right ? right : left;
    range.other = range.constant_right ? left : right;
    range.other_type = one ? check_stream_type(stream, range.other) : 0;
    const TypeId constant_type = one ? check_stream_type(stream, range.constant) : 0;
    uint32_t own_width = 0;
    bool own_signed = false;
    uint32_t constant_width = 0;
    bool constant_signed = false;
    const bool own = one && (check_common_integer(types, range.other_type, &own_width, &own_signed) ||
                             check_wide_type(types, range.other_type, &own_width, &own_signed));
    const bool constant_integer = one && (check_common_integer(types, constant_type, &constant_width, &constant_signed) ||
                                          check_wide_type(types, constant_type, &constant_width, &constant_signed));
    // (The integer promotions: narrower than int becomes int, but a _BitInt isn't promoted.)
    const bool other_bit = own && type_kind(types, type_unqualified(type_strip_aligned(types, range.other_type))) == BUSTER_TYPE_KIND_BIT_INT;
    const bool constant_bit = constant_integer && type_kind(types, type_unqualified(type_strip_aligned(types, constant_type))) == BUSTER_TYPE_KIND_BIT_INT;
    const uint32_t other_width = own && own_width < 32 && !other_bit ? 32 : own_width;
    const bool other_signed = own && own_width < 32 && !other_bit ? true : own_signed;
    const bool constant_promoted = constant_integer && constant_width < 32 && !constant_bit;
    constant_width = constant_promoted ? 32 : constant_width;
    constant_signed = constant_promoted || constant_signed;
    const CheckOp other_op = one ? graph->ops[range.other] : CHECK_OP_COUNT;
    range.known_boolean = other_op == CHECK_OP_NOT || other_op == CHECK_OP_LOGICAL_AND || other_op == CHECK_OP_LOGICAL_OR ||
                          (other_op >= CHECK_OP_LESS && other_op <= CHECK_OP_NOT_EQUAL);
    const bool narrow = own && constant_integer && own_width < (constant_width > other_width ? constant_width : other_width);
    const bool integers = one && own && constant_integer && (range.known_boolean || narrow);
    range.known_boolean = integers && range.known_boolean;
    const bool boolean_type = integers && type_unqualified(type_strip_aligned(types, range.other_type)) == BUSTER_TYPE_BOOL;
    range.ranged = integers;
    range.range_width = range.known_boolean || boolean_type ? 1 : integers ? own_width : 0;
    range.range_signed = !(range.known_boolean || boolean_type) && own_signed;
    // The comparison's type, of the usual arithmetic conversions.
    const bool same_sign = constant_signed == other_signed;
    const uint32_t signed_width = constant_signed ? constant_width : other_width;
    const uint32_t unsigned_width = constant_signed ? other_width : constant_width;
    range.width = constant_width > other_width ? constant_width : other_width;
    range.is_signed = same_sign ? constant_signed : signed_width > unsigned_width;
    if (range.ranged && range.range_width != 0 && range.width <= 128)
    {
        TypeId value_type = 0;
        const unsigned __int128 integer = check_common_stream_integer(stream, range.constant, &value_type);
        uint32_t value_width = 0;
        bool value_signed = false;
        check_common_integer(types, value_type, &value_width, &value_signed);
        range.converted = (unsigned __int128)check_common_integer_bits(integer, value_width, value_signed) & check_action_mask(range.width);
        range.place = check_common_range_compare(range.converted, range.width, range.is_signed, range.range_width, !range.range_signed);
    }
    return range;
}

// Whether comparison `node` may give -Wtautological-constant-out-of-range-compare (#2218), the comparison walk's test of
// whether to take it: its constant out of the other operand's range, as check_common_out_of_range finds it, or a
// comparison past 128 bits, which only that function decides. A constant in range, `c == 'x'`'s, costs no walk.
static bool check_common_range_out(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node)
{
    const CheckCommonValueRange range = check_common_range_facts(graph, stream, types, node);
    const bool wide = range.ranged && range.range_width != 0 && range.width > 128;
    const bool out = range.ranged && range.range_width != 0 && range.width <= 128 && (range.place & BUSTER_CHECK_RANGE_IN) == 0 &&
                     check_common_range_truth(graph->ops[node], range.place, range.constant_right) >= 0;
    return wide || out;
}

// Of up to 16 comparison nodes `nodes` (`lanes` live), those that may give -Wtautological-constant-out-of-range-compare
// (check_common_range_out), a bit each, for the comparison walk's reach where -Wsign-compare doesn't take every comparison.
// The lanes gather both operands and their value flags from the stream's flag column: one of them an integer constant
// expression's, which every candidate has, and, where only one is, the other operand neither an int or long class builtin
// that covers the constant's type (the TypeIds' builtin codes, gathered too) nor a narrow builtin compared with an int or
// unsigned int constant in its range (the constant's value gathered from its payload row), but for a boolean operator's,
// which no candidate's is; only the lanes left take the scalar test. A stream without the columns tests every comparison,
// as the scalar path, the twin and oracle, does through check_common_own_reach.
static uint32_t check_common_range_lanes(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, const uint32_t* nodes, __mmask16 lanes)
{
    const uint32_t count = graph->count < stream->count ? graph->count : stream->count;
    const __m512i indices = _mm512_maskz_loadu_epi32(lanes, nodes);
    const __mmask16 compares = _mm512_mask_cmplt_epu32_mask(lanes, indices, _mm512_set1_epi32((int)count));
    uint32_t selected = compares;
    if (compares != 0 && stream->dense_values == NULL && stream->value_flags != NULL && stream->value_types != NULL)
    {
        const __m512i limit = _mm512_set1_epi32((int)count);
        const __m512i left = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), compares, indices, graph->operand0, 4);
        const __m512i right = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), compares, indices, graph->operand1, 4);
        const __mmask16 inside = _mm512_mask_cmplt_epu32_mask(_mm512_mask_cmplt_epu32_mask(compares, left, limit), right, limit);
        const __m512i ice = _mm512_set1_epi32(BUSTER_CHECK_ICE);
        const __m512i left_flags = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), inside, left, stream->value_flags, 4);
        const __m512i right_flags = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), inside, right, stream->value_flags, 4);
        const __mmask16 left_ice = _mm512_mask_test_epi32_mask(inside, left_flags, ice);
        const __mmask16 right_ice = _mm512_mask_test_epi32_mask(inside, right_flags, ice);
        // Where one operand alone is an integer constant expression, it is the constant: the other one is no candidate's
        // when it is an int or a long class builtin at least as wide as the constant's type promoted, and no boolean
        // operator's (check_common_range_facts' narrowness test, its types' codes read from their TypeIds).
        const __mmask16 one = (__mmask16)(left_ice ^ right_ice);
        const __m512i left_types = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), one, left, stream->value_types, 4);
        const __m512i right_types = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), one, right, stream->value_types, 4);
        const __m512i left_ops = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), one, left, graph->ops, 4);
        const __m512i right_ops = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), one, right, graph->ops, 4);
        const __m512i other_code = _mm512_srli_epi32(_mm512_mask_blend_epi32(left_ice, left_types, right_types), BUSTER_TYPE_INDEX_SHIFT);
        const __m512i constant_code = _mm512_srli_epi32(_mm512_mask_blend_epi32(left_ice, right_types, left_types), BUSTER_TYPE_INDEX_SHIFT);
        const __m512i other_op = _mm512_mask_blend_epi32(left_ice, left_ops, right_ops);
        const __m512i bool_code = _mm512_set1_epi32(BUSTER_TYPE_CODE_BOOL);
        const __mmask16 boolean = _mm512_mask_cmple_epu32_mask(one, _mm512_sub_epi32(other_op, _mm512_set1_epi32(CHECK_OP_LESS)),
                                                                _mm512_set1_epi32(CHECK_OP_NOT_EQUAL - CHECK_OP_LESS)) |
                                  _mm512_mask_cmpeq_epi32_mask(one, other_op, _mm512_set1_epi32(CHECK_OP_NOT)) |
                                  _mm512_mask_cmpeq_epi32_mask(one, other_op, _mm512_set1_epi32(CHECK_OP_LOGICAL_AND)) |
                                  _mm512_mask_cmpeq_epi32_mask(one, other_op, _mm512_set1_epi32(CHECK_OP_LOGICAL_OR));
        const __m512i constant_offset = _mm512_sub_epi32(constant_code, bool_code);
        const __mmask16 int_wide = _mm512_mask_cmple_epu32_mask(one, _mm512_sub_epi32(other_code, _mm512_set1_epi32(BUSTER_TYPE_CODE_INT)), _mm512_set1_epi32(1)) &
                                   _mm512_mask_cmple_epu32_mask(one, constant_offset, _mm512_set1_epi32(BUSTER_TYPE_CODE_UINT - BUSTER_TYPE_CODE_BOOL));
        const __mmask16 long_wide = _mm512_mask_cmple_epu32_mask(one, _mm512_sub_epi32(other_code, _mm512_set1_epi32(BUSTER_TYPE_CODE_LONG)), _mm512_set1_epi32(3)) &
                                    _mm512_mask_cmple_epu32_mask(one, constant_offset, _mm512_set1_epi32(BUSTER_TYPE_CODE_ULLONG - BUSTER_TYPE_CODE_BOOL));
        // A narrow builtin (_Bool to unsigned short) compared with an int or unsigned int constant inside its range (char's
        // taken as 0 to 127, whichever its signedness): the constant's value, the low 32 bits of its payload row's integer
        // (none for no row, a zero), gathered too. An unsigned constant is in range up to the type's maximum.
        const __mmask16 narrow_other = (__mmask16)(_mm512_mask_cmple_epu32_mask(one, _mm512_sub_epi32(other_code, bool_code),
                                                                                _mm512_set1_epi32(BUSTER_TYPE_CODE_USHORT - BUSTER_TYPE_CODE_BOOL)) & ~boolean);
        const __mmask16 int_constant = _mm512_mask_cmple_epu32_mask(narrow_other, _mm512_sub_epi32(constant_code, _mm512_set1_epi32(BUSTER_TYPE_CODE_INT)),
                                                                     _mm512_set1_epi32(1));
        const bool rows_fit = stream->payload_rows != NULL && stream->payloads != NULL &&
                              (uint64_t)stream->payload_count * sizeof(CheckValue) < (UINT64_C(1) << 31);
        __mmask16 in_range = 0;
        if (int_constant != 0 && rows_fit)
        {
            const __m512i constant_nodes = _mm512_mask_blend_epi32(left_ice, right, left);
            const __m512i rows = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), int_constant, constant_nodes, stream->payload_rows, 4);
            const __mmask16 rowed = _mm512_mask_cmpneq_epu32_mask(int_constant, rows, _mm512_setzero_si512());
            const __m512i offsets = _mm512_mullo_epi32(_mm512_sub_epi32(rows, _mm512_set1_epi32(1)), _mm512_set1_epi32((int)sizeof(CheckValue)));
            const __m512i values = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), rowed, offsets,
                                                               (const char*)stream->payloads + offsetof(CheckValue, integer), 1);
            // The ranges of codes 2 to 7: _Bool, char, signed char, unsigned char, short and unsigned short.
            static const int32_t lows[16] = { 0, 0, -128, 0, -32768, 0 };
            static const int32_t highs[16] = { 1, 127, 127, 255, 32767, 65535 };
            const __m512i slot = _mm512_sub_epi32(other_code, bool_code);
            const __m512i low = _mm512_permutexvar_epi32(slot, _mm512_loadu_si512(lows));
            const __m512i high = _mm512_permutexvar_epi32(slot, _mm512_loadu_si512(highs));
            const __mmask16 unsigned_constant = _mm512_mask_cmpeq_epi32_mask(int_constant, constant_code, _mm512_set1_epi32(BUSTER_TYPE_CODE_UINT));
            // (Unmasked signed compares, which the baseline host's scalar emulation has too.)
            const __mmask16 signed_in = (__mmask16)(int_constant & ~unsigned_constant & _mm512_cmpge_epi32_mask(values, low) & _mm512_cmpge_epi32_mask(high, values));
            const __mmask16 unsigned_in = _mm512_mask_cmple_epu32_mask(unsigned_constant, values, high);
            in_range = (__mmask16)(signed_in | unsigned_in);
        }
        const __mmask16 covered = (__mmask16)((one & ~boolean & (int_wide | long_wide)) | in_range);
        selected = (uint32_t)(left_ice | right_ice) & ~(uint32_t)covered;
    }
    uint32_t result = 0;
    for (uint32_t bits = selected; bits != 0; bits &= bits - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctz(bits);
        result |= check_common_range_out(graph, stream, types, nodes[lane]) ? 1u << lane : 0u;
    }
    return result;
}

// clang's -Wtautological-constant-out-of-range-compare (#2218, CheckTautologicalComparison): an integer constant
// expression compared with an operand whose type's values all compare one way with it, the constant out of their range
// (a range's gap too, of a signed operand an unsigned comparison wraps). The operand's range is its type's (a _Bool's
// and a known boolean's 0 to 1, as "boolean expression"), not clang's tighter range of its value. Reported as
// DiagRuntimeBehavior reports it, at the operator, with the constant as the comparison's type holds it, an enumerator's
// name before it. Whether the comparison is always true or false whatever the warning's policy, which -Wsign-compare
// then leaves alone, as AnalyzeComparison does; in range, its other warnings (-Wtautological-constant-compare's) aren't
// given.
static bool check_common_out_of_range(CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const CheckStream* stream = emission->stream;
    const TypeTable* types = emission->types;
    const Parser* parser = emission->parser;
    const CheckOp op = graph->ops[node];
    const CheckCommonValueRange range = check_common_range_facts(graph, stream, types, node);
    const bool constant_right = range.constant_right;
    const uint32_t constant = range.constant;
    const uint32_t other = range.other;
    const TypeId other_type = range.other_type;
    const bool known_boolean = range.known_boolean;
    const bool ranged = range.ranged;
    const uint32_t range_width = range.range_width;
    const bool range_signed = range.range_signed;
    const uint32_t width = range.width;
    const bool is_signed = range.is_signed;
    uint32_t constant_width = 0;
    bool constant_signed = false;
    const CheckValue value = ranged ? check_stream_value(stream, constant) : (CheckValue){ 0 };
    uint32_t place = range.place;
    char digits[48];
    const char* text = NULL;
    if (ranged && range_width != 0 && width <= 128)
    {
        const unsigned __int128 converted = range.converted;
        // APSInt's decimal digits, as the comparison's type reads them.
        const bool negative = is_signed && ((converted >> (width - 1)) & 1u) != 0;
        unsigned __int128 magnitude = negative ? (0 - converted) & check_action_mask(width) : converted;
        uint32_t count = sizeof(digits) - 1;
        digits[count] = 0;
        do
        {
            count -= 1;
            digits[count] = (char)('0' + (uint32_t)(magnitude % 10));
            magnitude /= 10;
        }
        while (magnitude != 0);
        if (negative)
        {
            count -= 1;
            digits[count] = '-';
        }
        text = digits + count;
    }
    else if (ranged && range_width != 0 && range_width <= 127 && is_signed && value.type != 0 && check_wide_type(types, value.type, &constant_width, &constant_signed))
    {
        // A comparison past 128 bits whose type is the constant's own: past the operand's range by its magnitude's bits.
        const uint32_t count = check_wide_count(constant_width);
        uint64_t* limbs = check_wide_allocate(count);
        if (limbs != NULL && check_wide_operand(types, &value, limbs, count))
        {
            const bool negative = check_wide_negative(limbs, constant_width, constant_signed);
            if (negative)
            {
                check_wide_negate_in_place(limbs, count, constant_width);
            }
            const uint32_t bits = check_wide_bits(limbs, count);
            const __int128 small = bits <= 126 ? (negative ? -(__int128)(((unsigned __int128)limbs[1] << 64) | limbs[0]) :
                                                             (__int128)(((unsigned __int128)limbs[1] << 64) | limbs[0])) : 0;
            const __int128 low = range_signed ? -((__int128)1 << (range_width - 1)) : 0;
            const __int128 high = range_signed ? ((__int128)1 << (range_width - 1)) - 1 : (__int128)check_action_mask(range_width);
            place = bits > 126 ? (negative ? BUSTER_CHECK_RANGE_LESS : BUSTER_CHECK_RANGE_GREATER) :
                    small < low ? BUSTER_CHECK_RANGE_LESS : small > high ? BUSTER_CHECK_RANGE_GREATER :
                    small == low ? (low == high ? BUSTER_CHECK_RANGE_ONLY : BUSTER_CHECK_RANGE_MIN) :
                    small == high ? BUSTER_CHECK_RANGE_MAX : BUSTER_CHECK_RANGE_IN;
            text = check_wide_text(types, &value, emission->scratch);
        }
    }
    const int32_t truth = text != NULL ? check_common_range_truth(op, place, constant_right) : -1;
    const bool out = truth >= 0 && (place & BUSTER_CHECK_RANGE_IN) == 0;
    if (out)
    {
        Arena* scratch = emission->scratch;
        // An enumerator's name before its value.
        const uint32_t token = graph->tokens[constant];
        const uint32_t declaration = graph->ops[constant] == CHECK_OP_VALUE && token < emission->input->token_count && emission->input->declarations != NULL ?
                                     emission->input->declarations[token] : BUSTER_CHECK_NONE;
        const bool enumerator = declaration < emission->input->token_count && emission->input->declaration_kinds[declaration] == C_BINDING_ENUMERATOR;
        const uint32_t name = enumerator ? parser->tokens.names[token] : 0;
        const char* shown = enumerator ? check_warning_text(scratch, "'%.*s' (%s)", (int)intern_length(emission->pp->table, name),
                                                            intern_bytes(emission->pp->table, name), text) : text;
        const String8 type_name = known_boolean ? (String8){ 0 } :
                                  check_driver_warning_type(parser, emission->pp, emission->input, emission->types, emission->spell, scratch, other_type,
                                                            check_common_operand_spelling(parser, emission->input, graph, stream, emission->types, other));
        const char* message = shown == NULL ? NULL : known_boolean ?
                              check_warning_text(scratch, "result of comparison of constant %s with boolean expression is always %s", shown,
                                                 truth != 0 ? "true" : "false") :
                              type_name.length != 0 ?
                              check_warning_text(scratch, "result of comparison of constant %s with expression of type %.*s is always %s", shown,
                                                 (int)type_name.length, type_name.pointer, truth != 0 ? "true" : "false") : NULL;
        if (message != NULL)
        {
            // As a division's: queued to a file-scope declarator's end, else to the function's.
            const uint32_t at = graph->tokens[node];
            const uint32_t boundary = check_common_function_boundary(parser, at);
            const uint32_t declarator = boundary == at ? check_driver_file_initializer_end(parser, emission->input, at) : BUSTER_CHECK_NONE;
            const PPDiagnosticRecord* previous = emission->pp->diagnostic_last;
            check_common_report_evaluated(parser, emission->pp, emission->input, at, declarator != BUSTER_CHECK_NONE ? declarator : boundary,
                                          declarator != BUSTER_CHECK_NONE ? PP_DIAGNOSTIC_RUNTIME : PP_DIAGNOSTIC_DEFERRED,
                                          CHECK_WARNING_ID_TAUTOLOGICAL_OUT_OF_RANGE_COMPARE, false, message, NULL, 0, scratch);
            if (declarator != BUSTER_CHECK_NONE)
            {
                // After what building the declarator's expression queued, as CheckCompletedExpr analyzes it once complete.
                pp_diagnostic_suborder(emission->pp, previous, UINT64_MAX >> 1);
            }
        }
    }
    // In range, a constant from a macro or an enumerator decides nothing (IsEnumConstOrFromMacro).
    const bool decided = truth >= 0 && (out || !(check_common_from_macro(parser, emission->pp, graph->tokens[constant]) ||
                                                 (graph->ops[constant] == CHECK_OP_VALUE && graph->tokens[constant] < emission->input->token_count &&
                                                  emission->input->declarations != NULL &&
                                                  emission->input->declarations[graph->tokens[constant]] < emission->input->token_count &&
                                                  emission->input->declaration_kinds[emission->input->declarations[graph->tokens[constant]]] ==
                                                  C_BINDING_ENUMERATOR)));
    return decided;
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
    // An always true or false comparison gives no -Wsign-compare (AnalyzeComparison).
    const bool tautological = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL && check_common_out_of_range(emission, node);
    if (op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL && !tautological)
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
                                                                check_stream_type(stream, left),
                                                                check_common_operand_spelling(parser, emission->input, graph, stream, types, left));
            const String8 right_name = check_driver_warning_type(parser, emission->pp, emission->input, types, emission->spell, scratch,
                                                                 check_stream_type(stream, right),
                                                                 check_common_operand_spelling(parser, emission->input, graph, stream, types, right));
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

static bool check_common_call_prototype(const CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const uint32_t callee = graph->operand0[node];
    TypeId function = callee < graph->count ? check_stream_type(emission->stream, callee) : 0;
    function = function != 0 ? type_unqualified(type_strip_aligned(emission->types, function)) : 0;
    if (function != 0 && type_kind(emission->types, function) == BUSTER_TYPE_KIND_POINTER)
    {
        function = type_unqualified(type_strip_aligned(emission->types, type_element(emission->types, function)));
    }
    return function != 0 && type_kind(emission->types, function) == BUSTER_TYPE_KIND_FUNCTION &&
           (emission->types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
}

static TypeId check_common_enum_coerced(const CheckInput* input, const CheckStream* stream, uint32_t node);
static uint32_t check_common_enum_cast_unselected(const CheckGraph* graph, uint32_t node);
static uint32_t check_common_full_expression_end(const Parser* parser, const CheckGraph* graph, const uint32_t* parents, uint32_t node);

// Whether `source`, converted to an enumeration's target, may be an enumeration: its own kind an enumeration's (or an aligned
// typedef's, which hides it), a named constant leaf (an enumerator), or arms that carry the conversion to theirs. Rare:
// out of line, so that the hot candidate tests keep their code.
static __attribute__((noinline)) bool check_common_enum_cast_source(const Parser* parser, const CheckGraph* graph, const CheckStream* stream,
                                                                    const TypeTable* types, uint32_t source)
{
    const uint8_t source_kind = type_kind(types, check_stream_type(stream, source));
    // (A selection's arm may carry the conversion as a conditional's arms do: `_Generic(c, int: c ? g : g)`, or be an
    // enumerator, whose constant the selection folds to: `_Generic(1, int: B1)`, #1447.)
    const uint32_t chosen = graph->ops[source] == CHECK_OP_SELECT ? check_common_enum_cast_unselected(graph, source) : source;
    return source_kind == BUSTER_TYPE_KIND_ENUM || source_kind == BUSTER_TYPE_KIND_ALIGNED || check_common_conversion_arms(graph, source) ||
           (chosen != source && chosen < graph->count && check_common_conversion_arms(graph, chosen)) ||
           (chosen < graph->count && graph->ops[chosen] == CHECK_OP_VALUE && (check_stream_flags(stream, chosen) & BUSTER_CHECK_ICE) != 0 &&
            graph->tokens[chosen] < parser->tokens.count && parser->tokens.kinds[graph->tokens[chosen]] == PARSE_KIND_IDENTIFIER);
}

// Whether `source`'s implicit conversion to `target` may change its enumeration (#1447, -Wimplicit-enum-enum-cast): the
// target's own kind an enumeration's (or an aligned typedef's, which hides it), tested first as it rarely is, and the
// source one that may be an enumeration (check_common_enum_cast_source).
static inline bool check_common_enum_cast_candidate(bool named, const Parser* parser, const CheckGraph* graph, const CheckStream* stream,
                                                    const TypeTable* types, uint32_t source, TypeId target)
{
    const uint8_t target_kind = named ? type_kind(types, target) : BUSTER_TYPE_KIND_VOID;
    return __builtin_expect(target_kind == BUSTER_TYPE_KIND_ENUM || target_kind == BUSTER_TYPE_KIND_ALIGNED, 0) && source < graph->count &&
           check_common_enum_cast_source(parser, graph, stream, types, source);
}

// Whether an implicit conversion edge, its flags `flags`, from `source` to `target` is a candidate conversion between
// enumerations (check_common_enum_cast_candidate): an assignment's, neither a cast's nor an atomic's. Out of line, so
// that the candidate tests it joins keep their code where the unit names no tagged enumeration (CheckInput.enum_names).
static __attribute__((noinline)) bool check_common_enum_cast_edge(const Parser* parser, const CheckGraph* graph, const CheckStream* stream,
                                                                  const TypeTable* types, uint32_t source, uint32_t flags, TypeId target)
{
    return (flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (flags & (BUSTER_CHECK_CONVERT_CAST | BUSTER_CHECK_CONVERT_ATOMIC)) == 0 &&
           check_common_enum_cast_candidate(true, parser, graph, stream, types, source, target);
}

// Whether clang reaches its conversion between enumerations for the leaf `leaf` converted to the enumeration `target`
// (#1447): CheckImplicitConversion's range tests come first. The leaf's likely range (TryGetExprRange, approximate) is a
// constant's value's, its significant bits when negative and its active bits otherwise, or else its bit-field's width or
// its type's, an enumeration's integer type's. A range wider than the target's integer type returns before the
// enumerations are compared: a constant's -Wconstant-conversion, a value's -Wconversion family. So does a constant whose
// non-negative value fills a signed target's width exactly from a signed type (isSameWidthConstantConversion's
// -Wconstant-conversion).
static bool check_common_enum_cast_reached(const CheckStream* stream, const TypeTable* types, uint32_t leaf, TypeId target)
{
    uint32_t target_width = 0;
    bool target_signed = false;
    uint32_t source_width = 0;
    bool source_signed = false;
    const bool integers = check_common_integer(types, target, &target_width, &target_signed) &&
                          check_common_integer(types, check_stream_type(stream, leaf), &source_width, &source_signed);
    const bool constant = integers && check_common_integer_constant(stream, leaf);
    TypeId value_type = 0;
    const unsigned __int128 integer = constant ? check_common_stream_integer(stream, leaf, &value_type) : 0;
    uint32_t value_width = 0;
    bool value_signed = false;
    const bool typed = constant && check_common_integer(types, value_type, &value_width, &value_signed);
    const __int128 value = typed ? check_common_integer_bits(integer, value_width, value_signed) : 0;
    const bool negative = value < 0;
    // (Significant bits: a negative value's bits past its sign's copies, and its sign bit.)
    const unsigned __int128 magnitude = negative ? (unsigned __int128)~value : (unsigned __int128)value;
    const uint64_t high = (uint64_t)(magnitude >> 64);
    const uint64_t low = (uint64_t)magnitude;
    const uint32_t active = high != 0 ? 128 - (uint32_t)__builtin_clzll(high) : low != 0 ? 64 - (uint32_t)__builtin_clzll(low) : 0;
    const bool bit_field = !constant && (check_stream_flags(stream, leaf) & BUSTER_CHECK_BIT_FIELD) != 0 && check_stream_value(stream, leaf).bit_width != 0;
    const uint32_t width = constant ? active + (negative ? 1u : 0u) : bit_field ? check_stream_value(stream, leaf).bit_width : source_width;
    const bool same_width = constant && width == target_width && target_signed && !negative && source_signed;
    return integers && width <= target_width && !same_width;
}

// A selection's result past its _Generic or __builtin_choose_expr (CHECK_OP_SELECT): clang's IgnoreParens takes its
// selected arm.
static uint32_t check_common_enum_cast_unselected(const CheckGraph* graph, uint32_t node)
{
    uint32_t result = node;
    for (uint32_t steps = 0; result < graph->count && graph->ops[result] == CHECK_OP_SELECT && steps < graph->count; steps += 1)
    {
        result = graph->operand0[result];
    }
    return result;
}

// Where clang reports a conversion of the leaf `leaf`: its Expr::getExprLoc past its parentheses, a binary operator's (an
// assignment's and a comma's too) and a postfix one's at the operator, a member access at the member's name, any other at
// its first token inside the parentheses around it.
static uint32_t check_common_enum_cast_location(const Parser* parser, const CheckGraph* graph, uint32_t leaf)
{
    // (An assignment's or a comma's operator is the token before its right operand and the parentheses around it.)
    const CheckOp op = graph->ops[leaf];
    const bool operator = (op == CHECK_OP_COMMA || check_common_write_op(op)) && op != CHECK_OP_PRE_INCREMENT && op != CHECK_OP_PRE_DECREMENT &&
                          op != CHECK_OP_POST_INCREMENT && op != CHECK_OP_POST_DECREMENT && graph->operand1[leaf] < graph->count;
    uint32_t right = operator ? check_warning_graph_first(graph, parser, graph->operand1[leaf]) : 0;
    for (uint32_t steps = 0; right != 0 && parser->tokens.kinds[right - 1] == PARSE_KIND_L_PAREN && steps < parser->tokens.count; steps += 1)
    {
        right -= 1;
    }
    const uint32_t token = operator && right != 0 ? right - 1 : graph->tokens[leaf];
    const uint8_t role = token < parser->tokens.count ? parser->tree.roles[token] : PARSE_ROLE_NONE;
    const bool binary = role == PARSE_ROLE_MULTIPLICATIVE || role == PARSE_ROLE_ADDITIVE || role == PARSE_ROLE_SHIFT || role == PARSE_ROLE_RELATIONAL ||
                        role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_BITWISE_AND || role == PARSE_ROLE_BITWISE_XOR || role == PARSE_ROLE_BITWISE_OR ||
                        role == PARSE_ROLE_LOGICAL_AND || role == PARSE_ROLE_LOGICAL_OR || role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_COMMA;
    uint32_t first = check_warning_graph_first(graph, parser, leaf);
    for (uint32_t steps = 0; first < token && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN && parser->tree.roles[first] == PARSE_ROLE_OPEN_GROUP &&
                             parser->tree.parents[first] > token && steps < parser->tokens.count; steps += 1)
    {
        first += 1;
    }
    return operator || binary || role == PARSE_ROLE_POSTFIX ? token : role == PARSE_ROLE_MEMBER_OPERATOR && token + 1 < parser->tokens.count ? token + 1 : first;
}

// Where an enumeration conversion's target is spelled (#1447): its conversion node (`conversion`, BUSTER_CHECK_NONE for
// none), else a call's argument (`call` and `argument`, check_input_call_parameter_spelling), else an initializer's
// subobject (`object`, nonzero, declared at `declaration`, `offset` bits in, check_semantic_subobject_spelling).
typedef struct CheckCommonEnumTarget CheckCommonEnumTarget;
struct CheckCommonEnumTarget
{
    uint32_t conversion;
    uint32_t call;
    uint32_t argument;
    TypeId object;
    uint32_t declaration;
    uint64_t offset;
};

// The target's typedef as written (clang's sugar) for the conversion node `conversion` (BUSTER_CHECK_NONE for none): an
// assignment's left operand's, an initializer's declaration's (its `=` reads the specifiers back) or a return's
// function's; a call argument's parameter's or an initializer subobject's otherwise (CheckCommonEnumTarget). Only a
// warning reads it: the function's search is a scan of the unit's bodies.
static uint32_t check_common_enum_cast_spelling(const Parser* parser, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                                const CheckCommonEnumTarget* place, TypeId target)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t conversion = place->conversion;
    const CheckOp op = conversion < graph->count ? graph->ops[conversion] : CHECK_OP_COUNT;
    const uint32_t token = conversion < graph->count ? graph->tokens[conversion] : BUSTER_CHECK_NONE;
    const bool returned = op == CHECK_OP_CONVERT && token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_RETURN;
    const uint32_t function = returned ? check_semantic_function(parser, input, token) : BUSTER_CHECK_NONE;
    return op == CHECK_OP_ASSIGN ? check_common_operand_spelling(parser, input, graph, stream, types, graph->operand0[conversion]) :
           op == CHECK_OP_INITIALIZE ? check_input_typedef_spelling(input, parser, token) :
           function < input->token_count ? check_input_typedef_spelling(input, parser, function) :
           place->call < graph->count ? check_input_call_parameter_spelling(parser, input, stream, types, place->call, place->argument) :
           place->object != 0 ? check_semantic_subobject_spelling(parser, input, types, place->object, place->declaration, place->offset, target) : 0;
}

// The innermost macro argument occurrence that substituted `token` (clang's immediate macro argument expansion), as its
// expansion's and its parameter's indices plus one: the last argument substitution its origin passes through, which
// runs from the outermost expansion in; 0 for a token no argument substituted (a source token, a macro body's, a
// generated one).
static uint64_t check_common_argument_occurrence(const Parser* parser, Preprocessor* pp, uint32_t token)
{
    const uint32_t source = token < parser->tokens.count ? parser->tokens.sources[token] : BUSTER_PP_NONE;
    const bool macro = source < pp->output.count && pp->output.flags != NULL && (pp->output.flags[source] & BUSTER_PP_FROM_MACRO) != 0;
    uint32_t origin = macro && pp_history_ensure(pp) ? pp_history_at(pp, source) : 0;
    uint64_t result = 0;
    for (uint32_t steps = 0; origin != 0 && steps < 256; steps += 1)
    {
        const uint32_t index = (origin & BUSTER_PP_ORIGIN_INDEX) - 1;
        const uint32_t kind = origin & ~BUSTER_PP_ORIGIN_INDEX;
        const PPSubstitution* substitution = kind == BUSTER_PP_ORIGIN_ARGUMENT ? &pp->substitutions[index] : NULL;
        result = substitution != NULL ? ((uint64_t)substitution->expansion << 32 | substitution->parameter) + 1 : result;
        origin = kind == 0 ? pp->history[index].parent : substitution != NULL ? substitution->source : 0;
    }
    return result;
}

// Clang's conversion between different enumerations (#1447, CheckImplicitConversion's last case): `source`'s implicit
// conversion to `target`, carried to a conditional's arms (its true arm's first, the unselected one's too: no runtime
// pruning) and to a root comma's result, warns at each leaf whose type, an enumerator's taken as its enumeration
// (check_common_enum_coerced), is a named enumeration other than the target's named one. It is a plain Diag, published
// as the full expression completes.
static __attribute__((noinline)) void check_common_enum_cast(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, TypeTable* types,
                                   CheckWarningSpelling* spell, uint32_t source, TypeId target, const CheckCommonEnumTarget* place, bool arm,
                                   const uint32_t* parents, uint32_t at_once, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const TypeId target_enum = type_unqualified(type_strip_aligned(types, target));
    const bool target_named = type_kind(types, target_enum) == BUSTER_TYPE_KIND_ENUM;
    // (The target's names, rendered at its first leaf that converts another enumeration: most leaves are its own.)
    bool rendered = false;
    String8 target_name = { 0 };
    uint32_t work[16];
    uint32_t count = target_named && source < graph->count ? 1u : 0u;
    const uint32_t root = check_common_enum_cast_unselected(graph, source);
    count = root < graph->count ? count : 0;
    work[0] = root;
    while (count != 0 && !pp->fatal)
    {
        const uint32_t node = check_common_enum_cast_unselected(graph, work[--count]);
        const CheckOp op = graph->ops[node];
        if (op == CHECK_OP_CONDITIONAL && count + 2 <= BUSTER_ARRAY_LENGTH(work))
        {
            // (The false arm below the true one, which the stack gives first.)
            work[count] = graph->operand2[node];
            count += graph->operand2[node] < graph->count;
            work[count] = graph->operand1[node];
            count += graph->operand1[node] < graph->count;
            continue;
        }
        // (An arm's comma is its own leaf: only a root's carries the conversion to its result.)
        const uint32_t leaf = op == CHECK_OP_COMMA && node == root && !arm ? check_common_enum_cast_unselected(graph, graph->operand1[node]) : node;
        const TypeId coerced = leaf < graph->count ? check_common_enum_coerced(input, stream, leaf) : 0;
        const TypeId leaf_enum = coerced != 0 ? type_unqualified(type_strip_aligned(types, coerced)) : 0;
        const bool converts = leaf_enum != 0 && type_kind(types, leaf_enum) == BUSTER_TYPE_KIND_ENUM && leaf_enum != target_enum &&
                              check_common_enum_cast_reached(stream, types, leaf, target_enum);
        if (converts && !rendered)
        {
            rendered = true;
            const uint32_t target_spelling = check_common_enum_cast_spelling(parser, input, stream, types, place, target);
            target_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, target, target_spelling);
            const String8 target_plain = check_driver_warning_type(parser, pp, input, types, spell, scratch, target_enum, 0);
            // (An unnamed target warns of nothing.)
            target_name = memmem(target_plain.pointer, target_plain.length, "(unnamed", 8) == NULL ? target_name : (String8){ 0 };
        }
        if (converts && target_name.length != 0)
        {
            const bool enumerator = coerced != check_stream_type(stream, leaf);
            const uint32_t spelling = enumerator ? 0 : check_common_operand_spelling(parser, input, graph, stream, types, leaf);
            const String8 source_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, coerced, spelling);
            const String8 plain = check_driver_warning_type(parser, pp, input, types, spell, scratch, leaf_enum, 0);
            if (source_name.length != 0 && memmem(plain.pointer, plain.length, "(unnamed", 8) == NULL)
            {
                const char* message = check_warning_text(scratch, "implicit conversion from enumeration type %.*s to different enumeration type %.*s",
                                                          (int)source_name.length, source_name.pointer, (int)target_name.length, target_name.pointer);
                const uint32_t publication = at_once != BUSTER_CHECK_NONE ? at_once : check_common_full_expression_end(parser, graph, parents, leaf);
                const uint32_t location = check_common_enum_cast_location(parser, graph, leaf);
                // Its ranges are the leaf's and its context's (DiagnoseImpCast's CContext: the assignment's operator, the
                // declarator, the return, the call, a brace initializer's declaration), so a leaf a macro's argument writes
                // shows that argument's macro unless the context is written in the same argument too
                // (rangesInsideSameMacroArgExpansion).
                const uint32_t context = place->conversion < graph->count ? graph->tokens[place->conversion] :
                                         place->call < graph->count ? graph->tokens[place->call] : place->declaration;
                const uint64_t occurrence = check_common_argument_occurrence(parser, pp, location);
                pp->diagnostic_spans = occurrence != 0 && (context >= parser->tokens.count || check_common_argument_occurrence(parser, pp, context) != occurrence);
                check_common_report_evaluated(parser, pp, input, location, publication, PP_DIAGNOSTIC_DEFERRED,
                                              CHECK_WARNING_ID_IMPLICIT_ENUM_ENUM_CAST, false, message, NULL, 0, scratch);
                pp->diagnostic_spans = false;
            }
        }
    }
}

// Whether the enumeration definition `definition` holds an attribute named `name` (#2264): among the tokens between its
// `enum` keyword and its `{`, or in the GNU attributes right after its `}`, where clang applies an enumeration's
// attributes. With `argument`, only one whose first argument is that identifier (`enum_extensibility(open)`). Only
// -Wassign-enum's warnings ask, so its cost is theirs.
static bool check_common_enum_attribute(const Parser* parser, Preprocessor* pp, const CheckInputEnumDefinition* definition, const char* name,
                                        const char* argument)
{
    const uint32_t count = parser->tokens.count;
    uint32_t first = definition->open;
    while (first != 0 && parser->tokens.kinds[first - 1] != PARSE_KIND_ENUM && definition->open - first < 256)
    {
        first -= 1;
    }
    uint32_t last = definition->close + 1;
    // (The GNU attributes after its `}`: `__attribute__` and its parenthesized list, each.)
    while (last + 1 < count && parser->tokens.kinds[last] == PARSE_KIND_ATTRIBUTE && parser->tokens.kinds[last + 1] == PARSE_KIND_L_PAREN &&
           parser->tree.parents[last + 1] > last + 1 && parser->tree.parents[last + 1] < count)
    {
        last = parser->tree.parents[last + 1] + 1;
    }
    const uint64_t length = strlen(name);
    bool found = false;
    for (uint32_t pass = 0; !found && pass < 2; pass += 1)
    {
        const uint32_t begin = pass == 0 ? first : definition->close + 1;
        const uint32_t end = pass == 0 ? definition->open : last;
        for (uint32_t token = begin; !found && token < end && token < count; token += 1)
        {
            const String8 word = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ? check_semantic_name(parser, pp, token) : (String8){ 0 };
            // (A GNU spelling's `__name__` too.)
            const bool plain = word.length == length && memcmp(word.pointer, name, length) == 0;
            const bool wrapped = word.length == length + 4 && memcmp(word.pointer, "__", 2) == 0 && memcmp(word.pointer + 2, name, length) == 0 &&
                                 memcmp(word.pointer + 2 + length, "__", 2) == 0;
            const String8 value = (plain || wrapped) && argument != NULL && token + 2 < count && parser->tokens.kinds[token + 1] == PARSE_KIND_L_PAREN ?
                                  check_semantic_name(parser, pp, token + 2) : (String8){ 0 };
            found = (plain || wrapped) && (argument == NULL || (value.length == strlen(argument) && memcmp(value.pointer, argument, value.length) == 0));
        }
    }
    return found;
}

// -Wassign-enum (#2264, clang's DiagnoseAssignmentEnum): `source`, an integer constant expression of another type than the
// enumeration `target`, converted to it, whose value, in the enumeration's width and signedness, is none of its
// enumerators' (a flag_enum's: has a bit, and its complement too, outside its single-bit enumerators'), "integer constant
// not in range of enumerated type 'enum A'" at the source's expression location, the target as written without its
// qualifiers, given as the conversion is built. An enum_extensibility(open) one gives none.
static void check_common_assign_enum_at(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, TypeTable* types,
                                        CheckWarningSpelling* spell, uint32_t source, TypeId target, const CheckCommonEnumTarget* place, uint32_t publication,
                                        Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const TypeId enumeration = target != 0 ? type_unqualified(type_strip_aligned(types, target)) : 0;
    const bool candidate = enumeration != 0 && type_kind(types, enumeration) == BUSTER_TYPE_KIND_ENUM && source < graph->count && source < stream->count &&
                           stream->diagnostics[source] == CHECK_DIAG_NONE && (check_stream_flags(stream, source) & BUSTER_CHECK_ICE) != 0 &&
                           check_stream_constant_kind(stream, source) == CHECK_CONSTANT_INTEGER;
    const TypeId source_type = candidate ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, source))) : 0;
    const uint8_t source_kind = source_type != 0 ? type_kind(types, source_type) : BUSTER_TYPE_KIND_VOID;
    uint32_t width = 0;
    bool is_signed = false;
    uint32_t source_width = 0;
    bool source_signed = false;
    const bool integers = candidate && source_type != enumeration && source_kind != BUSTER_TYPE_KIND_POINTER &&
                          check_common_integer(types, enumeration, &width, &is_signed) && width != 0 && width <= 128 &&
                          check_common_integer(types, source_type, &source_width, &source_signed) && source_width != 0 && source_width <= 128;
    // Its definition: the one of the enumeration's type.
    const CheckInputEnumDefinition* definition = NULL;
    for (uint32_t i = 0; integers && definition == NULL && i < input->enum_definition_count; i += 1)
    {
        definition = type_unqualified(input->enum_definitions[i].type) == enumeration ? input->enum_definitions + i : NULL;
    }
    if (definition != NULL && !check_common_enum_attribute(parser, pp, definition, "enum_extensibility", "open"))
    {
        // (AdjustAPSInt: the value extended by its own signedness, or truncated, to the enumeration's width.)
        TypeId value_type = 0;
        const unsigned __int128 mask = check_action_mask(width);
        const unsigned __int128 value = (unsigned __int128)check_common_integer_bits(check_common_stream_integer(stream, source, &value_type), source_width,
                                                                                     source_signed) & mask;
        const bool flag = check_common_enum_attribute(parser, pp, definition, "flag_enum", NULL);
        bool found = false;
        unsigned __int128 flag_bits = 0;
        for (uint32_t i = 0; !found && i < input->enum_value_count; i += 1)
        {
            const uint32_t token = input->enum_values[i].token;
            // (An enumerator of a definition nested in this one's values is that one's.)
            bool nested = false;
            for (uint32_t d = 0; !nested && token > definition->open && token < definition->close && d < input->enum_definition_count; d += 1)
            {
                const CheckInputEnumDefinition* inner = input->enum_definitions + d;
                nested = inner != definition && inner->open > definition->open && inner->open < token && token < inner->close;
            }
            const unsigned __int128 enumerator = input->enum_values[i].value & mask;
            const bool own = token > definition->open && token < definition->close && !nested;
            flag_bits |= own && enumerator != 0 && (enumerator & (enumerator - 1)) == 0 ? enumerator : 0;
            found = own && !flag && enumerator == value;
        }
        found = found || (flag && ((value & ~flag_bits) == 0 || (~value & mask & ~flag_bits) == 0));
        const uint32_t location = check_common_enum_cast_location(parser, graph, source);
        if (!found && location < parser->tokens.count)
        {
            const uint64_t mark = scratch->length;
            const uint32_t spelling = check_common_enum_cast_spelling(parser, input, stream, types, place, target);
            const String8 name = check_driver_warning_type(parser, pp, input, types, spell, scratch, type_unqualified(target), spelling);
            const char* message = name.length != 0 ? check_warning_text(scratch, "integer constant not in range of enumerated type %.*s", (int)name.length, name.pointer) :
                                  NULL;
            if (message != NULL)
            {
                check_common_report_at(parser, pp, input, location, publication, CHECK_WARNING_ID_ASSIGN_ENUM, true, message, NULL, 0, scratch);
            }
            arena_pop_to(scratch, mark);
        }
    }
}

// The token past `node`'s subtree: the conversion of a source is built once the source is.
static uint32_t check_common_assign_enum_end(const Parser* parser, const CheckGraph* graph, uint32_t node)
{
    const uint32_t first = check_warning_graph_first(graph, parser, node);
    const uint32_t token = node < graph->count ? graph->tokens[node] : BUSTER_CHECK_NONE;
    uint32_t end = token < parser->tokens.count && token >= first ? token + 1 : parser->tokens.count;
    // (Past the tokens whose parents climb into the source's first token's subtree: a binary operator's right operand.)
    for (bool inside = true; inside && end < parser->tokens.count;)
    {
        uint32_t at = end;
        for (uint32_t steps = 0; at < parser->tokens.count && at != token && steps < parser->tokens.count; steps += 1)
        {
            const uint32_t up = parser->tree.parents[at];
            at = up != at ? up : parser->tokens.count;
        }
        inside = at == token;
        end += inside;
    }
    return end;
}

// -Wassign-enum's conversions (#2264): assignments' and compound assignments' right operands, initializers' values,
// returns', prototype arguments' and brace initializers' values, where the target is an enumeration. Off by default; a
// unit where nothing enables it, or that defines no enumeration, skips the scan.
static void check_common_assign_enum(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, TypeTable* types,
                                     CheckWarningSpelling* spell, const CheckProgram* program, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const bool enabled = types != NULL && input->enum_definition_count != 0 && check_common_enabled(pp, input, CHECK_WARNING_ID_ASSIGN_ENUM, true);
    const uint32_t count = enabled ? (graph->count < stream->count ? graph->count : stream->count) : 0;
    for (uint32_t node = 0; node < count && !pp->fatal; node += 1)
    {
        const CheckOp op = graph->ops[node];
        const uint32_t token = graph->tokens[node];
        const bool assign = op == CHECK_OP_ASSIGN || (check_common_write_op(op) && op != CHECK_OP_PRE_INCREMENT && op != CHECK_OP_PRE_DECREMENT &&
                                                      op != CHECK_OP_POST_INCREMENT && op != CHECK_OP_POST_DECREMENT);
        const bool returned = op == CHECK_OP_CONVERT && token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_RETURN;
        if (stream->diagnostics[node] != CHECK_DIAG_NONE)
        {
            continue;
        }
        if (assign && graph->operand0[node] < count)
        {
            const CheckCommonEnumTarget place = { .conversion = op == CHECK_OP_ASSIGN ? node : BUSTER_CHECK_NONE, .call = BUSTER_CHECK_NONE };
            check_common_assign_enum_at(parser, pp, input, stream, types, spell, graph->operand1[node], check_stream_type(stream, graph->operand0[node]), &place,
                                        check_common_assign_enum_end(parser, graph, node), scratch);
        }
        else if (op == CHECK_OP_INITIALIZE || returned)
        {
            const CheckCommonEnumTarget place = { .conversion = node, .call = BUSTER_CHECK_NONE };
            check_common_assign_enum_at(parser, pp, input, stream, types, spell, graph->operand0[node], check_stream_type(stream, node), &place,
                                        check_common_assign_enum_end(parser, graph, graph->operand0[node]), scratch);
        }
        else if (op == CHECK_OP_CALL && stream->argument_conversions != NULL)
        {
            const uint32_t first = graph->argument_offsets[node];
            for (uint32_t i = 0; i < graph->argument_counts[node] && first + i < graph->argument_count && !pp->fatal; i += 1)
            {
                const CheckConversion conversion = stream->argument_conversions[first + i];
                const CheckCommonEnumTarget place = { .conversion = BUSTER_CHECK_NONE, .call = node, .argument = i };
                if ((conversion.flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (conversion.flags & BUSTER_CHECK_CONVERT_CAST) == 0)
                {
                    const uint32_t argument = graph->argument_nodes[first + i];
                    check_common_assign_enum_at(parser, pp, input, stream, types, spell, argument, conversion.type, &place,
                                                check_common_assign_enum_end(parser, graph, argument), scratch);
                }
            }
        }
    }
    const CheckInitializers* initializers = enabled && program != NULL ? program->initializers : NULL;
    for (uint32_t index = 0; initializers != NULL && index < initializers->count && !pp->fatal; index += 1)
    {
        const CheckInitializer* plan = initializers->plans + index;
        const uint32_t declaration = initializers->declaration_tokens[index];
        for (uint32_t p = 0; p < plan->count && !pp->fatal; p += 1)
        {
            const CheckInitPart* part = plan->parts + p;
            // (A declaration's unbraced scalar enumeration has its INITIALIZE node; a braced one's and a compound literal's
            // have none.)
            const uint8_t role = declaration < parser->tokens.count ? parser->tree.roles[declaration] : PARSE_ROLE_NONE;
            const bool compound = role == PARSE_ROLE_OPEN_COMPOUND || role == PARSE_ROLE_CLOSE_COMPOUND || role == PARSE_ROLE_OPEN_COMPOUND_BODY;
            const uint32_t begin = check_sparse_get(input->initializer_begins, declaration);
            const bool brace = begin < parser->tokens.count && parser->tokens.kinds[begin] == PARSE_KIND_L_BRACE;
            const bool braced = part->kind == CHECK_INIT_VALUE && part->node < count &&
                                (compound || brace || type_kind(types, type_strip_aligned(types, plan->type)) != BUSTER_TYPE_KIND_ENUM);
            if (braced)
            {
                const CheckCommonEnumTarget place = { .conversion = BUSTER_CHECK_NONE, .call = BUSTER_CHECK_NONE, .object = plan->type, .declaration = declaration,
                                                      .offset = part->bit_offset };
                check_common_assign_enum_at(parser, pp, input, stream, types, spell, part->node, part->conversion.type, &place,
                                            check_common_assign_enum_end(parser, graph, part->node), scratch);
            }
        }
    }
}

// One argument conversion, interleaved with its own subtree. K&R parameters receive their promoted types;
// only a prototype converts directly to the declared parameter type.
static uint32_t check_common_enum_cast_arms(const CheckCommonEmission* emission, uint32_t owner);
static bool check_common_constant_arms(const CheckCommonEmission* emission, uint32_t owner);

static void check_common_call_argument(CheckCommonEmission* emission, uint32_t index, bool prototype)
{
    const CheckGraph* graph = &emission->input->graph;
    const CheckStream* stream = emission->stream;
    if (index < graph->argument_count && stream->argument_conversions != NULL)
    {
        const uint32_t argument = graph->argument_nodes[index];
        const CheckConversion conversion = stream->argument_conversions[index];
        const bool arms = check_common_conversion_arms(graph, argument);
        // (A conditional argument's arms report in their own frames, as an assignment's do: check_common_enum_cast_arms.)
        const uint32_t owner = BUSTER_CHECK_ARM_ARGUMENT | index | (prototype ? BUSTER_CHECK_ARM_PROTOTYPE : 0u);
        const bool conditional = argument < graph->count && graph->ops[check_common_enum_cast_unselected(graph, argument)] == CHECK_OP_CONDITIONAL;
        if (argument < graph->count && (conversion.flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (conversion.flags & BUSTER_CHECK_CONVERT_CAST) == 0 &&
            (conversion.flags & BUSTER_CHECK_CONVERT_ATOMIC) == 0 &&
            !check_common_conversion_source(check_stream_constant_kind(stream, argument), arms) && check_common_bool_candidate(graph, argument) &&
            (check_common_emit_evaluated(emission, argument) || (emission->emitted[argument] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0) &&
            check_common_bool_target(emission->types, prototype ? conversion.type : type_default_argument_promote(emission->types, conversion.type)))
        {
            // A `<<` passed to a _Bool parameter (#1447, check_common_int_in_bool).
            check_common_int_in_bool(emission->parser, emission->pp, emission->input, stream, emission->types, argument, emission->parents,
                                     emission->at_once, emission->scratch);
        }
        if (argument < graph->count && prototype && (conversion.flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 &&
            (conversion.flags & (BUSTER_CHECK_CONVERT_CAST | BUSTER_CHECK_CONVERT_ATOMIC)) == 0 &&
            check_common_enum_cast_candidate(emission->input->enum_names, emission->parser, graph, stream, emission->types, argument, conversion.type) &&
            !conditional)
        {
            // (Its parameter's typedef, #1447: the argument's parent is its CALL node.)
            const uint32_t parent = emission->parents != NULL ? emission->parents[argument] : BUSTER_CHECK_NONE;
            const uint32_t call = parent < graph->count && graph->ops[parent] == CHECK_OP_CALL && index >= graph->argument_offsets[parent] ? parent : BUSTER_CHECK_NONE;
            const CheckCommonEnumTarget place = { .conversion = BUSTER_CHECK_NONE, .call = call,
                                                  .argument = call != BUSTER_CHECK_NONE ? index - graph->argument_offsets[call] : 0 };
            check_common_enum_cast(emission->parser, emission->pp, emission->input, stream, emission->types, emission->spell, argument, conversion.type, &place,
                                   false, emission->parents, emission->at_once, emission->scratch);
        }
        if (argument < graph->count && (conversion.flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && !check_common_query_suppressed(graph, argument) &&
            (conversion.flags & (BUSTER_CHECK_CONVERT_CAST | BUSTER_CHECK_CONVERT_ATOMIC)) == 0 &&
            check_common_conversion_source(check_stream_constant_kind(stream, argument), arms) && !(conditional && check_common_constant_arms(emission, owner)) &&
            (check_common_emit_evaluated(emission, argument) || ((emission->emitted[argument] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0 &&
             (check_stream_constant_kind(stream, argument) == CHECK_CONSTANT_FLOATING || arms))))
        {
            const TypeId destination = prototype ? conversion.type : type_default_argument_promote(emission->types, conversion.type);
            const CheckValue value = check_stream_value(stream, argument);
            check_common_conversion_warning(emission->parser, emission->pp, emission->input, stream, emission->types, emission->spell,
                                            argument, &value, destination, check_common_emit_evaluated(emission, argument), emission->parents,
                                            emission->at_once, emission->scratch);
        }
    }
}

// An implicit scalar conversion precedes the source expression's nested conversions. Calls carry their
// conversions on argument edges instead and schedule them immediately before each argument subtree.
static bool check_common_bitfield_assignment(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node,
                                             uint32_t* bit_width, __int128* original, __int128* truncated);
static void check_common_bitfield_warning(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                          TypeTable* types, CheckWarningSpelling* spell, uint32_t source, uint32_t bit_width,
                                          __int128 original, __int128 truncated, uint32_t token, uint32_t publication, bool initialization,
                                          Arena* scratch);
static uint32_t check_common_full_expression_end(const Parser* parser, const CheckGraph* graph, const uint32_t* parents, uint32_t node);

// Whether `owner` is an argument slot's, and its slot's index.
static bool check_common_arm_argument(uint32_t owner)
{
    return (owner & BUSTER_CHECK_ARM_ARGUMENT) != 0;
}

static uint32_t check_common_arm_slot(uint32_t owner)
{
    return owner & ~(BUSTER_CHECK_ARM_ARGUMENT | BUSTER_CHECK_ARM_PROTOTYPE);
}

// Whether `owner` names a conversion node or an argument slot of the graph.
static bool check_common_arm_owner(const CheckCommonEmission* emission, uint32_t owner)
{
    const CheckGraph* graph = &emission->input->graph;
    return check_common_arm_argument(owner) ? check_common_arm_slot(owner) < graph->argument_count && emission->stream->argument_conversions != NULL :
                                              owner < graph->count;
}

// The conversion's source, its flags and its target: a node's (an assignment's right operand), or an argument's, whose
// target is its parameter's or, without a prototype, its promoted type.
static uint32_t check_common_arm_source(const CheckCommonEmission* emission, uint32_t owner, uint32_t* flags, TypeId* target)
{
    const CheckGraph* graph = &emission->input->graph;
    const CheckStream* stream = emission->stream;
    uint32_t source = BUSTER_CHECK_NONE;
    *flags = 0;
    *target = 0;
    if (check_common_arm_argument(owner))
    {
        const uint32_t slot = check_common_arm_slot(owner);
        const CheckConversion conversion = stream->argument_conversions[slot];
        source = graph->argument_nodes[slot];
        *flags = conversion.flags;
        *target = (owner & BUSTER_CHECK_ARM_PROTOTYPE) != 0 ? conversion.type : type_default_argument_promote(emission->types, conversion.type);
    }
    else
    {
        const CheckOp op = graph->ops[owner];
        const bool conversion = op == CHECK_OP_INITIALIZE || op == CHECK_OP_ASSIGN || op == CHECK_OP_CONVERT;
        source = conversion ? (op == CHECK_OP_ASSIGN ? graph->operand1[owner] : graph->operand0[owner]) : BUSTER_CHECK_NONE;
        *flags = conversion ? stream->conversions[owner].operands[op == CHECK_OP_ASSIGN ? 1u : 0u].flags : 0;
        *target = check_stream_type(stream, owner);
    }
    return source;
}

// The source of the conversion `owner` whose enumeration conversions its arms' frames report (#1895), else BUSTER_CHECK_NONE:
// an initialization's, an assignment's, a conversion's or a prototype's argument's that may change an enumeration
// (check_common_enum_cast_candidate), whose source is a conditional past its selections. Clang's CheckConditionalOperand
// checks each arm's own conversions before the arm's: the walk's BUSTER_CHECK_EMIT_ARM frames replay that, the
// condition's first and the true arm's before the false one's.
static uint32_t check_common_enum_cast_arms(const CheckCommonEmission* emission, uint32_t owner)
{
    const CheckGraph* graph = &emission->input->graph;
    uint32_t flags = 0;
    TypeId target = 0;
    const uint32_t source = check_common_arm_source(emission, owner, &flags, &target);
    const bool typed = !check_common_arm_argument(owner) || (owner & BUSTER_CHECK_ARM_PROTOTYPE) != 0;
    const bool candidate = typed && source < graph->count && (flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 &&
                           (flags & (BUSTER_CHECK_CONVERT_CAST | BUSTER_CHECK_CONVERT_ATOMIC)) == 0 &&
                           graph->ops[check_common_enum_cast_unselected(graph, source)] == CHECK_OP_CONDITIONAL &&
                           check_common_enum_cast_candidate(emission->input->enum_names, emission->parser, graph, emission->stream, emission->types, source, target);
    return candidate ? source : BUSTER_CHECK_NONE;
}

// Whether the conversion `owner`'s constant conversions are its arms' frames' (#1895): an initialization's, an assignment's
// (not a bit-field's, which check_common_bitfield_assignment reports), a conversion's or an argument's to an integer type,
// whose source is a conditional, as check_common_conversion_own's and check_common_call_argument's walk would carry it
// (check_common_conversion_warning_at). The conversion is evaluated where its node is, an argument's where the argument is.
static bool check_common_constant_arms(const CheckCommonEmission* emission, uint32_t owner)
{
    const CheckGraph* graph = &emission->input->graph;
    const CheckStream* stream = emission->stream;
    uint32_t flags = 0;
    TypeId target = 0;
    const uint32_t source = check_common_arm_source(emission, owner, &flags, &target);
    const bool argument = check_common_arm_argument(owner);
    const uint32_t evaluated_at = argument ? source : owner;
    const bool conditional = source < graph->count && graph->ops[source] == CHECK_OP_CONDITIONAL;
    const bool bitfield = !argument && conditional && graph->ops[owner] == CHECK_OP_ASSIGN && graph->operand0[owner] < graph->count &&
                          (check_stream_flags(stream, graph->operand0[owner]) & BUSTER_CHECK_BIT_FIELD) != 0;
    uint32_t width = 0;
    bool is_signed = false;
    return conditional && !bitfield && (flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (flags & (BUSTER_CHECK_CONVERT_CAST | BUSTER_CHECK_CONVERT_ATOMIC)) == 0 &&
           (!argument || !check_common_query_suppressed(graph, source)) &&
           (check_common_emit_evaluated(emission, evaluated_at) || (emission->emitted[evaluated_at] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0) &&
           check_common_integer(emission->types, target, &width, &is_signed);
}

// An arm frame's push, owned by the conversion `owner` (BUSTER_CHECK_EMIT_ARM, BUSTER_CHECK_EMIT_ARM_LEAF).
static void check_common_emit_arm(CheckCommonEmission* emission, uint32_t node, uint8_t kind, uint32_t owner)
{
    const uint32_t at = emission->work_count;
    check_common_emit_push(emission, node, kind);
    if (emission->work_count > at)
    {
        emission->work_owners[at] = owner;
    }
}

static void check_common_conversion_own(CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const CheckStream* stream = emission->stream;
    const CheckOp op = graph->ops[node];
    const bool evaluated = check_common_emit_evaluated(emission, node);
    if (op == CHECK_OP_INITIALIZE || op == CHECK_OP_ASSIGN || op == CHECK_OP_CONVERT)
    {
        const uint32_t source = op == CHECK_OP_ASSIGN ? graph->operand1[node] : graph->operand0[node];
        const uint32_t conversion = op == CHECK_OP_ASSIGN ? 1u : 0u;
        const uint32_t flags = stream->conversions[node].operands[conversion].flags;
        const bool arms = check_common_conversion_arms(graph, source);
        const bool assigned = source < graph->count && (flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (flags & BUSTER_CHECK_CONVERT_CAST) == 0;
        if (assigned && !check_common_conversion_source(check_stream_constant_kind(stream, source), arms) && check_common_bool_candidate(graph, source) &&
            (evaluated || (emission->emitted[node] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0) &&
            (flags & BUSTER_CHECK_CONVERT_ATOMIC) == 0 && check_common_bool_target(emission->types, check_stream_type(stream, node)))
        {
            // A `<<` converted to _Bool (#1447): DiagnoseIntInBoolContext's alone; a constant one's is its leaf's (check_common_conversion_warning).
            check_common_int_in_bool(emission->parser, emission->pp, emission->input, stream, emission->types, source, emission->parents,
                                     emission->at_once, emission->scratch);
        }
        // A constant a bit-field's assignment truncates (#1447): clang's AnalyzeAssignment warns of it alone, at the `=`, as the
        // full expression completes, in unreachable code too, and leaves out the value's own conversion.
        uint32_t bit_width = 0;
        __int128 original = 0;
        __int128 truncated = 0;
        const bool bitfield = op == CHECK_OP_ASSIGN && (evaluated || (emission->emitted[node] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0) &&
                              check_common_bitfield_assignment(graph, stream, emission->types, node, &bit_width, &original, &truncated);
        if (bitfield)
        {
            const uint32_t publication = emission->at_once != BUSTER_CHECK_NONE ? emission->at_once :
                                         check_common_full_expression_end(emission->parser, graph, emission->parents, node);
            check_common_bitfield_warning(emission->parser, emission->pp, emission->input, stream, emission->types, emission->spell, source, bit_width,
                                          original, truncated, graph->tokens[node], publication, false, emission->scratch);
        }
        // (A conditional source's arms report in their own frames: check_common_enum_cast_arms.)
        if (source < graph->count && (flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (flags & (BUSTER_CHECK_CONVERT_CAST | BUSTER_CHECK_CONVERT_ATOMIC)) == 0 &&
            check_common_enum_cast_candidate(emission->input->enum_names, emission->parser, graph, stream, emission->types, source, check_stream_type(stream, node)) &&
            graph->ops[check_common_enum_cast_unselected(graph, source)] != CHECK_OP_CONDITIONAL)
        {
            const CheckCommonEnumTarget place = { .conversion = node, .call = BUSTER_CHECK_NONE };
            check_common_enum_cast(emission->parser, emission->pp, emission->input, stream, emission->types, emission->spell, source,
                                   check_stream_type(stream, node), &place, false, emission->parents, emission->at_once, emission->scratch);
        }
        // (A conditional source's arms report in their own frames: check_common_constant_arms.)
        if (!bitfield && source < graph->count && (flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (flags & BUSTER_CHECK_CONVERT_CAST) == 0 &&
            check_common_conversion_source(check_stream_constant_kind(stream, source), arms) && !(arms && check_common_constant_arms(emission, node)) &&
            (flags & BUSTER_CHECK_CONVERT_ATOMIC) == 0 &&
            (evaluated || ((emission->emitted[node] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0 &&
             (check_stream_constant_kind(stream, source) == CHECK_CONSTANT_FLOATING || arms))))
        {
            const CheckValue value = check_stream_value(stream, source);
            const TypeId target = check_stream_type(stream, node);
            check_common_conversion_warning(emission->parser, emission->pp, emission->input, stream, emission->types, emission->spell,
                                            source, &value, target, evaluated, emission->parents, emission->at_once, emission->scratch);
        }
    }
    else if (evaluated || (emission->emitted[node] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0)
    {
        // A truth context's floating literal, converted to _Bool: its operands' in order, the leaf's warning alone.
        uint32_t operands[3];
        const uint32_t count = check_common_truth_operands(graph, node, operands);
        for (uint32_t i = 0; i < count && !emission->pp->fatal; i += 1)
        {
            const uint32_t source = operands[i];
            if (source < graph->count && check_stream_constant_kind(stream, source) == CHECK_CONSTANT_FLOATING)
            {
                const CheckValue value = check_stream_value(stream, source);
                check_common_conversion_leaf(emission->parser, emission->pp, emission->input, stream, emission->types, emission->spell, source, &value,
                                             BUSTER_TYPE_BOOL, emission->parents, emission->at_once, BUSTER_CHECK_LEAVES_ALL, emission->scratch);
            }
            else if (check_common_bool_candidate(graph, source))
            {
                // CheckBoolLikeConversion's DiagnoseIntInBoolContext (#1447): the operand itself, never a conditional's arms.
                check_common_int_in_bool(emission->parser, emission->pp, emission->input, stream, emission->types, source, emission->parents,
                                         emission->at_once, emission->scratch);
            }
        }
    }
}

// Whether `node` is a plain `x = y`, a -Wself-assign candidate (check_common_self_assigns).
static bool check_common_self_assign_candidate(const CheckGraph* graph, uint32_t node)
{
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    return graph->ops[node] == CHECK_OP_ASSIGN && left < graph->count && right < graph->count && graph->ops[left] == CHECK_OP_VALUE &&
           graph->ops[right] == CHECK_OP_VALUE;
}

// -Wself-assign: clang's DiagnoseSelfAssignment is a plain Diag where Sema builds the assignment, not a runtime-behavior
// one, so it warns at the operator as the assignment completes, in unreachable code and a constant condition's or a
// selection's other arm too, and only an unevaluated operand (sizeof's) drops it (check_common_report_evaluated). A variable's
// own name alone: member, subscript and indirection lvalues never warn, whatever object they reach. The candidates are the
// facts' (CheckCommonFacts.self_assigns), in node order.
static void check_common_self_assigns(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, TypeTable* types,
                                      CheckWarningSpelling* spell, const CheckCommonFacts* facts, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    for (uint32_t word = 0; facts->self_assigns != NULL && !pp->fatal && word < (graph->count + 63) / 64; word += 1)
    {
        for (uint64_t bits = facts->self_assigns[word]; bits != 0 && !pp->fatal; bits &= bits - 1)
        {
            const uint32_t node = word * 64 + (uint32_t)__builtin_ctzll(bits);
            const uint32_t token = graph->tokens[node];
            const CheckValue left_value = check_stream_value(stream, graph->operand0[node]);
            const CheckValue right_value = check_stream_value(stream, graph->operand1[node]);
            const bool volatile_object = (type_qualifiers(type_strip_aligned(types, left_value.type)) & BUSTER_TYPE_VOLATILE) != 0;
            if (!volatile_object && left_value.category == CHECK_LVALUE && right_value.category == CHECK_LVALUE && left_value.object != 0 &&
                left_value.object == right_value.object && left_value.offset == 0 && right_value.offset == 0 && !check_common_from_macro(parser, pp, token) &&
                !check_common_from_macro(parser, pp, graph->tokens[graph->operand0[node]]) &&
                !check_common_from_macro(parser, pp, graph->tokens[graph->operand1[node]]))
            {
                // (No mark around it: the type's spelling and the unevaluated contexts' memo, which these build lazily, are the
                // passes', which check_driver_common_warnings gives back after them.)
                const String8 type_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, left_value.type, 0);
                if (type_name.length != 0)
                {
                    const char* message = check_warning_text(scratch, "explicitly assigning value of variable of type %.*s to itself",
                                                              (int)type_name.length, type_name.pointer);
                    check_common_report_evaluated(parser, pp, input, token, token, PP_DIAGNOSTIC_SEMANTIC, CHECK_WARNING_ID_SELF_ASSIGN, true, message,
                                                  NULL, 0, scratch);
                }
            }
        }
    }
}

// A runtime warning at a node's own root: a constant subscript past its bound.
// A conditional has no warning of its own — its arms report at its slot instead.
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
    const bool evaluated = check_common_emit_evaluated(emission, node);
    if (op == CHECK_OP_SUBSCRIPT && evaluated)
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
                            check_common_report_bounds(parser, pp, input, graph->tokens[root], message, note.message != NULL ? &note : NULL,
                                                       note.message != NULL ? 1u : 0u, scratch);
                        }
                    }
                }
            }
        }
    }
}

#include "c_check_ignored_result.c"

// How many pending operands check_common_unused_of holds at once (a conditional's else arm, a logical operator's right
// operand and its own mark): a deeper nest is quiet.
#define BUSTER_CHECK_UNUSED_DEPTH 64u
// A pending operand's kind: inside a logical operator's operand, or the logical operator itself, whose operands both warn.
#define BUSTER_CHECK_UNUSED_LOGICAL 0x01u
#define BUSTER_CHECK_UNUSED_MARK 0x02u

// What a discarded value warns of (check_common_unused_of): the node clang reports, or none where the value is quiet, and
// whether that node is a call whose result warns (check_common_ignored_of), with what it warns of.
typedef struct CheckCommonUnused CheckCommonUnused;
struct CheckCommonUnused
{
    uint32_t node;
    bool call;
    CheckCommonIgnoredResult found;
};

// Whether `node` reads a volatile object (not an array's): clang's lvalue conversion of a volatile glvalue, a side effect
// a discarded value keeps.
static bool check_common_volatile_read(const CheckStream* stream, const TypeTable* types, uint32_t node)
{
    const TypeId type = node < stream->count ? type_strip_aligned(types, check_stream_type(stream, node)) : 0;
    const bool result = type != 0 && (type_qualifiers(type) & BUSTER_TYPE_VOLATILE) != 0 && check_stream_category(stream, node) == CHECK_LVALUE &&
                        !type_kind_is_array(type_kind(types, type));
    return result;
}

// Whether a discarded `node`'s value is of type void where clang's isUnusedResultAWarning stays quiet: a void value is
// no unused result (an extern void object's, #1447), but a dereference's or a subscript's warns all the same — clang
// warns of those unconditionally, and only they can yield void. The statement marker and this walk's leaf share it.
static bool check_common_void_quiet(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node)
{
    const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    const TypeId type = op != CHECK_OP_DEREFERENCE && op != CHECK_OP_SUBSCRIPT && node < stream->count ?
                        type_strip_aligned(types, check_stream_type(stream, node)) : 0;
    const bool result = type != 0 && type_kind(types, type) == BUSTER_TYPE_KIND_VOID;
    return result;
}

// Whether a call in a logical operator's operand, whose result this unit's marks don't say warns, keeps the operator's
// warning all the same (#1390): where a typedef of the unit has a result contract, which a return type spelled through
// typeof may carry past the declarator's specifiers, or where the callee is overloaded, whose candidate's contracts the
// name's declaration needn't be.
static bool check_common_unused_unknown(const CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    uint32_t callee = graph->operand0[node];
    for (uint32_t steps = 0; callee < graph->count && steps < 64 &&
                             (graph->ops[callee] == CHECK_OP_SELECT || graph->ops[callee] == CHECK_OP_ADDRESS || graph->ops[callee] == CHECK_OP_DEREFERENCE ||
                              graph->ops[callee] == CHECK_OP_CONVERT); steps += 1)
    {
        callee = graph->operand0[callee];
    }
    const bool result = emission->input->typedef_result_contracts || (callee < graph->count && check_stream_overload_head(emission->stream, callee) != 0);
    return result;
}

// Whether the discarded value of `node` warns, as clang's Expr::isUnusedResultAWarning decides (#1447, #1390), and where:
// a comma's is its right operand's, but for a literal 0, the `(x = y), 0` idiom, which is quiet; a conditional's warns
// where both arms' do (a GNU `x ?: y`'s where its else arm's does), and is reported at the else arm's, which clang
// evaluates last; a logical operator's warns where both operands' do, and is reported at the operator; a selection's is
// its chosen operand's, a statement expression's its last value's (none is quiet), a conversion's its operand's. Any
// other value warns where it is a call whose result warns, or where it has no effect, is no cast to void, is no void
// value but a dereference's or a subscript's, and reads no volatile object; in a logical operator's operand a call keeps
// the warning in the units check_common_unused_unknown says. The
// walk goes into the left operands first and holds the right ones, and each logical operator's mark after its operands,
// on a bounded stack; it runs only for a discarded statement's value, a comma's left operand and a discarded comma's, so
// that the graph's facts needn't hold an answer for every operator.
static __attribute__((noinline)) CheckCommonUnused check_common_unused_of(CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const CheckStream* stream = emission->stream;
    const TypeTable* types = emission->types;
    uint32_t pending[BUSTER_CHECK_UNUSED_DEPTH];
    uint8_t pending_kinds[BUSTER_CHECK_UNUSED_DEPTH];
    uint32_t depth = 0;
    CheckCommonUnused result = { .node = BUSTER_CHECK_NONE };
    uint32_t leaf = BUSTER_CHECK_NONE;
    bool call = false;
    bool warns = true;
    bool done = false;
    uint32_t at = node;
    uint8_t kind = 0;
    for (uint32_t steps = 0; warns && !done && steps <= 2 * (uint64_t)graph->count; steps += 1)
    {
        const bool mark = (kind & BUSTER_CHECK_UNUSED_MARK) != 0;
        const CheckOp op = at < graph->count ? graph->ops[at] : CHECK_OP_COUNT;
        const bool gnu = op == CHECK_OP_CONDITIONAL && graph->operand1[at] == graph->operand0[at];
        const bool logical = !mark && (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR);
        if (op == CHECK_OP_COUNT)
        {
            warns = false;
        }
        else if (!mark && op == CHECK_OP_COMMA)
        {
            warns = !check_common_literal_zero(emission->parser, stream, graph, graph->operand1[at]);
            at = graph->operand1[at];
        }
        else if (!mark && op == CHECK_OP_STATEMENT_VALUE)
        {
            // A statement expression's final value, or its own void where it ends without one (the value the reader makes
            // at its `)`), which is quiet.
            const uint32_t value = graph->operand0[at];
            warns = value < graph->count && graph->tokens[value] != graph->tokens[at];
            at = value;
        }
        else if (!mark && (op == CHECK_OP_SELECT || op == CHECK_OP_CONVERT || gnu))
        {
            at = gnu ? graph->operand2[at] : graph->operand0[at];
        }
        else if (logical || (!mark && op == CHECK_OP_CONDITIONAL))
        {
            // The right operand or the else arm waits, after a logical operator's own mark.
            warns = depth + (logical ? 2u : 1u) <= BUSTER_CHECK_UNUSED_DEPTH;
            if (warns && logical)
            {
                pending[depth] = at;
                pending_kinds[depth] = (uint8_t)(kind | BUSTER_CHECK_UNUSED_MARK);
                depth += 1;
            }
            if (warns)
            {
                pending[depth] = logical ? graph->operand1[at] : graph->operand2[at];
                pending_kinds[depth] = (uint8_t)(kind | (logical ? BUSTER_CHECK_UNUSED_LOGICAL : 0u));
                depth += 1;
            }
            kind = (uint8_t)(kind | (logical ? BUSTER_CHECK_UNUSED_LOGICAL : 0u));
            at = logical ? graph->operand0[at] : graph->operand1[at];
        }
        else
        {
            // A value, or a logical operator both of whose operands warned, which then warns itself, at its operator.
            const TypeId type = !mark && op == CHECK_OP_CAST ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, at))) : 0;
            const bool void_cast = type != 0 && type_kind(types, type) == BUSTER_TYPE_KIND_VOID;
            call = !mark && emission->ignored != NULL && check_common_ignored_op(op) && check_common_ignored_of(emission, at, &result.found);
            const bool unknown = !mark && !call && op == CHECK_OP_CALL && (kind & BUSTER_CHECK_UNUSED_LOGICAL) != 0 && check_common_unused_unknown(emission, at);
            warns = mark || call || unknown ||
                    (!void_cast && !check_common_node_effect(graph, at) && !check_common_volatile_read(stream, types, at) &&
                     !check_common_void_quiet(graph, stream, types, at));
            leaf = at;
            done = depth == 0;
            at = depth != 0 ? pending[depth - 1] : BUSTER_CHECK_NONE;
            kind = depth != 0 ? pending_kinds[depth - 1] : 0;
            depth -= depth != 0 ? 1u : 0u;
        }
    }
    result.node = warns && done ? leaf : BUSTER_CHECK_NONE;
    result.call = result.node != BUSTER_CHECK_NONE && call;
    // A pure or const call's warning is dropped where the discarded expression itself, past its selections, is written in
    // a macro's body (clang's ShouldSuppress): a comma's at its operator, a conditional's at its first token.
    uint32_t top = node;
    for (uint32_t steps = 0; result.call && top < graph->count && graph->ops[top] == CHECK_OP_SELECT && steps < graph->count; steps += 1)
    {
        top = graph->operand0[top];
    }
    const CheckOp top_op = result.call && top < graph->count ? graph->ops[top] : CHECK_OP_COUNT;
    result.found.macro = top_op == CHECK_OP_COMMA ? graph->tokens[top] :
                         top_op == CHECK_OP_CONDITIONAL ? check_common_emission_subtree_start(emission, graph->tokens[top]) : result.found.macro;
    return result;
}

// The warning of a discarded value that warns (check_common_unused_of), `message` its text for a value of no effect: a
// call's result's at once (check_common_ignored_report), a `void *` cast's question at once, and otherwise `message` at
// the value (a cast's at its `(`, a member's at its name, a keyword's, `__builtin_LINE()`'s, at the keyword) at the body's
// end. A cast spelled to void is quiet.
static __attribute__((noinline)) void check_common_unused_report(CheckCommonEmission* emission, const CheckCommonUnused* unused, const char* message)
{
    const CheckGraph* graph = &emission->input->graph;
    const Parser* parser = emission->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t token = unused->node < graph->count ? graph->tokens[unused->node] : BUSTER_CHECK_NONE;
    const uint8_t role = token < count ? parser->tree.roles[token] : PARSE_ROLE_NONE;
    const bool cast = role == PARSE_ROLE_CLOSE_CAST;
    if (unused->call)
    {
        check_common_ignored_report(emission, &unused->found);
    }
    else if (token < count && !(cast && check_common_void_cast(parser, emission->pp, emission->input, emission->stream, emission->types, token)))
    {
        const uint32_t opener = cast ? check_common_cast_opener(parser, token) :
                                role == PARSE_ROLE_CLOSE_KEYWORD ? check_common_ignored_opener(parser, token) : BUSTER_CHECK_NONE;
        const uint32_t spot = opener != BUSTER_CHECK_NONE ? (cast || opener == 0 ? opener : opener - 1) :
                              role == PARSE_ROLE_MEMBER_OPERATOR && token + 1 < count ? token + 1 : token;
        if (cast && graph->ops[unused->node] == CHECK_OP_CAST && check_common_void_pointer_cast(emission->stream, emission->types, unused->node))
        {
            check_common_report_discarded(parser, emission->pp, emission->input, spot, CHECK_WARNING_ID_UNUSED_VALUE,
                                          "expression result unused; should this cast be to 'void'?", NULL, 0, emission->scratch);
        }
        else
        {
            check_common_report_unused(parser, emission->pp, emission->input, spot, CHECK_WARNING_ID_UNUSED_VALUE, message, NULL, 0, emission->scratch);
        }
    }
}

// A discarded value the walks leave out as unevaluated (in unreachable code, a constant condition's other arm, a
// short-circuited side): only a call's result warns, at once, as clang's DiagnoseNoDiscard and pure or const call warnings
// are no runtime diagnostics, where an unused value's DiagRuntimeBehavior drops itself there; in an unevaluated operand
// the report drops itself (check_common_report_discarded).
static __attribute__((noinline)) void check_common_unused_unevaluated(CheckCommonEmission* emission, uint32_t node)
{
    const CheckCommonUnused unused = check_common_unused_of(emission, node);
    if (unused.call)
    {
        check_common_ignored_report(emission, &unused.found);
    }
}

// The diagnostics for one evaluated comma: every left operand is evaluated for effect only — a discarded
// comparison reports as a comparison when the operand's own root is the comparison, anything else as the
// operand's own value (check_common_unused_of), a call's whose result warns included. A chain ending a
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
    // Clang's CheckCommaOperands asks DiagnoseUnusedExprResult of the left operand (check_common_unused_of): a literal 0
    // that ends commas of its own is quiet, like the `expr, 0` result; a bare `0, x` still reports the `0`, and so does
    // a conditional's `0` arm where the other arm warns too.
    const CheckCommonUnused unused = left_node < graph->count ? check_common_unused_of(emission, left_node) : (CheckCommonUnused){ .node = BUSTER_CHECK_NONE };
    const uint32_t left_result = unused.node;
    const bool left_effect = left_node < graph->count ? left_result == BUSTER_CHECK_NONE : check_common_expression_has_effect(parser, left_start, comma);
    if (unused.call)
    {
        // A call's result the left operand drops (#1447).
        check_common_ignored_report(emission, &unused.found);
    }
    else if (left_start < comma && !left_effect)
    {
        uint32_t report = left_result < graph->count && graph->tokens[left_result] < count ? graph->tokens[left_result] : left;
        uint8_t report_role = report < count ? parser->tree.roles[report] : PARSE_ROLE_NONE;
        const uint8_t left_role = left < count ? parser->tree.roles[left] : PARSE_ROLE_NONE;
        // Clang points a discarded member at the member's name, and a keyword's value (`__builtin_LINE()`) at its keyword.
        const uint32_t keyword = report_role == PARSE_ROLE_CLOSE_KEYWORD ? check_common_ignored_opener(parser, report) : BUSTER_CHECK_NONE;
        if (report_role == PARSE_ROLE_MEMBER_OPERATOR && report + 1 < count)
        {
            report += 1;
            report_role = parser->tree.roles[report];
        }
        else if (keyword != BUSTER_CHECK_NONE && keyword > 0)
        {
            report = keyword - 1;
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
                    check_common_report_comma_left(parser, pp, input, cast != BUSTER_CHECK_NONE ? cast : report, node, scratch);
                }
            }
        }
        else
        {
            check_common_report_comma_left(parser, pp, input, report, node, scratch);
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
    Arena* scratch = emission->scratch;
    const uint32_t count = parser->tokens.count;
    const uint32_t comma = graph->tokens[node];
    const uint32_t left_node = graph->operand0[node];
    const uint32_t right_node = graph->operand1[node];
    if (left_node < graph->count)
    {
        emission->emitted[left_node] |= BUSTER_CHECK_EMITTED_TREE;
    }
    if (right_node < graph->count)
    {
        emission->emitted[right_node] |= BUSTER_CHECK_EMITTED_TREE;
    }
    const uint32_t parent = check_common_parent(parser, comma);
    const bool discarded = check_common_emit_evaluated(emission, node) && check_common_discarded_place(parser, comma);
    if (discarded && right_node < graph->count)
    {
        // A discarded comma's value is its right operand's (check_common_unused_of), but for the `expr, 0` idiom's literal
        // 0, through nested commas and the conditional arms that all warn: a call's whose result warns (#1447), or one of no
        // effect.
        const CheckCommonUnused unused = check_common_unused_of(emission, node);
        check_common_unused_report(emission, &unused, "expression result unused");
    }
    else if (discarded)
    {
        // A right operand outside the graph has no effect where its tokens have none.
        const uint32_t right = comma + 1;
        const uint32_t right_start = right < count ? check_common_emission_subtree_start(emission, right) : right;
        if (right_start < parent && !check_common_expression_has_effect(parser, right_start, parent))
        {
            check_common_report_unused(parser, pp, input, right, CHECK_WARNING_ID_UNUSED_VALUE, "expression result unused", NULL, 0, scratch);
        }
    }
}

// A discarded selection's or query builtin's marker (check_common_stmt_marker's, #1447), out of line as rare: a _Generic's
// or a __builtin_choose_expr's result is its chosen operand's, and a query builtin's constant reports at its keyword.
static __attribute__((noinline)) void check_common_selected_marker(CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    const Parser* parser = emission->parser;
    const uint32_t token = graph->tokens[node];
    const uint32_t count = parser->tokens.count;
    const CheckOp op = graph->ops[node];
    if (op == CHECK_OP_SELECT)
    {
        // A _Generic's or a __builtin_choose_expr's value is its chosen operand's (check_common_unused_of, #1447): a call's
        // whose result warns, or one of no effect whose role may warn, at it.
        const CheckCommonUnused unused = check_common_unused_of(emission, node);
        const uint32_t at = unused.node < graph->count ? graph->tokens[unused.node] : count;
        const uint8_t chosen_role = at < count ? parser->tree.roles[at] : PARSE_ROLE_NONE;
        if (unused.call || (at < count && (check_common_effect_role(chosen_role) || chosen_role == PARSE_ROLE_CLOSE_KEYWORD) &&
                            chosen_role != PARSE_ROLE_ASSIGN))
        {
            check_common_unused_report(emission, &unused, "expression result unused");
        }
    }
    else
    {
        // A __builtin_offsetof's, a __builtin_types_compatible_p's, a place's (__builtin_LINE's, __builtin_COLUMN's,
        // __builtin_FILE's, __builtin_FILE_NAME's, __builtin_FUNCTION's) or a type query's constant, at its keyword (#1447).
        const uint32_t open = check_common_ignored_opener(parser, token);
        const uint32_t keyword = open != BUSTER_CHECK_NONE && open > 0 ? open - 1 : count;
        const uint32_t name = keyword < count ? parser->tokens.names[keyword] : 0;
        if (name == C_NAME_BUILTIN_OFFSETOF || name == C_NAME_BUILTIN_TYPES_COMPATIBLE_P || name == C_NAME_BUILTIN_LINE || name == C_NAME_BUILTIN_COLUMN ||
            name == C_NAME_BUILTIN_FILE || name == C_NAME_BUILTIN_FILE_NAME || name == C_NAME_BUILTIN_FUNCTION || name == C_NAME_BUILTIN_VECTORELEMENTS ||
            name == C_NAME_BUILTIN_OMP_REQUIRED_SIMD_ALIGN || name == C_NAME_IS_BITWISE_CLONEABLE || name == C_NAME_IS_DESTRUCTIBLE ||
            name == C_NAME_IS_NOTHROW_DESTRUCTIBLE)
        {
            check_common_report_unused(parser, emission->pp, emission->input, keyword, CHECK_WARNING_ID_UNUSED_VALUE, "expression result unused", NULL, 0,
                                       emission->scratch);
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
    const bool discarded = check_common_discarded_place(parser, token);
    const CheckOp op = graph->ops[node];
    if (discarded && op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL)
    {
        // An indirect comparison takes the generic marker; a direct one already reported as a comparison, but a discarded
        // statement expression's value, whose warning is clang's DiagnoseUnusedExprResult's of the statement expression
        // (#1447).
        if (parent_role != PARSE_ROLE_TERMINATOR || check_common_statement_value(parser, parent))
        {
            check_common_report_unused(parser, emission->pp, emission->input, token, CHECK_WARNING_ID_UNUSED_VALUE,
                                       "expression result unused", NULL, 0, emission->scratch);
        }
    }
    else if (discarded && (op == CHECK_OP_CONDITIONAL || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR))
    {
        // A conditional's arms must both warn, through their commas, conditionals and logical operators, and the else arm's
        // value is the one reported: a call's whose result warns (#1447), or one of no effect; a logical operator's operands
        // must both warn, and it is reported itself (#1390, check_common_unused_of).
        const CheckCommonUnused unused = check_common_unused_of(emission, node);
        check_common_unused_report(emission, &unused, "expression result unused");
    }
    else if (discarded && (op == CHECK_OP_SELECT || (op == CHECK_OP_VALUE && role == PARSE_ROLE_CLOSE_KEYWORD)))
    {
        check_common_selected_marker(emission, node);
    }
    else if (discarded && op != CHECK_OP_INITIALIZE && op != CHECK_OP_CONVERT && op != CHECK_OP_CONDITION &&
             check_common_effect_role(role) && role != PARSE_ROLE_ASSIGN && role != PARSE_ROLE_EQUALITY &&
             role != PARSE_ROLE_RELATIONAL &&
             !(role == PARSE_ROLE_CLOSE_CAST && check_common_void_cast(parser, emission->pp, emission->input, emission->stream, emission->types, token)) &&
             parser->tokens.kinds[token] != PARSE_KIND_L_PAREN &&
             parser->tokens.kinds[token] != PARSE_KIND_PLUSPLUS && parser->tokens.kinds[token] != PARSE_KIND_MINUSMINUS &&
             !check_common_node_effect(graph, node) && !check_common_volatile_read(emission->stream, emission->types, node) &&
             !check_common_void_quiet(graph, emission->stream, emission->types, node))
    {
        // (A value of type void is no result clang warns of, but a dereference's or a subscript's is, #1447.)
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

// The ROOT frames of statement expression `node`'s inner roots (check_common_inner_roots), pushed backwards so that they
// run in their places' order. A key's place is its root, or a displaced root's operand, whose reader the root is. The sort
// by statement expression keeps the roots' order within one, where a displaced root's place comes before roots ahead of
// it: the statement expression's keys, as few as its statements, are put in their places' order here, the one time the
// scan enters it.
static void check_common_inner_push(CheckCommonEmission* emission, uint32_t node)
{
    uint32_t first = 0;
    uint32_t last = emission->inner_count;
    while (first < last)
    {
        const uint32_t middle = first + (last - first) / 2;
        const bool before = (uint32_t)(emission->inner[middle] >> 32) < node;
        first = before ? middle + 1 : first;
        last = before ? last : middle;
    }
    uint32_t end = first;
    while (end < emission->inner_count && (uint32_t)(emission->inner[end] >> 32) == node)
    {
        const uint64_t key = emission->inner[end];
        uint32_t at = end;
        while (at > first && emission->inner[at - 1] > key)
        {
            emission->inner[at] = emission->inner[at - 1];
            at -= 1;
        }
        emission->inner[at] = key;
        end += 1;
    }
    for (uint32_t i = end; i > first; i -= 1)
    {
        const uint32_t place = (uint32_t)emission->inner[i - 1];
        const uint32_t reader = emission->parents[place];
        check_common_emit_push(emission, reader < emission->input->graph.count ? reader : place, BUSTER_CHECK_EMIT_ROOT);
    }
}

// The worklist's frames replaying what recursion did: CONVERSIONS visits implicit conversions in reverse
// operand order before the runtime checks, TREE gates and marks, CHILDREN pushes operands and arguments,
// OWN runs the node's checks or a conditional's arm emissions, COMMA/TAIL/END run a comma's stages,
// MARKER and MARK the deferred statements, and the scan pairs descend then visit each node.
static void check_common_emit_run(CheckCommonEmission* emission)
{
    const CheckGraph* graph = &emission->input->graph;
    while (emission->work_count != 0 && !emission->pp->fatal)
    {
        emission->work_count -= 1;
        const uint32_t node = emission->work_nodes[emission->work_count];
        const uint8_t kind = emission->work_kinds[emission->work_count];
        const uint32_t owner = emission->work_owners[emission->work_count];
        switch (kind)
        {
            case BUSTER_CHECK_EMIT_ARM:
            case BUSTER_CHECK_EMIT_ARM_PRUNED:
                if (node < graph->count && check_common_arm_owner(emission, owner))
                {
                    // A selection's own visit, then its selected arm's; a conditional's own visit, its condition's, then its
                    // arms (pushed in reverse), a known condition's unselected one pruned; a leaf's own conversions, then its
                    // report.
                    const CheckOp op = graph->ops[node];
                    const bool reached = kind == BUSTER_CHECK_EMIT_ARM;
                    const bool branch = op == CHECK_OP_SELECT || op == CHECK_OP_CONDITIONAL;
                    if (branch && (emission->emitted[node] & BUSTER_CHECK_EMITTED_CONVERSIONS) == 0 && !check_common_query_suppressed(graph, node) &&
                        (check_common_emit_evaluated(emission, node) || (emission->emitted[node] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0))
                    {
                        emission->emitted[node] |= BUSTER_CHECK_EMITTED_CONVERSIONS;
                        check_common_conversion_own(emission, node);
                    }
                    uint32_t owner_flags = 0;
                    TypeId owner_target = 0;
                    (void)check_common_arm_source(emission, owner, &owner_flags, &owner_target);
                    if (op == CHECK_OP_CONDITIONAL && check_common_bool_target(emission->types, owner_target) && check_common_constant_arms(emission, owner))
                    {
                        check_common_int_in_bool(emission->parser, emission->pp, emission->input, emission->stream, emission->types, node, emission->parents,
                                                 emission->at_once, emission->scratch);
                    }
                    if (op == CHECK_OP_SELECT)
                    {
                        check_common_emit_arm(emission, graph->operand0[node], kind, owner);
                    }
                    else if (op == CHECK_OP_CONDITIONAL)
                    {
                        const uint32_t condition = graph->operand0[node];
                        const CheckValue condition_value = condition < graph->count ? check_stream_value(emission->stream, condition) : (CheckValue){ 0 };
                        bool truth = false;
                        const bool known = condition < graph->count && check_action_truth(emission->types, &condition_value, &truth);
                        const bool true_reached = reached && (!known || truth);
                        const bool false_reached = reached && (!known || !truth);
                        check_common_emit_arm(emission, graph->operand2[node], false_reached ? BUSTER_CHECK_EMIT_ARM : BUSTER_CHECK_EMIT_ARM_PRUNED, owner);
                        check_common_emit_arm(emission, graph->operand1[node], true_reached ? BUSTER_CHECK_EMIT_ARM : BUSTER_CHECK_EMIT_ARM_PRUNED, owner);
                        check_common_emit_reach(emission, condition, BUSTER_CHECK_EMIT_CONVERSIONS, BUSTER_CHECK_REACH_CONVERSION);
                    }
                    else
                    {
                        check_common_emit_arm(emission, node, reached ? BUSTER_CHECK_EMIT_ARM_LEAF : BUSTER_CHECK_EMIT_ARM_LEAF_PRUNED, owner);
                        check_common_emit_reach(emission, node, BUSTER_CHECK_EMIT_CONVERSIONS, BUSTER_CHECK_REACH_CONVERSION);
                    }
                }
                break;
            case BUSTER_CHECK_EMIT_ARM_LEAF:
            case BUSTER_CHECK_EMIT_ARM_LEAF_PRUNED:
                if (node < graph->count && check_common_arm_owner(emission, owner))
                {
                    // The owner's conversion of this arm (clang's CheckConditionalOperand's CheckImplicitConversion): a
                    // `<<` converted to _Bool, a constant's value, then the enumerations.
                    uint32_t flags = 0;
                    TypeId target = 0;
                    (void)check_common_arm_source(emission, owner, &flags, &target);
                    if (check_common_constant_arms(emission, owner))
                    {
                        if (check_common_bool_target(emission->types, target))
                        {
                            check_common_int_in_bool(emission->parser, emission->pp, emission->input, emission->stream, emission->types, node, emission->parents,
                                                     emission->at_once, emission->scratch);
                        }
                        const CheckValue leaf = check_stream_value(emission->stream, node);
                        if (kind == BUSTER_CHECK_EMIT_ARM_LEAF || leaf.constant_kind == CHECK_CONSTANT_FLOATING)
                        {
                            check_common_conversion_leaf(emission->parser, emission->pp, emission->input, emission->stream, emission->types, emission->spell, node,
                                                         &leaf, target, emission->parents, emission->at_once, BUSTER_CHECK_LEAVES_ALL, emission->scratch);
                        }
                    }
                    if (check_common_enum_cast_arms(emission, owner) != BUSTER_CHECK_NONE)
                    {
                        // (An argument's target has no typedef spelling here, as check_common_call_argument's had none.)
                        const CheckCommonEnumTarget place = { .conversion = check_common_arm_argument(owner) ? BUSTER_CHECK_NONE : owner, .call = BUSTER_CHECK_NONE };
                        check_common_enum_cast(emission->parser, emission->pp, emission->input, emission->stream, emission->types, emission->spell, node,
                                               target, &place, true, emission->parents, emission->at_once, emission->scratch);
                    }
                }
                break;
            case BUSTER_CHECK_EMIT_CONVERSIONS:
                // (Not in an unevaluated query's operand, whose conversions clang's DiagRuntimeBehavior drops.)
                if (node >= graph->count || (emission->emitted[node] & BUSTER_CHECK_EMITTED_CONVERSIONS) != 0 ||
                    (!check_common_emit_evaluated(emission, node) && (emission->emitted[node] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) == 0) ||
                    check_common_query_suppressed(graph, node))
                {
                    break;
                }
                emission->emitted[node] |= BUSTER_CHECK_EMITTED_CONVERSIONS;
                check_common_conversion_own(emission, node);
                if (graph->ops[node] == CHECK_OP_CALL && graph->argument_offsets != NULL && graph->argument_counts != NULL)
                {
                    // Each argument's conversion precedes its nested calls; the last argument is visited first.
                    const uint8_t argument_kind = check_common_call_prototype(emission, node) ?
                                                  BUSTER_CHECK_EMIT_ARGUMENT : BUSTER_CHECK_EMIT_PROMOTED_ARGUMENT;
                    check_common_emit_reach(emission, graph->operand0[node], BUSTER_CHECK_EMIT_CONVERSIONS, BUSTER_CHECK_REACH_CONVERSION);
                    const uint32_t offset = graph->argument_offsets[node];
                    for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                    {
                        // A conditional argument's arms come after its own frame, each arm's conversions before its own (#1895).
                        const uint32_t argument = graph->argument_nodes[offset + i];
                        const uint32_t owner = BUSTER_CHECK_ARM_ARGUMENT | (offset + i) |
                                               (argument_kind == BUSTER_CHECK_EMIT_ARGUMENT ? BUSTER_CHECK_ARM_PROTOTYPE : 0u);
                        const bool conditional = argument < graph->count &&
                                                 __builtin_expect(graph->ops[check_common_enum_cast_unselected(graph, argument)] == CHECK_OP_CONDITIONAL, 0);
                        if (conditional && (check_common_enum_cast_arms(emission, owner) != BUSTER_CHECK_NONE || check_common_constant_arms(emission, owner)))
                        {
                            check_common_emit_arm(emission, argument,
                                                  check_common_emit_evaluated(emission, argument) ? BUSTER_CHECK_EMIT_ARM : BUSTER_CHECK_EMIT_ARM_PRUNED, owner);
                        }
                        else
                        {
                            check_common_emit_reach(emission, argument, BUSTER_CHECK_EMIT_CONVERSIONS, BUSTER_CHECK_REACH_CONVERSION);
                        }
                        check_common_emit_push(emission, offset + i, argument_kind);
                    }
                }
                else if (graph->ops[node] == CHECK_OP_CONDITIONAL)
                {
                    // A conditional's two arms retain source order, including source-only floating literals.
                    check_common_emit_kids(emission, node, 3, BUSTER_CHECK_EMIT_CONVERSIONS);
                }
                else
                {
                    const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
                    // (Only a conditional source, past its selections, has arms: the rest pay one test.)
                    const uint32_t source = graph->ops[node] == CHECK_OP_ASSIGN ? graph->operand1[node] : graph->operand0[node];
                    const CheckOp source_op = source < graph->count ? graph->ops[source] : CHECK_OP_COUNT;
                    const bool branch = __builtin_expect(source_op == CHECK_OP_CONDITIONAL || source_op == CHECK_OP_SELECT, 0);
                    const uint32_t enum_arms = branch ? check_common_enum_cast_arms(emission, node) : BUSTER_CHECK_NONE;
                    const bool constant_arms = branch && check_common_constant_arms(emission, node);
                    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
                    {
                        if (operands[i] == source && (enum_arms != BUSTER_CHECK_NONE || constant_arms))
                        {
                            // (An unevaluated conversion's arms are pruned from the start, as the walk's would be.)
                            check_common_emit_arm(emission, source, check_common_emit_evaluated(emission, node) ? BUSTER_CHECK_EMIT_ARM : BUSTER_CHECK_EMIT_ARM_PRUNED,
                                                  node);
                        }
                        else
                        {
                            check_common_emit_reach(emission, operands[i], BUSTER_CHECK_EMIT_CONVERSIONS, BUSTER_CHECK_REACH_CONVERSION);
                        }
                    }
                    if (check_graph_has_arguments(graph->ops[node]) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
                    {
                        for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                        {
                            check_common_emit_reach(emission, graph->argument_nodes[graph->argument_offsets[node] + i], BUSTER_CHECK_EMIT_CONVERSIONS, BUSTER_CHECK_REACH_CONVERSION);
                        }
                    }
                }
                break;
            case BUSTER_CHECK_EMIT_TREE:
                if (node >= graph->count || (emission->emitted[node] & BUSTER_CHECK_EMITTED_TREE) != 0 ||
                    (!check_common_emit_evaluated(emission, node) && (emission->emitted[node] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) == 0))
                {
                    break;
                }
                emission->emitted[node] |= BUSTER_CHECK_EMITTED_TREE;
                // A comma the source writes; the reader's own (a variable length array's sizeof evaluating its operand,
                // c_check_vla_input.c) is at the sizeof and has no operand of the comma operator's to warn of.
                if (check_common_emit_evaluated(emission, node) && graph->ops[node] == CHECK_OP_COMMA && graph->tokens[node] < emission->parser->tokens.count &&
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
                if (node < graph->count && (check_common_emit_evaluated(emission, node) ||
                    (emission->emitted[node] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0))
                {
                    check_common_emit_kids(emission, node, graph->ops[node] == CHECK_OP_CONDITIONAL ? 1u : 3u, BUSTER_CHECK_EMIT_TREE);
                }
                break;
            case BUSTER_CHECK_EMIT_ARGUMENT:
            case BUSTER_CHECK_EMIT_PROMOTED_ARGUMENT:
                check_common_call_argument(emission, node, kind == BUSTER_CHECK_EMIT_ARGUMENT);
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
                // (A cast's comma before it of its type name's bounds, #2321, has no source comma.)
                if (node < graph->count && check_common_emit_evaluated(emission, node) && (emission->emitted[node] & BUSTER_CHECK_EMITTED_COMMA_MARKER) == 0 &&
                    !(graph->tokens[node] < emission->parser->tokens.count && emission->parser->tree.roles[graph->tokens[node]] == PARSE_ROLE_CLOSE_CAST))
                {
                    check_common_comma_marker(emission, node);
                }
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
                if (node < graph->count && check_common_emit_evaluated(emission, node))
                {
                    check_common_stmt_marker(emission, node);
                }
                break;
            case BUSTER_CHECK_EMIT_MARK:
                if (node < graph->count)
                {
                    emission->emitted[node] |= BUSTER_CHECK_EMITTED_TREE;
                }
                break;
            case BUSTER_CHECK_EMIT_SIGN_SCAN:
                if (node < graph->count && (emission->evaluated == NULL || emission->evaluated[node] != 0))
                {
                    check_common_sign_scan(emission, node);
                }
                break;
            case BUSTER_CHECK_EMIT_ARITHMETIC_SCAN:
                if (node < graph->count && (emission->evaluated == NULL || emission->evaluated[node] != 0) &&
                    (kind != BUSTER_CHECK_EMIT_ARITHMETIC_SCAN || (emission->emitted[node] & BUSTER_CHECK_EMITTED_ARITHMETIC_SCAN) == 0))
                {
                    emission->emitted[node] |= BUSTER_CHECK_EMITTED_ARITHMETIC_SCAN;
                    check_common_emit_push(emission, node, BUSTER_CHECK_EMIT_ARITHMETIC_OWN);
                    check_common_emit_kids(emission, node, 3, kind);
                    if (emission->inner_count != 0 && graph->ops[node] == CHECK_OP_STATEMENT_VALUE)
                    {
                        // A statement expression's statements, built before its value is read, each a whole root's visit.
                        check_common_inner_push(emission, node);
                    }
                }
                else if (node < graph->count && (emission->reach[node] & BUSTER_CHECK_REACH_DETACHED) != 0)
                {
                    check_common_emit_kids(emission, node, 3, BUSTER_CHECK_EMIT_DETACHED_SCAN);
                }
                break;
            case BUSTER_CHECK_EMIT_ARITHMETIC_OWN:
                check_common_arithmetic_own(emission, node);
                break;
            case BUSTER_CHECK_EMIT_ROOT:
                check_common_root_frames(emission, node);
                break;
            case BUSTER_CHECK_EMIT_SIGN_OWN:
                check_common_sign_own(emission, node);
                break;
            case BUSTER_CHECK_EMIT_SHIFT_SCAN:
                if (node < graph->count && (emission->emitted[node] & BUSTER_CHECK_EMITTED_SHIFT_SCAN) == 0)
                {
                    emission->emitted[node] |= BUSTER_CHECK_EMITTED_SHIFT_SCAN;
                    check_common_emit_push(emission, node, BUSTER_CHECK_EMIT_SHIFT_OWN);
                    check_common_emit_kids(emission, node, 3, kind);
                }
                break;
            case BUSTER_CHECK_EMIT_SHIFT_OWN:
                if (check_common_shift_op(graph->ops[node]))
                {
                    check_common_shift_count(emission, node, true);
                }
                break;
            case BUSTER_CHECK_EMIT_DETACHED_SCAN:
                if (node < graph->count && (emission->reach[node] & BUSTER_CHECK_REACH_DETACHED) != 0)
                {
                    check_common_emit_kids(emission, node, 3, kind);
                }
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

// Whether the discarded expression can produce an unused-value marker.
static bool check_common_marker_candidate(const CheckGraph* graph, uint32_t expr)
{
    const CheckOp op = expr < graph->count ? graph->ops[expr] : CHECK_OP_COUNT;
    const bool result = expr < graph->count &&
                        ((op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL) || op == CHECK_OP_CONDITIONAL ||
                         (op != CHECK_OP_INITIALIZE && op != CHECK_OP_CONVERT && op != CHECK_OP_CONDITION && !check_common_node_effect(graph, expr)));
    return result;
}

// The deferred warnings one evaluated expression root produces, in clang's order: arithmetic warnings, reverse-order
// implicit conversions, the deep subtree and discard marker, the root's own warning, and sign comparisons.
// The worklist drains before returning, so each push is the calls it replaces in reverse.
// A root's walks' frames, pushed to run next.
static void check_common_root_push(CheckCommonEmission* emission, uint32_t node)
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
    if (expr < graph->count && check_common_emit_evaluated(emission, expr) && graph->ops[expr] == CHECK_OP_COMMA)
    {
        check_common_emit_push(emission, expr, BUSTER_CHECK_EMIT_COMMA);
    }
    else if (expr < graph->count)
    {
        check_common_emit_push(emission, expr, BUSTER_CHECK_EMIT_OWN);
        check_common_emit_push(emission, expr, BUSTER_CHECK_EMIT_MARKER);
        check_common_emit_push(emission, expr, BUSTER_CHECK_EMIT_CHILDREN);
    }
    if ((reach & BUSTER_CHECK_REACH_CONVERSION) != 0)
    {
        check_common_emit_push(emission, node, BUSTER_CHECK_EMIT_CONVERSIONS);
    }
    if ((reach & BUSTER_CHECK_REACH_ARITHMETIC) != 0)
    {
        check_common_emit_push(emission, node, BUSTER_CHECK_EMIT_ARITHMETIC_SCAN);
    }
}

static void check_common_emit_root(CheckCommonEmission* emission, uint32_t node)
{
    check_common_root_push(emission, node);
    check_common_emit_run(emission);
}

// A root's visit in the roots' order, its walks' frames pushed to run next (#1447): a discarded call's result at once, an
// unevaluated statement's comma's, conditional's or selection's call, then its walks, or its statement's unused-value
// marker alone where it reaches nothing, or a dead root's detached static-selection arms.
// Always inlined, as the roots' loop's body it was (#1447); hot caller: check_common_graph_warnings, once per root.
static inline __attribute__((always_inline)) void check_common_root_frames(CheckCommonEmission* emission, uint32_t node)
{
    const Parser* parser = emission->parser;
    const CheckGraph* graph = &emission->input->graph;
    const CheckStream* stream = emission->stream;
    const uint32_t* parents = emission->parents;
    const uint8_t* evaluated = emission->evaluated;
    uint8_t* emitted = emission->emitted;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    const uint32_t expr = check_common_discarded(parser, graph, node);
    const CheckOp expr_op = expr != node && expr < graph->count && parents[node] == BUSTER_CHECK_NONE &&
                            stream->diagnostics[node] == CHECK_DIAG_NONE ? graph->ops[expr] : CHECK_OP_COUNT;
    if (expr_op != CHECK_OP_COUNT && check_common_ignored_op(expr_op))
    {
        check_common_ignored_statement(emission, expr);
    }
    else if ((expr_op == CHECK_OP_CONDITIONAL || expr_op == CHECK_OP_COMMA || expr_op == CHECK_OP_SELECT) && evaluated[node] == 0 &&
             check_common_discarded_place(parser, graph->tokens[expr]))
    {
        // An unevaluated statement's comma, conditional or selection, which no walk reaches: its value's call.
        check_common_unused_unevaluated(emission, expr);
    }
    const CheckCommonShiftExtra* extra = check_common_shift_extra_root(emission->shift_extras, node);
    if (parents[node] == BUSTER_CHECK_NONE && (evaluated[node] != 0 || (emitted[node] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0) &&
        (emitted[node] & BUSTER_CHECK_EMITTED_TREE) == 0 && stream->diagnostics[node] == CHECK_DIAG_NONE)
    {
        const bool comma = expr < graph->count && graph->ops[expr] == CHECK_OP_COMMA;
        if (emission->reach[node] != 0 || comma)
        {
            check_common_root_push(emission, node);
        }
        else if (vector && evaluated[node] != 0 && check_common_marker_candidate(graph, expr))
        {
            // A root that reaches nothing: its walk's frames would find no own warning and no subtree to enter
            // (its reach holds its expression's own bit), so that the statement's unused-value marker alone
            // may warn, as the walk would run it, before the walk's marks.
            check_common_stmt_marker(emission, expr);
            emitted[expr] |= BUSTER_CHECK_EMITTED_TREE;
            emitted[node] |= BUSTER_CHECK_EMITTED_TREE;
        }
        else if (check_common_marker_candidate(graph, expr))
        {
            check_common_root_push(emission, node);
        }
    }
    else if (parents[node] == BUSTER_CHECK_NONE && stream->diagnostics[node] == CHECK_DIAG_NONE && extra == NULL)
    {
        // A dead ordinary owner still supplies a source position for its detached arms. Enter only their
        // sparse edges, so their warnings precede later live operands without reviving ordinary dead code.
        check_common_emit_reach(emission, node, BUSTER_CHECK_EMIT_DETACHED_SCAN, BUSTER_CHECK_REACH_DETACHED);
    }
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

// Of 16 operators `ops`, the `lanes` whose operator is in `set`, a bit each: each operator's word of the set a lane permute
// away.
static uint32_t check_common_op_lanes(const CheckCommonOps* set, __m512i ops, __mmask16 lanes)
{
    const __m512i words = _mm512_maskz_loadu_epi32((__mmask16)((1u << BUSTER_ARRAY_LENGTH(set->words)) - 1), set->words);
    const __m512i word = _mm512_permutexvar_epi32(_mm512_srli_epi32(ops, 5), words);
    const __m512i bit = _mm512_sllv_epi32(_mm512_set1_epi32(1), _mm512_and_si512(ops, _mm512_set1_epi32(31)));
    return _mm512_mask_test_epi32_mask(lanes, word, bit);
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
        const uint32_t selected = vector ? check_common_op_lanes(set, _mm512_maskz_loadu_epi32((__mmask16)lanes, graph->ops + first), (__mmask16)lanes) :
                                           lanes;
        scan->base = first;
        scan->next = first + live;
        scan->pending = selected;
    }
    const uint32_t result = check_common_scan_take(scan, graph->count);
    return result;
}

// A batch of `live` (up to 16) listed comparison nodes (check_common_next_reach_op): those
// -Wtautological-constant-out-of-range-compare may warn of (check_common_range_lanes) reach their roots, a reach's bits
// being a union up the parents, so that the comparisons' order among the others' changes nothing.
static void check_common_range_batch(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, const uint32_t* parents, uint8_t* reach,
                                     const uint32_t* nodes, uint32_t live)
{
    const uint32_t count = graph->count;
    const __mmask16 lanes = (__mmask16)(live >= 16 ? 0xffffu : (1u << live) - 1);
    for (uint32_t bits = live != 0 ? check_common_range_lanes(graph, stream, types, nodes, lanes) : 0u; bits != 0; bits &= bits - 1)
    {
        uint8_t reached = BUSTER_CHECK_REACH_SIGN;
        for (uint32_t at = nodes[(uint32_t)__builtin_ctz(bits)]; reached != 0 && at < count; at = parents[at])
        {
            reached &= (uint8_t)~reach[at];
            reach[at] |= reached;
        }
    }
}

// check_common_next_op for the reach scan: with `comparisons`, each block's comparison nodes are appended to it (`*listed`
// of them so far), compressed from the same operator load, for -Wtautological-constant-out-of-range-compare's batches
// (check_common_range_lanes): a block holds one or two comparisons, and the batches' gathers take 16 at once.
static uint32_t check_common_next_reach_op(const CheckGraph* graph, const CheckCommonOps* set, uint32_t* comparisons, uint32_t* listed, CheckCommonScan* scan)
{
    while (scan->pending == 0 && scan->next < graph->count)
    {
        const uint32_t first = scan->next;
        const uint32_t live = graph->count - first < 16 ? graph->count - first : 16;
        const __mmask16 lanes = (__mmask16)(live == 16 ? 0xffffu : (1u << live) - 1);
        const __m512i ops = _mm512_maskz_loadu_epi32(lanes, graph->ops + first);
        if (comparisons != NULL)
        {
            const __mmask16 compares = _mm512_mask_cmple_epu32_mask(lanes, _mm512_sub_epi32(ops, _mm512_set1_epi32(CHECK_OP_LESS)),
                                                                     _mm512_set1_epi32(CHECK_OP_NOT_EQUAL - CHECK_OP_LESS));
            const __m512i nodes = _mm512_add_epi32(_mm512_set1_epi32((int)first), _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
            _mm512_mask_compressstoreu_epi32(comparisons + *listed, compares, nodes);
            *listed += (uint32_t)__builtin_popcount(compares);
        }
        scan->base = first;
        scan->next = first + live;
        scan->pending = check_common_op_lanes(set, ops, lanes);
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

// The token past the binary operator `op`'s right operand, its subtree's last token's next: the tokens after it that climb
// to it are its right operand's. Only a warning asks, so the climbs' cost is a warning's.
static uint32_t check_common_operand_end(const Parser* parser, uint32_t op)
{
    const uint32_t count = parser->tokens.count;
    uint32_t end = op < count ? op + 1 : count;
    bool inside = true;
    while (inside && end < count)
    {
        uint32_t at = end;
        for (uint32_t steps = 0; at < count && at != op && steps < count; steps += 1)
        {
            const uint32_t up = parser->tree.parents[at];
            at = up != at ? up : count;
        }
        inside = at == op;
        end += inside ? 1u : 0u;
    }
    return end;
}

static uint32_t check_driver_pointer_spelling(const Parser* parser, const CheckInput* input, const CheckStream* stream, const TypeTable* types, uint32_t node);
static bool check_driver_size_sugar(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types, uint32_t node);

// The root of a binary operator's (`op`) operand from `token`, one of its tokens, climbing at most `count` parents; or
// BUSTER_CHECK_NONE. With `strings`, through string literals alone (a concatenation's).
static uint32_t check_common_operand_root(const Parser* parser, uint32_t op, uint32_t token, bool strings)
{
    const uint32_t count = parser->tokens.count;
    uint32_t at = token;
    for (uint32_t steps = 0; at < count && parser->tree.parents[at] != op && (!strings || parser->tokens.kinds[at] == PARSE_KIND_STRING) && steps < count; steps += 1)
    {
        const uint32_t up = parser->tree.parents[at];
        at = up != at ? up : count;
    }
    return at < count && parser->tree.parents[at] == op && (!strings || parser->tokens.kinds[at] == PARSE_KIND_STRING) ? at : BUSTER_CHECK_NONE;
}

// -Wstring-plus-int (#2190, clang's diagnoseStringPlusInt): a `+` of a string literal, unparenthesized, and an operand of
// integer, boolean or enumeration type, in either order, "adding 'T' to a string does not append to the string" at the
// `+`, T the operand's type as written, with its note there; given as clang builds the `+`, after its operands' own. A
// `+` whose neighbor isn't a string literal is skipped at once.
static void check_common_string_plus_int(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                         CheckWarningSpelling* spell, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const uint8_t roles[] = { PARSE_ROLE_ADDITIVE };
    CheckCommonScan scan = { 0 };
    const bool enabled = stream != NULL && types != NULL && input->token_nodes != NULL && check_common_enabled(pp, input, CHECK_WARNING_ID_STRING_PLUS_INT, false);
    for (uint32_t token = enabled ? check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector) : count; token < count;
         token = check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector))
    {
        const bool plus = parser->tokens.kinds[token] == PARSE_KIND_PLUS && token > 0 && token + 1 < count;
        const uint32_t left_string = plus && parser->tokens.kinds[token - 1] == PARSE_KIND_STRING ? check_common_operand_root(parser, token, token - 1, true) : BUSTER_CHECK_NONE;
        const uint32_t right_string = plus && left_string == BUSTER_CHECK_NONE && parser->tokens.kinds[token + 1] == PARSE_KIND_STRING ?
                                      check_common_operand_root(parser, token, token + 1, true) : BUSTER_CHECK_NONE;
        const bool string = left_string != BUSTER_CHECK_NONE || right_string != BUSTER_CHECK_NONE;
        const uint32_t index = !string ? BUSTER_CHECK_NONE : left_string != BUSTER_CHECK_NONE ? check_common_operand_root(parser, token, token + 1, false) :
                               check_common_operand_root(parser, token, token - 1, false);
        const uint32_t node = index < input->token_count ? input->token_nodes[index] : BUSTER_CHECK_NONE;
        const TypeId value = node < stream->count ? check_stream_type(stream, node) : 0;
        const uint8_t value_kind = value != 0 ? type_kind(types, type_unqualified(type_strip_aligned(types, value))) : 0;
        if (value_kind == BUSTER_TYPE_KIND_INTEGER || value_kind == BUSTER_TYPE_KIND_ENUM || value_kind == BUSTER_TYPE_KIND_BIT_INT)
        {
            // Its type as clang names it past its implicit conversions: a typedef's or typeof's spelling, an integer's
            // __size_t sugar (check_driver_conversion_words' way).
            const uint64_t mark = scratch->length;
            const uint32_t walked = check_driver_pointer_spelling(parser, input, stream, types, node);
            const bool integer = value_kind == BUSTER_TYPE_KIND_INTEGER;
            const bool sized = integer && check_driver_size_sugar(parser, pp, input, stream, types, node);
            const uint32_t spelling = sized ? BUSTER_CHECK_NONE : integer && walked == BUSTER_CHECK_NONE ? 0 : walked;
            const String8 name = check_driver_warning_type_sugared(parser, pp, input, types, spell, scratch, value, spelling, check_driver_operand_sugar(input, stream, node));
            const char* message = name.length != 0 ? check_warning_text(scratch, "adding %.*s to a string does not append to the string", (int)name.length, name.pointer) : NULL;
            const CheckCommonNote note = { token, "use array indexing to silence this warning" };
            if (message != NULL)
            {
                check_common_report_at(parser, pp, input, token, check_common_operand_end(parser, token), CHECK_WARNING_ID_STRING_PLUS_INT, false, message, &note, 1,
                                       scratch);
            }
            arena_pop_to(scratch, mark);
        }
    }
}

// The root of the operand of a binary operator `op` that holds `token`, climbing at most the token count's parents, or
// BUSTER_CHECK_NONE.
static uint32_t check_common_operand_root_of(const Parser* parser, uint32_t op, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t at = token;
    for (uint32_t steps = 0; at < count && parser->tree.parents[at] != op && steps < count; steps += 1)
    {
        const uint32_t up = parser->tree.parents[at];
        at = up != at ? up : count;
    }
    return at < count && parser->tree.parents[at] == op ? at : BUSTER_CHECK_NONE;
}

// -Wshift-op-parentheses (#2264, clang's DiagnoseAdditionInShift as it builds a shift): each operand of a `<<` or `>>` that
// is a `+` or a `-`, unparenthesized, the left one first, "operator '<<' has lower precedence than '+'; '+' will be
// evaluated first" at the `+`, with its note there; given as the shift is built, after its operands' own.
static void check_common_shift_op_parentheses(const Parser* parser, Preprocessor* pp, const CheckInput* input, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const uint8_t roles[] = { PARSE_ROLE_SHIFT };
    CheckCommonScan scan = { 0 };
    const bool enabled = check_common_enabled(pp, input, CHECK_WARNING_ID_SHIFT_OP_PARENTHESES, false);
    for (uint32_t token = enabled ? check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector) : count; token < count;
         token = check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector))
    {
        const uint8_t kind = parser->tokens.kinds[token];
        const bool shift = (kind == PARSE_KIND_LESSLESS || kind == PARSE_KIND_GREATERGREATER) && token > 0 && token + 1 < count;
        const uint32_t sides[2] = {
            shift ? check_common_operand_root_of(parser, token, token - 1) : BUSTER_CHECK_NONE,
            shift ? check_common_operand_root_of(parser, token, token + 1) : BUSTER_CHECK_NONE,
        };
        uint32_t end = BUSTER_CHECK_NONE;
        for (uint32_t i = 0; i < 2; i += 1)
        {
            const uint32_t side = sides[i];
            const uint8_t side_kind = side < count ? parser->tokens.kinds[side] : 0;
            if (side < count && parser->tree.roles[side] == PARSE_ROLE_ADDITIVE && (side_kind == PARSE_KIND_PLUS || side_kind == PARSE_KIND_MINUS))
            {
                const uint64_t mark = scratch->length;
                const char* shifted = kind == PARSE_KIND_LESSLESS ? "<<" : ">>";
                const char* added = side_kind == PARSE_KIND_PLUS ? "+" : "-";
                const char* message = check_warning_text(scratch, "operator '%s' has lower precedence than '%s'; '%s' will be evaluated first", shifted, added, added);
                const CheckCommonNote note = { side, check_warning_text(scratch, "place parentheses around the '%s' expression to silence this warning", added) };
                end = end == BUSTER_CHECK_NONE ? check_common_operand_end(parser, token) : end;
                if (message != NULL && note.message != NULL)
                {
                    // Its range, and its note's, is the `+` expression's, which shows the macro a `+` from its argument came
                    // through (Preprocessor.diagnostic_arguments, which its note keeps too).
                    pp->diagnostic_arguments = true;
                    check_common_report_at(parser, pp, input, side, end, CHECK_WARNING_ID_SHIFT_OP_PARENTHESES, false, message, &note, 1, scratch);
                    pp->diagnostic_arguments = false;
                }
                arena_pop_to(scratch, mark);
            }
        }
    }
}

// A chained comparison (#2264, clang's diagnoseChainedComparison as it builds a relational comparison): a `<`, `>`, `<=` or
// `>=` whose left operand is one, unparenthesized, "chained comparison 'X < Y < Z' does not behave the same as a
// mathematical expression" at the outer operator, -Wparentheses' error by default.
static void check_common_chained_comparisons(const Parser* parser, Preprocessor* pp, const CheckInput* input, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const uint8_t roles[] = { PARSE_ROLE_RELATIONAL };
    CheckCommonScan scan = { 0 };
    const bool enabled = check_common_enabled(pp, input, CHECK_WARNING_ID_CHAINED_COMPARISON, false);
    for (uint32_t token = enabled ? check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector) : count; token < count;
         token = check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector))
    {
        // The left operand's root: the comparison's child before it. (The scalar scan visits every token.)
        uint32_t left = token > 0 && parser->tree.roles[token] == PARSE_ROLE_RELATIONAL ? token - 1 : count;
        for (uint32_t steps = 0; left < count && parser->tree.parents[left] != token && steps < count; steps += 1)
        {
            const uint32_t up = parser->tree.parents[left];
            left = up != left ? up : count;
        }
        if (left < token && parser->tree.roles[left] == PARSE_ROLE_RELATIONAL)
        {
            const uint64_t mark = scratch->length;
            const char* message = check_warning_text(scratch, "chained comparison 'X %s Y %s Z' does not behave the same as a mathematical expression",
                                                     check_common_operator_spelling(parser, left), check_common_operator_spelling(parser, token));
            if (message != NULL)
            {
                check_common_report(parser, pp, input, token, CHECK_WARNING_ID_CHAINED_COMPARISON, false, message, NULL, 0, scratch);
            }
            arena_pop_to(scratch, mark);
        }
    }
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
            // Clang checks the condition once it is whole (CheckBooleanCondition): after the diagnostics its operands gave as
            // they were built, a call's format or a statement expression's own (#1941), so published at its `)` or `;`.
            check_common_report_at(parser, pp, input, token, parent, CHECK_WARNING_ID_PARENTHESES, false,
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
                // Clang warns as it builds the bitwise operator, after its right operand's own diagnostics (#1941).
                check_common_report_at(parser, pp, input, parent, check_common_operand_end(parser, parent), CHECK_WARNING_ID_PARENTHESES, false, message,
                                       notes, 2, scratch);
            }
        }
        else if (role == PARSE_ROLE_PREFIX && parser->tokens.kinds[token] == PARSE_KIND_EXCLAIM && check_common_comparison_role(parent_role) && token < parent)
        {
            const CheckCommonNote notes[2] = {
                { token, "add parentheses after the '!' to evaluate the comparison first" },
                { token, "add parentheses around left hand side expression to silence this warning" },
            };
            // Clang warns as it builds the comparison, after its right operand's own diagnostics (#1941).
            check_common_report_at(parser, pp, input, token, check_common_operand_end(parser, parent), CHECK_WARNING_ID_LOGICAL_NOT_PARENTHESES, false,
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
            // (A statement expression's value is the walk's, as the statement expression's discarded value, #1447.)
            if (!check_common_statement_value(parser, parent))
            {
                check_common_report_discarded(parser, pp, input, token, CHECK_WARNING_ID_UNUSED_COMPARISON, message,
                                              note.message[0] != 0 ? &note : NULL, note.message[0] != 0 ? 1u : 0u, scratch);
            }
        }
    }
}

static uint32_t check_driver_pointer_spelling(const Parser* parser, const CheckInput* input, const CheckStream* stream, const TypeTable* types, uint32_t node);

// The root of the operand of the binary operator `op` that holds `token`, one of its tokens, or BUSTER_CHECK_NONE.
static uint32_t check_common_sizeof_decay_operand(const Parser* parser, uint32_t op, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t at = token;
    for (uint32_t steps = 0; at < count && parser->tree.parents[at] != op && steps < count; steps += 1)
    {
        const uint32_t up = parser->tree.parents[at];
        at = up != at ? up : count;
    }
    return at < count && parser->tree.parents[at] == op ? at : BUSTER_CHECK_NONE;
}

// -Wsizeof-array-decay (#2264, clang's warnOnSizeofOnArrayDecay): `sizeof` of a binary operator, through parentheses,
// whose left or right operand is an array that decays to the operator's own type, as in `sizeof(a + 1)`: "sizeof on
// pointer operation will return size of 'int *' instead of 'int[10]'" at the operator, once for each such operand. Given
// as clang builds the `sizeof`: published at the token past its `)`, after the operand's -Wunevaluated-expression. The
// additive, assignment and comma operators are the ones whose type can be an operand's decayed pointer.
static void check_common_sizeof_array_decay(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                            CheckWarningSpelling* spell, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const uint8_t roles[] = { PARSE_ROLE_ADDITIVE, PARSE_ROLE_ASSIGN, PARSE_ROLE_COMMA, PARSE_ROLE_SEPARATOR };
    CheckCommonScan scan = { 0 };
    const bool enabled = stream != NULL && types != NULL && input->token_nodes != NULL && check_common_enabled(pp, input, CHECK_WARNING_ID_SIZEOF_ARRAY_DECAY, false);
    for (uint32_t token = enabled ? check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector) : count; token < count;
         token = check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector))
    {
        const uint8_t role = parser->tree.roles[token];
        uint32_t close = check_common_parent(parser, token);
        for (uint32_t steps = 0; close < count && parser->tree.roles[close] == PARSE_ROLE_CLOSE_GROUP && steps < count; steps += 1)
        {
            close = check_common_parent(parser, close);
        }
        // A sizeof's comma is a separator (check_common_comma_operator).
        const bool binary = (role == PARSE_ROLE_ADDITIVE || role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_COMMA || role == PARSE_ROLE_SEPARATOR) && token > 0 &&
                            token + 1 < count;
        uint32_t opener = BUSTER_CHECK_NONE;
        for (uint32_t at = binary && close < count && parser->tree.roles[close] == PARSE_ROLE_CLOSE_SIZEOF ? token : 0; at != 0 && opener == BUSTER_CHECK_NONE;)
        {
            at -= 1;
            opener = parser->tree.roles[at] == PARSE_ROLE_OPEN_SIZEOF && parser->tree.parents[at] == close ? at : BUSTER_CHECK_NONE;
        }
        const bool sized = opener != BUSTER_CHECK_NONE && opener > 0 && parser->tokens.kinds[opener - 1] == PARSE_KIND_SIZEOF &&
                           parser->tokens.names[opener - 1] == C_NAME_SIZEOF;
        const uint32_t op_node = sized && token < input->token_count ? input->token_nodes[token] : BUSTER_CHECK_NONE;
        const TypeId result = op_node < stream->count ? type_strip_aligned(types, check_stream_type(stream, op_node)) : 0;
        const uint32_t sides[2] = {
            result != 0 ? check_common_sizeof_decay_operand(parser, token, token - 1) : BUSTER_CHECK_NONE,
            result != 0 ? check_common_sizeof_decay_operand(parser, token, token + 1) : BUSTER_CHECK_NONE,
        };
        for (uint32_t side = 0; side < 2; side += 1)
        {
            const uint32_t node = sides[side] < input->token_count ? input->token_nodes[sides[side]] : BUSTER_CHECK_NONE;
            const TypeId array = node < stream->count ? type_strip_aligned(types, check_stream_type(stream, node)) : 0;
            const TypeId decayed = array != 0 && type_kind_is_array(type_kind(types, array)) ? type_find_pointer(types, type_element(types, array)) : 0;
            if (decayed != 0 && decayed == result)
            {
                const uint64_t mark = scratch->length;
                const String8 pointer = check_driver_warning_type_sugared(parser, pp, input, types, spell, scratch, decayed, 0, 0);
                const uint32_t walked = check_driver_pointer_spelling(parser, input, stream, types, node);
                const String8 name = check_driver_warning_type_sugared(parser, pp, input, types, spell, scratch, array, walked == BUSTER_CHECK_NONE ? 0 : walked,
                                                                       check_driver_operand_sugar(input, stream, node));
                const char* message = pointer.length != 0 && name.length != 0 ?
                    check_warning_text(scratch, "sizeof on pointer operation will return size of %.*s instead of %.*s", (int)pointer.length, pointer.pointer,
                                       (int)name.length, name.pointer) : NULL;
                if (message != NULL)
                {
                    check_common_report_at(parser, pp, input, token, close + 1, CHECK_WARNING_ID_SIZEOF_ARRAY_DECAY, false, message, NULL, 0, scratch);
                }
                arena_pop_to(scratch, mark);
            }
        }
    }
}

// The hexadecimal digits of `value << count` as clang's APInt prints a shift's result (DiagnoseBadShiftValues), without
// leading zeros: those of value << (count % 4), three 64-bit limbs wide, then count / 4 zeros. `digits` holds 48 digits,
// their terminator and count / 4 zeros, at most 32 of them.
static void check_common_shift_digits(unsigned __int128 value, uint32_t count, char digits[48 + 32 + 1])
{
    const uint32_t nibble = count % 4u;
    uint64_t limbs[3] = { (uint64_t)value, (uint64_t)(value >> 64), 0 };
    limbs[2] = nibble != 0 ? limbs[1] >> (64u - nibble) : 0;
    limbs[1] = nibble != 0 ? limbs[1] << nibble | limbs[0] >> (64u - nibble) : limbs[1];
    limbs[0] = limbs[0] << nibble;
    uint32_t used = 0;
    bool leading = true;
    for (uint32_t i = 48; i != 0; i -= 1)
    {
        const uint32_t digit = (uint32_t)(limbs[(i - 1) / 16] >> (((i - 1) % 16) * 4)) & 0xfu;
        leading = leading && digit == 0 && i != 1;
        if (!leading)
        {
            digits[used] = "0123456789ABCDEF"[digit];
            used += 1;
        }
    }
    const uint32_t zeros = count / 4u < 32u ? count / 4u : 32u;
    memset(digits + used, '0', zeros);
    digits[used + zeros] = 0;
}

// -Wshift-sign-overflow (#2264, clang's DiagnoseBadShiftValues as it builds a shift): a `<<` of a signed left operand and
// a count that both fold, the left one nonnegative and the count below its promoted type's width, whose result needs
// exactly one bit more than that width, the sign bit's: "signed shift result (0x…) sets the sign bit of the shift
// expression's type ('T') and becomes negative" at the `<<`, given as the shift is built. Off by default. It is no
// runtime warning, so an unevaluated or unreachable shift gives it too; under -fwrapv (or -fno-strict-overflow) signed
// overflow is defined and clang gives none. A unit where nothing enables it skips the scan.
static void check_common_shift_sign_overflow(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                             CheckWarningSpelling* spell, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const uint8_t roles[] = { PARSE_ROLE_SHIFT };
    CheckCommonScan scan = { 0 };
    const PredefinesOptions* predefines = &pp->options.predefines;
    const bool wrapv = predefines->wrapv != 0 ? predefines->wrapv == BUSTER_PREDEFINES_ON : predefines->strict_overflow == BUSTER_PREDEFINES_OFF;
    const CheckGraph* graph = stream != NULL ? stream->graph : NULL;
    const bool enabled = !wrapv && graph != NULL && types != NULL && input->token_nodes != NULL &&
                         check_common_enabled(pp, input, CHECK_WARNING_ID_SHIFT_SIGN_OVERFLOW, true);
    for (uint32_t token = enabled ? check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector) : count; token < count;
         token = check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector))
    {
        const uint32_t node = parser->tokens.kinds[token] == PARSE_KIND_LESSLESS && token < input->token_count ? input->token_nodes[token] : BUSTER_CHECK_NONE;
        const bool shift = node < stream->count && graph->ops[node] == CHECK_OP_SHIFT_LEFT && graph->operand0[node] < stream->count &&
                           graph->operand1[node] < stream->count;
        const uint32_t left = shift ? graph->operand0[node] : BUSTER_CHECK_NONE;
        const uint32_t right = shift ? graph->operand1[node] : BUSTER_CHECK_NONE;
        const TypeId type = shift ? check_stream_type(stream, node) : 0;
        uint32_t width = 0;
        bool is_signed = false;
        uint32_t count_width = 0;
        bool count_signed = false;
        const bool folded = shift && check_action_integer_type(types, type, &width, &is_signed) && is_signed && width != 0 && width <= 128 &&
                            check_action_integer_type(types, check_stream_type(stream, right), &count_width, &count_signed) &&
                            count_width != 0 && count_width <= 128 &&
                            check_stream_constant_kind(stream, left) == CHECK_CONSTANT_INTEGER &&
                            check_stream_constant_kind(stream, right) == CHECK_CONSTANT_INTEGER;
        const unsigned __int128 value = folded ? check_stream_value(stream, left).integer & check_action_mask(width) : 0;
        const unsigned __int128 shifted = folded ? check_stream_value(stream, right).integer & check_action_mask(count_width) : 0;
        const bool count_negative = count_signed && count_width != 0 && ((shifted >> (count_width - 1)) & 1) != 0;
        const bool counted = folded && !count_negative && shifted < width && value != 0 && ((value >> (width - 1)) & 1) == 0;
        const uint32_t places = counted ? (uint32_t)shifted : 0;
        const uint32_t length = value == 0 ? 0 : (uint64_t)(value >> 64) != 0 ? 128u - (uint32_t)__builtin_clzll((uint64_t)(value >> 64)) :
                                                                               64u - (uint32_t)__builtin_clzll((uint64_t)value);
        // ResultBits = the left operand's significant bits (its length and a sign bit) plus the count: one past the width.
        if (counted && length + places == width)
        {
            const uint64_t mark = scratch->length;
            char digits[48 + 32 + 1];
            check_common_shift_digits(value, places, digits);
            const uint32_t spelling = check_stream_type(stream, left) == type ? check_input_operand_type_spelling(input, parser, left) : 0;
            const String8 name = check_driver_warning_type(parser, pp, input, types, spell, scratch, type, spelling);
            const char* message = name.length != 0 ?
                                  check_warning_text(scratch, "signed shift result (0x%s) sets the sign bit of the shift expression's type (%.*s) and becomes negative",
                                                     digits, (int)name.length, name.pointer) : NULL;
            if (message != NULL)
            {
                check_common_report_at(parser, pp, input, token, check_common_operand_end(parser, token), CHECK_WARNING_ID_SHIFT_SIGN_OVERFLOW, true, message, NULL, 0,
                                       scratch);
            }
            arena_pop_to(scratch, mark);
        }
    }
    // The constant reader's (CHECK_WARNING_EVENT_SHIFT_SIGN), but where the graph has the shift: an enumerator's, a bound's.
    for (uint32_t i = 0; enabled && i < input->warning_events.count && !pp->fatal; i += 1)
    {
        const CheckWarningEvent* event = input->warning_events.events + i;
        const uint32_t token = event->token;
        const uint32_t node = event->kind == CHECK_WARNING_EVENT_SHIFT_SIGN && token < input->token_count ? input->token_nodes[token] : BUSTER_CHECK_NONE;
        uint32_t width = 0;
        bool is_signed = false;
        if (event->kind == CHECK_WARNING_EVENT_SHIFT_SIGN && token < count && (node >= graph->count || graph->tokens[node] != token) &&
            check_action_integer_type(types, event->payload.first_type, &width, &is_signed) && width != 0 && width <= 128)
        {
            const uint64_t mark = scratch->length;
            const uint64_t value = (uint64_t)event->payload.token << 32 | event->payload.data;
            const uint32_t length = value != 0 ? 64u - (uint32_t)__builtin_clzll(value) : 0;
            char digits[48 + 32 + 1];
            check_common_shift_digits(value, width - length, digits);
            const String8 name = check_driver_warning_type(parser, pp, input, types, spell, scratch, event->payload.first_type, event->payload.name);
            const char* message = name.length != 0 ?
                                  check_warning_text(scratch, "signed shift result (0x%s) sets the sign bit of the shift expression's type (%.*s) and becomes negative",
                                                     digits, (int)name.length, name.pointer) : NULL;
            if (message != NULL)
            {
                check_common_report_at(parser, pp, input, token, check_common_operand_end(parser, token), CHECK_WARNING_ID_SHIFT_SIGN_OVERFLOW, true, message, NULL, 0,
                                       scratch);
            }
            arena_pop_to(scratch, mark);
        }
    }
}

// The type clang's getEnumCoercedType gives an operand: an enumerator's own enumeration where the operand names one
// (parentheses are no nodes) and that enumeration's definition is complete, past its closing brace, else its type.
static TypeId check_common_enum_coerced(const CheckInput* input, const CheckStream* stream, uint32_t node)
{
    const CheckGraph* graph = &input->graph;
    TypeId result = check_stream_type(stream, node);
    const uint32_t token = graph->tokens[node];
    const uint32_t declaration = graph->ops[node] == CHECK_OP_VALUE && token < input->token_count && input->declarations != NULL ?
                                 input->declarations[token] : BUSTER_CHECK_NONE;
    if (declaration < input->token_count && input->declaration_kinds[declaration] == C_BINDING_ENUMERATOR)
    {
        // The definitions are in their `enum` keywords' order, which is their braces' but for one nested in a C23 fixed
        // underlying type (`enum E : typeof(sizeof(enum F { x })) { e }`). The last that opens before the declaration holds
        // it, but for that one and one nested in an enumerator's value (`sizeof(enum I { x })`), where every definition is
        // searched.
        uint32_t low = 0;
        uint32_t high = input->enum_definition_count;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            const bool before = input->enum_definitions[middle].open < declaration;
            low = before ? middle + 1 : low;
            high = before ? high : middle;
        }
        const CheckInputEnumDefinition* nearest = low != 0 ? input->enum_definitions + low - 1 : NULL;
        const bool held = nearest != NULL && declaration < nearest->close;
        result = held && token > nearest->close ? nearest->type : result;
        // An inner definition can have closed before the enumerator of an outer definition.
        // Reduce initialized indices, not TypeId payloads from inactive vector lanes (Clang 23 MSan).
        uint32_t selected = 0;
        for (uint32_t i = 0; !held && i < input->enum_definition_count; i += 1)
        {
            const CheckInputEnumDefinition* definition = input->enum_definitions + i;
            const uint32_t candidate = definition->open < declaration && declaration < definition->close && token > definition->close ? i + 1 : 0;
            selected = candidate > selected ? candidate : selected;
        }
        result = selected != 0 ? input->enum_definitions[selected - 1].type : result;
    }
    return result;
}

// Whether `type` is clang's isFloatingType: a real or complex floating type.
static bool check_common_floating_kind(const TypeTable* types, TypeId type)
{
    const uint8_t kind = check_action_kind(types, type);
    return kind == BUSTER_TYPE_KIND_FLOATING ||
           (kind == BUSTER_TYPE_KIND_COMPLEX && check_action_kind(types, type_element(types, type_strip_aligned(types, type))) == BUSTER_TYPE_KIND_FLOATING);
}

// A conditional's place, clang's: its `?`, the `:`'s child. A GNU `a ?: b`'s `?` and `:` are adjacent.
static uint32_t check_common_enum_question(const Parser* parser, uint32_t colon, bool conditional)
{
    uint32_t question = colon;
    for (uint32_t at = conditional ? colon : 0; at != 0 && question == colon;)
    {
        at -= 1;
        question = parser->tree.parents[at] == colon && parser->tree.roles[at] == PARSE_ROLE_OPEN_TERNARY ? at : question;
    }
    return question;
}

// One operator's enumeration mix (clang's checkEnumArithmeticConversions), reported at `question` (a conditional's `?`):
// `kind` its message's (0 arithmetic, 1 bitwise, 2 comparison, 3 conditional, 4 compound assignment), `omitted` a GNU
// `a ?: b`'s, whose omitted middle operand is its condition promoted (clang's UsualUnaryConversions): an enumeration's is
// an integer then, and a floating one an rvalue. Each operand's type is as getEnumCoercedType gives it
// (check_common_enum_coerced), with its typedef spelling where it is the operand's own (0 for none).
static void check_common_enum_mix(const Parser* parser, Preprocessor* pp, const CheckInput* input, TypeTable* types, CheckWarningSpelling* spell,
                                  Arena* scratch, uint32_t kind, uint32_t question, bool omitted, TypeId left_coerced, TypeId right_coerced,
                                  uint32_t left_spelling, uint32_t right_spelling)
{
    static const char* const kinds[] = { "arithmetic between", "bitwise operation between", "comparison of", "conditional expression between",
                                         "compound assignment of" };
    const bool compound = kind == 4;
    // A comparison's operands are rvalues first (clang's DefaultLvalueConversion): unqualified, a typedef that
    // carries a qualifier itself spelled as its type.
    const bool rvalue_left = kind == 2 || omitted;
    const bool rvalue_right = kind == 2;
    const TypeId left_type = rvalue_left ? type_unqualified(left_coerced) : left_coerced;
    const TypeId right_type = rvalue_right ? type_unqualified(right_coerced) : right_coerced;
    const bool left_enum = !omitted && check_action_kind(types, left_type) == BUSTER_TYPE_KIND_ENUM;
    const bool right_enum = check_action_kind(types, right_type) == BUSTER_TYPE_KIND_ENUM;
    const bool floating = (!compound && left_enum && check_common_floating_kind(types, right_type)) ||
                          (right_enum && check_common_floating_kind(types, left_type));
    const bool mixed = !floating && !compound && left_enum && right_enum &&
                       type_unqualified(type_strip_aligned(types, left_type)) != type_unqualified(type_strip_aligned(types, right_type));
    if (floating || mixed)
    {
        const TypeId left_alias = left_spelling < input->token_count && input->declared_types != NULL ? input->declared_types[left_spelling] : 0;
        const TypeId right_alias = right_spelling < input->token_count && input->declared_types != NULL ? input->declared_types[right_spelling] : 0;
        left_spelling = rvalue_left && left_alias != 0 && type_qualifiers(left_alias) != 0 ? 0 : left_spelling;
        right_spelling = rvalue_right && right_alias != 0 && type_qualifiers(right_alias) != 0 ? 0 : right_spelling;
        const String8 left_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, left_type, left_spelling);
        const String8 right_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, right_type, right_spelling);
        const String8 left_plain = mixed ? check_driver_warning_type(parser, pp, input, types, spell, scratch, left_type, 0) : (String8){ 0 };
        const String8 right_plain = mixed ? check_driver_warning_type(parser, pp, input, types, spell, scratch, right_type, 0) : (String8){ 0 };
        const bool anonymous = mixed && (memmem(left_plain.pointer, left_plain.length, "(unnamed", 8) != NULL ||
                                         memmem(right_plain.pointer, right_plain.length, "(unnamed", 8) != NULL);
        const uint32_t id = floating ? CHECK_WARNING_ID_ENUM_FLOAT_CONVERSION : anonymous ? CHECK_WARNING_ID_ANON_ENUM_ENUM_CONVERSION :
                            kind == 3 ? CHECK_WARNING_ID_ENUM_COMPARE_CONDITIONAL : kind == 2 ? CHECK_WARNING_ID_ENUM_COMPARE :
                            CHECK_WARNING_ID_ENUM_ENUM_CONVERSION;
        const char* message = floating ?
            check_warning_text(scratch, "%s %s type %.*s %s %s type %.*s", kinds[kind], left_enum ? "enumeration" : "floating-point",
                               (int)left_name.length, left_name.pointer, kind == 2 ? "with" : kind == 4 ? "from" : "and",
                               left_enum ? "floating-point" : "enumeration", (int)right_name.length, right_name.pointer) :
            check_warning_text(scratch, "%s different enumeration types (%.*s and %.*s)", kinds[kind], (int)left_name.length, left_name.pointer,
                               (int)right_name.length, right_name.pointer);
        if (left_name.length != 0 && right_name.length != 0 && message != NULL)
        {
            check_common_report(parser, pp, input, question, id, id != CHECK_WARNING_ID_ENUM_COMPARE, message, NULL, 0, scratch);
        }
    }
}

// Clang's checkEnumArithmeticConversions (#1447): the usual arithmetic conversions of an operator whose operands are
// enumerations of different types, or an enumeration and a floating type, warned as the operator is built, at it. The
// operands are as written, an enumerator as its enumeration (check_common_enum_coerced). A compound assignment warns only
// for an enumeration converted to its floating target. Only a unit where such an operator may mix enumerations is
// scanned: an enumerator named as its operand (CheckInput.enum_operands, an int to the actions), or an operand of an
// enumeration type in any form, a member, a call or a cast as well as a variable (CheckStream.enum_operands).
static __attribute__((noinline)) void check_common_enum_arithmetic(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, TypeTable* types,
                                         CheckWarningSpelling* spell, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const CheckOp ops[] =
    {
        CHECK_OP_ADD, CHECK_OP_SUBTRACT, CHECK_OP_MULTIPLY, CHECK_OP_DIVIDE, CHECK_OP_REMAINDER, CHECK_OP_LESS, CHECK_OP_LESS_EQUAL,
        CHECK_OP_GREATER, CHECK_OP_GREATER_EQUAL, CHECK_OP_EQUAL, CHECK_OP_NOT_EQUAL, CHECK_OP_BIT_AND, CHECK_OP_BIT_OR, CHECK_OP_BIT_XOR,
        CHECK_OP_CONDITIONAL, CHECK_OP_ADD_ASSIGN, CHECK_OP_SUBTRACT_ASSIGN, CHECK_OP_MULTIPLY_ASSIGN, CHECK_OP_DIVIDE_ASSIGN,
        CHECK_OP_REMAINDER_ASSIGN, CHECK_OP_BIT_AND_ASSIGN, CHECK_OP_BIT_OR_ASSIGN, CHECK_OP_BIT_XOR_ASSIGN,
    };
    const CheckCommonOps set = check_common_ops(ops, BUSTER_ARRAY_LENGTH(ops));
    CheckCommonScan scan = { 0 };
    for (uint32_t node = input->enum_operands || stream->enum_operands ? check_common_next_op(graph, &set, &scan, vector) : graph->count; node < graph->count && !pp->fatal;
         node = check_common_next_op(graph, &set, &scan, vector))
    {
        const CheckOp op = graph->ops[node];
        const bool compound = (op >= CHECK_OP_ADD_ASSIGN && op <= CHECK_OP_REMAINDER_ASSIGN) || op == CHECK_OP_BIT_AND_ASSIGN ||
                              op == CHECK_OP_BIT_OR_ASSIGN || op == CHECK_OP_BIT_XOR_ASSIGN;
        const uint32_t kind = op >= CHECK_OP_ADD && op <= CHECK_OP_REMAINDER ? 0 : op >= CHECK_OP_BIT_AND && op <= CHECK_OP_BIT_XOR ? 1 :
                              op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL ? 2 : op == CHECK_OP_CONDITIONAL ? 3 : compound ? 4 : 5;
        const uint32_t left = op == CHECK_OP_CONDITIONAL ? graph->operand1[node] : graph->operand0[node];
        const uint32_t right = op == CHECK_OP_CONDITIONAL ? graph->operand2[node] : graph->operand1[node];
        // The fast rejection: a mix needs an operand that may be an enumeration's, by its own kind (an aligned typedef's
        // hides its type's), or a named constant leaf, which may be an enumerator.
        bool enumish = false;
        for (uint32_t i = 0; i < 2; i += 1)
        {
            const uint32_t operand = i == 0 ? left : right;
            const uint8_t operand_kind = operand < graph->count ? type_kind(types, check_stream_type(stream, operand)) : BUSTER_TYPE_KIND_VOID;
            enumish |= operand_kind == BUSTER_TYPE_KIND_ENUM || operand_kind == BUSTER_TYPE_KIND_ALIGNED ||
                       (operand < graph->count && graph->ops[operand] == CHECK_OP_VALUE && (check_stream_flags(stream, operand) & BUSTER_CHECK_ICE) != 0 &&
                        graph->tokens[operand] < parser->tokens.count && parser->tokens.kinds[graph->tokens[operand]] == PARSE_KIND_IDENTIFIER);
        }
        if (kind < 5 && left < graph->count && right < graph->count && enumish && stream->diagnostics[node] == CHECK_DIAG_NONE)
        {
            const uint32_t question = check_common_enum_question(parser, graph->tokens[node], op == CHECK_OP_CONDITIONAL);
            const TypeId left_coerced = check_common_enum_coerced(input, stream, left);
            const TypeId right_coerced = check_common_enum_coerced(input, stream, right);
            const uint32_t left_spelling = left_coerced == check_stream_type(stream, left) ? check_common_operand_spelling(parser, input, graph, stream, types, left) : 0;
            const uint32_t right_spelling = right_coerced == check_stream_type(stream, right) ? check_common_operand_spelling(parser, input, graph, stream, types, right) : 0;
            check_common_enum_mix(parser, pp, input, types, spell, scratch, kind, question, op == CHECK_OP_CONDITIONAL && question + 1 == graph->tokens[node],
                                  left_coerced, right_coerced, left_spelling, right_spelling);
        }
    }
    // The constant reader's mixes (CHECK_WARNING_EVENT_ENUM_MIX), but where the graph has the operator: a
    // _Static_assert's, an initializer's.
    for (uint32_t i = 0; i < input->warning_events.count && !pp->fatal; i += 1)
    {
        const CheckWarningEvent* event = input->warning_events.events + i;
        const uint32_t token = event->token;
        const uint32_t node = event->kind == CHECK_WARNING_EVENT_ENUM_MIX && token < input->token_count && input->token_nodes != NULL ?
                              input->token_nodes[token] : BUSTER_CHECK_NONE;
        if (event->kind == CHECK_WARNING_EVENT_ENUM_MIX && token < parser->tokens.count && (node >= graph->count || graph->tokens[node] != token))
        {
            const uint8_t role = parser->tree.roles[token];
            const bool conditional = role == PARSE_ROLE_CLOSE_TERNARY;
            const uint32_t kind = conditional ? 3 : role == PARSE_ROLE_RELATIONAL || role == PARSE_ROLE_EQUALITY ? 2 :
                                  role == PARSE_ROLE_BITWISE_AND || role == PARSE_ROLE_BITWISE_OR || role == PARSE_ROLE_BITWISE_XOR ? 1 : 0;
            const uint32_t question = check_common_enum_question(parser, token, conditional);
            check_common_enum_mix(parser, pp, input, types, spell, scratch, kind, question, conditional && question + 1 == token, event->payload.first_type,
                                  event->payload.second_type, event->payload.name, event->payload.data);
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

// Where the full expression holding `node` completes: the exclusive end (CheckGraph.source_positions) of its root up
// `parents`, or of the first initialization on the way, since each declarator's initializer is a full expression of its
// own while its declaration's discard wrapper spans them all; the node's own token where the graph keeps no ends. A
// conversion's warning is located at its constant, but clang's Sema::CheckCompletedExpr checks an expression's implicit
// conversions there, in its own order (AnalyzeImplicitConversions: a call's last argument first, an assignment before its
// operands), which publishing them all at this one boundary keeps. Only a reported warning asks: the climb is its cost.
static uint32_t check_common_full_expression_end(const Parser* parser, const CheckGraph* graph, const uint32_t* parents, uint32_t node)
{
    uint32_t root = node;
    for (uint32_t steps = 0; parents != NULL && root < graph->count && graph->ops[root] != CHECK_OP_INITIALIZE && parents[root] < graph->count &&
         steps < graph->count; steps += 1)
    {
        root = parents[root];
    }
    const uint32_t end = graph->source_positions != NULL && root < graph->count ? graph->source_positions[root] : BUSTER_CHECK_NONE;
    const uint32_t result = end <= parser->tokens.count ? end : graph->tokens[node];
    return result;
}

// An expression clang's DiagnoseIntInBoolContext reads (#1447) once its implicit conversions are passed: the graph's
// CHECK_OP_CONVERT nodes, which only implicit conversions make.
static uint32_t check_common_bool_expression(const CheckGraph* graph, uint32_t node)
{
    uint32_t result = node;
    for (uint32_t steps = 0; result < graph->count && graph->ops[result] == CHECK_OP_CONVERT && steps < graph->count; steps += 1)
    {
        result = graph->operand0[result];
    }
    return result;
}

// Whether `node` is clang's getIntegerLiteral of DiagnoseIntInBoolContext: an integer literal, or one under a unary
// minus, neither parenthesized (the parser's group closer is no parent of either) nor a character constant.
static bool check_common_bool_literal(const Parser* parser, const CheckGraph* graph, const CheckStream* stream, uint32_t node)
{
    const bool negative = node < graph->count && graph->ops[node] == CHECK_OP_NEGATIVE;
    const uint32_t literal = negative ? graph->operand0[node] : node;
    const uint32_t token = literal < graph->count ? graph->tokens[literal] : BUSTER_CHECK_NONE;
    const uint32_t sign = negative ? graph->tokens[node] : token;
    const bool result = token < parser->tokens.count && sign < parser->tokens.count && graph->ops[literal] == CHECK_OP_VALUE &&
                        parser->tokens.kinds[token] == PARSE_KIND_NUMBER && check_common_integer_constant(stream, literal) &&
                        parser->tree.roles[parser->tree.parents[token] < parser->tokens.count ? parser->tree.parents[token] : token] != PARSE_ROLE_CLOSE_GROUP &&
                        parser->tree.roles[parser->tree.parents[sign] < parser->tokens.count ? parser->tree.parents[sign] : sign] != PARSE_ROLE_CLOSE_GROUP;
    return result;
}

// A literal's own value, its sign aside (getIntegerLiteral's IntegerLiteral under a unary minus).
static bool check_common_bool_literal_zero(const CheckGraph* graph, const CheckStream* stream, uint32_t node)
{
    const uint32_t literal = graph->ops[node] == CHECK_OP_NEGATIVE ? graph->operand0[node] : node;
    TypeId type = 0;
    return check_common_stream_integer(stream, literal, &type) == 0;
}

// clang's evaluation of a `<<` of two literals (EvaluateAsInt in C, which notes but survives undefined shifts): a count
// past the width shifts by the width less one, and a negative one shifts right by its magnitude, in the shift's type.
static bool check_common_bool_shift_value(const TypeTable* types, const CheckStream* stream, uint32_t node, uint32_t left, uint32_t right)
{
    uint32_t width = 0;
    bool is_signed = false;
    TypeId left_type = 0;
    TypeId right_type = 0;
    uint32_t right_width = 0;
    bool right_signed = false;
    check_common_integer(types, check_stream_type(stream, node), &width, &is_signed);
    check_common_integer(types, check_stream_type(stream, right), &right_width, &right_signed);
    const __int128 value = check_common_integer_bits(check_common_stream_integer(stream, left, &left_type), width, is_signed);
    const __int128 count = check_common_integer_bits(check_common_stream_integer(stream, right, &right_type), right_width, right_signed);
    const unsigned __int128 limit = width != 0 ? width - 1 : 0;
    const unsigned __int128 magnitude = count < 0 ? (unsigned __int128)(-count) : (unsigned __int128)count;
    const uint32_t amount = (uint32_t)(magnitude < limit ? magnitude : limit);
    const unsigned __int128 shifted = count < 0 ? (unsigned __int128)(value >> amount) : (unsigned __int128)value << amount;
    return width != 0 && (shifted & check_action_mask(width)) != 0;
}

// Where a conditional begins, clang's getExprLoc of one: its condition's first token, through the parentheses around it.
static uint32_t check_common_conditional_begin(const Parser* parser, const CheckGraph* graph, uint32_t node)
{
    const uint32_t condition = graph->operand0[node];
    uint32_t token = condition < graph->count ? graph->tokens[condition] : graph->tokens[node];
    bool climbing = true;
    for (uint32_t steps = 0; climbing && token < parser->tokens.count && steps < parser->tokens.count; steps += 1)
    {
        const uint32_t parent = parser->tree.parents[token];
        climbing = parent < parser->tokens.count && parent != token && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_GROUP && parent < graph->tokens[node];
        token = climbing ? parent : token;
    }
    return token < parser->tokens.count ? check_common_subtree_start(parser, token) : graph->tokens[node];
}

// clang's DiagnoseIntInBoolContext of `node` (#1447): converted to _Bool, a `<<` of a literal 0, or of two literals,
// always evaluates to one truth (-Wtautological-constant-compare), and any other signed one is -Wint-in-bool-context's;
// a conditional of two integer literal arms, not both 0 or 1 and both nonzero, always evaluates to true. Clang's Diag
// reports it at once, where its full expression completes (a literal conversion's place), in unreachable code too.
static void check_common_int_in_bool(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                     uint32_t node, const uint32_t* parents, uint32_t at_once, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t expression = check_common_bool_expression(graph, node);
    const CheckOp op = expression < graph->count ? graph->ops[expression] : CHECK_OP_COUNT;
    uint32_t token = BUSTER_CHECK_NONE;
    uint32_t id = CHECK_WARNING_ID_TAUTOLOGICAL_CONSTANT_COMPARE;
    const char* message = NULL;
    if (op == CHECK_OP_SHIFT_LEFT)
    {
        const uint32_t left = graph->operand0[expression];
        const uint32_t right = graph->operand1[expression];
        const bool left_literal = left < graph->count && check_common_bool_literal(parser, graph, stream, left);
        const bool right_literal = right < graph->count && check_common_bool_literal(parser, graph, stream, right);
        uint32_t width = 0;
        bool is_signed = false;
        token = graph->tokens[expression];
        if (left_literal && check_common_bool_literal_zero(graph, stream, left))
        {
            message = "converting the result of '<<' to a boolean always evaluates to false";
        }
        else if (left_literal && right_literal)
        {
            message = check_common_bool_shift_value(types, stream, expression, left, right) ? "converting the result of '<<' to a boolean always evaluates to true" :
                                                                                              "converting the result of '<<' to a boolean always evaluates to false";
        }
        else if (check_common_integer(types, check_stream_type(stream, expression), &width, &is_signed) && is_signed)
        {
            id = CHECK_WARNING_ID_INT_IN_BOOL_CONTEXT;
            message = "converting the result of '<<' to a boolean; did you mean to compare with '0'?";
        }
    }
    else if (op == CHECK_OP_CONDITIONAL && graph->operand1[expression] != graph->operand0[expression])
    {
        // The arms as written: a literal an implicit conversion reaches (its type the conditional's) is clang's cast.
        const uint32_t arms[2] = { graph->operand1[expression], graph->operand2[expression] };
        const TypeId type = type_unqualified(type_strip_aligned(types, check_stream_type(stream, expression)));
        bool literals = true;
        bool zero = false;
        bool idiom = true;
        for (uint32_t i = 0; i < 2; i += 1)
        {
            const bool literal = arms[i] < graph->count && check_common_bool_literal(parser, graph, stream, arms[i]) &&
                                 type_unqualified(type_strip_aligned(types, check_stream_type(stream, arms[i]))) == type;
            literals &= literal;
            const uint32_t plain = literal && graph->ops[arms[i]] == CHECK_OP_NEGATIVE ? graph->operand0[arms[i]] : arms[i];
            TypeId arm_type = 0;
            const unsigned __int128 value = literal ? check_common_stream_integer(stream, plain, &arm_type) : 0;
            zero |= literal && value == 0;
            idiom &= literal && value <= 1;
        }
        if (literals && !idiom && !zero)
        {
            token = check_common_conditional_begin(parser, graph, expression);
            message = "converting the result of '?:' with integer constants to a boolean always evaluates to 'true'";
        }
    }
    if (message != NULL && token < parser->tokens.count)
    {
        const uint32_t publication = at_once != BUSTER_CHECK_NONE ? at_once : check_common_full_expression_end(parser, graph, parents, expression);
        check_common_report_evaluated(parser, pp, input, token, publication, PP_DIAGNOSTIC_DEFERRED, id, id == CHECK_WARNING_ID_INT_IN_BOOL_CONTEXT,
                                      message, NULL, 0, scratch);
    }
}

// Whether `node` is an operand DiagnoseIntInBoolContext may warn of (check_common_int_in_bool): a `<<` or a conditional,
// its implicit conversions passed. The walks' candidates test it before any type.
static bool check_common_bool_candidate(const CheckGraph* graph, uint32_t node)
{
    // (The operator alone answers for nearly every node; only an implicit conversion is passed.)
    const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    const uint32_t expression = op == CHECK_OP_CONVERT ? check_common_bool_expression(graph, node) : node;
    const CheckOp inner = op == CHECK_OP_CONVERT && expression < graph->count ? graph->ops[expression] : op;
    return inner == CHECK_OP_SHIFT_LEFT || inner == CHECK_OP_CONDITIONAL;
}

// Whether `target` is _Bool, the conversion DiagnoseIntInBoolContext follows.
static bool check_common_bool_target(const TypeTable* types, TypeId target)
{
    return type_unqualified(type_strip_aligned(types, target)) == BUSTER_TYPE_BOOL;
}

// Whether an integer constant fills `target`'s width exactly (#1447, clang's isSameWidthConstantConversion): a signed
// source's non-negative value of as many bits as the signed target has, which the target holds as a negative one.
static bool check_common_same_width(const TypeTable* types, const CheckValue* value, TypeId target)
{
    uint32_t source_width = 0;
    uint32_t target_width = 0;
    bool source_signed = false;
    bool target_signed = false;
    bool result = false;
    const bool narrow = check_common_integer(types, value->type, &source_width, &source_signed) && check_common_integer(types, target, &target_width, &target_signed);
    if (narrow && source_signed && target_signed && target_width != 0 && target_width < 128 && target_width < source_width)
    {
        const __int128 original = check_common_integer_bits(value->integer, source_width, source_signed);
        result = original >= ((__int128)1 << (target_width - 1)) && original < ((__int128)1 << target_width);
    }
    else if (!narrow && check_common_integer_kind_wide(types, value->type, target))
    {
        // A _BitInt past 128 bits on either side (#1447).
        result = check_wide_same_width(types, value, target);
    }
    return result;
}

// Whether `type` is a character type, plain, signed or unsigned char: clang's isCharType.
static bool check_common_character_type(const TypeTable* types, TypeId type)
{
    const TypeId bare = type_unqualified(type_strip_aligned(types, type));
    return bare == BUSTER_TYPE_CHAR || bare == BUSTER_TYPE_SCHAR || bare == BUSTER_TYPE_UCHAR;
}

// A value-changing implicit conversion of a constant, shared by initializers, assignments, return conversions, a call's
// per-argument assignment and a truth context's: the warning clang spells at the constant's own token. An integer one is
// clang's DiagRuntimeBehavior's, deferred to its function's end; at file scope clang delays it to its declarator's end,
// after that initializer's literal conversions and its shift counts (PP_DIAGNOSTIC_RUNTIME at the same boundary, #1447). A floating literal's is clang's
// Diag's (DiagnoseFloatingImpCast, CheckBoolLikeConversion), at once, as its full expression completes: `at_once`, or the
// end check_common_full_expression_end finds up `parents`. Its phase there, after the lexer's lookahead, puts it before
// whatever the next statement or declarator reports from that token. Where either may report is unchanged.
// Whether `target` takes clang's implicit-conversion value warnings (#1447, review on #1453). CheckImplicitConversion gives
// -Wliteral-conversion a builtin target alone (DiagnoseFloatingImpCast reads a BuiltinType), so not an enumeration, whose
// integer type check_common_integer reads, but -Wconstant-conversion an enumeration too, by its integer type's range. An
// _Atomic type takes neither. (An _Atomic destination's conversion is to its value type: BUSTER_CHECK_CONVERT_ATOMIC.)
static bool check_common_conversion_target(const TypeTable* types, TypeId target, bool floating)
{
    const TypeId bare = target != 0 ? type_unqualified(type_strip_aligned(types, target)) : 0;
    const uint8_t kind = bare != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_VOID;
    const bool result = bare != 0 && kind != BUSTER_TYPE_KIND_ATOMIC && (!floating || kind != BUSTER_TYPE_KIND_ENUM);
    return result;
}

static void check_common_conversion_leaf(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                            TypeTable* types, CheckWarningSpelling* spell, uint32_t source, const CheckValue* value,
                                            TypeId target, const uint32_t* parents, uint32_t at_once, uint8_t leaves, Arena* scratch)
{
    __int128 converted = 0;
    // An assignment's or an increment's value, which the stream may hold as its stored constant, is none to clang's
    // evaluator (#1447: `char c = x = 1000;`, `{ x = 300 }`), so its conversion warns of nothing.
    const CheckOp op = source < input->graph.count ? input->graph.ops[source] : CHECK_OP_COUNT;
    const bool written = op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN);
    const bool plain = !written && check_common_conversion_target(types, target, false);
    const bool width = plain && value->constant_kind == CHECK_CONSTANT_INTEGER && check_common_same_width(types, value, target);
    const uint8_t kind = width ? BUSTER_CHECK_LEAVES_WIDTH : BUSTER_CHECK_LEAVES_INTEGER;
    const bool listed_character = width && (leaves & BUSTER_CHECK_LEAVES_LISTED) != 0 && check_common_character_type(types, target);
    const bool candidate = plain && value->constant_kind == CHECK_CONSTANT_INTEGER && (leaves & kind) != 0 && !listed_character;
    // A _BitInt past 128 bits on either side (#1447, c_check_wide.c): its own rules' twin and its decimal digits.
    bool wide = false;
    const bool wide_changes = candidate && check_common_integer_kind_wide(types, value->type, target) && check_wide_conversion_changes(types, value, target, &wide);
    if (candidate && (wide ? wide_changes : check_common_conversion_changes(types, value->type, target, value->integer, &converted)) &&
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
            const char* original_text = original;
            const char* changed_text = changed;
            if (wide)
            {
                CheckValue result = *value;
                check_action_convert_value(types, &result, target, source);
                original_text = check_wide_text(types, value, scratch);
                changed_text = check_wide_text(types, &result, scratch);
            }
            const char* message = original_text == NULL || changed_text == NULL ? NULL :
                                  check_warning_text(scratch, "implicit conversion from %.*s to %.*s changes value from %s to %s",
                                                     (int)source_name.length, source_name.pointer, (int)target_name.length, target_name.pointer,
                                                     original_text, changed_text);
            const CheckGraph* graph = &input->graph;
            const uint32_t condition = graph->ops[source] == CHECK_OP_CONDITIONAL ? graph->operand0[source] : BUSTER_CHECK_NONE;
            const uint32_t token = condition < graph->count ? graph->tokens[condition] : graph->tokens[source];
            // A same-width conversion is clang's Diag, at once, as its full expression completes (a literal's place).
            const bool body = !width && check_common_function_end(parser, token) != token;
            const uint32_t publication = body ? check_common_function_boundary(parser, token) :
                                         at_once != BUSTER_CHECK_NONE ? at_once : check_common_full_expression_end(parser, graph, parents, source);
            check_common_report_evaluated(parser, pp, input, token, publication, body || width ? PP_DIAGNOSTIC_DEFERRED : PP_DIAGNOSTIC_RUNTIME,
                                        CHECK_WARNING_ID_CONSTANT_CONVERSION, false, message, NULL, 0, scratch);
        }
    }
    else if (value->constant_kind == CHECK_CONSTANT_FLOATING && (leaves & BUSTER_CHECK_LEAVES_FLOATING) != 0 &&
             check_common_conversion_target(types, target, true))
    {
        const CheckGraph* graph = &input->graph;
        uint32_t width = 0;
        bool is_signed = false;
        // The default warning is syntactic: a literal through parentheses and at most one unary sign. An explicit
        // cast, arithmetic constant expression or folded call belongs to other, opt-in floating-conversion groups.
        if (check_common_floating_literal(parser, graph, source) && check_common_integer(types, target, &width, &is_signed))
        {
            CheckValue changed = *value;
            changed.flags &= ~BUSTER_CHECK_GNU_FLOAT_RANGE;
            check_action_convert_value(types, &changed, target, source);
            const bool out_of_range = (changed.flags & BUSTER_CHECK_GNU_FLOAT_RANGE) != 0;
            const __int128 target_integer = check_common_integer_value(&changed, width, is_signed);
            const long double target_float = is_signed ? (long double)target_integer : (long double)changed.integer;
            // A __float128's value is its bits' (c_check_quad.c), which its mirror may round. A -0 changes too, as
            // clang takes its sign (its mirror's is the bits').
            const bool quad = type_unqualified(type_strip_aligned(types, value->type)) == BUSTER_TYPE_FLOAT128;
            const bool negative_zero = value->floating == 0 && signbit(value->floating) != 0;
            const bool different = negative_zero || (quad ? check_common_quad_changed(value->integer, target_integer, changed.integer, is_signed) :
                                                            value->floating != target_float);
            if (out_of_range || different)
            {
                const String8 source_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, value->type, 0);
                const String8 target_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, type_unqualified(target), 0);
                const char* message = NULL;
                if (out_of_range && source_name.length != 0 && target_name.length != 0)
                {
                    message = check_warning_text(scratch, "implicit conversion of out of range value from %.*s to %.*s is undefined",
                                                 (int)source_name.length, source_name.pointer, (int)target_name.length, target_name.pointer);
                }
                else if (source_name.length != 0 && target_name.length != 0)
                {
                    const TypeId source_type = type_unqualified(type_strip_aligned(types, value->type));
                    const uint32_t bits = source_type == BUSTER_TYPE_BF16 ? 8 : source_type == BUSTER_TYPE_HALF || source_type == BUSTER_TYPE_FLOAT16 ? 11 :
                                          source_type == BUSTER_TYPE_FLOAT ? 24 : source_type == BUSTER_TYPE_DOUBLE ? 53 : source_type == BUSTER_TYPE_FLOAT128 ? 113 : 64;
                    char original[96];
                    char result[48];
                    if (source_type == BUSTER_TYPE_FLOAT128)
                    {
                        check_common_quad_number(original, sizeof(original), value->integer, (bits * 59 + 195) / 196);
                    }
                    else
                    {
                        check_common_float_number(original, sizeof(original), value->floating, (bits * 59 + 195) / 196);
                    }
                    check_common_number(result, sizeof(result), target_integer);
                    const bool boolean = type_unqualified(type_strip_aligned(types, target)) == BUSTER_TYPE_BOOL;
                    const char* converted_text = boolean ? (changed.integer != 0 ? "true" : "false") : result;
                    message = check_warning_text(scratch, "implicit conversion from %.*s to %.*s changes value from %s to %s",
                                                 (int)source_name.length, source_name.pointer, (int)target_name.length, target_name.pointer, original, converted_text);
                }
                const uint32_t publication = at_once != BUSTER_CHECK_NONE ? at_once : check_common_full_expression_end(parser, graph, parents, source);
                check_common_report_evaluated(parser, pp, input, graph->tokens[source], publication, PP_DIAGNOSTIC_DEFERRED,
                                            CHECK_WARNING_ID_LITERAL_CONVERSION, false, message, NULL, 0, scratch);
            }
        }
    }
}

// Carry an enclosing conversion through conditional arms in source order and through the result of a comma.
// Integer conversion warnings keep their control-flow pruning; literal floating conversions inspect both arms.
// Only these branching expressions use a worklist, with a small local frontier and bounded arena growth.

static void check_common_conversion_warning_at(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                                TypeTable* types, CheckWarningSpelling* spell, uint32_t source, const CheckValue* value,
                                                TypeId target, const uint32_t* parents, uint32_t at_once, bool evaluated, uint8_t leaves,
                                                Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t graph_count = graph->count;
    uint32_t target_width = 0;
    bool target_signed = false;
    const bool boolean = (leaves & BUSTER_CHECK_LEAVES_BOOL) != 0 && check_common_bool_target(types, target);
    if (check_common_conversion_arms(graph, source) && check_common_integer(types, target, &target_width, &target_signed))
    {
        uint64_t local[16];
        uint64_t* work = local;
        uint32_t capacity = BUSTER_ARRAY_LENGTH(local);
        uint32_t count = 1;
        work[0] = (uint64_t)evaluated << 32 | source;
        while (count != 0 && !pp->fatal)
        {
            const uint64_t item = work[--count];
            const uint32_t node = (uint32_t)item;
            const bool reached = (item >> 32) != 0;
            const CheckOp op = graph->ops[node];
            uint32_t children[2] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
            bool selected[2] = { reached, reached };
            if (op == CHECK_OP_CONDITIONAL)
            {
                if (boolean)
                {
                    check_common_int_in_bool(parser, pp, input, stream, types, node, parents, at_once, scratch);
                }
                children[0] = graph->operand2[node];
                children[1] = graph->operand1[node];
                const uint32_t condition = graph->operand0[node];
                const CheckValue condition_value = condition < graph_count ? check_stream_value(stream, condition) : (CheckValue){ 0 };
                bool truth = false;
                const bool known = condition < graph_count && check_action_truth(types, &condition_value, &truth);
                selected[0] &= !known || !truth;
                selected[1] &= !known || truth;
            }
            else
            {
                // Clang checks a root comma's result as one expression. A comma in a conditional arm is
                // itself that arm's expression. Neither case recursively distributes its outer conversion.
                const uint32_t leaf_node = op == CHECK_OP_COMMA && node == source ? graph->operand1[node] : node;
                if (leaf_node < graph_count && boolean)
                {
                    check_common_int_in_bool(parser, pp, input, stream, types, leaf_node, parents, at_once, scratch);
                }
                if (leaf_node < graph_count)
                {
                    const CheckValue leaf = check_stream_value(stream, leaf_node);
                    if (reached || leaf.constant_kind == CHECK_CONSTANT_FLOATING)
                    {
                        check_common_conversion_leaf(parser, pp, input, stream, types, spell, leaf_node, &leaf, target, parents, at_once, leaves, scratch);
                    }
                }
            }
            for (uint32_t child = 0; child < BUSTER_ARRAY_LENGTH(children) && !pp->fatal; child += 1)
            {
                if (children[child] < graph_count)
                {
                    if (count == capacity)
                    {
                        const uint64_t grown = (uint64_t)capacity * 2;
                        uint64_t* next = grown <= UINT32_MAX ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, grown) : NULL;
                        if (next == NULL)
                        {
                            check_common_exhausted(pp);
                        }
                        else
                        {
                            memcpy(next, work, (uint64_t)count * sizeof(uint64_t));
                            work = next;
                            capacity = (uint32_t)grown;
                        }
                    }
                    if (!pp->fatal)
                    {
                        work[count++] = (uint64_t)selected[child] << 32 | children[child];
                    }
                }
            }
        }
    }
    else
    {
        if (boolean)
        {
            check_common_int_in_bool(parser, pp, input, stream, types, source, parents, at_once, scratch);
        }
        if (evaluated || value->constant_kind == CHECK_CONSTANT_FLOATING)
        {
            check_common_conversion_leaf(parser, pp, input, stream, types, spell, source, value, target, parents, at_once, leaves, scratch);
        }
    }
}

static void check_common_conversion_warning(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                            TypeTable* types, CheckWarningSpelling* spell, uint32_t source, const CheckValue* value,
                                            TypeId target, bool evaluated, const uint32_t* parents, uint32_t at_once, Arena* scratch)
{
    check_common_conversion_warning_at(parser, pp, input, stream, types, spell, source, value, target, parents, at_once, evaluated,
                                       BUSTER_CHECK_LEAVES_ALL, scratch);
}

// A brace initializer's part whose conversion reports at once (check_common_initializer_conversions): a floating literal's,
// an integer constant's that fills a non-character target's width exactly (#1447, check_common_same_width), or a
// conditional's or a comma's, whose arms carry either.
static bool check_common_initializer_bitfield(const CheckInput* input, const CheckStream* stream, const TypeTable* types, const CheckInitPart* part,
                                              __int128* original, __int128* truncated);

static bool check_common_initializer_literal(const Parser* parser, const CheckInput* input, const CheckStream* stream,
                                              const TypeTable* types, const CheckInitPart* part)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool target = part->kind == CHECK_INIT_VALUE && part->node < stream->count && check_common_integer(types, part->conversion.type, &width, &is_signed);
    const CheckValue value = target && check_common_integer_constant(stream, part->node) ? check_stream_value(stream, part->node) : (CheckValue){ 0 };
    __int128 original = 0;
    __int128 truncated = 0;
    return target &&
           (check_common_conversion_arms(&input->graph, part->node) || check_common_initializer_bitfield(input, stream, types, part, &original, &truncated) ||
            check_common_enum_cast_candidate(input->enum_names, parser, &input->graph, stream, types, part->node, part->conversion.type) ||
            (check_common_bool_target(types, part->conversion.type) && check_common_bool_candidate(&input->graph, part->node)) ||
            (type_kind(types, type_unqualified(type_strip_aligned(types, check_stream_type(stream, part->node)))) == BUSTER_TYPE_KIND_FLOATING &&
             check_common_floating_literal(parser, &input->graph, part->node)) ||
            (value.constant_kind == CHECK_CONSTANT_INTEGER && check_common_same_width(types, &value, part->conversion.type) &&
             !check_common_character_type(types, part->conversion.type)));
}

static bool check_common_constant_changes(const TypeTable* types, const CheckStream* stream, uint32_t source, TypeId target);
static bool check_common_bitfield_truncates(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, TypeId field, uint32_t bit_width,
                                            uint32_t source, __int128* original, __int128* truncated);

// A brace initializer's bit-field part whose constant its bit-field truncates (#1447, check_common_bitfield_truncates).
static bool check_common_initializer_bitfield(const CheckInput* input, const CheckStream* stream, const TypeTable* types, const CheckInitPart* part,
                                              __int128* original, __int128* truncated)
{
    const bool result = part->kind == CHECK_INIT_VALUE && part->bit_width != 0 && part->node < stream->count &&
                        (part->conversion.flags & BUSTER_CHECK_CONVERT_ATOMIC) == 0 &&
                        check_common_bitfield_truncates(&input->graph, stream, types, part->conversion.type, part->bit_width, part->node, original, truncated);
    return result;
}

// An integer constant's value-changing conversion in a brace initializer's part (#1447, -Wconstant-conversion), or a
// conditional's or a comma's, whose arms carry the conversion (check_common_conversion_warning_at): the walk's leaf
// tests the value itself.
static bool check_common_initializer_integer(const CheckInput* input, const CheckStream* stream, const TypeTable* types, const CheckInitPart* part)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool target = part->kind == CHECK_INIT_VALUE && part->node < stream->count && check_common_integer(types, part->conversion.type, &width, &is_signed);
    const CheckValue value = target && check_common_integer_constant(stream, part->node) ? check_stream_value(stream, part->node) : (CheckValue){ 0 };
    return target && (check_common_conversion_arms(&input->graph, part->node) ||
                      (value.constant_kind == CHECK_CONSTANT_INTEGER && !check_common_same_width(types, &value, part->conversion.type) &&
                       check_common_constant_changes(types, stream, part->node, part->conversion.type)));
}

// An integer plan key's mark (its low half's top bit, above any plan's index): the plan is a compound literal's.
#define BUSTER_CHECK_INITIALIZER_INNER 0x80000000u

// The brace initializers' plans whose conversions the walk reports (check_common_initializer_roots): those with a literal
// conversion by their first graph node, those with an integer constant's by their last value node.
typedef struct CheckCommonInitializerKeys CheckCommonInitializerKeys;
struct CheckCommonInitializerKeys
{
    const uint64_t* literal;
    uint32_t literal_count;
    const uint64_t* integer;
    uint32_t integer_count;
};

// Brace initializers keep their conversions in plans rather than graph operators. Retain one sparse key for each
// plan that has a literal conversion, ordered by its first graph root. The walk visits that plan's elements in
// reverse initializer order at that point, preserving the declaration's position among the surrounding roots.
// A plan with an integer constant's conversion (#1447) takes a second key, its last value node: clang's
// DiagRuntimeBehavior queues those at the full expression's end, after the runtime warnings its values queued as they
// were built, so the walk reports them, in the same reverse order, once its roots past the plan's values begin.
// Scalar '=' initializers already have a conversion operator and are omitted.
static CheckCommonInitializerKeys check_common_initializer_roots(const Parser* parser, Preprocessor* pp, const CheckInput* input,
                                                                 const CheckStream* stream, const TypeTable* types, const CheckProgram* program,
                                                                 const uint8_t* evaluated, bool* source_contexts, Arena* scratch)
{
    const CheckInitializers* initializers = program != NULL ? program->initializers : NULL;
    uint32_t count = 0;
    uint32_t integer_count = 0;
    uint64_t* keys = NULL;
    uint64_t* temporary = NULL;
    uint64_t* integer_keys = NULL;
    uint64_t* integer_temporary = NULL;
    // One pass over the plans; only one with a floating value part (CheckInitializer.floating_values) or an integer
    // constant that may change (CheckInitializer.changed_integers) has its parts read, and the first that converts one
    // takes room for every plan after it.
    for (uint32_t i = 0; initializers != NULL && i < initializers->count && !pp->fatal; i += 1)
    {
        const CheckInitializer* plan = initializers->plans + i;
        const uint32_t declaration = initializers->declaration_tokens[i];
        const bool scalar = !(plan->floating_values || plan->changed_integers || plan->enum_values) ||
                            (declaration < input->token_count && check_common_sparse_get(input->initializer_nodes, declaration) < input->graph.count);
        bool converted = false;
        bool integers = false;
        uint32_t first = BUSTER_CHECK_NONE;
        uint32_t last = 0;
        for (uint32_t p = 0; !scalar && p < plan->count; p += 1)
        {
            const CheckInitPart* part = plan->parts + p;
            const bool literal = check_common_initializer_literal(parser, input, stream, types, part);
            converted |= literal;
            integers |= plan->changed_integers && check_common_initializer_integer(input, stream, types, part);
            *source_contexts |= literal && evaluated != NULL && evaluated[part->node] == 0;
            first = part->kind == CHECK_INIT_VALUE && part->node < first ? part->node : first;
            last = part->kind == CHECK_INIT_VALUE && part->node < input->graph.count && part->node > last ? part->node : last;
        }
        if (converted && first < input->graph.count && keys == NULL)
        {
            const uint64_t capacity = (uint64_t)initializers->count - i;
            keys = CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, capacity);
            temporary = keys != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, capacity) : NULL;
            if (temporary == NULL)
            {
                check_common_exhausted(pp);
            }
        }
        if (converted && first < input->graph.count && temporary != NULL)
        {
            keys[count++] = (uint64_t)first << 32 | i;
        }
        if (integers && first < input->graph.count && integer_keys == NULL)
        {
            const uint64_t capacity = (uint64_t)initializers->count - i;
            integer_keys = CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, capacity);
            integer_temporary = integer_keys != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, capacity) : NULL;
            if (integer_temporary == NULL)
            {
                check_common_exhausted(pp);
            }
        }
        if (integers && first < input->graph.count && integer_temporary != NULL)
        {
            // A compound literal's plan is its full expression's, which clang's DiagRuntimeBehavior completes after the
            // expression's own runtime warnings: BUSTER_CHECK_INITIALIZER_INNER.
            const bool inner = declaration < input->token_count && check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND);
            integer_keys[integer_count++] = (uint64_t)last << 32 | i | (inner ? BUSTER_CHECK_INITIALIZER_INNER : 0u);
        }
    }
    const uint32_t varying = input->graph.count != 0 ? UINT32_MAX >> __builtin_clz(input->graph.count) : 0;
    const CheckCommonInitializerKeys result =
    {
        .literal = count != 0 ? c_bindings_sort(keys, temporary, count, !parser->options.scalar, varying) : NULL,
        .literal_count = count,
        .integer = integer_count != 0 ? c_bindings_sort(integer_keys, integer_temporary, integer_count, !parser->options.scalar, varying) : NULL,
        .integer_count = integer_count,
    };
    return result;
}

// Literal conversions are source checks even in unreachable statements and short-circuited operands. They still
// exclude sizeof/alignof operands and static selections' unchosen arms. Only a skipped conversion with a floating
// value or branching source builds this context, reusing visit bytes and keeping runtime reachability unchanged.
static bool check_common_literal_contexts(const CheckGraph* graph, const Parser* parser, const uint32_t* parents, uint8_t* emitted, Arena* scratch)
{
    const uint32_t graph_count = graph->count;
    for (uint32_t node = 0; node < graph_count; node += 1)
    {
        emitted[node] = parents[node] == BUSTER_CHECK_NONE && !check_common_query_suppressed(graph, node) ?
                        BUSTER_CHECK_EMITTED_LITERAL_CONTEXT : 0;
    }
    const bool valid = check_common_unselected_roots(graph, parser, emitted, emitted, 0, scratch);
    for (uint32_t node = graph_count; node-- > 0;)
    {
        if ((emitted[node] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) == 0)
        {
            continue;
        }
        const CheckOp op = graph->ops[node];
        const bool query = op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF;
        const uint32_t operand = graph->operand0[node];
        const bool bound = query && operand < graph_count && (graph->ops[operand] == CHECK_OP_VLA_BOUND || graph->ops[operand] == CHECK_OP_VLA_EXTENT);
        const uint32_t operands[] = { operand, graph->operand1[node], graph->operand2[node] };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
        {
            if (operands[i] < graph_count && (!query || (bound && i == 0)))
            {
                emitted[operands[i]] |= BUSTER_CHECK_EMITTED_LITERAL_CONTEXT;
            }
        }
        if (!query && check_graph_has_arguments(op) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
        {
            for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
            {
                const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
                if (argument < graph_count)
                {
                    emitted[argument] |= BUSTER_CHECK_EMITTED_LITERAL_CONTEXT;
                }
            }
        }
    }
    return valid;
}

// Whether constant `source`'s value changes in its implicit conversion to `target`: check_common_conversion_warning's test
// of the value, before the literal's spelling and the types' names, which only a warning reads. An integer conversion to
// an integer type at least as wide changes no value (check_common_conversion_changes: an unsigned source keeps its bits,
// a signed one its value, and a negative one fits the target's signed half), which the types alone say, so that only a
// narrowing conversion reads the value's row.
static bool check_common_constant_changes(const TypeTable* types, const CheckStream* stream, uint32_t source, TypeId target)
{
    uint32_t source_width = 0;
    uint32_t target_width = 0;
    bool source_signed = false;
    bool target_signed = false;
    const bool widening = check_common_integer(types, check_stream_type(stream, source), &source_width, &source_signed) &&
                          check_common_integer(types, target, &target_width, &target_signed) && target_width >= source_width;
    bool result = false;
    if (!widening)
    {
        TypeId type = 0;
        const unsigned __int128 integer = check_common_stream_integer(stream, source, &type);
        __int128 converted = 0;
        result = check_common_conversion_changes(types, type, target, integer, &converted);
        if (!result && check_common_integer_kind_wide(types, type, target))
        {
            // A _BitInt past 128 bits on either side (#1447): its whole value's.
            bool wide = false;
            const CheckValue value = check_stream_value(stream, source);
            result = check_wide_conversion_changes(types, &value, target, &wide) && wide;
        }
    }
    return result;
}

// A constant's truncation by the bit-field it is stored in (#1447, clang's AnalyzeBitFieldAssignment): the bit-field of
// `bit_width` bits and type `field` (no _Bool's), the constant node `source`'s value, of its own type, past parentheses and
// implicit conversions. Its width is its type's, or, for a `-` or a `~` of a negative or unsigned value, its significant
// bits; a value no wider than the bit-field is kept, and another one is truncated to the bit-field's width and extended back
// as the bit-field's signedness says. A truncation that changes it warns: of a 1 in a one-bit bit-field, as
// -Wsingle-bit-bitfield-constant-conversion's. `*original` and `*truncated` are the two values.
static bool check_common_bitfield_truncates(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, TypeId field, uint32_t bit_width,
                                            uint32_t source, __int128* original, __int128* truncated)
{
    uint32_t field_width = 0;
    bool field_signed = false;
    uint32_t width = 0;
    bool is_signed = false;
    bool result = false;
    TypeId type = 0;
    const bool constant = bit_width != 0 && bit_width < 128 && source < graph->count && check_common_integer_constant(stream, source);
    const unsigned __int128 integer = constant ? check_common_stream_integer(stream, source, &type) : 0;
    // A value whose bits are below 2^(w-1), which a w-bit bit-field of either signedness holds, as the 0s and 1s stored in
    // flags are, is kept before any type is read.
    if (constant && integer >= ((unsigned __int128)1 << (bit_width - 1)) && type_unqualified(type_strip_aligned(types, field)) != BUSTER_TYPE_BOOL &&
        check_common_integer(types, field, &field_width, &field_signed))
    {
        if (check_common_integer(types, type, &width, &is_signed) && width != 0)
        {
            *original = check_common_integer_bits(integer, width, is_signed);
            const CheckOp op = graph->ops[source];
            uint32_t original_width = width;
            if ((op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT) && (!is_signed || *original < 0))
            {
                // APInt's significant bits of the value's bits, read as signed: its bit length past its sign bits, and one.
                const __int128 bits = check_common_integer_bits(integer, width, true);
                const unsigned __int128 magnitude = (unsigned __int128)(bits < 0 ? ~bits : bits);
                const uint64_t high = (uint64_t)(magnitude >> 64);
                const uint32_t length = high != 0 ? 128u - (uint32_t)__builtin_clzll(high) :
                                        (uint64_t)magnitude != 0 ? 64u - (uint32_t)__builtin_clzll((uint64_t)magnitude) : 0u;
                original_width = length + 1u;
            }
            if (original_width > bit_width)
            {
                const unsigned __int128 bits = (unsigned __int128)*original & check_action_mask(bit_width);
                const bool negative = field_signed && ((bits >> (bit_width - 1)) & 1) != 0;
                *truncated = negative ? (__int128)(bits | ~check_action_mask(bit_width)) : (__int128)bits;
                result = *truncated != *original;
            }
        }
    }
    return result;
}

// The bit-field truncation's warning (check_common_bitfield_truncates) at `token`, published at `publication`: clang's
// Diag, as an assignment's full expression completes, but in an unevaluated operand, or, in an unevaluated operand too, as
// an `initialization` is checked. The constant's type is spelled as written, its typedef's name and all.
static void check_common_bitfield_warning(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                          TypeTable* types, CheckWarningSpelling* spell, uint32_t source, uint32_t bit_width,
                                          __int128 original, __int128 truncated, uint32_t token, uint32_t publication, bool initialization,
                                          Arena* scratch)
{
    const String8 name = check_driver_warning_type(parser, pp, input, types, spell, scratch, check_stream_type(stream, source),
                                                   check_input_operand_type_spelling(input, parser, source));
    if (name.length != 0)
    {
        const bool single = bit_width == 1 && original == 1;
        char from[48];
        char to[48];
        check_common_number(from, sizeof(from), original);
        check_common_number(to, sizeof(to), truncated);
        const char* message = check_warning_text(scratch, "implicit truncation from %.*s to %sbit-field changes value from %s to %s", (int)name.length,
                                                  name.pointer, single ? "a one-bit wide " : "", from, to);
        const uint32_t id = single ? CHECK_WARNING_ID_SINGLE_BIT_BITFIELD_CONSTANT_CONVERSION : CHECK_WARNING_ID_BITFIELD_CONSTANT_CONVERSION;
        if (initialization)
        {
            const uint32_t publication_source = check_common_publication_source(parser, pp, publication);
            check_common_report_published_phase(parser, pp, input, token, publication_source, PP_DIAGNOSTIC_DEFERRED, id, false, message, NULL, 0, scratch);
        }
        else
        {
            check_common_report_evaluated(parser, pp, input, token, publication, PP_DIAGNOSTIC_DEFERRED, id, false, message, NULL, 0, scratch);
        }
    }
}

// An assignment's bit-field truncation (check_common_bitfield_truncates): its left operand's bit-field, its right operand's
// constant.
static bool check_common_bitfield_assignment(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node,
                                             uint32_t* bit_width, __int128* original, __int128* truncated)
{
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    bool result = false;
    if (graph->ops[node] == CHECK_OP_ASSIGN && left < graph->count && right < graph->count && (check_stream_flags(stream, left) & BUSTER_CHECK_BIT_FIELD) != 0 &&
        check_common_integer_constant(stream, right))
    {
        const CheckValue field = check_stream_value(stream, left);
        *bit_width = field.bit_width;
        result = check_common_bitfield_truncates(graph, stream, types, field.type, field.bit_width, right, original, truncated);
    }
    return result;
}

// The walks' candidates at `node` (BUSTER_CHECK_REACH_*), by what their tests read of the graph and the stream. The
// conversion walk's (BUSTER_CHECK_REACH_CONVERSION), which gives every implicit conversion's warning: a constant an
// initialization's, an assignment's, a conversion's or a call's argument's conversion may change, an integer one only where
// its value changes (check_common_constant_changes, #1340), a floating one, or a conditional's or a comma's arms; and a truth
// context's floating literal. The ordinary walk's (BUSTER_CHECK_REACH_TREE): a self-assignment's two names, a constant index
// outside its array, a comma the source writes. What their tests read past them only takes warnings away, so that a node it
// rejects can't warn, and the walks needn't visit a subtree without one.
static uint8_t check_common_candidate_bits(const Parser* parser, const CheckGraph* graph, const CheckStream* stream, const TypeTable* types,
                                           uint32_t node, bool self_assign, bool enum_names)
{
    const CheckOp op = graph->ops[node];
    bool own = false;
    bool bound = false;
    bool conversion = false;
    if (op == CHECK_OP_INITIALIZE || op == CHECK_OP_ASSIGN || op == CHECK_OP_CONVERT)
    {
        const uint32_t left = graph->operand0[node];
        const uint32_t right = graph->operand1[node];
        const uint32_t source = op == CHECK_OP_ASSIGN ? right : left;
        if (source < graph->count && source < stream->count)
        {
            const CheckConstantKind constant = check_stream_constant_kind(stream, source);
            const bool arms = check_common_conversion_arms(graph, source);
            if (check_common_conversion_source(constant, arms))
            {
                const uint32_t flags = stream->conversions[node].operands[op == CHECK_OP_ASSIGN ? 1u : 0u].flags;
                const bool assignment = (flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (flags & BUSTER_CHECK_CONVERT_CAST) == 0;
                uint32_t bit_width = 0;
                __int128 original = 0;
                __int128 truncated = 0;
                // Or a constant a bit-field's assignment truncates (#1447, check_common_bitfield_assignment), which only an
                // assignment of an integer constant reads.
                conversion = assignment && (constant != CHECK_CONSTANT_INTEGER || arms ||
                                            check_common_constant_changes(types, stream, source, check_stream_type(stream, node)) ||
                                            (op == CHECK_OP_ASSIGN &&
                                             check_common_bitfield_assignment(graph, stream, types, node, &bit_width, &original, &truncated)));
            }
            // A conversion between enumerations (#1447, check_common_enum_cast).
            conversion = conversion || (enum_names && check_common_enum_cast_edge(parser, graph, stream, types, source,
                                                                                    stream->conversions[node].operands[op == CHECK_OP_ASSIGN ? 1u : 0u].flags,
                                                                                    check_stream_type(stream, node)));
            if (!conversion && check_common_bool_candidate(graph, source))
            {
                // A `<<` converted to _Bool, constant or not (#1447, check_common_int_in_bool).
                const uint32_t flags = stream->conversions[node].operands[op == CHECK_OP_ASSIGN ? 1u : 0u].flags;
                conversion = (flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (flags & BUSTER_CHECK_CONVERT_CAST) == 0 &&
                             check_common_bool_target(types, check_stream_type(stream, node));
            }
        }
        own = self_assign && op == CHECK_OP_ASSIGN && left < graph->count && right < graph->count &&
              graph->ops[left] == CHECK_OP_VALUE && graph->ops[right] == CHECK_OP_VALUE;
    }
    else if (op == CHECK_OP_CALL && graph->argument_offsets != NULL && graph->argument_counts != NULL && stream->argument_conversions != NULL)
    {
        // The parameter's type an integer argument converts to: a prototype's, or an unprototyped call's promoted one; the
        // callee's type is read at the first such argument.
        const uint32_t offset = graph->argument_offsets[node];
        bool typed = false;
        bool prototype = false;
        for (uint32_t i = 0; !conversion && i < graph->argument_counts[node]; i += 1)
        {
            const uint32_t source = graph->argument_nodes[offset + i];
            if (source < graph->count && source < stream->count)
            {
                // A conversion between enumerations (#1447, check_common_enum_cast), a prototype's parameter's.
                const bool enum_cast = enum_names && check_common_enum_cast_edge(parser, graph, stream, types, source, stream->argument_conversions[offset + i].flags,
                                                                                  stream->argument_conversions[offset + i].type);
                const CheckConstantKind constant = check_stream_constant_kind(stream, source);
                const bool arms = check_common_conversion_arms(graph, source);
                if (check_common_conversion_source(constant, arms))
                {
                    const CheckConversion edge = stream->argument_conversions[offset + i];
                    const bool assignment = (edge.flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (edge.flags & BUSTER_CHECK_CONVERT_CAST) == 0;
                    const bool integer = constant == CHECK_CONSTANT_INTEGER && !arms;
                    if (assignment && integer && !typed)
                    {
                        const uint32_t callee = graph->operand0[node];
                        TypeId function = callee < graph->count ? check_stream_type(stream, callee) : 0;
                        function = function != 0 ? type_unqualified(type_strip_aligned(types, function)) : 0;
                        if (function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_POINTER)
                        {
                            function = type_unqualified(type_strip_aligned(types, type_element(types, function)));
                        }
                        prototype = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION &&
                                    (types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
                        typed = true;
                    }
                    conversion = assignment && (!integer || check_common_constant_changes(types, stream, source, prototype ? edge.type :
                                                                                           type_default_argument_promote(types, edge.type)));
                }
                conversion |= enum_cast;
                if (!conversion && check_common_bool_candidate(graph, source))
                {
                    // A `<<` passed to a _Bool parameter, constant or not (#1447): an unprototyped call's promoted one is never _Bool.
                    const CheckConversion edge = stream->argument_conversions[offset + i];
                    conversion = (edge.flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (edge.flags & BUSTER_CHECK_CONVERT_CAST) == 0 &&
                                 check_common_bool_target(types, edge.type);
                }
            }
        }
    }
    else if (op == CHECK_OP_SUBSCRIPT)
    {
        const uint32_t array = graph->operand0[node];
        const uint32_t index = graph->operand1[node];
        const TypeId type = array < graph->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, array))) : 0;
        uint32_t width = 0;
        bool sign = false;
        if (type != 0 && check_common_integer_constant(stream, index) && type_kind(types, type) == BUSTER_TYPE_KIND_ARRAY &&
            types->types.extent[type_index(type)] != 0 && check_common_integer(types, check_stream_type(stream, index), &width, &sign))
        {
            TypeId index_type = 0;
            const __int128 value = check_common_integer_bits(check_common_stream_integer(stream, index, &index_type), width, sign);
            own = value < 0 || (unsigned __int128)value >= types->types.extent[type_index(type)];
            bound = own;
        }
    }
    else if (op == CHECK_OP_COMMA)
    {
        const uint32_t token = graph->tokens[node];
        own = token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_COMMA;
    }
    else if (op == CHECK_OP_NOT || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITION || op == CHECK_OP_CONDITIONAL)
    {
        // (A static assertion's operand, a root, takes its bit after the facts' pass: check_common_assertion_reach.)
        conversion = check_common_truth_candidate(graph, stream, node);
    }
    const uint8_t result = (uint8_t)((own ? BUSTER_CHECK_REACH_TREE : 0u) | (conversion ? BUSTER_CHECK_REACH_CONVERSION : 0u) |
                                     (bound ? BUSTER_CHECK_REACH_BOUND : 0u));
    return result;
}

// A node's own bits of its reach (BUSTER_CHECK_REACH_*): its own frame's candidate, a division by zero, a shift by a
// constant, and a comparison. A plain `x = y` is a candidate for -Wself-assign alone, and a comparison's bit
// is -Wsign-compare's: each counts only where its warning may report.
static uint8_t check_common_own_reach(const Parser* parser, const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node,
                                      const CheckCommonEnabled* enabled)
{
    const CheckOp op = graph->ops[node];
    const uint32_t divisor = graph->operand1[node];
    TypeId divisor_type = 0;
    // A null dereference shares the arithmetic scan with division and shifts.
    const bool division = ((op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER) && check_common_integer_constant(stream, divisor) &&
                           check_common_stream_integer(stream, divisor, &divisor_type) == 0) ||
                          ((op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER) && !check_common_integer_constant(stream, divisor) &&
                           check_common_zero_divisor(graph, stream, types, enabled->const_values, node, divisor)) ||
                          (op == CHECK_OP_DEREFERENCE && check_common_null_dereference(graph, stream, types, node));
    const bool shift = check_common_shift_op(op) && check_common_integer_constant(stream, divisor);
    const bool comparison = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL &&
                            (enabled->sign_compare || (enabled->out_of_range && check_common_range_out(graph, stream, types, node)));
    // A source comma's left-operand marker is the arithmetic scan's (check_common_arithmetic_own).
    const bool comma = op == CHECK_OP_COMMA && graph->tokens[node] < parser->tokens.count && parser->tokens.kinds[graph->tokens[node]] == PARSE_KIND_COMMA;
    // (A self-assignment is no walk's: check_common_self_assigns gives it, in unreachable code too.)
    const uint8_t result = (uint8_t)(check_common_candidate_bits(parser, graph, stream, types, node, false, enabled->enum_cast) |
                                     (division || shift || comma ? BUSTER_CHECK_REACH_ARITHMETIC : 0u) | (comparison ? BUSTER_CHECK_REACH_SIGN : 0u) |
                                     (shift ? BUSTER_CHECK_REACH_SHIFT : 0u));
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

// The scan's next node below `count` that isn't evaluated (`evaluated`'s zero), or `count`: the vector path tests 64 nodes at
// a time; the scalar path, its twin, gives every node, so that its caller tests each.
static uint32_t check_common_next_unevaluated(const uint8_t* evaluated, uint32_t count, CheckCommonScan* scan, bool vector)
{
    while (scan->pending == 0 && scan->next < count)
    {
        const uint32_t first = scan->next;
        const uint32_t live = count - first < 64 ? count - first : 64;
        const uint64_t lanes = live == 64 ? UINT64_MAX : (UINT64_C(1) << live) - 1;
        const __m512i bytes = vector ? _mm512_maskz_loadu_epi8((__mmask64)lanes, evaluated + first) : _mm512_setzero_si512();
        const uint64_t selected = vector ? _mm512_mask_testn_epi8_mask((__mmask64)lanes, bytes, bytes) : lanes;
        scan->base = first;
        scan->next = first + live;
        scan->pending = selected;
    }
    const uint32_t result = check_common_scan_take(scan, count);
    return result;
}

#include "c_check_unsequenced.c"

// One brace initializer's literal conversions (check_common_initializer_roots' plan `index`), its parts in reverse
// initializer order, each part's target conversion, then its nested ones; then the parts' own root walks, in order.
static void check_common_initializer_conversions(CheckCommonEmission* emission, const CheckProgram* program, uint32_t index)
{
    const Parser* parser = emission->parser;
    Preprocessor* pp = emission->pp;
    const CheckInput* input = emission->input;
    const CheckStream* stream = emission->stream;
    const CheckGraph* graph = &input->graph;
    const uint8_t* evaluated = emission->evaluated;
    const CheckInitializer* plan = program->initializers->plans + index;
    const uint32_t declaration = program->initializers->declaration_tokens[index];
    // The initializer is one full expression: its parts' conversions, nested ones too, complete at its end.
    const uint32_t end = check_common_sparse_get(input->initializer_ends, declaration);
    emission->at_once = end <= parser->tokens.count ? end : BUSTER_CHECK_NONE;
    // Its constants its bit-fields truncate (#1447): clang's CheckBitFieldInitialization's Diag, as each member is initialized,
    // so in initializer order, before the full expression's conversions, at each value's first token (its grouping parenthesis,
    // if any), in unreachable code and unevaluated operands too.
    for (uint32_t p = 0; p < plan->count && !pp->fatal; p += 1)
    {
        const CheckInitPart* part = plan->parts + p;
        const uint32_t root = part->node;
        __int128 original = 0;
        __int128 truncated = 0;
        if (part->kind == CHECK_INIT_VALUE && root < graph->count && stream->diagnostics[root] == CHECK_DIAG_NONE &&
            check_common_initializer_bitfield(input, stream, emission->types, part, &original, &truncated))
        {
            check_common_bitfield_warning(parser, pp, input, stream, emission->types, emission->spell, root, part->bit_width, original, truncated,
                                          part->token, emission->at_once, true, emission->scratch);
        }
    }
    for (uint32_t p = plan->count; p != 0 && !pp->fatal; p -= 1)
    {
        const CheckInitPart* part = plan->parts + p - 1;
        const uint32_t root = part->node;
        if (part->kind == CHECK_INIT_VALUE && root < graph->count && stream->diagnostics[root] == CHECK_DIAG_NONE &&
            (evaluated == NULL || evaluated[root] != 0 || (emission->emitted[root] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0))
        {
            // Range initializers have one conversion per selected element, even when every write uses
            // the same source node. The graph's expression is traversed once; its conversions remain distinct.
            // (Not a compound literal's in an unevaluated query's operand: check_common_query_suppressed.)
            // (An _Atomic element converts to its value type, so the planner marks it: BUSTER_CHECK_CONVERT_ATOMIC.)
            const bool atomic = (part->conversion.flags & BUSTER_CHECK_CONVERT_ATOMIC) != 0;
            if (!atomic && check_common_enum_cast_candidate(input->enum_names, parser, graph, stream, emission->types, root, part->conversion.type))
            {
                // (Its member's or element's typedef, #1447.)
                const CheckCommonEnumTarget place = { .conversion = BUSTER_CHECK_NONE, .call = BUSTER_CHECK_NONE, .object = plan->type,
                                                      .declaration = declaration, .offset = part->bit_offset };
                check_common_enum_cast(parser, pp, input, stream, emission->types, emission->spell, root, part->conversion.type, &place, false, emission->parents,
                                       emission->at_once, emission->scratch);
            }
            if (!atomic && !check_common_query_suppressed(graph, root) && check_common_initializer_literal(parser, input, stream, emission->types, part))
            {
                const CheckValue value = check_stream_value(stream, root);
                check_common_conversion_warning_at(parser, pp, input, stream, emission->types, emission->spell, root, &value, part->conversion.type,
                                                   emission->parents, emission->at_once, evaluated == NULL || evaluated[root] != 0,
                                                   BUSTER_CHECK_LEAVES_FLOATING | BUSTER_CHECK_LEAVES_WIDTH | BUSTER_CHECK_LEAVES_LISTED | BUSTER_CHECK_LEAVES_BOOL,
                                                   emission->scratch);
            }
            // A plan part can also have a graph parent, such as its declaration's discard wrapper.
            // Its nested conversions still belong immediately after this part's target conversion;
            // the conversion visit bit prevents the later parent walk from reporting them again.
            check_common_emit_push(emission, root, BUSTER_CHECK_EMIT_CONVERSIONS);
            check_common_emit_run(emission);
        }
    }
    // The parts' own root walks, in initializer order (#1943): clang queues their runtime warnings (a comma's left operand,
    // a shift count) as it parses each value, before the list's checks.
    for (uint32_t p = 0; p < plan->count && !pp->fatal; p += 1)
    {
        const CheckInitPart* part = plan->parts + p;
        const uint32_t root = part->node;
        if (part->kind == CHECK_INIT_VALUE && root < graph->count && stream->diagnostics[root] == CHECK_DIAG_NONE &&
            (evaluated == NULL || evaluated[root] != 0 || (emission->emitted[root] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0) &&
            emission->parents[root] == BUSTER_CHECK_NONE && (emission->emitted[root] & BUSTER_CHECK_EMITTED_TREE) == 0)
        {
            check_common_emit_root(emission, root);
        }
    }
    emission->at_once = BUSTER_CHECK_NONE;
}

// One brace initializer's integer constant conversions (#1447, check_common_initializer_roots' plan `index`), its parts
// in reverse initializer order as its literal ones: clang's DiagRuntimeBehavior queues them where the full expression
// completes, after its values' own runtime warnings, inside a function to its end and at file scope to the declarator's
// end (check_common_conversion_leaf), and drops them in unreachable code, as it does every runtime warning.
static void check_common_initializer_integers(CheckCommonEmission* emission, const CheckProgram* program, uint32_t index)
{
    const Parser* parser = emission->parser;
    Preprocessor* pp = emission->pp;
    const CheckInput* input = emission->input;
    const CheckStream* stream = emission->stream;
    const CheckGraph* graph = &input->graph;
    const uint8_t* evaluated = emission->evaluated;
    const CheckInitializer* plan = program->initializers->plans + index;
    const uint32_t declaration = program->initializers->declaration_tokens[index];
    const uint32_t end = check_common_sparse_get(input->initializer_ends, declaration);
    emission->at_once = end <= parser->tokens.count ? end : BUSTER_CHECK_NONE;
    for (uint32_t p = plan->count; p != 0 && !pp->fatal; p -= 1)
    {
        const CheckInitPart* part = plan->parts + p - 1;
        const uint32_t root = part->node;
        // (An _Atomic element converts to its value type, so the planner marks it: BUSTER_CHECK_CONVERT_ATOMIC.)
        const bool atomic = (part->conversion.flags & BUSTER_CHECK_CONVERT_ATOMIC) != 0;
        if (!atomic && part->kind == CHECK_INIT_VALUE && root < graph->count && stream->diagnostics[root] == CHECK_DIAG_NONE &&
            (evaluated == NULL || evaluated[root] != 0) && !check_common_query_suppressed(graph, root) &&
            check_common_initializer_integer(input, stream, emission->types, part))
        {
            const CheckValue value = check_stream_value(stream, root);
            check_common_conversion_warning_at(parser, pp, input, stream, emission->types, emission->spell, root, &value, part->conversion.type,
                                               emission->parents, emission->at_once, true, BUSTER_CHECK_LEAVES_INTEGER | BUSTER_CHECK_LEAVES_LISTED,
                                               emission->scratch);
        }
    }
    emission->at_once = BUSTER_CHECK_NONE;
}

// The static selection enclosing a detached root. The source parser retains that edge even though the evaluated
// graph retains only the chosen operand. Earlier statements inside a selected GNU statement-expression are also
// disconnected: their source subtree still belongs to the chosen arm, so keep walking for a genuinely unselected owner.
static uint32_t check_common_shift_selection(const Parser* parser, const CheckInput* input, uint32_t root)
{
    const CheckGraph* graph = &input->graph;
    uint32_t selection = graph->count;
    uint32_t token = graph->tokens[root];
    for (uint32_t steps = 0; token < parser->tokens.count && steps < parser->tokens.count; steps += 1)
    {
        const uint32_t parent = parser->tree.parents[token];
        if (parent == token || parent >= parser->tokens.count)
        {
            break;
        }
        token = parent;
        const uint32_t node = input->token_nodes != NULL && token < input->token_count ? input->token_nodes[token] : BUSTER_CHECK_NONE;
        if (node < graph->count && node != root && graph->ops[node] == CHECK_OP_SELECT)
        {
            const uint32_t chosen = graph->operand0[node];
            const bool in_chosen = chosen < graph->count && check_common_token_descendant(parser, graph->tokens[root], graph->tokens[chosen]);
            if (!in_chosen)
            {
                selection = node;
                break;
            }
        }
    }
    return selection;
}

// Shift diagnostics in unselected _Generic/choose arms have no CFG block in Clang. Prepare sparse source edges for
// them only when an unevaluated count can warn. Reuse the existing static-selection root classification, then sort
// roots by their enclosing selection. A second key order supports root ownership queries without a dense column.
// Propagating the detached bit in graph order makes a skipped ordinary subtree traversable for these edges alone.
static bool check_common_shift_extras(const Parser* parser, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                      const CheckCommonFacts* facts, CheckCommonShiftExtras* extras, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const CheckOp shifts[] = { CHECK_OP_SHIFT_LEFT, CHECK_OP_SHIFT_RIGHT, CHECK_OP_SHIFT_LEFT_ASSIGN, CHECK_OP_SHIFT_RIGHT_ASSIGN };
    const CheckCommonOps set = check_common_ops(shifts, BUSTER_ARRAY_LENGTH(shifts));
    CheckCommonScan scan = { 0 };
    bool needed = false;
    for (uint32_t node = check_common_next_op(graph, &set, &scan, vector); !needed && node < graph->count;
         node = check_common_next_op(graph, &set, &scan, vector))
    {
        const uint32_t suppressed = graph->warning_suppressed != NULL ? graph->warning_suppressed[node] : 0;
        needed = check_common_shift_op(graph->ops[node]) && facts->evaluated[node] == 0 &&
                 (suppressed & (CHECK_WARNING_SHIFT_COUNT_NEGATIVE | CHECK_WARNING_SHIFT_COUNT_OVERFLOW)) == 0 &&
                 check_common_shift_warning(graph, stream, types, node) != 0;
    }
    bool valid = true;
    if (needed)
    {
        const uint8_t* unselected = check_common_unselected(graph, parser, scratch);
        valid = unselected != NULL;
        uint32_t count = 0;
        CheckCommonScan roots = { 0 };
        for (uint32_t node = valid ? check_common_next_root(facts->parents, graph->count, &roots, vector) : graph->count;
             node < graph->count; node = check_common_next_root(facts->parents, graph->count, &roots, vector))
        {
            count += facts->parents[node] == BUSTER_CHECK_NONE && unselected[node] != 0;
        }
        uint64_t* keys = count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, count) : NULL;
        uint64_t* temporary = keys != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, count) : NULL;
        CheckCommonShiftExtra* records = temporary != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, CheckCommonShiftExtra, count) : NULL;
        valid &= count == 0 || records != NULL;
        if (valid && count != 0)
        {
            roots = (CheckCommonScan){ 0 };
            uint32_t used = 0;
            for (uint32_t node = check_common_next_root(facts->parents, graph->count, &roots, vector); node < graph->count;
                 node = check_common_next_root(facts->parents, graph->count, &roots, vector))
            {
                if (facts->parents[node] == BUSTER_CHECK_NONE && unselected[node] != 0)
                {
                    const uint32_t selection = check_common_shift_selection(parser, input, node);
                    if (selection < graph->count)
                    {
                        keys[used++] = (uint64_t)selection << 32 | node;
                    }
                }
            }
            count = used;
            const uint32_t varying = UINT32_MAX >> __builtin_clz(graph->count);
            const uint64_t* ordered = c_bindings_sort(keys, temporary, count, vector, varying);
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t root = (uint32_t)ordered[i];
                records[i] = (CheckCommonShiftExtra){ .root = root, .selection = (uint32_t)(ordered[i] >> 32) };
                keys[i] = (uint64_t)root << 32 | i;
            }
            extras->records = records;
            extras->root_order = c_bindings_sort(keys, temporary, count, vector, varying);
            extras->count = count;
            uint32_t extra = 0;
            for (uint32_t node = 0; node < graph->count; node += 1)
            {
                while (extra < count && records[extra].selection == node)
                {
                    const uint32_t root = records[extra++].root;
                    if ((facts->reach[root] & (BUSTER_CHECK_REACH_SHIFT | BUSTER_CHECK_REACH_DETACHED)) != 0)
                    {
                        facts->reach[node] |= BUSTER_CHECK_REACH_ARITHMETIC | BUSTER_CHECK_REACH_DETACHED;
                    }
                }
                const uint32_t parent = facts->parents[node];
                if (parent < graph->count && (facts->reach[node] & BUSTER_CHECK_REACH_DETACHED) != 0)
                {
                    facts->reach[parent] |= BUSTER_CHECK_REACH_ARITHMETIC | BUSTER_CHECK_REACH_DETACHED;
                }
            }
        }
    }
    return valid;
}

// A root the reader adds after other roots its operand precedes: a statement's condition (CHECK_OP_CONDITION, a switch's
// CHECK_OP_POSITIVE), which its keyword's closing parenthesis wraps after the body, and a for increment's discard
// (CHECK_OP_CAST at the `)`), which the `)` adds after the body. Clang analyzes each full expression as it is built, before
// what follows, so the walks take such a root where its operand stands. A declarator's discard at its declaration's `;`
// follows the later declarators' values, but no root between: it keeps its place, as it costs a sort to move.
static bool check_common_displaced_root(const Parser* parser, const CheckGraph* graph, const uint32_t* parents, uint32_t node)
{
    const CheckOp op = graph->ops[node];
    const uint32_t operand = graph->operand0[node];
    const uint32_t token = graph->tokens[node];
    const bool result = (op == CHECK_OP_CONDITION || op == CHECK_OP_POSITIVE || op == CHECK_OP_CAST) && parents[node] == BUSTER_CHECK_NONE &&
                        operand < graph->count && operand + 1 < node &&
                        (op != CHECK_OP_CAST || (token < parser->tokens.count && parser->tree.roles[token] == PARSE_ROLE_CLOSE_FOR));
    return result;
}

// The displaced roots (check_common_displaced_root), each its operand's node above its own, in their operands' order
// (c_bindings_sort's stable radix sort of the operands, through `temporary`, which the closers' order, nearly always
// their operands', usually spares): their count, the sorted keys in `*sorted`,
// each root marked BUSTER_CHECK_EMITTED_DISPLACED in `emitted`, which the root order skips where it stands. At most half
// the graph's nodes are displaced: each has an operand and a node between them. They are found from the parser's
// closers, a statement's condition's `)` (its node the token's) and a for clause's `)` (the discard of its increment's
// value, the node of the token its last token climbs to), 64 roles at a time on the vector path (check_common_next_token), every token on its twin.
static uint32_t check_common_displaced_roots(const Parser* parser, const CheckInput* input, const uint32_t* parents, uint8_t* emitted, uint64_t* items,
                                             uint64_t* temporary, const uint64_t** sorted, bool vector)
{
    static const uint8_t closers[] = { PARSE_ROLE_CLOSE_CONDITION, PARSE_ROLE_CLOSE_FOR };
    const CheckGraph* graph = &input->graph;
    const uint32_t tokens = input->token_nodes != NULL && input->token_count < parser->tokens.count ? input->token_count : parser->tokens.count;
    CheckCommonScan scan = { 0 };
    uint32_t count = 0;
    uint64_t last = 0;
    bool ordered = true;
    for (uint32_t token = input->token_nodes != NULL ? check_common_next_token(parser, tokens, BUSTER_CHECK_NONE, closers, BUSTER_ARRAY_LENGTH(closers), &scan, vector) :
                                                       tokens;
         token < tokens; token = check_common_next_token(parser, tokens, BUSTER_CHECK_NONE, closers, BUSTER_ARRAY_LENGTH(closers), &scan, vector))
    {
        const uint8_t role = parser->tree.roles[token];
        // A for clause's increment is the `)`'s child its last token climbs to; an empty one's last token is the `;`.
        uint32_t increment = role == PARSE_ROLE_CLOSE_FOR && token != 0 && parser->tokens.kinds[token - 1] != PARSE_KIND_SEMI ? token - 1 : BUSTER_CHECK_NONE;
        for (uint32_t steps = 0; increment < token && parser->tree.parents[increment] != token && steps < token; steps += 1)
        {
            const uint32_t up = parser->tree.parents[increment];
            increment = up < token && up != increment ? up : BUSTER_CHECK_NONE;
        }
        const uint32_t value = role == PARSE_ROLE_CLOSE_CONDITION ? input->token_nodes[token] : increment < tokens ? input->token_nodes[increment] : BUSTER_CHECK_NONE;
        const uint32_t node = value < graph->count && role == PARSE_ROLE_CLOSE_FOR ? parents[value] : value;
        if (node < graph->count && check_common_displaced_root(parser, graph, parents, node) && (emitted[node] & BUSTER_CHECK_EMITTED_DISPLACED) == 0)
        {
            const uint64_t key = ((uint64_t)graph->operand0[node] << 32) | node;
            emitted[node] |= BUSTER_CHECK_EMITTED_DISPLACED;
            ordered &= key > last;
            last = key;
            items[count++] = key;
        }
    }
    const uint32_t varying = graph->count != 0 ? UINT32_MAX >> __builtin_clz(graph->count) : 0;
    *sorted = !ordered ? c_bindings_sort(items, temporary, count, vector, varying) : items;
    return count;
}

// The roots in the order the walks take them: check_common_next_root's, with the displaced ones (check_common_displaced_roots)
// moved ahead of the first root past their operands.
typedef struct CheckCommonRootOrder CheckCommonRootOrder;
struct CheckCommonRootOrder
{
    CheckCommonScan roots;
    uint32_t natural;
    const uint64_t* displaced;
    uint32_t displaced_count;
    uint32_t displaced_at;
    // Where the last root taken stands: its node, or a displaced root's operand.
    uint32_t position;
    // The displaced roots' marks (BUSTER_CHECK_EMITTED_DISPLACED), which the natural order skips where any root is displaced or
    // inner (`skipping`).
    const uint8_t* emitted;
    bool skipping;
};

// The roots of statement expressions' statements that the arithmetic scan takes where it enters their statement expression
// (#1447, check_common_inner_push): clang builds a statement expression's statements, queueing their runtime warnings,
// between what its enclosing expression built before it and after it, where the roots' order, the node order, has them
// first. A root is taken so where its statement expression is evaluated in an enclosing root the walks take, and then each
// is marked BUSTER_CHECK_EMITTED_DISPLACED, which the roots' order skips, and its statement expression and the nodes up to its
// enclosing root reach the arithmetic scan. Their count, as keys of the statement expression's node and the root (a displaced
// one's operand, the place check_common_displaced_roots gives it, so that its order among the statements is the same),
// sorted, in `*sorted`. The reader's list of statement expressions finds them, and each one's walk back over its own nodes,
// a parse-tree climb each, is scalar: it visits no node outside a statement expression but the one it stops at, so that a
// unit with few pays for their nodes alone.
static uint32_t check_common_inner_roots(const Parser* parser, const CheckInput* input, const CheckStream* stream, const uint32_t* parents,
                                         const uint8_t* evaluated, uint8_t* reach, uint8_t* emitted, uint64_t* items, uint64_t* temporary,
                                         uint64_t** sorted, bool vector)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t tokens = parser->tokens.count;
    uint32_t count = 0;
    // The reader's statement expressions (CheckInput.regions), each opened before those it holds: backwards, inner ones come
    // first, so that a root is its innermost one's. A unit without one reads nothing.
    for (uint32_t region = parser->tree.depths != NULL && input->token_nodes != NULL ? input->region_count : 0; region-- > 0;)
    {
        // The statement expression's node, its `)`'s, and its block's `}`, which that `)` closes right after.
        const uint32_t close = input->regions[region].closing;
        const uint32_t value = close < input->token_count ? input->token_nodes[close] : BUSTER_CHECK_NONE;
        const uint32_t brace = close != 0 && close < tokens ? close - 1 : BUSTER_CHECK_NONE;
        const bool statement = value < graph->count && graph->ops[value] == CHECK_OP_STATEMENT_VALUE && brace < tokens &&
                               parser->tree.roles[brace] == PARSE_ROLE_CLOSE_BLOCK && evaluated[value] != 0;
        uint32_t enclosing = value;
        for (uint32_t steps = 0; statement && parents[enclosing] < graph->count && steps < graph->count; steps += 1)
        {
            enclosing = parents[enclosing];
        }
        if (statement && evaluated[enclosing] != 0 &&
            stream->diagnostics[enclosing] == CHECK_DIAG_NONE)
        {
            // Its statements' nodes come just before its own, their tokens under its `}`: the walk back from it stops at the
            // first node outside, so that it visits its own nodes alone, and a unit's statement expressions their own.
            const uint32_t depth = parser->tree.depths[brace];
            for (uint32_t node = value; node-- > 0 && check_common_token_below(parser, graph->tokens[node], brace, depth);)
            {
                if (parents[node] == BUSTER_CHECK_NONE && (emitted[node] & BUSTER_CHECK_EMITTED_DISPLACED) == 0)
                {
                    const uint32_t place = check_common_displaced_root(parser, graph, parents, node) ? graph->operand0[node] : node;
                    emitted[node] |= BUSTER_CHECK_EMITTED_DISPLACED;
                    items[count++] = (uint64_t)value << 32 | place;
                }
            }
            for (uint32_t at = value; at < graph->count && (reach[at] & BUSTER_CHECK_REACH_ARITHMETIC) == 0; at = parents[at])
            {
                reach[at] |= BUSTER_CHECK_REACH_ARITHMETIC;
            }
        }
    }
    const uint32_t varying = graph->count != 0 ? UINT32_MAX >> __builtin_clz(graph->count) : 0;
    *sorted = count != 0 ? c_bindings_sort(items, temporary, count, vector, varying) : items;
    return count;
}

static uint32_t check_common_root_order_next(const CheckGraph* graph, const uint32_t* parents, CheckCommonRootOrder* order, bool vector)
{
    while (order->natural < graph->count && order->skipping && (order->emitted[order->natural] & BUSTER_CHECK_EMITTED_DISPLACED) != 0)
    {
        order->natural = check_common_next_root(parents, graph->count, &order->roots, vector);
    }
    const bool moved = order->displaced_at < order->displaced_count && (uint32_t)(order->displaced[order->displaced_at] >> 32) < order->natural;
    const uint32_t result = moved ? (uint32_t)order->displaced[order->displaced_at] : order->natural;
    order->position = moved ? (uint32_t)(order->displaced[order->displaced_at] >> 32) : order->natural;
    order->displaced_at += moved ? 1u : 0u;
    if (!moved && order->natural < graph->count)
    {
        order->natural = check_common_next_root(parents, graph->count, &order->roots, vector);
    }
    return result;
}

static void check_common_graph_warnings(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                        TypeTable* types, CheckWarningSpelling* spell, const CheckCommonFacts* facts,
                                        const CheckProgram* program, CheckCommonShiftWarnings* shifts, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    // Division and subscript warnings apply only to evaluated operands (facts->evaluated): sizeof and alignof operands,
    // unselected constant ternary arms, short-circuited sides and _Generic's other arms never run. Shift counts also
    // visit detached static-selection arms through their separate diagnostic edges. Each node's parent
    // (facts->parents) is a subscript's use context: an `&a[i]` spine relaxes the bound to allow one past the end, an
    // explicit `(void)` cast discards the lvalue unchecked, and any other operand inside an address-of's subtree — a
    // nested index — is unchecked as well.
    const uint8_t* evaluated = facts->evaluated;
    const uint32_t* parents = facts->parents;
    bool source_contexts = false;
    if (evaluated != NULL)
    {
        // Only an unevaluated conversion, call or truth-context node can need a literal-only source context. Unevaluated
        // nodes are few: the vector path skips the evaluated ones 64 at a time; the scalar twin visits all nodes and applies
        // the same tests.
        const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
        CheckCommonScan scan = { 0 };
        for (uint32_t node = check_common_next_unevaluated(evaluated, graph->count, &scan, vector); node < graph->count && !source_contexts;
             node = check_common_next_unevaluated(evaluated, graph->count, &scan, vector))
        {
            const CheckOp op = graph->ops[node];
            if (evaluated[node] == 0 && (op == CHECK_OP_INITIALIZE || op == CHECK_OP_ASSIGN || op == CHECK_OP_CONVERT))
            {
                const uint32_t source = op == CHECK_OP_ASSIGN ? graph->operand1[node] : graph->operand0[node];
                source_contexts |= source < graph->count &&
                                   (check_stream_constant_kind(stream, source) == CHECK_CONSTANT_FLOATING || check_common_conversion_arms(graph, source) ||
                                    check_common_bool_candidate(graph, source));
                // A bit-field's truncation, which clang's Diag gives in unreachable code too (#1447).
                uint32_t bit_width = 0;
                __int128 original = 0;
                __int128 truncated = 0;
                source_contexts |= check_common_bitfield_assignment(graph, stream, types, node, &bit_width, &original, &truncated);
                // A conversion between enumerations (#1447, check_common_enum_cast), also clang's Diag.
                source_contexts |= check_common_enum_cast_candidate(input->enum_names, parser, graph, stream, types, source, check_stream_type(stream, node));
            }
            source_contexts |= evaluated[node] == 0 && check_common_truth_candidate(graph, stream, node);
            if (evaluated[node] == 0 && op == CHECK_OP_CALL && graph->argument_offsets != NULL && graph->argument_counts != NULL)
            {
                for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                {
                    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
                    source_contexts |= argument < graph->count &&
                                       (check_stream_constant_kind(stream, argument) == CHECK_CONSTANT_FLOATING || check_common_conversion_arms(graph, argument) ||
                                        check_common_bool_candidate(graph, argument));
                    source_contexts |= stream->argument_conversions != NULL &&
                                       check_common_enum_cast_candidate(input->enum_names, parser, graph, stream, types, argument,
                                                                        stream->argument_conversions[graph->argument_offsets[node] + i].type);
                }
            }
        }
    }
    // A static assertion's operand is no runtime operator's: each has a constant-warning entry (check_input_const_warning).
    for (uint32_t i = 0; evaluated != NULL && graph->contexts != NULL && i < graph->const_warning_count && !source_contexts; i += 1)
    {
        const uint32_t node = graph->const_warnings[i].node;
        source_contexts |= node < graph->count && evaluated[node] == 0 && (graph->contexts[node] & BUSTER_CHECK_CONTEXT_ASSERT) != 0 &&
                           check_common_truth_floating(graph, stream, node);
    }
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
    uint32_t* work_owners = graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, work_capacity) : NULL;
    if (graph->count != 0 && (work_nodes == NULL || work_kinds == NULL || work_owners == NULL))
    {
        check_common_exhausted(pp);
    }
    CheckCommonShiftExtras shift_extras = { 0 };
    const bool extras_ready = ready && check_common_shift_extras(parser, input, stream, types, facts, &shift_extras, scratch);
    if (ready && !extras_ready)
    {
        check_common_exhausted(pp);
    }
    if (extras_ready && work_nodes != NULL && work_kinds != NULL)
    {
        const CheckCommonInitializerKeys initializer_keys = check_common_initializer_roots(parser, pp, input, stream, types, program, evaluated,
                                                                                          &source_contexts, scratch);
        const uint32_t initializer_count = initializer_keys.literal_count;
        const uint64_t* initializers = initializer_keys.literal;
        if (source_contexts && parents != NULL)
        {
            if (!check_common_literal_contexts(graph, parser, parents, emitted, scratch))
            {
                check_common_exhausted(pp);
            }
        }
        else
        {
            memset(emitted, 0, graph->count);
        }
        CheckCommonIgnored ignored = { 0 };
        CheckCommonEmission emission =
        {
            .parser = parser,
            .pp = pp,
            .input = input,
            .stream = stream,
            .program = program,
            .types = types,
            .spell = spell,
            .shifts = shifts,
            .shift_extras = &shift_extras,
            .evaluated = evaluated,
            .parents = parents,
            .flushed = facts->flushed,
            .emitted = emitted,
            .at_once = BUSTER_CHECK_NONE,
            .reach = reach,
            .work_nodes = work_nodes,
            .work_kinds = work_kinds,
            .work_owners = work_owners,
            .work_capacity = work_capacity,
            .work_count = 0,
            .scratch = scratch,
            .ignored = &ignored,
        };
        if (!check_common_ignored_prepare(&ignored, parser, pp, input, types, scratch))
        {
            check_common_exhausted(pp);
        }
        const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
        // Expression roots in graph order: each evaluated expression emits its subtree's deferred warnings
        // where clang's traversal puts them — the diagnostics' shared publication slot keeps this order. A root
        // whose subtree holds no reach and whose discarded expression is no comma and can't have a marker can't warn.
        // An expression statement's call warns of its discarded result at once (#1447), in unreachable code too, as does
        // the call an unevaluated statement's comma, conditional or selection leaves as its value (the walk's own where
        // it's evaluated); an initializer's values, which a declaration's `;` casts to void as roots of their own, are
        // read, not discarded.
        // The displaced roots (check_common_displaced_root) join the roots' order at their operands, ahead of the later roots
        // their own nodes follow; a root walk skips them where they stand.
        // -Wunsequenced's candidates and its walk's tables (c_check_unsequenced.c), where it can warn at all.
        CheckUnseqStorage unsequenced = { .vector = vector };
        bool unsequenced_failed = false;
        uint8_t* unsequenced_marks = check_common_enabled(pp, input, CHECK_WARNING_ID_UNSEQUENCED, false) ?
                                           check_unseq_marks(graph, parents, vector, scratch, &unsequenced_failed) : NULL;
        // The brace initializers whose lists can warn, each list one full expression (check_unseq_plans), by their last
        // value's node, where the walk checks them.
        uint32_t unsequenced_plan_count = 0;
        const uint64_t* unsequenced_plans = unsequenced_marks != NULL ? check_unseq_plans(input, program, parents, unsequenced_marks, vector, scratch,
                                                                                        &unsequenced_plan_count, &unsequenced_failed) : NULL;
        uint32_t unsequenced_plan = 0;
        if (unsequenced_failed)
        {
            check_common_exhausted(pp);
        }
        const uint32_t displaced_capacity = graph->count / 2 + 1;
        uint64_t* displaced = CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, displaced_capacity);
        uint64_t* displaced_temporary = displaced != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, displaced_capacity) : NULL;
        if (displaced_temporary == NULL)
        {
            check_common_exhausted(pp);
        }
        const uint64_t* displaced_sorted = displaced;
        // A statement expression's inner roots first, which the displaced roots then leave alone. Each holds a node of its own,
        // so that a graph's nodes bound their count as they do the displaced roots'.
        uint64_t* inner = displaced_temporary != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, graph->count) : NULL;
        uint64_t* inner_temporary = inner != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, graph->count) : NULL;
        if (displaced_temporary != NULL && inner_temporary == NULL)
        {
            check_common_exhausted(pp);
        }
        uint64_t* inner_sorted = inner;
        emission.inner_count = inner_temporary != NULL ? check_common_inner_roots(parser, input, stream, facts->parents, facts->evaluated, facts->reach, emitted,
                                                                                   inner, inner_temporary, &inner_sorted, vector) : 0;
        emission.inner = inner_sorted;
        const uint32_t displaced_count = displaced_temporary != NULL ?
                                         check_common_displaced_roots(parser, input, parents, emitted, displaced, displaced_temporary, &displaced_sorted, vector) : 0;
        CheckCommonRootOrder order = { .displaced = displaced_sorted, .displaced_count = displaced_count, .emitted = emitted,
                                       .skipping = displaced_count != 0 || emission.inner_count != 0 };
        order.natural = check_common_next_root(parents, graph->count, &order.roots, vector);
        // An initializer's plan (its key's first node) emits before the first root at or after that node, a displaced root
        // standing at its operand (CheckCommonRootOrder.position), as main's walk of the roots goes on unchanged between
        // them: a unit with none tests one bound per root.
        uint32_t initializer = 0;
        uint32_t next_init = initializer_count != 0 ? (uint32_t)(initializers[0] >> 32) : UINT32_MAX;
        // A plan's integer constant conversions (its key's last value node) emit before the first root past its values: a
        // root's own expression, or the value a discard wrapper's operand is, at the root order's position.
        // A compound literal's plan (BUSTER_CHECK_INITIALIZER_INNER), whose values are roots of their own, waits instead for
        // the walk of the first root past them, its full expression's, and emits before the next root.
        uint32_t integer = 0;
        uint32_t next_integer = initializer_keys.integer_count != 0 ? (uint32_t)(initializer_keys.integer[0] >> 32) : UINT32_MAX;
        uint32_t walked = 0;
        bool any_walked = false;
        // The stashed shift warnings no walk will reach, dropped or kept, before the roots' order: a kept one in an unevaluated
        // root (a folded condition's) is reported where that root stands (check_common_shift_span).
        check_common_shift_unreached(parser, pp, shifts, input, stream, types, program, parents, evaluated, scratch);
        for (uint32_t node = check_common_root_order_next(graph, parents, &order, vector); node < graph->count && !pp->fatal;
             node = check_common_root_order_next(graph, parents, &order, vector))
        {
            // (A pending plan's key is below its first root past it: only then is the root's operand read.)
            const uint32_t at = order.position;
            const uint32_t operand = next_integer < at && graph->ops[node] == CHECK_OP_CAST && graph->operand0[node] < at ? graph->operand0[node] : at;
            while (next_integer < UINT32_MAX && !pp->fatal &&
                   (((uint32_t)initializer_keys.integer[integer] & BUSTER_CHECK_INITIALIZER_INNER) != 0 ? any_walked && next_integer < walked :
                                                                                                       next_integer < operand))
            {
                check_common_initializer_integers(&emission, program, (uint32_t)initializer_keys.integer[integer] & ~BUSTER_CHECK_INITIALIZER_INNER);
                integer += 1;
                next_integer = integer < initializer_keys.integer_count ? (uint32_t)(initializer_keys.integer[integer] >> 32) : UINT32_MAX;
            }
            // (The scalar root scan also gives nodes with a parent, which the walk skips: only a root has been walked.)
            const bool root = parents[node] == BUSTER_CHECK_NONE;
            walked = root ? at : walked;
            any_walked |= root;
            if (next_init <= at)
            {
                while (initializer < initializer_count && (uint32_t)(initializers[initializer] >> 32) <= order.position && !pp->fatal)
                {
                    check_common_initializer_conversions(&emission, program, (uint32_t)initializers[initializer]);
                    initializer += 1;
                }
                next_init = initializer < initializer_count ? (uint32_t)(initializers[initializer] >> 32) : UINT32_MAX;
            }
            if (root && evaluated[node] == 0 && shifts != NULL && shifts->count != 0)
            {
                check_common_shift_span(pp, shifts, check_common_emission_subtree_start(&emission, graph->tokens[node]), graph->tokens[node]);
            }
            // (A brace initializer's value: its list was checked as one, with its plan.)
            const bool planned = unsequenced_marks != NULL && node < graph->count && (unsequenced_marks[node] & BUSTER_CHECK_UNSEQ_PLANNED) != 0;
            check_common_root_frames(&emission, node);
            if (emission.work_count != 0)
            {
                check_common_emit_run(&emission);
            }
            // Its unsequenced accesses, after its conversions, as clang's CheckCompletedExpr checks them (#1953); not in
            // unreachable code, which clang's DiagRuntimeBehavior drops.
            // (Only a root holding a modification can warn: the cheap tests first.)
            const uint32_t root_token = graph->tokens[node];
            const bool unsequenced_candidate = root && !planned && unsequenced_marks != NULL && node < graph->count &&
                                               (unsequenced_marks[node] & BUSTER_CHECK_UNSEQ_MODIFIES) != 0 && evaluated[node] != 0;
            const bool unsequenced_dropped = unsequenced_candidate && program != NULL &&
                                             (facts->flushed == NULL || root_token >= parser->tokens.count || facts->flushed[root_token] == 0) &&
                                             root_token < parser->tokens.count && check_program_token_dropped(program, root_token);
            if (unsequenced_candidate && !unsequenced_dropped && !pp->fatal)
            {
                check_unseq_check(&emission, &unsequenced, unsequenced_marks, &node, NULL, 1);
            }
            // A brace initializer's list, once its last value is walked.
            while (unsequenced_plan < unsequenced_plan_count && (uint32_t)(unsequenced_plans[unsequenced_plan] >> 32) <= node && !pp->fatal)
            {
                check_unseq_plan(&emission, &unsequenced, unsequenced_marks, program, (uint32_t)unsequenced_plans[unsequenced_plan]);
                unsequenced_plan += 1;
            }
        }
        // The plans whose first node follows every root.
        for (; initializer < initializer_count && !pp->fatal; initializer += 1)
        {
            check_common_initializer_conversions(&emission, program, (uint32_t)initializers[initializer]);
        }
        for (; integer < initializer_keys.integer_count && !pp->fatal; integer += 1)
        {
            check_common_initializer_integers(&emission, program, (uint32_t)initializer_keys.integer[integer] & ~BUSTER_CHECK_INITIALIZER_INNER);
        }
        // A source comma no walk reaches, unevaluated (check_common_unused_unevaluated): its left operand's call whose result
        // warns, as clang's CheckCommaOperands asks DiagnoseUnusedExprResult of it.
        static const CheckOp comma_ops[] = { CHECK_OP_COMMA };
        const CheckCommonOps commas = check_common_ops(comma_ops, BUSTER_ARRAY_LENGTH(comma_ops));
        CheckCommonScan unevaluated = { 0 };
        for (uint32_t node = check_common_next_op(graph, &commas, &unevaluated, vector); node < graph->count && !pp->fatal;
             node = check_common_next_op(graph, &commas, &unevaluated, vector))
        {
            const uint32_t comma = graph->tokens[node];
            if (graph->ops[node] == CHECK_OP_COMMA && facts->evaluated[node] == 0 && stream->diagnostics[node] == CHECK_DIAG_NONE &&
                comma < parser->tokens.count && parser->tokens.kinds[comma] == PARSE_KIND_COMMA)
            {
                check_common_unused_unevaluated(&emission, graph->operand0[node]);
            }
        }
        // Anything a root walk did not reach still reports its own warning at its own place — through the
        // own frame, so a conditional's arms still emit ahead of it.
        CheckCommonScan rest = { 0 };
        for (uint32_t node = check_common_next_reach(reach, graph->count, BUSTER_CHECK_REACH_TREE, &rest, vector); node < graph->count && !pp->fatal;
             node = check_common_next_reach(reach, graph->count, BUSTER_CHECK_REACH_TREE, &rest, vector))
        {
            if (stream->diagnostics[node] == CHECK_DIAG_NONE &&
                (evaluated == NULL || evaluated[node] != 0 || (emitted[node] & BUSTER_CHECK_EMITTED_LITERAL_CONTEXT) != 0) &&
                (emitted[node] & BUSTER_CHECK_EMITTED_TREE) == 0)
            {
                check_common_emit_push(&emission, node, BUSTER_CHECK_EMIT_OWN);
                check_common_emit_push(&emission, node, BUSTER_CHECK_EMIT_CONVERSIONS);
                check_common_emit_run(&emission);
            }
        }
    }
    check_common_shift_finish(pp, shifts);
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
    // An _Atomic argument's lvalue conversion drops _Atomic (#2234): its value type is what the format checks.
    const TypeId stripped = result != 0 ? type_strip_aligned(types, result) : 0;
    result = stripped != 0 && type_kind(types, stripped) == BUSTER_TYPE_KIND_ATOMIC ? type_element(types, stripped) : result;
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
    // (From the node's token itself: a call's is its `)`, whose arguments' commas are no argument's end, #1941.)
    for (uint32_t at = token < parser->tokens.count ? token + 1 : 0; at-- > 0;)
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

// A format argument's type as clang's lvalue conversion leaves it (#1447): without its top-level qualifiers (an array's
// stay with its element), and without the sugar of a typedef that holds them itself (clang's getUnqualifiedType
// desugars `CI` of `typedef const int CI` to `int`); `spelling` is updated to match.
static TypeId check_common_format_value(const Parser* parser, const CheckInput* input, const TypeTable* types, TypeId type, uint32_t* spelling)
{
    const TypeId stripped = type != 0 ? type_strip_aligned(types, type) : 0;
    const bool array = stripped != 0 && type_kind_is_array(type_kind(types, type_unqualified(stripped)));
    const bool qualified = type != 0 && !array && type_qualifiers(type) != 0;
    uint32_t written = 0;
    const uint32_t declaration = *spelling != 0 && *spelling != BUSTER_CHECK_NONE ? check_input_sugar_declaration(input, parser, *spelling, &written) : 0;
    const bool typedef_name = declaration != 0 && declaration < input->token_count && input->declared_types != NULL &&
                              parser->tokens.kinds[declaration] != PARSE_KIND_TYPEOF;
    const TypeId declared = typedef_name ? input->declared_types[declaration] : 0;
    *spelling = qualified && declared != 0 && type_qualifiers(declared) != 0 ? 0 : *spelling;
    return qualified ? type_unqualified(type) : type;
}

static void check_common_format_report_type(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                            TypeTable* types, CheckWarningSpelling* spell, const CheckGraph* graph, uint32_t offset, uint32_t argument,
                                            uint32_t format_token, const char* expected, bool width, uint32_t* order, Arena* scratch)
{
    const uint32_t argument_node = graph->argument_nodes[offset + argument];
    const TypeId argument_type = check_common_format_argument_type(graph, stream, types, offset, argument);
    uint32_t spelling = argument_node < graph->count && argument_type == check_stream_type(stream, argument_node) ?
                        check_common_operand_spelling(parser, input, graph, stream, types, argument_node) : 0;
    const TypeId type = check_common_format_value(parser, input, types, argument_type, &spelling);
    const String8 actual = check_common_type_text(parser, pp, input, types, spell, scratch, type,
                                                  spelling);
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
    const TypeId argument_type = check_common_format_argument_type(graph, stream, types, offset, argument);
    uint32_t spelling = argument_node < graph->count && argument_type == check_stream_type(stream, argument_node) ?
                        check_common_operand_spelling(parser, input, graph, stream, types, argument_node) : 0;
    const TypeId type = check_common_format_value(parser, input, types, argument_type, &spelling);
    const String8 actual = check_common_type_text(parser, pp, input, types, spell, scratch, type,
                                                  spelling);
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
            // Clang checks the format as it builds the call, once its arguments are: after the diagnostics they gave as they
            // were built, a statement expression's own or a nested call's (#1941), so published from the call's `)` on.
            uint32_t order = graph->tokens[node] < parser->tokens.count ? parser->tokens.sources[graph->tokens[node]] : 0;
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
// indirect jump or asm goto every label it may enter. `seen` and `queue` hold body->count elements each. A body's
// statement expressions' entries, after its own (from entry_count), start at each one's first, taken as reached: whether
// one is evaluated is its expression's (check_common_statement_roots), and what its exits reach is reached (#1447).
static void check_common_body_reached(const CheckProgram* program, const CheckProgramBody* body, uint8_t* seen, uint32_t* queue)
{
    const CheckControl* entries = program->controls + body->first;
    memset(seen, 0, body->count);
    uint32_t head = 0;
    uint32_t tail = 1;
    queue[0] = 0;
    seen[0] = 1;
    const uint32_t index = (uint32_t)(body - program->bodies);
    for (uint32_t r = 0; body->entry_count < body->count && program->regions != NULL && r < program->input->region_count; r += 1)
    {
        const uint32_t first = program->regions[r].first;
        if (program->regions[r].body == index && first < body->count && seen[first] == 0)
        {
            seen[first] = 1;
            queue[tail++] = first;
        }
    }
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
            const bool known = !jump && check_program_branch_truth(program, entry->node, entry->node, &truth);
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
                               check_program_case_value(program, condition, item->type, &selected.lower, &selected.wide_lower);
            const bool taken = known && check_program_case_compare(program, &selected, false, item, false) >= 0 &&
                               check_program_case_compare(program, &selected, false, item, true) <= 0;
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
// results — clear their root here, which keeps the node's whole operand chain unevaluated. Initializer entries
// name a plan instead of a node. Their part roots and scalar conversion roots also become propagation barriers:
// a declaration's unreferenced discard wrapper must not make its unreachable initializer evaluated again.
static bool check_common_unreachable(const CheckProgram* program, const CheckGraph* graph,
                                     uint8_t* evaluated, uint8_t** blocked, Arena* scratch)
{
    bool valid = true;
    for (uint32_t b = 0; valid && b < program->body_count; b += 1)
    {
        const CheckProgramBody* body = program->bodies + b;
        if (body->count == 0 || body->entry_count == 0)
        {
            continue;
        }
        // Each body's marks and queue in the same scratch as the last body's, cache-warm, given back after it.
        uint64_t mark = scratch->length;
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
            if (kind == CHECK_CONTROL_INITIALIZE && seen[i] == 0 && program->initializers != NULL &&
                entries[i].target < program->initializers->count && *blocked == NULL)
            {
                // (The barriers' bytes, which a unit without such an initializer never takes. They outlive this body's
                // marks and queue, whose scratch is given back above them, so that the next bodies' don't reuse them.)
                *blocked = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count);
                valid = *blocked != NULL;
                if (valid)
                {
                    memset(*blocked, 0, graph->count);
                    mark = scratch->length;
                }
            }
            if (valid && kind == CHECK_CONTROL_INITIALIZE && seen[i] == 0 && program->initializers != NULL &&
                entries[i].target < program->initializers->count)
            {
                const uint32_t declaration = entries[i].token;
                const uint32_t scalar = declaration < program->input->token_count ?
                                        check_common_sparse_get(program->input->initializer_nodes, declaration) : BUSTER_CHECK_NONE;
                if (scalar < graph->count)
                {
                    evaluated[scalar] = 0;
                    (*blocked)[scalar] = 1;
                }
                const CheckInitializer* plan = program->initializers->plans + entries[i].target;
                for (uint32_t part = 0; part < plan->count; part += 1)
                {
                    if (plan->parts[part].kind == CHECK_INIT_VALUE && plan->parts[part].node < graph->count)
                    {
                        evaluated[plan->parts[part].node] = 0;
                        (*blocked)[plan->parts[part].node] = 1;
                    }
                }
            }
        }
        arena_pop_to(scratch, mark);
    }
    return valid;
}

// A static selection's range: the tokens from just past its keyword up to its closing token, which it excludes.
typedef struct CheckCommonRange CheckCommonRange;
struct CheckCommonRange
{
    uint32_t first;
    uint32_t end;
};

// Only selections with a matching keyword need an operand span. The keyword pass and the closing-token sort join
// these records once, so neither an __extension__ nor a selection's lookup scans the other selections.
typedef struct CheckCommonSelection CheckCommonSelection;
struct CheckCommonSelection
{
    uint32_t close;
    uint32_t node;
    uint32_t first;
    CheckCommonRange chosen;
};

// The tokens of the operand the static selection `node` chooses, from just past the top-level comma before it (or the
// selection's `(`) up to the comma after it (or its `)`), which it excludes: a root there, a statement of a statement
// expression in the chosen arm, is evaluated where the selection is (#2023). Its operand is the selection's first,
// under the conversions at its `)`; none (an empty range) for one the reader folded to a constant at its `)`, or
// without the parser's depths.
static CheckCommonRange check_common_chosen_span(const CheckGraph* graph, const Parser* parser, uint32_t node)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t close = graph->ops[node] == CHECK_OP_SELECT ? graph->tokens[node] : BUSTER_CHECK_NONE;
    uint32_t chosen = close < count ? graph->operand0[node] : BUSTER_CHECK_NONE;
    for (uint32_t steps = 0; chosen < graph->count && graph->ops[chosen] == CHECK_OP_CONVERT && graph->tokens[chosen] == close && steps < graph->count;
         steps += 1)
    {
        chosen = graph->operand0[chosen];
    }
    const uint32_t token = chosen < graph->count ? graph->tokens[chosen] : BUSTER_CHECK_NONE;
    CheckCommonRange result = { 0 };
    if (parser->tree.depths != NULL && token < close)
    {
        // (The selection's contents are one deeper than its parentheses.)
        const uint32_t depth = parser->tree.depths[close] + 1;
        uint32_t first = token;
        while (first > 0 && parser->tree.depths[first - 1] >= depth &&
               !(parser->tokens.kinds[first - 1] == PARSE_KIND_COMMA && parser->tree.depths[first - 1] == depth))
        {
            first -= 1;
        }
        uint32_t end = token;
        while (end < close && !(parser->tokens.kinds[end] == PARSE_KIND_COMMA && parser->tree.depths[end] == depth))
        {
            end += 1;
        }
        result = (CheckCommonRange){ .first = first, .end = end };
    }
    return result;
}

// The token under a static selection's conversions at its closing parenthesis. A conversion cycle has that closing
// token too, so, like check_common_chosen_span, the bounded walk yields no chosen span (the token is not before it).
static uint32_t check_common_chosen_token(const CheckGraph* graph, const Parser* parser, uint32_t node)
{
    const uint32_t nodes = graph->count;
    const uint32_t count = parser->tokens.count;
    const CheckOp* const ops = graph->ops;
    const uint32_t* const tokens = graph->tokens;
    const uint32_t* const operands = graph->operand0;
    const uint32_t close = ops[node] == CHECK_OP_SELECT ? tokens[node] : BUSTER_CHECK_NONE;
    uint32_t chosen = close < count ? operands[node] : BUSTER_CHECK_NONE;
    for (uint32_t steps = 0; chosen < nodes && ops[chosen] == CHECK_OP_CONVERT && tokens[chosen] == close && steps < nodes; steps += 1)
    {
        chosen = operands[chosen];
    }
    const uint32_t result = chosen < nodes ? tokens[chosen] : BUSTER_CHECK_NONE;
    return result;
}

typedef struct CheckCommonSelectionWork CheckCommonSelectionWork;
struct CheckCommonSelectionWork
{
    uint64_t walked;
    uint64_t indexed;
    uint64_t queries;
};

// Parser depths count open bracket and secondary (? / case) groups. There are at most count openers, and the sum of
// positive depth changes is linear in their count (parse_brackets_* and parse_secondary_*); 2 * count also covers the
// closer's depth adjustment. Check that bound before the index fills an entry for each newly entered depth. Synthetic
// trees with arbitrary depths retain the exact walk, rather than allocating by an unchecked depth or filling gaps
// quadratically. The vector path checks 16 depths at a time; the scalar twin checks every adjacent pair.
static bool check_common_chosen_depths(const Parser* parser, bool vector, uint32_t* maximum, uint64_t* visited)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t* const depths = parser->tree.depths;
    uint32_t highest = 0;
    uint32_t previous = 0;
    uint64_t rises = 0;
    bool bounded = depths != NULL;
    for (uint32_t first = 0; bounded && first < count;)
    {
        const uint32_t live = count - first < 16 ? count - first : 16;
        if (BUSTER_VECTOR_PATH(vector))
        {
            const __mmask16 lanes = (__mmask16)((1u << live) - 1);
            const __m512i values = _mm512_maskz_loadu_epi32(lanes, depths + first);
            const __m512i before = _mm512_alignr_epi32(values, _mm512_set1_epi32((int)previous), 15);
            const __mmask16 rising = _mm512_mask_cmpgt_epu32_mask(lanes, values, before);
            const __m512i increments = _mm512_maskz_sub_epi32(rising, values, before);
            const uint32_t high = _mm512_reduce_max_epu32(values);
            highest = high > highest ? high : highest;
            rises += (uint64_t)_mm512_reduce_add_epi64(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(increments)));
            rises += (uint64_t)_mm512_reduce_add_epi64(_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(increments, 1)));
            previous = depths[first + live - 1];
        }
        else
        {
            for (uint32_t lane = 0; lane < live; lane += 1)
            {
                const uint32_t depth = depths[first + lane];
                highest = depth > highest ? depth : highest;
                rises += depth > previous ? depth - previous : 0;
                previous = depth;
            }
        }
        bounded = highest <= count && rises <= (uint64_t)count * 2;
        first += live;
        *visited += live;
    }
    *maximum = highest;
    return bounded;
}

// Fill every selection's chosen range. Short operands keep the walk: its visited tokens are exactly end - first,
// so switching after count visits bounds that work by 2 * count, including the final operand. Only overlapping long
// operands pay for the index. Queries sorted by their chosen token read two sweeps: the nearest earlier token below
// their depth or comma at that depth, and the next comma at that depth. A query's depth is its selection's closing
// depth + 1, even when a test's chosen token is unrelated to that selection. This preserves the walk's exact meaning.
// Selections live below this helper's scratch mark and do not overlap the immutable graph or parser columns.
static bool check_common_chosen_spans(const CheckGraph* graph, const Parser* parser, CheckCommonSelection* selections, uint32_t selection_count,
                                      bool vector, Arena* scratch, CheckCommonSelectionWork* work)
{
    const uint32_t count = parser->tokens.count;
    uint64_t visited = 0;
    uint64_t indexed_work = 0;
    uint64_t indexed_queries = 0;
    uint32_t completed = 0;
    while (completed < selection_count && visited < count)
    {
        const CheckCommonRange chosen = check_common_chosen_span(graph, parser, selections[completed].node);
        selections[completed].chosen = chosen;
        visited += chosen.end - chosen.first;
        completed += 1;
    }
    uint32_t maximum = 0;
    const bool indexed = completed < selection_count && check_common_chosen_depths(parser, vector, &maximum, &indexed_work);
    bool valid = true;
    if (completed < selection_count && !indexed)
    {
        for (uint32_t i = completed; i < selection_count; i += 1)
        {
            const CheckCommonRange chosen = check_common_chosen_span(graph, parser, selections[i].node);
            selections[i].chosen = chosen;
            visited += chosen.end - chosen.first;
        }
    }
    else if (indexed)
    {
        uint32_t queries = 0;
        uint32_t last = 0;
        for (uint32_t i = completed; i < selection_count; i += 1)
        {
            const uint32_t token = check_common_chosen_token(graph, parser, selections[i].node);
            const uint32_t close = selections[i].close;
            // The temporary range holds the query's token and bound until the two sweeps replace them.
            selections[i].chosen = token < close ? (CheckCommonRange){ .first = token, .end = close } : (CheckCommonRange){ 0 };
            queries += token < close;
            last = token < close && close > last ? close : last;
        }
        const uint64_t mark = scratch->length;
        uint64_t* keys = queries != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, queries) : NULL;
        uint64_t* temporary = queries != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, queries) : NULL;
        uint32_t* positions = queries != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, (uint64_t)maximum + 1) : NULL;
        valid = queries == 0 || (keys != NULL && temporary != NULL && positions != NULL);
        if (valid && queries != 0)
        {
            indexed_queries = queries;
            uint32_t used = 0;
            uint32_t any = 0;
            uint32_t every = UINT32_MAX;
            for (uint32_t i = completed; i < selection_count; i += 1)
            {
                const uint32_t token = selections[i].chosen.first;
                if (selections[i].chosen.end > token)
                {
                    keys[used] = (uint64_t)token << 32 | i;
                    used += 1;
                    any |= token;
                    every &= token;
                }
            }
            const uint64_t* const sorted = c_bindings_sort(keys, temporary, queries, vector, any ^ every);
            const uint32_t* const depths = parser->tree.depths;
            const uint8_t* const kinds = parser->tokens.kinds;
            const uint32_t through = (uint32_t)(sorted[queries - 1] >> 32) + 1;
            positions[0] = 0;
            indexed_work += 1;
            uint32_t previous = 0;
            uint32_t query = 0;
            for (uint32_t first = 0; first < through;)
            {
                const uint32_t live = through - first < 16 ? through - first : 16;
                const uint32_t lanes = (1u << live) - 1;
                uint32_t pending = lanes;
                if (BUSTER_VECTOR_PATH(vector))
                {
                    const __m512i values = _mm512_maskz_loadu_epi32((__mmask16)lanes, depths + first);
                    const __m512i before = _mm512_alignr_epi32(values, _mm512_set1_epi32((int)previous), 15);
                    const __m512i codes = _mm512_maskz_loadu_epi8((__mmask64)lanes, kinds + first);
                    pending = _mm512_mask_cmpneq_epu32_mask((__mmask16)lanes, values, before) |
                              (uint32_t)_mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, codes, _mm512_set1_epi8((char)PARSE_KIND_COMMA));
                    for (uint32_t q = query; q < queries && (uint32_t)(sorted[q] >> 32) < first + live; q += 1)
                    {
                        pending |= 1u << ((uint32_t)(sorted[q] >> 32) - first);
                    }
                }
                while (pending != 0)
                {
                    const uint32_t lane = (uint32_t)__builtin_ctz(pending);
                    pending &= pending - 1;
                    const uint32_t at = first + lane;
                    // Start is exclusive of the previous comma, so answer before processing this token.
                    while (query < queries && (uint32_t)(sorted[query] >> 32) == at)
                    {
                        const uint32_t i = (uint32_t)sorted[query];
                        const uint32_t depth = depths[selections[i].close] + 1;
                        selections[i].chosen.first = depth <= previous ? positions[depth] : at;
                        query += 1;
                    }
                    const uint32_t depth = depths[at];
                    // A newly entered depth starts just after the last shallower token. Each filled entry is charged
                    // to the checked sum of positive depth changes; leaving a depth needs no clearing.
                    for (uint32_t level = previous; level < depth;)
                    {
                        const uint32_t fill = depth - level < 16 ? depth - level : 16;
                        if (BUSTER_VECTOR_PATH(vector))
                        {
                            _mm512_mask_storeu_epi32(positions + level + 1, (__mmask16)((1u << fill) - 1), _mm512_set1_epi32((int)at));
                        }
                        else
                        {
                            for (uint32_t d = 1; d <= fill; d += 1)
                            {
                                positions[level + d] = at;
                            }
                        }
                        level += fill;
                        indexed_work += fill;
                    }
                    if (kinds[at] == PARSE_KIND_COMMA)
                    {
                        positions[depth] = at + 1;
                    }
                    previous = depth;
                }
                first += live;
                indexed_work += live;
            }
            memset(positions, 0xff, ((uint64_t)maximum + 1) * sizeof(uint32_t));
            indexed_work += (uint64_t)maximum + 1;
            query = queries;
            for (uint32_t end = last; end != 0 && query != 0;)
            {
                const uint32_t first = end > 64 ? end - 64 : 0;
                const uint32_t live = end - first;
                const uint64_t lanes = live == 64 ? UINT64_MAX : (UINT64_C(1) << live) - 1;
                uint64_t pending = lanes;
                if (BUSTER_VECTOR_PATH(vector))
                {
                    const __m512i codes = _mm512_maskz_loadu_epi8((__mmask64)lanes, kinds + first);
                    pending = _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, codes, _mm512_set1_epi8((char)PARSE_KIND_COMMA));
                    for (uint32_t q = query; q != 0 && (uint32_t)(sorted[q - 1] >> 32) >= first; q -= 1)
                    {
                        pending |= UINT64_C(1) << ((uint32_t)(sorted[q - 1] >> 32) - first);
                    }
                }
                while (pending != 0)
                {
                    const uint32_t lane = 63 - (uint32_t)__builtin_clzll(pending);
                    pending &= ~(UINT64_C(1) << lane);
                    const uint32_t at = first + lane;
                    if (kinds[at] == PARSE_KIND_COMMA)
                    {
                        positions[depths[at]] = at;
                    }
                    // End is inclusive of the chosen token when it is itself a comma, so answer after processing it.
                    while (query != 0 && (uint32_t)(sorted[query - 1] >> 32) == at)
                    {
                        const uint32_t i = (uint32_t)sorted[query - 1];
                        const uint32_t close = selections[i].close;
                        const uint32_t depth = depths[close] + 1;
                        const uint32_t comma = depth <= maximum ? positions[depth] : BUSTER_CHECK_NONE;
                        selections[i].chosen.end = comma < close ? comma : close;
                        query -= 1;
                    }
                }
                end = first;
                indexed_work += live;
            }
        }
        arena_pop_to(scratch, mark);
    }
    if (work != NULL)
    {
        *work = (CheckCommonSelectionWork){
            .walked = visited,
            .indexed = indexed_work,
            .queries = indexed_queries,
        };
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
        const CheckCommonRange chosen = check_common_chosen_span(graph, parser, node);
        for (uint32_t candidate = 0; close < parser->tokens.count && candidate < graph->count; candidate += 1)
        {
            const uint32_t at = graph->tokens[candidate];
            if (roots[candidate] != 0 && candidate != selected && at > token && at < close && !(at >= chosen.first && at < chosen.end))
            {
                marks[candidate] = value;
            }
        }
    }
}

// Sets the bits from `first` up to `end`, which it excludes, a word at a time.
static uint32_t check_common_set_bits(uint64_t* bits, uint32_t first, uint32_t end)
{
    uint32_t touched = 0;
    for (uint32_t at = first; at < end;)
    {
        const uint32_t word = at >> 6;
        const uint32_t stop = end - (at & ~63u) < 64 ? end : (at & ~63u) + 64;
        const uint32_t width = stop - at;
        const uint64_t ones = width == 64 ? UINT64_MAX : ((UINT64_C(1) << width) - 1) << (at & 63);
        bits[word] |= ones;
        at = stop;
        touched += 1;
    }
    return touched;
}

// Union of nonempty intervals sorted by first (the key's high half). Charge a word only when new bits are added:
// every interval contributes at most one shared boundary word, and every other touched word is new. The return value
// lets the regression check that bound without timing; production only needs the union and its final end.
static uint64_t check_common_merge_ranges(uint64_t* bits, const uint64_t* ranges, uint32_t count, uint32_t* covered)
{
    uint64_t touched = 0;
    uint32_t end = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t first = (uint32_t)(ranges[i] >> 32);
        const uint32_t next = (uint32_t)ranges[i];
        const uint32_t begin = first > end ? first : end;
        touched += check_common_set_bits(bits, begin, next);
        end = next > end ? next : end;
    }
    *covered = end;
    return touched;
}

// A static selection (_Generic, __builtin_choose_expr) checks every arm but evaluates only the chosen one; its other
// arms and controlling expression are otherwise unreferenced roots. Set their marks to value (marks may be roots).
// Find all closing tokens and their last keyword child in batched scans, join them by a radix sort, and find the chosen
// spans together. Sort the at most two excluded pieces per selection by start and merge them while setting their bits:
// nested unselected arms never write an already covered word again apart from their boundary word (#1341, #1360).
// Work is linear in nodes, tokens and selections for parser-produced depths. An __extension__ has no matching keyword
// and needs no chosen-span work. The defensive walk remains for a selected node in roots; callers supply parentless
// roots, so a production selection's operand cannot take it. A failed scratch allocation leaves marks unchanged.
static bool check_common_unselected_roots(const CheckGraph* graph, const Parser* parser, const uint8_t* roots, uint8_t* marks, uint8_t value,
                                          Arena* scratch)
{
    // These borrowed columns remain unchanged through the scratch-only planning; marks may alias roots, not the graph
    // or parser. Cache their counts and pointers before writing scratch and the final marks.
    const uint32_t count = parser->tokens.count;
    const uint32_t nodes = graph->count;
    const CheckOp* const ops = graph->ops;
    const uint32_t* const tokens = graph->tokens;
    const uint32_t* const operands = graph->operand0;
    const uint8_t* const kinds = parser->tokens.kinds;
    const uint32_t* const parents = parser->tree.parents;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    const uint64_t mark = scratch->length;
    static const CheckOp selects[] = { CHECK_OP_SELECT };
    const CheckCommonOps set = check_common_ops(selects, BUSTER_ARRAY_LENGTH(selects));
    uint32_t selections = 0;
    uint32_t last = 0;
    bool walk = false;
    CheckCommonScan scan = { 0 };
    for (uint32_t node = check_common_next_op(graph, &set, &scan, vector); node < nodes; node = check_common_next_op(graph, &set, &scan, vector))
    {
        const uint32_t close = tokens[node];
        if (ops[node] == CHECK_OP_SELECT && close < count)
        {
            const uint32_t selected = operands[node];
            walk |= selected < nodes && roots[selected] != 0;
            selections += 1;
            last = close >= last ? close + 1 : last;
        }
    }
    bool valid = true;
    if (selections != 0 && !walk)
    {
        // Both sorts need at most two keys per selection. Their count is uint32_t; refuse an unrepresentable count
        // before allocating or changing marks, just as an arena unable to hold these columns is refused.
        valid = selections <= UINT32_MAX / 2;
        const uint64_t words = ((uint64_t)last + 63) / 64;
        uint64_t* const bits = valid ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, words) : NULL;
        uint64_t* const keys = bits != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, (uint64_t)selections * 2) : NULL;
        uint64_t* const temporary = keys != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, (uint64_t)selections * 2) : NULL;
        CheckCommonSelection* const records = temporary != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, CheckCommonSelection, selections) : NULL;
        valid = records != NULL;
        uint32_t used = 0;
        if (valid)
        {
            memset(bits, 0, words * sizeof(uint64_t));
            scan = (CheckCommonScan){ 0 };
            for (uint32_t node = check_common_next_op(graph, &set, &scan, vector); node < nodes; node = check_common_next_op(graph, &set, &scan, vector))
            {
                const uint32_t close = tokens[node];
                if (ops[node] == CHECK_OP_SELECT && close < count)
                {
                    bits[close >> 6] |= UINT64_C(1) << (close & 63);
                    keys[used] = (uint64_t)close << 32 | node;
                    used += 1;
                }
            }
        }
        const uint32_t varying = last != 0 ? UINT32_MAX >> __builtin_clz(last) : 0;
        const uint64_t* const closes = valid ? c_bindings_sort(keys, temporary, selections, vector, varying) : NULL;
        used = 0;
        // The two halves do not overlap the closing-token keys. Claim each closing token once, by its last keyword
        // child, exactly as the oracle's backward walk does. Sort these sparse claims to join the closing-token keys.
        for (uint32_t end = valid ? last : 0; end != 0 && used < selections;)
        {
            const uint32_t first = end > 64 ? end - 64 : 0;
            const uint32_t live = end - first;
            const uint64_t lanes = live == 64 ? UINT64_MAX : (UINT64_C(1) << live) - 1;
            uint64_t pending = lanes;
            if (vector)
            {
                const __m512i block = _mm512_maskz_loadu_epi8((__mmask64)lanes, kinds + first);
                pending = _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, block, _mm512_set1_epi8((char)PARSE_KIND_GENERIC)) |
                          _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, block, _mm512_set1_epi8((char)PARSE_KIND_TYPE_ARGUMENT));
            }
            while (pending != 0)
            {
                const uint32_t lane = 63 - (uint32_t)__builtin_clzll(pending);
                pending &= ~(UINT64_C(1) << lane);
                const uint32_t at = first + lane;
                const uint8_t kind = kinds[at];
                const uint32_t parent = parents[at];
                if ((kind == PARSE_KIND_GENERIC || kind == PARSE_KIND_TYPE_ARGUMENT) && at < parent && parent < last &&
                    (bits[parent >> 6] & (UINT64_C(1) << (parent & 63))) != 0)
                {
                    bits[parent >> 6] &= ~(UINT64_C(1) << (parent & 63));
                    keys[selections + used] = (uint64_t)parent << 32 | (at + 1);
                    used += 1;
                }
            }
            end = first;
        }
        const uint64_t* const keywords = valid ? c_bindings_sort(keys + selections, temporary + selections, used, vector, varying) : NULL;
        uint32_t matched = 0;
        uint32_t keyword = 0;
        for (uint32_t i = 0; valid && i < selections && keyword < used; i += 1)
        {
            const uint32_t close = (uint32_t)(closes[i] >> 32);
            while (keyword < used && (uint32_t)(keywords[keyword] >> 32) < close)
            {
                keyword += 1;
            }
            if (keyword < used && (uint32_t)(keywords[keyword] >> 32) == close)
            {
                records[matched] = (CheckCommonSelection){
                    .close = close,
                    .node = (uint32_t)closes[i],
                    .first = (uint32_t)keywords[keyword],
                };
                matched += 1;
            }
        }
        valid = valid && check_common_chosen_spans(graph, parser, records, matched, vector, scratch, NULL);
        uint32_t pieces = 0;
        for (uint32_t i = 0; valid && i < matched; i += 1)
        {
            const uint32_t first = records[i].first;
            const uint32_t close = records[i].close;
            const CheckCommonRange chosen = records[i].chosen;
            const bool split = chosen.end > chosen.first && chosen.end > first && chosen.first < close;
            const uint32_t gap = split && chosen.first > first ? chosen.first : first;
            const uint32_t resume = split && chosen.end < close ? chosen.end : close;
            const uint32_t end = split ? gap : close;
            if (first < end)
            {
                keys[pieces++] = (uint64_t)first << 32 | end;
            }
            if (split && resume < close)
            {
                keys[pieces++] = (uint64_t)resume << 32 | close;
            }
        }
        const uint64_t* const ranges = valid ? c_bindings_sort(keys, temporary, pieces, vector, varying) : NULL;
        uint32_t covered = 0;
        const uint32_t lowest = pieces != 0 ? (uint32_t)(ranges[0] >> 32) : last;
        if (valid && pieces != 0)
        {
            memset(bits, 0, words * sizeof(uint64_t));
            check_common_merge_ranges(bits, ranges, pieces, &covered);
            // Duplicate closing tokens simply contribute their own pieces to the union. That excludes the intersection
            // of their chosen spans, including an empty one, without a binary lookup or another node scan per selection.
            for (uint32_t first = 0; first < nodes;)
            {
                const uint32_t live = nodes - first < 16 ? nodes - first : 16;
                const uint32_t lanes = live == 16 ? 0xffffu : (1u << live) - 1;
                if (vector)
                {
                    const __m512i root_bytes = _mm512_maskz_loadu_epi8((__mmask64)lanes, roots + first);
                    const __m512i places = _mm512_maskz_loadu_epi32((__mmask16)lanes, tokens + first);
                    const __mmask16 candidates = (__mmask16)_mm512_mask_test_epi8_mask((__mmask64)lanes, root_bytes, root_bytes) &
                                                 _mm512_mask_cmplt_epu32_mask((__mmask16)lanes, places, _mm512_set1_epi32((int)covered)) &
                                                 _mm512_mask_cmpge_epu32_mask((__mmask16)lanes, places, _mm512_set1_epi32((int)lowest));
                    if (candidates != 0)
                    {
                        const __m512i covers = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), candidates, _mm512_srli_epi32(places, 5), bits, 4);
                        const __m512i bit = _mm512_sllv_epi32(_mm512_set1_epi32(1), _mm512_and_si512(places, _mm512_set1_epi32(31)));
                        const __mmask16 marked = _mm512_mask_test_epi32_mask(candidates, covers, bit);
                        _mm512_mask_storeu_epi8(marks + first, (__mmask64)marked, _mm512_set1_epi8((char)value));
                    }
                }
                else
                {
                    for (uint32_t node = first; node < first + live; node += 1)
                    {
                        const uint32_t token = tokens[node];
                        if (roots[node] != 0 && token < covered && token >= lowest && (bits[token >> 6] & (UINT64_C(1) << (token & 63))) != 0)
                        {
                            marks[node] = value;
                        }
                    }
                }
                first += live;
            }
        }
    }
    if (walk)
    {
        check_common_unselected_walk(graph, parser, roots, marks, value);
    }
    arena_pop_to(scratch, mark);
    return valid;
}

// Whether a node discards its first operand's value: a comma's left operand, and a void cast's, statement-level or explicit.
static bool check_common_discards(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node)
{
    const CheckOp op = graph->ops[node];
    const TypeId result_type = op == CHECK_OP_CAST ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, node))) : 0;
    const bool result = op == CHECK_OP_COMMA || (result_type != 0 && type_kind(types, result_type) == BUSTER_TYPE_KIND_VOID);
    return result;
}

// The operands and arguments of `node` that its evaluation evaluates, as check_common_evaluated's reverse pass reads them:
// an operand's or argument's bit `i`, any at 32 and past. A sizeof's or alignof's operand isn't, but a variably modified
// type's bound; nor is a constant condition's unselected arm, nor the arm of a condition one operand of a logical
// operator decides though the other isn't known (`x && 0`, `x || 1`, through `!` and casts), which clang's CFG builder
// leaves unreachable for the runtime warnings (#1447, tryEvaluateBool in VisitConditionalOperator and
// VisitLogicalOperator: check_program_branch_truth, with the unit's program).
static uint32_t check_common_operand_mask(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, const CheckProgram* program,
                                          uint32_t node)
{
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
        bool known = condition < graph->count && check_action_truth(types, &condition_value, &truth);
        if (!known && program != NULL && condition < graph->count)
        {
            known = check_program_branch_truth(program, node, condition, &truth);
        }
        if (known)
        {
            mask = op == CHECK_OP_CONDITIONAL ? (truth ? 3u : 5u) :
                   ((op == CHECK_OP_LOGICAL_AND && !truth) || (op == CHECK_OP_LOGICAL_OR && truth)) ? 1u : 3u;
        }
    }
    return mask;
}

// The statements of the statement expression `node` (a STATEMENT_VALUE, at its `)`) that the source doesn't evaluate: their
// roots, which no parent reads, taken as unevaluated as it is (#1447: clang's CFG leaves a short-circuited operand's, a
// constant conditional's other arm's and unreachable code's statement expression's blocks unreachable). One in a sizeof's
// or an alignof's operand is unevaluated, not unreachable, and its statements keep their warnings. Its statements' nodes
// precede it, from its `(` on; the scan back stops at the first node before that.
static void check_common_statement_roots(const CheckGraph* graph, const Parser* parser, const uint32_t* parents, uint32_t node, uint8_t* evaluated)
{
    bool queried = false;
    for (uint32_t at = parents[node], steps = 0; !queried && at < graph->count && steps < graph->count; at = parents[at], steps += 1)
    {
        queried = graph->ops[at] == CHECK_OP_SIZEOF || graph->ops[at] == CHECK_OP_ALIGNOF;
    }
    const uint32_t close = queried ? BUSTER_CHECK_NONE : graph->tokens[node];
    uint32_t open = BUSTER_CHECK_NONE;
    for (uint32_t at = close; close < parser->tokens.count && open == BUSTER_CHECK_NONE && at-- > 0;)
    {
        open = parser->tree.roles[at] == PARSE_ROLE_OPEN_GROUP && parser->tree.parents[at] == close ? at : open;
    }
    bool inside = open < close;
    for (uint32_t at = node; inside && at-- > 0;)
    {
        const uint32_t token = graph->tokens[at];
        inside = token > open;
        evaluated[at] = inside && token < close && parents[at] == BUSTER_CHECK_NONE ? 0 : evaluated[at];
    }
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
            // Exclude dropped ranges before propagating evaluation. A detached root in an unevaluated query (a VLA bound in
            // sizeof's pointer type) stays one, as clang's comma and unused-value warnings reach it; the conversion walk
            // leaves its nodes out itself (check_common_query_suppressed).
            evaluated[node] = parents[node] == BUSTER_CHECK_NONE &&
                              (program == NULL || program->dropped_range_count == 0 || !check_program_token_dropped(program, graph->tokens[node]));
        }
        // An unreachable initializer's roots, which no parent makes evaluated (check_common_unreachable); NULL in a unit
        // without one, whose propagation then reads no barrier.
        uint8_t* blocked = NULL;
        if (program != NULL && program->controls != NULL && !check_common_unreachable(program, graph, evaluated, &blocked, scratch))
        {
            result = NULL;
        }
        if (!check_common_unselected_roots(graph, parser, evaluated, evaluated, 0, scratch))
        {
            result = NULL;
        }
        for (uint32_t node = graph->count; node-- > 0;)
        {
            const CheckOp op = graph->ops[node];
            if (evaluated[node] == 0 && op == CHECK_OP_STATEMENT_VALUE)
            {
                check_common_statement_roots(graph, parser, parents, node, evaluated);
            }
            if (evaluated[node] == 0)
            {
                continue;
            }
            const uint32_t mask = check_common_operand_mask(graph, stream, types, program, node);
            const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
            {
                if (operands[i] < graph->count && (blocked == NULL || blocked[operands[i]] == 0) && (i >= 32 || (mask & (1u << i)) != 0))
                {
                    evaluated[operands[i]] = 1;
                }
            }
            if (check_graph_has_arguments(op) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
            {
                for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                {
                    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
                    if (argument < graph->count && (blocked == NULL || blocked[argument] == 0) && (i >= 32 || (mask & (1u << i)) != 0))
                    {
                        evaluated[argument] = 1;
                    }
                }
            }
        }
    }
    return result;
}

// A static assertion's floating literal, `<<` or conditional (check_common_truth_operands) as the conversion walk's
// candidate: its operand, a root with a constant-warning entry each, takes the bit from that short list rather than a
// test of every node's context. Clang's DiagnoseIntInBoolContext warns of the latter two there too (#2409).
static void check_common_assertion_reach(const CheckGraph* graph, const CheckStream* stream, uint8_t* reach)
{
    for (uint32_t i = 0; graph->contexts != NULL && i < graph->const_warning_count; i += 1)
    {
        const uint32_t node = graph->const_warnings[i].node;
        if (node < graph->count && (graph->contexts[node] & BUSTER_CHECK_CONTEXT_ASSERT) != 0 && check_common_truth_candidate(graph, stream, node))
        {
            reach[node] |= BUSTER_CHECK_REACH_CONVERSION;
        }
    }
}

// The operators whose own reach (check_common_own_reach) may have a bit: check_common_candidate_bits', a truth context's
// (#1453), a null dereference's (#1447, check_common_arithmetic_own's), arithmetic and comparisons. Comparisons stay
// last so an ignored -Wsign-compare can omit them from the candidate set.
static const CheckOp check_common_reach_ops[] =
{
    CHECK_OP_INITIALIZE,
    CHECK_OP_ASSIGN,
    CHECK_OP_CONVERT,
    CHECK_OP_CALL,
    CHECK_OP_SUBSCRIPT,
    CHECK_OP_COMMA,
    CHECK_OP_NOT,
    CHECK_OP_LOGICAL_AND,
    CHECK_OP_LOGICAL_OR,
    CHECK_OP_CONDITION,
    CHECK_OP_CONDITIONAL,
    CHECK_OP_DEREFERENCE,
    CHECK_OP_DIVIDE,
    CHECK_OP_REMAINDER,
    CHECK_OP_SHIFT_LEFT,
    CHECK_OP_SHIFT_RIGHT,
    CHECK_OP_SHIFT_LEFT_ASSIGN,
    CHECK_OP_SHIFT_RIGHT_ASSIGN,
    CHECK_OP_LESS,
    CHECK_OP_LESS_EQUAL,
    CHECK_OP_GREATER,
    CHECK_OP_GREATER_EQUAL,
    CHECK_OP_EQUAL,
    CHECK_OP_NOT_EQUAL,
};

// The operators that may leave an operand unevaluated (check_common_operand_mask).
static const CheckOp check_common_mask_ops[] =
{
    CHECK_OP_SIZEOF,
    CHECK_OP_ALIGNOF,
    CHECK_OP_CONDITIONAL,
    CHECK_OP_LOGICAL_AND,
    CHECK_OP_LOGICAL_OR,
};

// Each node's reader, the node that reads it as an operand or an argument, into `parents` (BUSTER_CHECK_NONE for a node no
// other reads; `count` + 1 entries, the last a sink that the absent operands write). True where the graph is a tree in
// node order, which the walks up from a node to its root need: each node's operands and arguments precede it, and no node
// has two readers, or one reading it twice (each edge writes its child's entry, and only distinct children's entries
// count as many as the edges). The vector path takes 16 nodes at a time: each operand slot's present children and their
// nodes compressed to the front and written a child at a time (two edges that write one child fail the count, whatever
// order they write in), and the arguments of the nodes whose operators have them, found from the same 16 operators; the
// scalar path, its twin, a node at a time.
static bool check_common_parents(const CheckGraph* graph, uint32_t* parents, bool scalar)
{
    (void)scalar;

    const uint32_t count = graph->count;
    memset(parents, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
    uint64_t edges = 0;
    bool ordered = true;
    uint32_t node = 0;
    const bool arguments = graph->argument_offsets != NULL && graph->argument_counts != NULL;
    // The operators with arguments, a bit each, as check_graph_has_arguments has them: a lane's word a permute away.
    uint32_t argued[(CHECK_OP_COUNT + 31) / 32] = { 0 };
    for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
    {
        argued[op >> 5] |= check_graph_has_arguments((CheckOp)op) ? 1u << (op & 31) : 0u;
    }
    _Static_assert(CHECK_OP_COUNT <= 512, "the operators' words fit one register");
    const __m512i words = _mm512_maskz_loadu_epi32((__mmask16)((1u << BUSTER_ARRAY_LENGTH(argued)) - 1), argued);
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const __m512i limit = _mm512_set1_epi32((int)count);
    const uint32_t* slots[] = { graph->operand0, graph->operand1, graph->operand2 };
    _Alignas(64) uint32_t packed_children[16];
    _Alignas(64) uint32_t packed_nodes[16];
    for (; !BUSTER_SCALAR_PATH(scalar) && count - node >= 16; node += 16)
    {
        const __m512i nodes = _mm512_add_epi32(_mm512_set1_epi32((int)node), lanes);
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(slots); i += 1)
        {
            const __m512i children = _mm512_loadu_si512(slots[i] + node);
            const __mmask16 edge = _mm512_cmplt_epu32_mask(children, limit);
            const uint32_t present = (uint32_t)__builtin_popcount(edge);
            ordered &= _mm512_mask_cmplt_epu32_mask(edge, children, nodes) == edge;
            edges += present;
            // The edges' children and nodes packed to the front, then stored a child at a time: a scatter costs as much
            // whatever its mask, and most slots of most nodes are absent (clang would make the stores a scatter again).
            _mm512_store_si512(packed_children, _mm512_maskz_compress_epi32(edge, children));
            _mm512_store_si512(packed_nodes, _mm512_maskz_compress_epi32(edge, nodes));
#pragma clang loop vectorize(disable) interleave(disable)
            for (uint32_t k = 0; k < present; k += 1)
            {
                parents[packed_children[k]] = packed_nodes[k];
            }
        }
        const __m512i ops = _mm512_loadu_si512(graph->ops + node);
        const __m512i word = _mm512_permutexvar_epi32(_mm512_srli_epi32(ops, 5), words);
        const __m512i bit = _mm512_sllv_epi32(_mm512_set1_epi32(1), _mm512_and_si512(ops, _mm512_set1_epi32(31)));
        for (uint32_t argued_lanes = arguments ? _mm512_test_epi32_mask(word, bit) : 0; argued_lanes != 0; argued_lanes &= argued_lanes - 1)
        {
            const uint32_t at = node + (uint32_t)__builtin_ctz(argued_lanes);
            for (uint32_t a = 0; a < graph->argument_counts[at]; a += 1)
            {
                const uint32_t argument = graph->argument_nodes[graph->argument_offsets[at] + a];
                const bool edge = argument < count;
                ordered &= !edge || argument < at;
                parents[edge ? argument : count] = at;
                edges += edge;
            }
        }
    }
    for (; node < count; node += 1)
    {
        const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(operands); o += 1)
        {
            const bool edge = operands[o] < count;
            ordered &= !edge || operands[o] < node;
            parents[edge ? operands[o] : count] = node;
            edges += edge;
        }
        if (arguments && check_graph_has_arguments(graph->ops[node]))
        {
            for (uint32_t a = 0; a < graph->argument_counts[node]; a += 1)
            {
                const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + a];
                const bool edge = argument < count;
                ordered &= !edge || argument < node;
                parents[edge ? argument : count] = node;
                edges += edge;
            }
        }
    }
    uint64_t children = 0;
    for (uint32_t at = 0; at < count; at += 1)
    {
        children += parents[at] != BUSTER_CHECK_NONE;
    }
    return ordered && children == edges;
}

// The vector path's tree (check_common_facts): each node's parent, check_common_parents' into `parents`; and with
// `used`, whether its reader uses its value: every node with a parent but a comma's or a void cast's first operand
// (check_common_discards), which a tree's one parent decides, its nodes found 16 at a time. True where the graph is a tree
// in node order.
static bool check_common_facts_tree(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t* parents, uint8_t* used)
{
    const uint32_t count = graph->count;
    const bool result = check_common_parents(graph, parents, false);
    if (result && used != NULL)
    {
        for (uint32_t node = 0; node < count; node += 1)
        {
            used[node] = parents[node] != BUSTER_CHECK_NONE;
        }
        static const CheckOp discarding[] = { CHECK_OP_CAST, CHECK_OP_COMMA };
        const CheckCommonOps set = check_common_ops(discarding, BUSTER_ARRAY_LENGTH(discarding));
        CheckCommonScan scan = { 0 };
        for (uint32_t node = check_common_next_op(graph, &set, &scan, true); node < count; node = check_common_next_op(graph, &set, &scan, true))
        {
            const uint32_t operand = graph->operand0[node];
            if (operand < count && check_common_discards(graph, stream, types, node))
            {
                used[operand] = 0;
            }
        }
    }
    return result;
}

// Clears the nodes from the `depth` on `stack` with every node below them (operands and arguments) that is still
// evaluated, a worklist's walk: check_common_evaluated_tree's, which visits each node once in its tree.
static void check_common_clear_below(const CheckGraph* graph, uint8_t* evaluated, uint32_t* stack, uint32_t depth)
{
    const uint32_t count = graph->count;
    while (depth != 0)
    {
        const uint32_t at = stack[--depth];
        if (evaluated[at] != 0)
        {
            evaluated[at] = 0;
            const uint32_t children[] = { graph->operand0[at], graph->operand1[at], graph->operand2[at] };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(children); i += 1)
            {
                if (children[i] < count)
                {
                    stack[depth++] = children[i];
                }
            }
            if (check_graph_has_arguments(graph->ops[at]) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
            {
                for (uint32_t i = 0; i < graph->argument_counts[at]; i += 1)
                {
                    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[at] + i];
                    if (argument < count)
                    {
                        stack[depth++] = argument;
                    }
                }
            }
        }
    }
}

// check_common_evaluated's vector path, over the tree check_common_facts_tree found: a node is evaluated where its root is
// and no node on its way there leaves its operand out (check_common_operand_mask). So every node a parent reads starts
// evaluated, and then the roots the control and selection walks left out and the operands the masks leave out (their
// nodes found 16 at a time) are cleared with every node below them, a worklist's walk each, which visits each node once.
static uint8_t* check_common_evaluated_tree(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, const Parser* parser,
                                            const CheckProgram* program, const uint32_t* parents, Arena* scratch)
{
    const uint32_t count = graph->count;
    uint8_t* evaluated = count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    uint32_t* stack = count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    uint8_t* result = stack != NULL ? evaluated : NULL;
    if (result != NULL)
    {
        for (uint32_t node = 0; node < count; node += 1)
        {
            // (A dropped range's root, as check_common_evaluated leaves it out.)
            evaluated[node] = parents[node] == BUSTER_CHECK_NONE &&
                              (program == NULL || program->dropped_range_count == 0 || !check_program_token_dropped(program, graph->tokens[node]));
        }
        // An unreachable initializer's roots under a parent (check_common_unreachable's barriers), cleared below.
        uint8_t* blocked = NULL;
        if (program != NULL && program->controls != NULL && !check_common_unreachable(program, graph, evaluated, &blocked, scratch))
        {
            result = NULL;
        }
        if (!check_common_unselected_roots(graph, parser, evaluated, evaluated, 0, scratch))
        {
            result = NULL;
        }
        // A node a parent reads takes its evaluation from it, whatever the walks above marked it.
        for (uint32_t node = 0; node < count; node += 1)
        {
            evaluated[node] |= parents[node] != BUSTER_CHECK_NONE;
        }
        CheckCommonScan roots = { 0 };
        const CheckCommonOps masked = check_common_ops(check_common_mask_ops, BUSTER_ARRAY_LENGTH(check_common_mask_ops));
        CheckCommonScan masks = { 0 };
        uint32_t root = check_common_next_root(parents, count, &roots, true);
        uint32_t node = check_common_next_op(graph, &masked, &masks, true);
        while (root < count || node < count)
        {
            uint32_t depth = 0;
            if (root < count)
            {
                // A root the walks cleared: marked again so that its own walk clears it with everything below it.
                if (evaluated[root] == 0)
                {
                    evaluated[root] = 1;
                    stack[depth++] = root;
                }
                root = check_common_next_root(parents, count, &roots, true);
            }
            else
            {
                const uint32_t mask = check_common_operand_mask(graph, stream, types, program, node);
                const uint32_t slots[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
                for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(slots); i += 1)
                {
                    if (slots[i] < count && (mask & (1u << i)) == 0)
                    {
                        stack[depth++] = slots[i];
                    }
                }
                node = check_common_next_op(graph, &masked, &masks, true);
            }
            check_common_clear_below(graph, evaluated, stack, depth);
        }
        // Each barrier, with every node below it: the scalar walk's evaluation never reaches them.
        for (uint32_t node = 0; blocked != NULL && node < count; node += 1)
        {
            if (blocked[node] != 0)
            {
                stack[0] = node;
                check_common_clear_below(graph, evaluated, stack, 1);
            }
        }
    }
    return result;
}

// The evaluation of a body clang ends after the unit's first error (#1447's semantic recovery, phase 3): its
// AnalysisBasedWarnings::IssueWarnings, finding an uncompilable error, builds no CFG and flushes every runtime-behavior
// warning the body queued (DiagRuntimeBehavior, DiagIfReachable), reachable or not. So in such a body (`flushed`, by its
// tokens) a node is evaluated where its root is and no `sizeof` or `_Alignof` on its way leaves it out, which clang never
// queues: the arms of a constant or decided condition, the short-circuited operands, a static selection's other arms and
// the statements no control reaches are. A node's parents follow it (check_common_facts), so one pass down from the last
// node sets each from its parent, which both paths' evaluation leaves for this pass alike.
static void check_common_flush(const CheckGraph* graph, const uint32_t* parents, const uint8_t* flushed, uint32_t token_count, uint8_t* evaluated)
{
    for (uint32_t node = graph->count; node-- > 0;)
    {
        if (graph->tokens[node] < token_count && flushed[graph->tokens[node]] != 0)
        {
            const CheckOp op = graph->ops[node];
            evaluated[node] = parents[node] == BUSTER_CHECK_NONE ? 1 : evaluated[node];
            uint32_t mask = UINT32_MAX;
            if (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF)
            {
                // A variably modified type's bound is evaluated even in sizeof, as check_common_operand_mask has it.
                const uint32_t operand = graph->operand0[node];
                mask = operand < graph->count && (graph->ops[operand] == CHECK_OP_VLA_BOUND || graph->ops[operand] == CHECK_OP_VLA_EXTENT) ? 1u : 0u;
            }
            const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
            {
                if (operands[i] < node && parents[operands[i]] == node)
                {
                    evaluated[operands[i]] = evaluated[node] != 0 && (mask & (1u << i)) != 0;
                }
            }
            if (check_graph_has_arguments(op) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
            {
                for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                {
                    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
                    if (argument < node && parents[argument] == node)
                    {
                        evaluated[argument] = evaluated[node] != 0 && (i >= 32 || (mask & (1u << i)) != 0);
                    }
                }
            }
        }
    }
}

// A GNU statement expression's statements are graph roots of their own, evaluated as roots are; clang's CFG holds them
// where it holds the statement expression (#1447). So for -Wuninitialized, where its value, the node at its `)`, is not
// evaluated (a sizeof's or an alignof's operand, a condition's arm left out), neither is any of its nodes, the run of
// nodes just before its own whose tokens its parentheses hold, nested statement expressions' and array bounds' with
// them (the warnings that read a context keep a bound's, constant-evaluated to clang). The reader's list of statement
// expressions finds them, and each one left out clears its own nodes once. Scalar: it runs only in a unit with statement
// expressions, with the uninitialized analysis enabled, and clears only the nodes of those in an unevaluated operand,
// rare and few, one store a node, which a vector store would save little of; its loop over the statement expressions
// costs a load and a test each. It isn't measured on the 9700X: no unit of the timed corpus reaches the clearing.
static void check_common_region_evaluation(const CheckInput* input, const Parser* parser, uint8_t* evaluated)
{
    const CheckGraph* graph = &input->graph;
    for (uint32_t region = evaluated != NULL && input->token_nodes != NULL ? input->region_count : 0; region-- > 0;)
    {
        const uint32_t open = input->regions[region].opening;
        const uint32_t close = input->regions[region].closing;
        const uint32_t value = close < input->token_count && close < parser->tokens.count ? input->token_nodes[close] : BUSTER_CHECK_NONE;
        if (value < graph->count && graph->ops[value] == CHECK_OP_STATEMENT_VALUE && evaluated[value] == 0)
        {
            for (uint32_t node = value; node-- > 0 && graph->tokens[node] > open && graph->tokens[node] < close;)
            {
                evaluated[node] = 0;
            }
        }
    }
}

// The graph's facts (CheckCommonFacts) into `facts`: each node's parent, whether its value is used (where -Wuninitialized
// reads it) and its reach, then which nodes are evaluated. The scalar path makes them in one pass over the nodes in graph
// order, so that its operands' and arguments' already hold; an operand that doesn't precede its node, which the graph's
// validation rules out, gives it every bit of reach. The vector path, where the graph is a tree in that order
// (check_common_facts_tree), visits only the nodes that may have a bit of their own reach and carries each bit up to the
// first ancestor that has it, whose ancestors all do; and takes the evaluated nodes by check_common_evaluated_tree. False
// where scratch can't hold them. (Whether a discarded value warns is check_common_unused_of's, asked only of a discarded
// one.)
static bool check_common_facts(const CheckInput* input, const CheckStream* stream, const TypeTable* types, const Parser* parser,
                               const CheckProgram* program, const CheckCommonEnabled* enabled, const uint8_t* flushed, Arena* scratch,
                               CheckCommonFacts* facts)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t count = graph->count;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    uint32_t* parents = count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : NULL;
    uint8_t* used = count != 0 && enabled->uninitialized ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : NULL;
    uint8_t* reach = count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    uint64_t* self_assigns = count != 0 && enabled->self_assign ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, ((uint64_t)count + 63) / 64) : NULL;
    uint8_t* evaluated = NULL;
    const bool allocated = parents != NULL && (used != NULL || !enabled->uninitialized) && reach != NULL && (self_assigns != NULL || !enabled->self_assign ||
                           count == 0);
    if (allocated && self_assigns != NULL)
    {
        memset(self_assigns, 0, (((uint64_t)count + 63) / 64) * sizeof(uint64_t));
    }
    const bool walk = allocated && vector && check_common_facts_tree(graph, stream, types, parents, used);
    if (walk)
    {
        memset(reach, 0, count);
        // The comparisons, last in the list, only where -Wsign-compare may warn: it takes every one. Otherwise only those
        // -Wtautological-constant-out-of-range-compare may warn of reach their roots, in batches after the scan.
        const CheckCommonOps own = check_common_ops(check_common_reach_ops, BUSTER_ARRAY_LENGTH(check_common_reach_ops) - (enabled->sign_compare ? 0u : 6u));
        CheckCommonScan owners = { 0 };
        uint32_t* comparisons = enabled->out_of_range && !enabled->sign_compare ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 16) : NULL;
        uint32_t listed = 0;
        uint32_t tested = 0;
        for (uint32_t node = check_common_next_reach_op(graph, &own, comparisons, &listed, &owners); node < count;
             node = check_common_next_reach_op(graph, &own, comparisons, &listed, &owners))
        {
            // A full batch of comparisons is tested as soon as it is listed, while the scan is near their nodes: tested at
            // the end, a large unit's gathers would find its columns cold.
            for (; listed - tested >= 16; tested += 16)
            {
                check_common_range_batch(graph, stream, types, parents, reach, comparisons + tested, 16);
            }
            uint8_t bits = check_common_own_reach(parser, graph, stream, types, node, enabled);
            if (self_assigns != NULL && check_common_self_assign_candidate(graph, node))
            {
                self_assigns[node >> 6] |= UINT64_C(1) << (node & 63);
            }
            for (uint32_t at = node; bits != 0 && at < count; at = parents[at])
            {
                bits &= (uint8_t)~reach[at];
                reach[at] |= bits;
            }
        }
        // The rest of the comparisons, fewer than 16.
        if (comparisons != NULL)
        {
            check_common_range_batch(graph, stream, types, parents, reach, comparisons + tested, listed - tested);
        }
        // (Short of scratch for the list, the facts fail as a whole rather than leave comparisons out.)
        const bool short_of = enabled->out_of_range && !enabled->sign_compare && comparisons == NULL;
        check_common_assertion_reach(graph, stream, reach);
        evaluated = short_of ? NULL : check_common_evaluated_tree(graph, stream, types, parser, program, parents, scratch);
    }
    else if (allocated)
    {
        memset(parents, 0xff, (uint64_t)count * sizeof(uint32_t));
        if (used != NULL)
        {
            memset(used, 0, count);
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            const CheckOp op = graph->ops[node];
            // A comma's left operand and a cast to void's, statement-level or explicit, drop their operand's result.
            const bool discards = used != NULL && check_common_discards(graph, stream, types, node);
            uint8_t bits = check_common_own_reach(parser, graph, stream, types, node, enabled);
            if (self_assigns != NULL && check_common_self_assign_candidate(graph, node))
            {
                self_assigns[node >> 6] |= UINT64_C(1) << (node & 63);
            }
            const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
            {
                if (operands[i] < count)
                {
                    parents[operands[i]] = node;
                    if (used != NULL)
                    {
                        used[operands[i]] |= (uint8_t)!(i == 0 && discards);
                    }
                    bits |= (uint8_t)(operands[i] < node ? reach[operands[i]] : BUSTER_CHECK_REACH_ALL);
                }
            }
            if (check_graph_has_arguments(op) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
            {
                for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                {
                    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
                    if (argument < count)
                    {
                        parents[argument] = node;
                        if (used != NULL)
                        {
                            used[argument] = 1;
                        }
                        bits |= (uint8_t)(argument < node ? reach[argument] : BUSTER_CHECK_REACH_ALL);
                    }
                }
            }
            reach[node] = bits;
        }
        check_common_assertion_reach(graph, stream, reach);
        evaluated = check_common_evaluated(graph, stream, types, parser, program, parents, scratch);
    }
    if (evaluated != NULL && flushed != NULL)
    {
        check_common_flush(graph, parents, flushed, parser->tokens.count, evaluated);
    }
    *facts = (CheckCommonFacts){ .parents = parents, .used = used, .evaluated = evaluated, .reach = reach, .flushed = flushed, .self_assigns = self_assigns };
    const bool result = count == 0 || evaluated != NULL;
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
// Always inlined, as it was before #1447 grew the type readers it inlines past clang's threshold.
static inline __attribute__((always_inline)) uint32_t check_common_address_root(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node, bool members)
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
    // (In a unit with statement expressions, the analysis's own copy of the facts, without an unevaluated one's nodes:
    // clang's CFG holds none of them, where the warnings that read a context keep a bound's.)
    uint8_t* regional = input->region_count != 0 && facts->evaluated != NULL && graph->count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    if (regional != NULL)
    {
        memcpy(regional, facts->evaluated, graph->count);
        check_common_region_evaluation(input, parser, regional);
    }
    const uint8_t* evaluated = regional != NULL ? regional : input->region_count != 0 ? NULL : facts->evaluated;
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
        // A local declaration is one with a body, an entry of its column: no other warns. A compound literal's unnamed
        // object has one too, but it is no variable, and clang's -Wuninitialized follows variables alone: the literal's
        // own value, at its closing `}`, would otherwise be a read inside its initializer's range, a self-use. The
        // variables its initializer reads keep their own warnings.
        for (uint32_t declaration = check_common_next_entry(input->declaration_bodies, 0, count, vector); valid && declaration < count;
             declaration = check_common_next_entry(input->declaration_bodies, declaration + 1, count, vector))
        {
            const bool local = input->declaration_kinds[declaration] == C_BINDING_OBJECT && check_common_sparse_get(input->declaration_bodies, declaration) < parser->tokens.count &&
                               !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) &&
                               !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL) &&
                               !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND);
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
            // A body clang ends after the unit's first error gets no CFG analysis (check_common_flush), and so no
            // -Wuninitialized.
            const uint32_t declared_body = facts->flushed != NULL ? check_common_sparse_get(input->declaration_bodies, declaration) : BUSTER_CHECK_NONE;
            const bool analysed = declared_body >= parser->tokens.count || facts->flushed[declared_body] == 0;
            if (local && !parameter && scalar && braced && self_initialization)
            {
                if (!braced_written && analysed)
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
                if (analysed)
                {
                    check_common_report_published(parser, pp, input, read_token, publication, CHECK_WARNING_ID_UNINITIALIZED, true, message,
                                                  self_initialization ? NULL : &note, self_initialization ? 0u : 1u, scratch);
                }
            }
            else if (parameter && scalar && !addressed[declaration] && first_use_read[declaration] == BUSTER_CHECK_NONE &&
                     written[declaration] != 0 && statement_write[declaration] != 0 && (analysed || facts->flushed[declared_body] != 2))
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

// Whether a builtin node is one clang's EvaluatedExprVisitor leaves unvisited, a leaf of a simple condition: an
// unevaluated-argument builtin's call (constant_p, object_size, dynamic_object_size, classify_type).
static bool check_common_loop_unevaluated_builtin(const CheckGraph* graph, uint32_t node)
{
    const CheckOp op = graph->ops[node];
    const uint32_t id = graph->members != NULL ? graph->members[node] : 0;
    return (op == CHECK_OP_BUILTIN_QUERY || op == CHECK_OP_BUILTIN_MISC || op == CHECK_OP_BUILTIN_UNARY) &&
           (id == CBUILTIN_CONSTANT_P || id == CBUILTIN_OBJECT_SIZE || id == CBUILTIN_DYNAMIC_OBJECT_SIZE || id == CBUILTIN_CLASSIFY_TYPE);
}

// Whether `node`'s evaluation is clang's EvaluatedExprVisitor's: not under a sizeof (of a variable array too, whose bounds
// it never visits either), an alignof or an unevaluated-argument builtin's call, whose operands it skips.
static bool check_common_loop_evaluated(const CheckGraph* graph, const uint32_t* parents, uint32_t node)
{
    bool result = true;
    for (uint32_t at = node, steps = 0; result && at < graph->count && steps < graph->count; steps += 1)
    {
        result = graph->ops[at] != CHECK_OP_SIZEOF && graph->ops[at] != CHECK_OP_SIZEOF_VLA && graph->ops[at] != CHECK_OP_ALIGNOF &&
                 !check_common_loop_unevaluated_builtin(graph, at);
        at = parents[at];
    }
    return result;
}

// Whether the visitor reaches `token`, inside the for statement whose `)` is `close` (#1821): its node, where it has one,
// is evaluated (check_common_loop_evaluated), and so is the value of each GNU statement expression around it there, a
// node at its `)`, as a statement expression's statements are graph roots of their own that only the parse tree places in
// its operand; none is in a specifier's operand (typeof's), nor has a value without a node. And each static selection
// around it there chooses the operand it is in (check_warning_graph_chooses), as the visitor reads the chosen operand
// alone: a selection's other operands and its controlling expression are graph roots of their own too, which a selection
// inside a statement expression inside another selection's chosen operand would hide from a test of the roots alone.
static bool check_common_loop_reached(const Parser* parser, const CheckInput* input, const uint32_t* parents, uint32_t token, uint32_t close)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t first = token < input->token_count && input->token_nodes != NULL ? input->token_nodes[token] : BUSTER_CHECK_NONE;
    bool result = first >= graph->count || check_common_loop_evaluated(graph, parents, first);
    for (uint32_t at = token, steps = 0; result && at < parser->tokens.count && at != close && parser->tree.parents[at] != at && steps < parser->tokens.count;
         steps += 1)
    {
        at = parser->tree.parents[at];
        const bool group = at < input->token_count && parser->tree.roles[at] == PARSE_ROLE_CLOSE_GROUP && input->expression_regions != NULL &&
                           input->expression_regions[at] != 0;
        const uint32_t node = group && input->token_nodes != NULL ? input->token_nodes[at] : BUSTER_CHECK_NONE;
        // (A specifier's operand, typeof's or _Alignas's, is the visitor's no more than a type's, nor is a sizeof's or an
        // alignof's, a type name's bounds too.)
        const uint8_t role = at < parser->tokens.count ? parser->tree.roles[at] : PARSE_ROLE_NONE;
        const bool unevaluated = role == PARSE_ROLE_CLOSE_SPECIFIER || role == PARSE_ROLE_CLOSE_SIZEOF ||
                                 (role == PARSE_ROLE_PREFIX && parser->tokens.kinds[at] == PARSE_KIND_SIZEOF) ||
                                 (role == PARSE_ROLE_CLOSE_KEYWORD && !check_warning_graph_chooses(input, parser, at, token));
        result = !unevaluated &&
                 (!group || (node < graph->count && graph->ops[node] == CHECK_OP_STATEMENT_VALUE && check_common_loop_evaluated(graph, parents, node)));
    }
    return result;
}

// Whether `token` is inside the statement `close`, a for's closing parenthesis, holds: its parse-tree climb reaches it.
static bool check_common_loop_within(const Parser* parser, uint32_t token, uint32_t close)
{
    uint32_t at = token;
    for (uint32_t steps = 0; at < parser->tokens.count && at != close && steps < parser->tokens.count && parser->tree.parents[at] != at; steps += 1)
    {
        at = parser->tree.parents[at];
    }
    return at == close;
}

// Whether the object `declaration` is clang's local, non-volatile variable (not hasGlobalStorage): a block's or a
// parameter's, neither static nor thread-local, of a type `type` that isn't volatile.
static bool check_common_loop_local(const Parser* parser, const CheckInput* input, uint32_t declaration, TypeId type)
{
    return check_common_sparse_get(input->declaration_bodies, declaration) < parser->tokens.count &&
           !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) &&
           !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL) && (type_qualifiers(type) & BUSTER_TYPE_VOLATILE) == 0;
}

// clang's DeclExtractor over a for loop's condition, `first` its first node: whether it is simple (a tree of operators,
// casts, literals, variables, a static selection's chosen operand and the leaves clang leaves unvisited), and its
// variables in their first uses' order in `decls`, their count in `*count`, and the first use's token in `*at`. `stack`
// and `decls` hold the graph's count each.
static bool check_common_loop_extract(const Parser* parser, const Preprocessor* pp, const CheckInput* input, const CheckStream* stream, uint32_t first,
                                      uint32_t* stack, uint32_t* decls, uint32_t* count, uint32_t* at)
{
    const CheckGraph* graph = &input->graph;
    uint32_t depth = 0;
    stack[depth++] = first;
    bool simple = true;
    *count = 0;
    *at = BUSTER_CHECK_NONE;
    while (simple && depth != 0)
    {
        const uint32_t node = stack[--depth];
        const CheckOp op = graph->ops[node];
        const uint32_t token = graph->tokens[node];
        uint32_t operands[3] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        if (op == CHECK_OP_VALUE)
        {
            const uint8_t kind = token < parser->tokens.count ? parser->tokens.kinds[token] : PARSE_KIND_END;
            const uint32_t declaration = kind == PARSE_KIND_IDENTIFIER && token < input->token_count ? input->declarations[token] : BUSTER_CHECK_NONE;
            const bool variable = declaration < input->token_count && input->declaration_kinds[declaration] == C_BINDING_OBJECT;
            // (__builtin_offsetof's folded value, clang's OffsetOfExpr, is a leaf too: its node is at its `)`, whose child its `(`
            // is, after the builtin's name.)
            uint32_t opener = BUSTER_CHECK_NONE;
            for (uint32_t back = kind == PARSE_KIND_R_PAREN ? token : 0; back != 0 && opener == BUSTER_CHECK_NONE;)
            {
                back -= 1;
                opener = parser->tree.parents[back] == token && parser->tokens.kinds[back] == PARSE_KIND_L_PAREN ? back : BUSTER_CHECK_NONE;
            }
            const uint32_t name = opener != BUSTER_CHECK_NONE && opener != 0 ?
                                  parser->tokens.names[opener - 1] : 0;
            const bool offsetof = name != 0 && intern_length(pp->table, name) == 18 && memcmp(intern_bytes(pp->table, name), "__builtin_offsetof", 18) == 0;
            simple = variable || offsetof || kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER;
            bool seen = false;
            for (uint32_t i = 0; variable && i < *count; i += 1)
            {
                seen |= decls[i] == declaration;
            }
            if (variable && !seen)
            {
                decls[(*count)++] = declaration;
                // A volatile or global variable leaves the whole condition unanalysed, as clang returns.
                simple &= check_common_loop_local(parser, input, declaration, check_stream_type(stream, node));
            }
            *at = variable && *at == BUSTER_CHECK_NONE ? token : *at;
        }
        else if ((op >= CHECK_OP_ADD && op <= CHECK_OP_LOGICAL_OR) || op == CHECK_OP_ASSIGN || op == CHECK_OP_COMMA ||
                 (op >= CHECK_OP_ADD_ASSIGN && op <= CHECK_OP_BIT_XOR_ASSIGN))
        {
            operands[0] = graph->operand1[node];
            operands[1] = graph->operand0[node];
        }
        else if (op == CHECK_OP_CONDITIONAL)
        {
            operands[0] = graph->operand2[node];
            operands[1] = graph->operand1[node];
            operands[2] = graph->operand0[node];
        }
        else if ((op >= CHECK_OP_POSITIVE && op <= CHECK_OP_BIT_NOT) || op == CHECK_OP_ADDRESS || op == CHECK_OP_CAST || op == CHECK_OP_CONVERT ||
                 op == CHECK_OP_CONDITION || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT) || op == CHECK_OP_SELECT)
        {
            // (A _Generic's or a __builtin_choose_expr's chosen operand alone, as clang's visitor takes.)
            operands[0] = graph->operand0[node];
        }
        else if (check_common_loop_unevaluated_builtin(graph, node))
        {
        }
        else
        {
            // A sizeof or an alignof is simple and its operand unvisited; a dereference, a subscript, a member, a call or
            // anything else isn't.
            simple = op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF;
        }
        for (uint32_t i = 0; simple && i < 3; i += 1)
        {
            if (operands[i] < graph->count)
            {
                // (A tree's nodes are each pushed once: the graph's count bounds the stack.)
                stack[depth++] = operands[i];
            }
        }
    }
    return simple;
}

// Whether the use of a loop's variable at `token` may change it, clang's DeclMatcher: any but a read of its value (an
// assignment's target, an increment's operand, an address, a member's object or an array's decay). An unevaluated use
// is none, a statement expression's inside an unevaluated operand too (check_common_loop_reached).
static bool check_common_loop_changes(const Parser* parser, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                      const uint32_t* parents, uint32_t token, uint32_t close)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t node = token < input->token_count && input->token_nodes != NULL ? input->token_nodes[token] : BUSTER_CHECK_NONE;
    bool result = false;
    if (node < graph->count)
    {
        const uint32_t parent = parents[node];
        const CheckOp op = parent < graph->count ? graph->ops[parent] : CHECK_OP_COUNT;
        const bool target = parent < graph->count && graph->operand0[parent] == node;
        result = type_kind_is_array(check_action_kind(types, check_stream_type(stream, node))) || op == CHECK_OP_ADDRESS ||
                 (target && (op == CHECK_OP_ASSIGN || op == CHECK_OP_MEMBER || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT) ||
                             (op >= CHECK_OP_ADD_ASSIGN && op <= CHECK_OP_BIT_XOR_ASSIGN)));
        // (Only a change asks for the climb.)
        result = result && check_common_loop_reached(parser, input, parents, token, close);
    }
    return result;
}

// An increment or decrement of a variable named alone, clang's ProcessIterationStmt, as the tokens `first` and `first +
// 1`: its declaration, or BUSTER_CHECK_NONE, and whether it increments.
static uint32_t check_common_loop_step(const Parser* parser, const CheckInput* input, uint32_t first, bool* increment)
{
    const uint8_t a = parser->tokens.kinds[first];
    const uint8_t b = parser->tokens.kinds[first + 1];
    const bool prefix = (a == PARSE_KIND_PLUSPLUS || a == PARSE_KIND_MINUSMINUS) && b == PARSE_KIND_IDENTIFIER;
    const bool postfix = a == PARSE_KIND_IDENTIFIER && (b == PARSE_KIND_PLUSPLUS || b == PARSE_KIND_MINUSMINUS);
    const uint32_t name = prefix ? first + 1 : first;
    *increment = (prefix ? a : b) == PARSE_KIND_PLUSPLUS;
    return (prefix || postfix) && name < input->token_count ? input->declarations[name] : BUSTER_CHECK_NONE;
}

// clang's -Wfor-loop-analysis (#1447, SemaStmt.cpp's CheckForLoopConditionalStatement and CheckForRedundantIteration),
// warned as each for statement is built, after its body's own warnings (published past its last token, as the parser's
// lookahead is):
//   - a simple condition's variables (check_common_loop_extract), local and not volatile, that neither the condition, the
//     increment nor the body may change (check_common_loop_changes), where none of them returns, breaks or goes to;
//   - the increment's ++ or -- of a variable named alone, repeated as the body block's last statement, with no continue
//     of this loop in the body, at the body's, with a note at the increment's.
// Off by default: a unit where nothing enables it skips the scan of its for statements.
static __attribute__((noinline)) void check_common_for_loops(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                   const uint32_t* parents, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const uint8_t roles[] = { PARSE_ROLE_CLOSE_FOR };
    CheckCommonScan scan = { 0 };
    const bool enabled = parents != NULL && check_common_enabled(pp, input, CHECK_WARNING_ID_FOR_LOOP_ANALYSIS, true);
    // The condition's walk's stack and variables, made at the first loop that needs them.
    uint32_t* stack = NULL;
    uint32_t* decls = NULL;
    for (uint32_t close = enabled ? check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector) : count;
         close < count && !pp->fatal; close = check_common_next_token(parser, count, BUSTER_CHECK_NONE, roles, BUSTER_ARRAY_LENGTH(roles), &scan, vector))
    {
        if (parser->tree.roles[close] != PARSE_ROLE_CLOSE_FOR)
        {
            continue;
        }
        const uint32_t open = check_common_opener(parser, close);
        uint32_t semis[2] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        for (uint32_t at = open < close ? open + 1 : close; at < close; at += 1)
        {
            const bool separator = parser->tokens.kinds[at] == PARSE_KIND_SEMI && parser->tree.parents[at] == close;
            semis[1] = separator && semis[0] != BUSTER_CHECK_NONE ? at : semis[1];
            semis[0] = separator && semis[0] == BUSTER_CHECK_NONE ? at : semis[0];
        }
        // The statement's end: its body's last token.
        uint32_t end = close;
        while (end + 1 < count && check_common_loop_within(parser, end + 1, close))
        {
            end += 1;
        }
        if (semis[1] == BUSTER_CHECK_NONE || open >= close)
        {
            continue;
        }
        // The condition, clang's Second: its truth test's operand, the root a token of it climbs to that is the test (a
        // statement expression's statements and a static selection's other operands are roots of their own). Clang skips
        // it where the warning is ignored at its first token.
        uint32_t root = BUSTER_CHECK_NONE;
        const bool conditional = semis[0] + 1 < semis[1] &&
                                 check_common_severity(pp, parser, input, semis[0] + 1, CHECK_WARNING_ID_FOR_LOOP_ANALYSIS, true) != CHECK_WARNING_IGNORE;
        // (A token inside braces is a statement expression's or a compound literal's: a loop nested there has a test of its
        // own.)
        uint32_t braces = 0;
        for (uint32_t at = semis[0] + 1; conditional && at < semis[1] && root >= input->graph.count; at += 1)
        {
            const uint8_t kind = parser->tokens.kinds[at];
            braces -= kind == PARSE_KIND_R_BRACE && braces != 0;
            uint32_t up = braces == 0 && at < input->token_count && input->token_nodes != NULL ? input->token_nodes[at] : BUSTER_CHECK_NONE;
            braces += kind == PARSE_KIND_L_BRACE;
            for (uint32_t steps = 0; up < input->graph.count && parents[up] < input->graph.count && steps < input->graph.count; steps += 1)
            {
                up = parents[up];
            }
            root = up < input->graph.count && input->graph.ops[up] == CHECK_OP_CONDITION ? input->graph.operand0[up] : BUSTER_CHECK_NONE;
        }
        if (root < input->graph.count && stack == NULL)
        {
            stack = CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, input->graph.count);
            decls = stack != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, input->graph.count) : NULL;
            if (decls == NULL)
            {
                check_common_exhausted(pp);
            }
        }
        uint32_t used = 0;
        uint32_t at = BUSTER_CHECK_NONE;
        if (root < input->graph.count && decls != NULL && check_common_loop_extract(parser, pp, input, stream, root, stack, decls, &used, &at) && used != 0)
        {
            bool found = false;
            for (uint32_t token = semis[0] + 1; !found && token <= end; token += 1)
            {
                const uint8_t kind = parser->tokens.kinds[token];
                const uint32_t declaration = kind == PARSE_KIND_IDENTIFIER && token < input->token_count ? input->declarations[token] : BUSTER_CHECK_NONE;
                bool listed = false;
                for (uint32_t i = 0; declaration != BUSTER_CHECK_NONE && i < used; i += 1)
                {
                    listed |= decls[i] == declaration;
                }
                const bool jump = kind == PARSE_KIND_RETURN || kind == PARSE_KIND_BREAK || kind == PARSE_KIND_GOTO;
                found = (jump && check_common_loop_reached(parser, input, parents, token, close)) ||
                        (listed && check_common_loop_changes(parser, input, stream, types, parents, token, close));
            }
            if (!found)
            {
                String8 names[4];
                for (uint32_t i = 0; i < used && i < 4; i += 1)
                {
                    const uint32_t name = parser->tokens.names[decls[i]];
                    names[i] = (String8){ .pointer = (char*)intern_bytes(pp->table, name), .length = intern_length(pp->table, name) };
                }
                const char* message =
                    used == 1 ? check_warning_text(scratch, "variable '%.*s' used in loop condition not modified in loop body",
                                                   (int)names[0].length, names[0].pointer) :
                    used == 2 ? check_warning_text(scratch, "variables '%.*s' and '%.*s' used in loop condition not modified in loop body",
                                                   (int)names[0].length, names[0].pointer, (int)names[1].length, names[1].pointer) :
                    used == 3 ? check_warning_text(scratch, "variables '%.*s', '%.*s', and '%.*s' used in loop condition not modified in loop body",
                                                   (int)names[0].length, names[0].pointer, (int)names[1].length, names[1].pointer,
                                                   (int)names[2].length, names[2].pointer) :
                    used == 4 ? check_warning_text(scratch, "variables '%.*s', '%.*s', '%.*s', and '%.*s' used in loop condition not modified in loop body",
                                                   (int)names[0].length, names[0].pointer, (int)names[1].length, names[1].pointer,
                                                   (int)names[2].length, names[2].pointer, (int)names[3].length, names[3].pointer) :
                    "variables used in loop condition not modified in loop body";
                check_common_report_at(parser, pp, input, at, end + 1, CHECK_WARNING_ID_FOR_LOOP_ANALYSIS, true, message, NULL, 0, scratch);
            }
        }
        // The increment and the body block's last statement, each exactly a variable's ++ or --.
        const uint32_t block = close + 1 < count && parser->tree.roles[close + 1] == PARSE_ROLE_OPEN_BLOCK ? parser->tree.parents[close + 1] : BUSTER_CHECK_NONE;
        bool header_increment = false;
        bool body_increment = false;
        // (Clang skips it where the warning is ignored at the increment's first token.)
        const uint32_t header = semis[1] + 3 == close &&
                                check_common_severity(pp, parser, input, semis[1] + 1, CHECK_WARNING_ID_FOR_LOOP_ANALYSIS, true) != CHECK_WARNING_IGNORE ?
                                check_common_loop_step(parser, input, semis[1] + 1, &header_increment) : BUSTER_CHECK_NONE;
        const uint32_t before = block < count && block >= close + 5 ? block - 4 : BUSTER_CHECK_NONE;
        const bool alone = before < count && parser->tokens.kinds[block - 1] == PARSE_KIND_SEMI && parser->tree.parents[block - 1] == block &&
                           (before == close + 1 || (parser->tokens.kinds[before] == PARSE_KIND_SEMI && parser->tree.roles[before] == PARSE_ROLE_TERMINATOR) ||
                            parser->tree.roles[before] == PARSE_ROLE_CLOSE_BLOCK);
        const uint32_t last = header != BUSTER_CHECK_NONE && alone && block == end ? check_common_loop_step(parser, input, block - 3, &body_increment) :
                              BUSTER_CHECK_NONE;
        if (last != BUSTER_CHECK_NONE && last == header && body_increment == header_increment)
        {
            // A continue of this loop, not of a loop inside it, skips the body's step.
            bool continued = false;
            for (uint32_t token = close + 2; !continued && token < block; token += 1)
            {
                bool own = parser->tokens.kinds[token] == PARSE_KIND_CONTINUE;
                for (uint32_t up = parser->tree.parents[token], steps = 0; own && up < count && up != close && steps < count; steps += 1)
                {
                    const uint8_t role = parser->tree.roles[up];
                    const uint32_t opener = role == PARSE_ROLE_CLOSE_CONDITION ? check_common_opener(parser, up) : BUSTER_CHECK_NONE;
                    own = role != PARSE_ROLE_CLOSE_FOR && role != PARSE_ROLE_DO &&
                          !(opener != BUSTER_CHECK_NONE && opener != 0 && parser->tokens.kinds[opener - 1] == PARSE_KIND_WHILE);
                    up = parser->tree.parents[up];
                }
                continued = own;
            }
            if (!continued)
            {
                const uint32_t body_name = parser->tokens.kinds[block - 3] == PARSE_KIND_IDENTIFIER ? block - 3 : block - 2;
                const uint32_t header_name = parser->tokens.kinds[semis[1] + 1] == PARSE_KIND_IDENTIFIER ? semis[1] + 1 : semis[1] + 2;
                const uint32_t name = parser->tokens.names[body_name];
                const char* word = body_increment ? "incremented" : "decremented";
                const char* message = check_warning_text(scratch, "variable '%.*s' is %s both in the loop header and in the loop body",
                                                         (int)intern_length(pp->table, name), intern_bytes(pp->table, name), word);
                const CheckCommonNote note = { header_name, check_warning_text(scratch, "%s here", word) };
                check_common_report_at(parser, pp, input, body_name, end + 1, CHECK_WARNING_ID_FOR_LOOP_ANALYSIS, true, message, &note, 1, scratch);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// -Wtype-safety (#1447): clang's CheckArgumentWithTypeTag at each call of a function or a function pointer whose
// declaration has argument_with_type_tag or pointer_with_type_tag rules (c_check_type_safety_input.c), in the order its
// checkCall makes them, after the call's format warnings. clang is the external oracle of the behavior; no clang code is
// incorporated.

// clang's isLayoutCompatible of two types in C (SemaChecking's): the same type without qualifiers; two complete enumerations
// of one underlying type; two structures whose members pair off in order, or two unions whose members each match another's
// (the first unmatched one that does), members matching when their types are layout-compatible, they are both bit-fields of
// one width or neither, and, a structure's, their largest alignment attributes are equal. The recursion of its members'
// types is a stack of frames here: a frame waits on its pair of members' types (`child`) and goes on with the answer.
#define BUSTER_CHECK_LAYOUT_DEPTH 64u

typedef struct CheckCommonLayoutFrame CheckCommonLayoutFrame;
struct CheckCommonLayoutFrame
{
    TypeId a;
    TypeId b;
    // The member of `a` the frame is at, and the member of `b` a union's tries.
    uint32_t i;
    uint32_t j;
    // A union's members of `b` no member of `a` has matched yet, a bit each.
    uint64_t unmatched;
    bool started;
};

static TypeId check_common_layout_canonical(const TypeTable* types, TypeId type)
{
    return type != 0 ? type_unqualified(type_strip_aligned(types, type)) : 0;
}

// Whether a frame's types are layout-compatible without their members' (*decided, and the answer), or how its members go
// on: its members' first pair's alignment and bit-field widths checked, the frame set up.
static bool check_common_layout_start(const TypeTable* types, CheckCommonLayoutFrame* frame, bool* decided)
{
    const TypeId a = check_common_layout_canonical(types, frame->a);
    const TypeId b = check_common_layout_canonical(types, frame->b);
    const uint8_t kind = a != 0 && b != 0 ? type_kind(types, a) : BUSTER_TYPE_KIND_NONE;
    bool result = a != 0 && a == b;
    *decided = true;
    if (!result && kind != BUSTER_TYPE_KIND_NONE && kind == type_kind(types, b) && kind == BUSTER_TYPE_KIND_ENUM)
    {
        result = type_is_complete(types, a) && type_is_complete(types, b) && types->types.base[type_index(a)] == types->types.base[type_index(b)];
    }
    else if (!result && kind != BUSTER_TYPE_KIND_NONE && kind == type_kind(types, b) && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION))
    {
        const uint32_t a_tag = types->types.index[type_index(a)];
        const uint32_t b_tag = types->types.index[type_index(b)];
        const uint32_t a_count = types->tags.member_count[a_tag];
        const uint32_t b_count = types->tags.member_count[b_tag];
        // (An incomplete record has no members: clang compares their empty lists, as here.)
        if (kind == BUSTER_TYPE_KIND_STRUCT)
        {
            result = a_count == b_count;
            *decided = !result || a_count == 0;
        }
        else
        {
            // (Past 64 members of `b`, taken as compatible, so that no warning comes of it.)
            const bool wide = b_count > 64;
            *decided = wide || a_count == 0;
            result = wide || b_count == 0;
            frame->unmatched = wide ? 0 : b_count == 64 ? UINT64_MAX : ((uint64_t)1 << b_count) - 1;
        }
        frame->a = a;
        frame->b = b;
        frame->i = 0;
        frame->j = 0;
    }
    frame->started = true;
    return result;
}

static bool check_common_layout_compatible(const TypeTable* types, TypeId first, TypeId second)
{
    CheckCommonLayoutFrame stack[BUSTER_CHECK_LAYOUT_DEPTH];
    uint32_t depth = 1;
    stack[0] = (CheckCommonLayoutFrame){ .a = first, .b = second };
    bool answer = false;
    // Whether `answer` is a child's, which the top frame waits on.
    bool returned = false;
    while (depth != 0)
    {
        CheckCommonLayoutFrame* frame = &stack[depth - 1];
        bool decided = false;
        bool pushed = false;
        if (!frame->started)
        {
            answer = check_common_layout_start(types, frame, &decided);
        }
        else
        {
            const bool is_struct = type_kind(types, frame->a) == BUSTER_TYPE_KIND_STRUCT;
            const uint32_t a_tag = types->types.index[type_index(frame->a)];
            const uint32_t b_tag = types->types.index[type_index(frame->b)];
            const uint32_t a_begin = types->tags.member_begin[a_tag];
            const uint32_t b_begin = types->tags.member_begin[b_tag];
            const uint32_t a_count = types->tags.member_count[a_tag];
            const uint32_t b_count = types->tags.member_count[b_tag];
            if (returned && is_struct)
            {
                // A structure's pair answered: the next pair, or the answer.
                decided = !answer;
                frame->i += 1;
            }
            else if (returned)
            {
                // A union's member of `a` against `b`'s member j: matched, or the next of `b`'s.
                frame->unmatched &= answer ? ~((uint64_t)1 << frame->j) : UINT64_MAX;
                frame->i += answer ? 1u : 0u;
                frame->j = answer ? 0u : frame->j + 1;
            }
            while (!decided && !pushed)
            {
                while (!is_struct && frame->j < b_count && ((frame->unmatched >> frame->j) & 1u) == 0)
                {
                    frame->j += 1;
                }
                const uint32_t candidate = is_struct ? frame->i : frame->j;
                const uint32_t x = a_begin + frame->i;
                const uint32_t y = b_begin + candidate;
                const bool ended = frame->i == a_count;
                const bool exhausted = !ended && candidate >= b_count;
                const bool a_bits = !ended && !exhausted && (types->members.flags[x] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
                const bool b_bits = !ended && !exhausted && (types->members.flags[y] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
                const bool fits = !ended && !exhausted && a_bits == b_bits && (!a_bits || types->members.bit_width[x] == types->members.bit_width[y]) &&
                                  (!is_struct || types->members.alignment_log2[x] == types->members.alignment_log2[y]);
                if (ended || exhausted)
                {
                    // A structure's members all paired, a union's all matched (and none of `b`'s left over), or not.
                    answer = ended && (is_struct || frame->unmatched == 0);
                    decided = true;
                }
                else if (fits && depth < BUSTER_CHECK_LAYOUT_DEPTH)
                {
                    stack[depth] = (CheckCommonLayoutFrame){ .a = types->members.type[x], .b = types->members.type[y] };
                    pushed = true;
                }
                else if (fits)
                {
                    // Past the stack's depth: taken as compatible, so that no warning comes of it.
                    answer = true;
                    decided = true;
                }
                else if (is_struct)
                {
                    answer = false;
                    decided = true;
                }
                else
                {
                    frame->j += 1;
                }
            }
        }
        returned = false;
        if (pushed)
        {
            depth += 1;
        }
        else if (decided)
        {
            depth -= 1;
            returned = depth != 0;
        }
    }
    return answer;
}

// clang's getExprLoc of an argument's expression `node` (#1447), the argument's tokens from `first` to `last`: a
// parenthesized argument's `(`; else, through implicit conversions, a binary operator's operator, a member access's
// member; else its first token (a conditional's too).
static uint32_t check_common_type_safety_location(const Parser* parser, const CheckGraph* graph, uint32_t node, uint32_t first, uint32_t last)
{
    uint32_t at = node;
    while (at < graph->count && graph->ops[at] == CHECK_OP_CONVERT)
    {
        at = graph->operand0[at];
    }
    const CheckOp op = at < graph->count ? graph->ops[at] : CHECK_OP_COUNT;
    const bool parenthesized = first < parser->tokens.count && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN && parser->tree.parents[first] == last;
    const bool binary = (op >= CHECK_OP_ADD && op <= CHECK_OP_LOGICAL_OR) || op == CHECK_OP_ASSIGN || op == CHECK_OP_COMMA || op == CHECK_OP_MEMBER ||
                        op == CHECK_OP_ARROW || (op >= CHECK_OP_ADD_ASSIGN && op <= CHECK_OP_BIT_XOR_ASSIGN);
    const uint32_t token = at < graph->count ? graph->tokens[at] : BUSTER_CHECK_NONE;
    return !parenthesized && binary && token >= first && token <= last ? token : first;
}

// The first token of argument `index` (from 0) of the call whose `)` is `closing`: after the `(` or the index-th comma at
// the arguments' level; or `closing` where there is none.
static uint32_t check_common_type_safety_first(const Parser* parser, uint32_t closing, uint32_t index)
{
    uint32_t open = closing;
    uint32_t depth = 0;
    bool found = closing >= parser->tokens.count || parser->tokens.kinds[closing] != PARSE_KIND_R_PAREN;
    while (!found && open > 0)
    {
        open -= 1;
        const uint8_t kind = parser->tokens.kinds[open];
        const bool closer = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE;
        const bool opener = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        found = opener && depth == 0;
        depth = closer ? depth + 1 : opener && depth != 0 ? depth - 1 : depth;
    }
    uint32_t result = found && open < closing ? open + 1 : closing;
    uint32_t commas = 0;
    depth = 0;
    for (uint32_t t = result; found && commas < index && t < closing; t += 1)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        const bool closer = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE;
        const bool opener = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        const bool comma = kind == PARSE_KIND_COMMA && depth == 0;
        commas += comma ? 1u : 0u;
        result = comma ? t + 1 : result;
        depth = opener ? depth + 1 : closer && depth != 0 ? depth - 1 : depth;
    }
    return commas == index ? result : closing;
}

// The last token of the argument that starts at `first`: before the next comma at its level, or the call's `)`.
static uint32_t check_common_type_safety_last(const Parser* parser, uint32_t first)
{
    const uint32_t count = parser->tokens.count;
    uint32_t t = first;
    bool ended = false;
    while (!ended && t < count)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        const bool opener = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        const uint32_t next = opener && parser->tree.parents[t] > t && parser->tree.parents[t] < count ? parser->tree.parents[t] + 1 : t + 1;
        const uint8_t after = next < count ? parser->tokens.kinds[next] : PARSE_KIND_R_PAREN;
        ended = after == PARSE_KIND_COMMA || after == PARSE_KIND_R_PAREN || after == PARSE_KIND_R_SQUARE || after == PARSE_KIND_R_BRACE;
        t = ended ? next - 1 : next;
    }
    return t < count ? t : count - 1;
}

// An argument's type as a call reads it, an array's or a function's decayed.
static TypeId check_common_type_safety_decay(TypeTable* types, TypeId type)
{
    const TypeId bare = type != 0 ? type_strip_aligned(types, type) : 0;
    const uint8_t kind = bare != 0 ? type_kind(types, type_unqualified(bare)) : BUSTER_TYPE_KIND_NONE;
    return type_kind_is_array(kind) ? type_pointer(types, type_element(types, type_unqualified(bare))) :
           kind == BUSTER_TYPE_KIND_FUNCTION ? type_pointer(types, type_unqualified(bare)) : bare != 0 ? type_unqualified(bare) : 0;
}

static bool check_common_type_safety_void_pointer(const TypeTable* types, TypeId type)
{
    const TypeId bare = type != 0 ? type_unqualified(type_strip_aligned(types, type)) : 0;
    return bare != 0 && type_kind(types, bare) == BUSTER_TYPE_KIND_POINTER && type_kind(types, type_unqualified(type_strip_aligned(types, type_element(types, bare)))) == BUSTER_TYPE_KIND_VOID;
}

// clang's IsSameCharType: plain char and the signed or unsigned char it is.
static bool check_common_type_safety_same_char(const TypeTable* types, TypeId a, TypeId b)
{
    const TypeId x = a != 0 ? type_strip_aligned(types, a) : 0;
    const TypeId y = b != 0 ? type_strip_aligned(types, b) : 0;
    const TypeId same = types->char_unsigned ? BUSTER_TYPE_UCHAR : BUSTER_TYPE_SCHAR;
    return (x == BUSTER_TYPE_CHAR && y == same) || (x == same && y == BUSTER_TYPE_CHAR);
}

// The tag a call's type tag argument `node` names (clang's FindTypeTagExpr and GetMatchingCType): through casts, & and *, a
// comma's right operand and a conditional's arm its constant condition takes, a variable's first tag (its own or inherited)
// or an integer literal's registered value before the call; *wrong where the variable's tag is of another kind than `kind`.
static const CheckTypeTagDatatype* check_common_type_safety_tag(const Parser* parser, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                                                uint32_t node, uint32_t kind, uint32_t call, bool* wrong)
{
    const CheckGraph* graph = &input->graph;
    const CheckTypeTags* tags = input->type_tags;
    uint32_t at = node;
    bool searching = true;
    for (uint32_t steps = 0; searching && at < graph->count && steps < graph->count; steps += 1)
    {
        const CheckOp op = graph->ops[at];
        CheckValue condition = op == CHECK_OP_CONDITIONAL && graph->operand0[at] < graph->count ? check_stream_value(stream, graph->operand0[at]) : (CheckValue){ 0 };
        bool truth = false;
        const bool constant = op == CHECK_OP_CONDITIONAL && condition.constant_kind != CHECK_CONSTANT_NONE && check_action_truth(types, &condition, &truth);
        searching = op == CHECK_OP_CONVERT || op == CHECK_OP_CAST || op == CHECK_OP_ADDRESS || op == CHECK_OP_DEREFERENCE || op == CHECK_OP_COMMA || constant;
        at = !searching ? at : op == CHECK_OP_COMMA ? graph->operand1[at] : !constant ? graph->operand0[at] : truth ? graph->operand1[at] : graph->operand2[at];
    }
    const uint32_t token = at < graph->count && graph->ops[at] == CHECK_OP_VALUE ? graph->tokens[at] : BUSTER_CHECK_NONE;
    const uint8_t token_kind = token < parser->tokens.count ? parser->tokens.kinds[token] : PARSE_KIND_SEMI;
    const uint32_t declaration = token_kind == PARSE_KIND_IDENTIFIER && token < input->token_count ? input->declarations[token] : BUSTER_CHECK_NONE;
    const CheckTypeTagDeclaration* entry = declaration != BUSTER_CHECK_NONE ? check_type_tags_declaration(tags, declaration) : NULL;
    const CheckTypeTagDatatype* named = entry != NULL && entry->datatype_count != 0 ? tags->datatypes + entry->datatype_first : NULL;
    const bool literal = token_kind == PARSE_KIND_NUMBER && check_common_integer_constant(stream, at);
    const CheckValue value = literal ? check_stream_value(stream, at) : (CheckValue){ 0 };
    *wrong = named != NULL && named->kind != kind;
    return named != NULL ? (*wrong ? NULL : named) : literal && (value.integer >> 64) == 0 ? check_type_tags_magic(tags, kind, (uint64_t)value.integer, call) : NULL;
}

static void check_common_type_safety(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, TypeTable* types,
                                     CheckWarningSpelling* spell, Arena* scratch)
{
    const CheckTypeTags* tags = input->type_tags;
    const CheckGraph* graph = &input->graph;
    if (tags == NULL || tags->declaration_count == 0 || !check_common_enabled(pp, input, CHECK_WARNING_ID_TYPE_SAFETY, false))
    {
        return;
    }
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    static const CheckOp calls[] = { CHECK_OP_CALL };
    const CheckCommonOps set = check_common_ops(calls, BUSTER_ARRAY_LENGTH(calls));
    CheckCommonScan scan = { 0 };
    for (uint32_t node = check_common_next_op(graph, &set, &scan, vector); node < graph->count; node = check_common_next_op(graph, &set, &scan, vector))
    {
        if (graph->ops[node] != CHECK_OP_CALL || stream->diagnostics[node] != CHECK_DIAG_NONE)
        {
            continue;
        }
        // A function's or a function pointer's name, through its conversions (clang's NamedDecl of the callee).
        uint32_t callee = graph->operand0[node];
        while (callee < graph->count && graph->ops[callee] == CHECK_OP_CONVERT)
        {
            callee = graph->operand0[callee];
        }
        const uint32_t token = callee < graph->count && graph->ops[callee] == CHECK_OP_VALUE ? graph->tokens[callee] : BUSTER_CHECK_NONE;
        const uint32_t declaration = token < parser->tokens.count && token < input->token_count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ?
                                     input->declarations[token] : BUSTER_CHECK_NONE;
        const CheckTypeTagDeclaration* entry = declaration != BUSTER_CHECK_NONE ? check_type_tags_declaration(tags, declaration) : NULL;
        const uint32_t offset = graph->argument_offsets[node];
        const uint32_t count = graph->argument_counts[node];
        const uint32_t closing = graph->tokens[node];
        for (uint32_t r = 0; entry != NULL && r < entry->rule_count && offset <= graph->argument_count && count <= graph->argument_count - offset; r += 1)
        {
            const CheckTypeTagRule rule = tags->rules[entry->rule_first + r];
            const uint32_t tag_node = rule.tag - 1 < count ? graph->argument_nodes[offset + rule.tag - 1] : BUSTER_CHECK_NONE;
            const uint32_t argument_node = rule.argument - 1 < count ? graph->argument_nodes[offset + rule.argument - 1] : BUSTER_CHECK_NONE;
            if (tag_node >= graph->count || argument_node >= graph->count)
            {
                continue;
            }
            bool wrong = false;
            const CheckTypeTagDatatype* datatype = check_common_type_safety_tag(parser, input, stream, types, tag_node, rule.kind, closing, &wrong);
            const uint32_t tag_first = check_common_type_safety_first(parser, closing, rule.tag - 1);
            if (wrong)
            {
                const uint32_t at = check_common_type_safety_location(parser, graph, tag_node, tag_first, check_common_type_safety_last(parser, tag_first));
                check_common_report_at(parser, pp, input, at, closing, CHECK_WARNING_ID_TYPE_SAFETY, false, "this type tag was not designed to be used with this function",
                                       NULL, 0, scratch);
                continue;
            }
            if (datatype == NULL)
            {
                continue;
            }
            // The argument as clang reads it: its conversion's type, but a pointer's conversion to void * (a bit cast), whose
            // pointer a pointer_with_type_tag reads.
            const TypeId source = check_stream_type(stream, argument_node);
            const TypeId decayed = check_common_type_safety_decay(types, source);
            const CheckConversion conversion = stream->argument_conversions != NULL ? stream->argument_conversions[offset + rule.argument - 1] : (CheckConversion){ 0 };
            const TypeId converted = conversion.type != 0 ? type_unqualified(type_strip_aligned(types, conversion.type)) : decayed;
            const CheckValue value = check_stream_value(stream, argument_node);
            const bool null = check_action_null(types, &value, 0);
            const bool stripped = rule.pointer && check_common_type_safety_void_pointer(types, converted) && decayed != 0 &&
                                  type_kind(types, decayed) == BUSTER_TYPE_KIND_POINTER && decayed != converted && !null;
            const TypeId argument = stripped ? decayed : converted;
            if (argument == 0 || (rule.pointer && check_common_type_safety_void_pointer(types, argument)))
            {
                continue;
            }
            const uint32_t argument_first = check_common_type_safety_first(parser, closing, rule.argument - 1);
            const uint32_t at = check_common_type_safety_location(parser, graph, argument_node, argument_first, check_common_type_safety_last(parser, argument_first));
            const String8 kind = { .pointer = (char*)intern_bytes(pp->table, rule.kind), .length = intern_length(pp->table, rule.kind) };
            if ((datatype->flags & BUSTER_CHECK_TYPE_TAG_NULL) != 0)
            {
                if (stripped || !null)
                {
                    const char* message = check_warning_text(scratch, "specified %.*s type tag requires a null pointer", (int)kind.length, kind.pointer);
                    if (message != NULL)
                    {
                        check_common_report_at(parser, pp, input, at, closing, CHECK_WARNING_ID_TYPE_SAFETY, false, message, NULL, 0, scratch);
                    }
                }
                continue;
            }
            const TypeId required = rule.pointer ? type_pointer(types, datatype->type) : datatype->type;
            const bool layout = (datatype->flags & BUSTER_CHECK_TYPE_TAG_LAYOUT) != 0;
            bool mismatch = false;
            if (!layout)
            {
                mismatch = required == 0 || type_strip_aligned(types, argument) != type_strip_aligned(types, required);
                const TypeId argument_char = rule.pointer ? type_element(types, type_strip_aligned(types, argument)) : argument;
                const TypeId required_char = rule.pointer && required != 0 ? type_element(types, type_strip_aligned(types, required)) : required;
                mismatch &= !check_common_type_safety_same_char(types, argument_char, required_char);
            }
            else
            {
                mismatch = !check_common_layout_compatible(types, rule.pointer ? type_element(types, type_strip_aligned(types, argument)) : argument,
                                                           rule.pointer ? type_element(types, type_strip_aligned(types, required)) : required);
            }
            if (mismatch)
            {
                // The argument's type as written where it is the argument's own (its spelling's typedef), its conversion's
                // canonical one otherwise; the tag's as its attribute names it.
                const TypeId shown = stripped || conversion.type == 0 ? source : argument;
                const uint32_t spelling = shown == source ? check_common_operand_spelling(parser, input, graph, stream, types, argument_node) : 0;
                const String8 actual = check_common_type_text(parser, pp, input, types, spell, scratch, shown, spelling);
                const String8 wanted = layout ? (String8){ "", 0 } : check_common_type_text(parser, pp, input, types, spell, scratch, required, datatype->spelling);
                const char* message = layout ? check_warning_text(scratch, "argument type %.*s doesn't match specified '%.*s' type tag ", (int)actual.length, actual.pointer,
                                                                  (int)kind.length, kind.pointer) :
                                               check_warning_text(scratch, "argument type %.*s doesn't match specified '%.*s' type tag that requires %.*s", (int)actual.length,
                                                                  actual.pointer, (int)kind.length, kind.pointer, (int)wanted.length, wanted.pointer);
                if (message != NULL && actual.length != 0 && (layout || wanted.length != 0))
                {
                    check_common_report_at(parser, pp, input, at, closing, CHECK_WARNING_ID_TYPE_SAFETY, false, message, NULL, 0, scratch);
                }
            }
        }
    }
}

// `flushed`: the tokens of the bodies clang ends after the unit's first error (check_common_flush), or null in a unit
// without one.
static void check_driver_common_warnings(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                         TypeTable* types, const CheckProgram* program, const uint8_t* flushed, CheckCommonShiftWarnings* shifts,
                                         Arena* scratch)
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
        .out_of_range = check_common_enabled(pp, input, CHECK_WARNING_ID_TAUTOLOGICAL_OUT_OF_RANGE_COMPARE, false),
        .self_assign = check_common_enabled(pp, input, CHECK_WARNING_ID_SELF_ASSIGN, true),
        .uninitialized = check_common_enabled(pp, input, CHECK_WARNING_ID_UNINITIALIZED, true) ||
                         check_common_enabled(pp, input, CHECK_WARNING_ID_UNUSED_BUT_SET_PARAMETER, true),
        .enum_cast = input->enum_names,
        .const_values = &input->const_values,
    };
    check_common_enum_arithmetic(parser, pp, input, stream, types, &spell, scratch);
    check_common_empty_bodies(parser, pp, input, scratch);
    check_common_dangling_else(parser, pp, input, scratch);
    check_common_switches(parser, pp, input, stream, types, &spell, program, scratch);
    CheckCommonFacts facts = { 0 };
    if (!check_common_facts(input, stream, types, parser, program, &enabled, flushed, scratch, &facts))
    {
        check_common_exhausted(pp);
    }
    // At one assignment, clang's -Wself-assign is given as Sema builds it, before the condition that reads it gives
    // -Wparentheses.
    check_common_self_assigns(parser, pp, input, stream, types, &spell, &facts, scratch);
    check_common_parentheses(parser, pp, input, stream, types, scratch);
    check_common_chained_comparisons(parser, pp, input, scratch);
    check_common_shift_op_parentheses(parser, pp, input, scratch);
    check_common_assign_enum(parser, pp, input, stream, types, &spell, program, scratch);
    check_common_string_plus_int(parser, pp, input, stream, types, &spell, scratch);
    check_common_sizeof_array_decay(parser, pp, input, stream, types, &spell, scratch);
    check_common_shift_sign_overflow(parser, pp, input, stream, types, &spell, scratch);
    check_common_graph_warnings(parser, pp, input, stream, types, &spell, &facts, program, shifts, scratch);
    check_common_for_loops(parser, pp, input, stream, types, facts.parents, scratch);
    check_common_return_stack(parser, pp, input, stream, types, scratch);
    if (enabled.uninitialized)
    {
        check_common_uninitialized(parser, pp, input, stream, types, &facts, scratch);
    }
    check_common_format(parser, pp, input, stream, types, &spell, program, scratch);
    check_common_type_safety(parser, pp, input, stream, types, &spell, scratch);
    arena_pop_to(scratch, mark);
    check_common_contexts = (CheckCommonContexts){ 0 };
}
