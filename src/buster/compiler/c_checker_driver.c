// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source checking and object emission share one typed stream. Unsupported constructs are errors, never silently
// accepted. Diagnostics retain source/macro history and are ordered by the point their constraint is known.

static const char* check_driver_action_message(CheckDiagnostic diagnostic)
{
    const char* result = "invalid expression";
    switch (diagnostic)
    {
        case CHECK_DIAG_INVALID_TYPE: result = "invalid operand type"; break;
        case CHECK_DIAG_ARITHMETIC_REQUIRED: result = "arithmetic operands required"; break;
        case CHECK_DIAG_INTEGER_REQUIRED: result = "integer operands required"; break;
        case CHECK_DIAG_SCALAR_REQUIRED: result = "scalar operand required"; break;
        case CHECK_DIAG_POINTER_REQUIRED: result = "pointer operand required"; break;
        case CHECK_DIAG_LVALUE_REQUIRED: result = "lvalue required"; break;
        case CHECK_DIAG_MODIFIABLE_REQUIRED: result = "modifiable lvalue required"; break;
        case CHECK_DIAG_INCOMPATIBLE: result = "incompatible operand or conversion types"; break;
        case CHECK_DIAG_INCOMPLETE: result = "incomplete type is not permitted here"; break;
        case CHECK_DIAG_BIT_FIELD: result = "invalid use of a bit-field"; break;
        case CHECK_DIAG_REGISTER_ADDRESS: result = "address of register object requested"; break;
        case CHECK_DIAG_MEMBER: result = "no such member in the record type"; break;
        case CHECK_DIAG_CALL: result = "called object is not a function or function pointer"; break;
        case CHECK_DIAG_ARGUMENT_COUNT: result = "incorrect number of arguments"; break;
        case CHECK_DIAG_RESOURCE: result = "type checker ran out of memory"; break;
        case CHECK_DIAG_UNSUPPORTED: result = "expression is not yet supported by the type checker"; break;
        case CHECK_DIAG_NONE: break;
    }
    return result;
}

static const char* check_driver_fault_message(CheckFault fault)
{
    const char* result = "invalid constant expression";
    switch (fault)
    {
        case CHECK_FAULT_SIGNED_OVERFLOW: result = "signed overflow in constant expression"; break;
        case CHECK_FAULT_DIVIDE_BY_ZERO: result = "division by zero in constant expression"; break;
        case CHECK_FAULT_SHIFT_COUNT: result = "invalid shift count in constant expression"; break;
        case CHECK_FAULT_SHIFT_VALUE: result = "invalid signed shift in constant expression"; break;
        case CHECK_FAULT_UNSUPPORTED_CONSTANT: result = "constant expression is not yet supported"; break;
        case CHECK_FAULT_NONE: break;
    }
    return result;
}

static const char* check_driver_input_message(CheckInputMessage message)
{
    const char* result = "invalid checker input";
    switch (message)
    {
        case CHECK_INPUT_UNSUPPORTED: result = "construct is not yet supported by the type checker"; break;
        case CHECK_INPUT_DECLARATION: result = "invalid or unsupported declaration"; break;
        case CHECK_INPUT_UNDECLARED: result = "use of undeclared identifier"; break;
        case CHECK_INPUT_LITERAL: result = "invalid or unsupported literal"; break;
        case CHECK_INPUT_RESOURCE: result = "type checker ran out of memory"; break;
        case CHECK_INPUT_REDECLARATION: result = "conflicting declaration"; break;
        case CHECK_INPUT_RETURN: result = "return does not match the function result type"; break;
        case CHECK_INPUT_TARGET: result = "unsupported checker language or target model"; break;
        case CHECK_INPUT_INCOMPLETE_MEMBER: result = "record member has incomplete type"; break;
        case CHECK_INPUT_INCOMPLETE_OBJECT: result = "object has incomplete type"; break;
        case CHECK_INPUT_LOOP_HINT: result = "invalid '#pragma clang loop' directive"; break;
        case CHECK_INPUT_LOOP_PLACEMENT: result = "expected a for, while, or do-while loop to follow '#pragma clang loop'"; break;
        case CHECK_INPUT_LOOP_MISSING: result = "missing argument; expected an integer value"; break;
        case CHECK_INPUT_LOOP_EXPRESSION: result = "expected expression"; break;
        case CHECK_INPUT_LOOP_PARENTHESIS: result = "expected ')'"; break;
        case CHECK_INPUT_NAKED_STATEMENT: result = "non-ASM statement in naked function is not supported"; break;
        case CHECK_INPUT_NAKED_PARAMETER: result = "parameter references not allowed in naked functions"; break;
        case CHECK_INPUT_CONSTANT_INITIALIZER: result = "initializer element is not a compile-time constant"; break;
        case CHECK_INPUT_SECTION: result = "invalid '#pragma clang section' or section type conflict"; break;
        case CHECK_INPUT_OPTIMIZE: result = "invalid '#pragma clang optimize'"; break;
    }
    return result;
}

static const char* check_driver_input_report_message(const CheckInput* input, const CheckInputReport* report)
{
    const CheckInputLoopDiagnostic* loop = input->loop_diagnostic;
    const CheckInputSectionDiagnostic* section = check_input_section_report(input, report);
    const CheckInputOptimizeDiagnostic* optimize = check_input_optimize_report(input, report);
    return loop != NULL && report->pragma == loop->pragma && report->token == loop->position ? loop->text :
           section != NULL ? section->text.pointer : optimize != NULL ? optimize->text.pointer : check_driver_input_message(report->message);
}

static const char* check_driver_report_message(const CheckReport* report)
{
    const char* result = "invalid expression";
    switch (report->message)
    {
        case CHECK_MESSAGE_ACTION: result = check_driver_action_message(report->diagnostic); break;
        case CHECK_MESSAGE_CONSTANT: result = check_driver_fault_message(report->fault); break;
        case CHECK_MESSAGE_ICE: result = "expression is not an integer constant expression"; break;
        case CHECK_MESSAGE_STATIC_ASSERT: result = "static assertion failed"; break;
        case CHECK_MESSAGE_STATIC_INITIALIZER: result = "initializer is not a constant expression"; break;
    }
    return result;
}

static const char* check_driver_initializer_message(CheckInitStatus status)
{
    const char* result = "invalid initializer";
    switch (status)
    {
        case CHECK_INIT_NON_CONSTANT: result = "initializer is not a constant expression"; break;
        case CHECK_INIT_UNSUPPORTED: result = "initializer construct is not supported"; break;
        case CHECK_INIT_RESOURCE: result = "initializer checking capacity exhausted"; break;
        case CHECK_INIT_INVALID:
        case CHECK_INIT_CHECK_FAILED:
        case CHECK_INIT_OK: break;
    }
    return result;
}

static void check_driver_diagnose(const Parser* parser, Preprocessor* pp, uint32_t token, const char* message)
{
    if (token < parser->tokens.count)
    {
        pp_diagnose_output(pp, PP_SEVERITY_ERROR, parser->tokens.sources[token], "%s", message);
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, parse_end_location(pp), "%s", message);
    }
}

typedef struct CheckDriverDiagnostic CheckDriverDiagnostic;
struct CheckDriverDiagnostic
{
    uint32_t token;
    const char* message;
    uint32_t position;
    uint32_t completion;
};

// The warnings a token has, as check_driver_reports sorts and walks them: a key whose high half is the token, then the
// flags of its warnings and of those the source doesn't ignore, 16 bits each (the per-token warnings' flags are below
// bit 16, and the dense column this replaced (#592) kept 16 bits of each), so that the keys sort by token and the OR of
// two keys of one token is the token's combined warnings.
static uint64_t check_driver_warning_key(uint32_t token, uint32_t flags, uint32_t enabled)
{
    return (uint64_t)token << 32 | (uint64_t)(uint16_t)flags << 16 | (uint16_t)enabled;
}

static uint32_t check_driver_publication_position(const Parser* parser, const Preprocessor* pp, uint32_t boundary, uint32_t publication_end)
{
    return boundary < parser->tokens.count ? parser->tokens.sources[boundary] :
           boundary == parser->tokens.count ? publication_end : pp->output.count + 1u;
}

// Clang defers runtime-behavior warnings until the function is complete, including the lookahead after its closing
// brace. Required constant evaluation diagnoses immediately instead. Follow only proven parser parents for a
// sparse warning; no clean unit acquires a token-wide completion column.
static uint32_t check_driver_warning_position(const Parser* parser, const Preprocessor* pp, const CheckInput* input, const CheckRepair* repair,
                                              const uint64_t* completions, uint32_t completion_count, uint32_t token, uint32_t warning, uint32_t publication_end,
                                              const Parser* original, PPDiagnosticPhase* phase)
{
    *phase = PP_DIAGNOSTIC_SEMANTIC;
    const uint32_t runtime = CHECK_WARNING_INTEGER_OVERFLOW | CHECK_WARNING_SHIFT_COUNT_NEGATIVE | CHECK_WARNING_SHIFT_COUNT_OVERFLOW |
                             CHECK_WARNING_SHIFT_NEGATIVE_VALUE | CHECK_WARNING_SHIFT_OVERFLOW;
    uint32_t boundary = token;
    const Parser* publication_parser = parser;
    bool completed = false;
    uint32_t low = 0;
    uint32_t high = completion_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = (completions[middle] >> 32) < token;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    // The stable token sort preserves append order, so the final matching rewrite supplies its completion. Each
    // token has at most eight legacy warning bits: all such lookups inspect at most eight times the sparse facts.
    for (uint32_t i = low; i < completion_count && (completions[i] >> 32) == token; i += 1)
    {
        const CheckWarningEvent* event = input->warning_events.events + (uint32_t)completions[i];
        if (event->kind == CHECK_WARNING_EVENT_MESSAGE && event->id < 8 && event->payload.data == warning)
        {
            boundary = event->completion_kind == CHECK_WARNING_COMPLETE_TU_END ? parser->tokens.count + 1u :
                       event->completion + (event->completion_kind == CHECK_WARNING_COMPLETE_AFTER_DELIMITER ? 1u : 0u);
            completed = true;
        }
    }
    const uint32_t constant = input->constant_warnings != NULL && token < input->token_count ? input->constant_warnings[token] : 0;
    if (warning == CHECK_WARNING_EXCESS_INITIALIZERS)
    {
        for (uint32_t parent = token, steps = 0; parent < parser->tokens.count && steps < parser->tokens.count; steps += 1)
        {
            const uint8_t role = parser->tree.roles[parent];
            if (role == PARSE_ROLE_CLOSE_INITIALIZER || role == PARSE_ROLE_CLOSE_COMPOUND_BODY)
            {
                boundary = parent + 1u;
            }
            if (role == PARSE_ROLE_CLOSE_COMPOUND_BODY)
            {
                break;
            }
            parent = parser->tree.parents[parent];
        }
    }
    if (!completed && (warning & runtime) != 0 && (constant & warning) == 0)
    {
        const uint32_t expression = token < input->token_count && input->token_nodes != NULL ? input->token_nodes[token] : BUSTER_CHECK_NONE;
        const bool eager = warning == CHECK_WARNING_INTEGER_OVERFLOW && expression < input->graph.count && input->graph.source_positions != NULL &&
                           input->graph.source_positions[expression] <= parser->tokens.count;
        if (eager)
        {
            boundary = input->graph.source_positions[expression];
        }
        else
        {
            *phase = PP_DIAGNOSTIC_DEFERRED;
            // Repair may replace a real, completed body's final brace with a synthetic one. Its original tree still
            // proves that body's lookahead; only genuinely unfinished bodies need the repaired TU-end boundary.
            const uint32_t original_end = original != parser ? check_diagnostic_parse_end(original) : 0;
            for (uint32_t parent = token, steps = 0; original != parser && parent < original->tokens.count && steps < original->tokens.count; steps += 1)
            {
                if (original->tree.roles[parent] == PARSE_ROLE_CLOSE_BODY && original->tokens.kinds[parent] == PARSE_KIND_R_BRACE && parent + 1u < original_end)
                {
                    boundary = parent + 1u;
                    publication_parser = original;
                    break;
                }
                parent = original->tree.parents[parent];
            }
            if (publication_parser == parser)
            {
                const uint32_t node = token < input->token_count && input->token_nodes != NULL ? input->token_nodes[token] : BUSTER_CHECK_NONE;
                if (node < input->graph.count && input->graph.source_positions != NULL)
                {
                    boundary = input->graph.source_positions[node];
                }
                for (uint32_t parent = token, steps = 0; parent < parser->tokens.count && steps < parser->tokens.count; steps += 1)
                {
                    if (parser->tree.roles[parent] == PARSE_ROLE_CLOSE_BODY)
                    {
                        boundary = repair != NULL && parent >= repair->original_end ? parser->tokens.count + 1u : parent + 1u;
                        break;
                    }
                    parent = parser->tree.parents[parent];
                }
            }
        }
    }
    return check_driver_publication_position(publication_parser, pp, boundary, publication_parser == parser ? publication_end : pp->output.count);
}

#include "c_check_warning_render.c"

static void check_driver_pragma_report(Preprocessor* pp, const CheckInput* input, const CheckInputFpPragma* record, bool quiet)
{
    if ((record->warnings & CHECK_FP_PRAGMA_INVALID_PLACEMENT) != 0)
    {
        pp_diagnose_output(pp, PP_SEVERITY_ERROR, check_diagnostic_pragma_source(record), "floating-point pragma is not permitted within an expression");
    }
    else if (!quiet && (record->warnings & CHECK_FP_PRAGMA_UNKNOWN_STATE) != 0)
    {
        const CheckWarningSeverity severity = check_diagnostic_severity_at(input, record->token, record->source_begin,
                                                                           CHECK_WARNING_UNKNOWN_PRAGMA, false);
        if (severity != CHECK_WARNING_IGNORE)
        {
            const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                           severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
            pp_diagnose_output(pp, pp_severity, check_diagnostic_pragma_source(record), "%s", check_diagnostic_message(CHECK_WARNING_UNKNOWN_PRAGMA));
        }
    }
}

static void check_driver_packed_report(const Parser* parser, Preprocessor* pp, const CheckInput* input,
                                       const CheckInputPackedMember* member)
{
    const CheckWarningSeverity severity = check_diagnostic_severity(input, member->token, CHECK_WARNING_ADDRESS_OF_PACKED_MEMBER);
    if (severity != CHECK_WARNING_IGNORE)
    {
        const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                       severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
        pp_diagnose_output(pp, pp_severity, parser->tokens.sources[member->token],
                           "taking address of packed member '%.*s' of class or structure '%.*s' may result in an unaligned pointer value",
                           (int)member->member_name.length, member->member_name.pointer,
                           (int)member->record_name.length, member->record_name.pointer);
    }
}

static void check_driver_visibility_report(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckInputVisibilityPragma* record, bool quiet)
{
    const uint32_t flag = record->warning == 2 ? CHECK_WARNING_IGNORED_ATTRIBUTES : CHECK_WARNING_VISIBILITY_PRAGMA;
    const CheckWarningSeverity severity = record->error ? CHECK_WARNING_ERROR :
        check_diagnostic_severity_at(input, record->token, record->source, flag, false);
    if ((record->error || record->warning != 0) && severity != CHECK_WARNING_IGNORE && (!quiet || record->error))
    {
        const PPSeverity pp_severity = record->error ? PP_SEVERITY_ERROR : severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                       severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
        const uint32_t publication = record->warning == 1 && !record->error ? record->source :
                                     check_driver_publication_position(parser, pp, record->token, pp->output.count);
        const PPDiagnosticPhase phase = record->warning == 1 && !record->error ? PP_DIAGNOSTIC_LEXICAL : PP_DIAGNOSTIC_SEMANTIC;
        if (record->warning == 2)
        {
            pp_diagnose_output_phase(pp, pp_severity, record->diagnostic_source, publication, phase, "unknown visibility '%.*s' [%s-Wignored-attributes]",
                               (int)record->name.length, record->name.pointer, severity >= CHECK_WARNING_ERROR ? "-Werror," : "");
        }
        else
        {
            char promoted[256];
            const char* message = record->message;
            const char* option = !record->error && severity >= CHECK_WARNING_ERROR ? strstr(message, "[-W") : NULL;
            if (option != NULL)
            {
                snprintf(promoted, sizeof(promoted), "%.*s[-Werror,%s", (int)(option - message), message, option + 1);
                message = promoted;
            }
            if (record->diagnostic_location != 0)
            {
                const uint32_t saved_position = pp->diagnostic_position;
                pp->diagnostic_position = publication;
                pp_diagnose(pp, pp_severity, record->diagnostic_location, "%s", message);
                pp->diagnostic_position = saved_position;
            }
            else
            {
                pp_diagnose_output_phase(pp, pp_severity, record->diagnostic_source, publication, phase, "%s", message);
            }
        }
    }
}

static void check_driver_return_reports(const Parser* parser, const Parser* original, Preprocessor* pp, const CheckProgram* program, CheckWarningSeverity baseline, bool quiet)
{
    uint64_t first_error = UINT64_MAX;
    for (const PPDiagnosticRecord* record = pp->diagnostic_first; !quiet && record != NULL && pp->errors != 0; record = record->next)
    {
        if (record->severity == PP_SEVERITY_ERROR || record->severity == PP_SEVERITY_FATAL || record->severity == PP_SEVERITY_DEFERRED_ERROR)
        {
            const uint64_t position = record->severity == PP_SEVERITY_DEFERRED_ERROR ?
                                      (record->position & ~UINT64_C(3)) | PP_DIAGNOSTIC_SEMANTIC : record->position;
            first_error = position < first_error ? position : first_error;
        }
    }
    for (uint32_t i = 0; !quiet && i < original->diagnostic_count; i += 1)
    {
        const ParseDiagnostic* diagnostic = original->diagnostics + i;
        if (diagnostic->error)
        {
            const uint32_t source = diagnostic->token < original->tokens.count ? original->tokens.sources[diagnostic->token] : pp->output.count;
            const uint64_t position = (uint64_t)source * 4 + PP_DIAGNOSTIC_SEMANTIC;
            first_error = position < first_error ? position : first_error;
        }
    }
    for (uint32_t b = 0; !quiet && !pp->fatal && b < program->body_count; b += 1)
    {
        const CheckProgramBody* body = program->bodies + b;
        const uint32_t token = parser->tree.parents[body->opening];
        if (body->return_warning != 0 && token < parser->tokens.count &&
            (pp->options.system_header_warnings || !check_graph_system_token(&program->input->graph, token)))
        {
            const uint32_t publication = check_driver_publication_position(original, pp, token + 1u, pp->output.count);
            const CheckWarningSeverity severity = check_diagnostic_severity_from(program->input, token, BUSTER_CHECK_NONE, CHECK_WARNING_RETURN_TYPE, baseline);
            if (severity != CHECK_WARNING_IGNORE && (uint64_t)publication * 4 + PP_DIAGNOSTIC_RETURN < first_error)
            {
                const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                               severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
                pp_diagnose_output_phase(pp, pp_severity, parser->tokens.sources[token], publication, PP_DIAGNOSTIC_RETURN, "non-void function does not return a value%s [-Wreturn-type]",
                                   body->return_warning == 2 ? " in all control paths" : "");
            }
        }
    }
}

static void check_driver_remaining_pragmas(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t* fp, uint32_t* visibility, bool quiet)
{
    const CheckInputVisibilityPragmas* history = input->visibility_pragmas;
    const uint32_t count = history != NULL ? history->count : 0;
    while (!pp->fatal && (*fp < input->fp_pragma_count || *visibility < count))
    {
        const uint32_t first_fp = *fp < input->fp_pragma_count ? input->fp_pragmas[*fp].source_begin : UINT32_MAX;
        const uint32_t first_visibility = *visibility < count ? history->records[*visibility].source : UINT32_MAX;
        if (first_fp < first_visibility)
        {
            check_driver_pragma_report(pp, input, input->fp_pragmas + *fp, quiet);
            *fp += 1;
        }
        else
        {
            check_driver_visibility_report(parser, pp, input, history->records + *visibility, quiet);
            *visibility += 1;
        }
    }
}

static void check_driver_x86_reports(const Parser* parser, const Parser* original, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                      const TypeTable* types, bool quiet)
{
    for (uint32_t i = 0; !quiet && !pp->fatal && i < stream->x86_count; i += 1)
    {
        const CheckBuiltinX86Effect* effect = stream->x86 + i;
        for (uint32_t e = 0; !pp->fatal && e < effect->range_event_count; e += 1)
        {
            const uint32_t token = check_diagnostic_x86_token(input, effect->node);
            const CheckWarningSeverity severity = check_diagnostic_severity(input, token, CHECK_WARNING_ARGUMENT_OUTSIDE_RANGE);
            const CheckBuiltinX86RangeEvent* event = effect->range_events + e;
            // A failed/incomplete program cannot prove deadness. Report any still-deferred event conservatively.
            if (event->disposition != CHECK_BUILTIN_X86_SUPPRESSED && severity != CHECK_WARNING_IGNORE && token < parser->tokens.count)
            {
                uint32_t publication = parser->tokens.sources[token];
                PPDiagnosticPhase phase = PP_DIAGNOSTIC_SEMANTIC;
                const uint32_t original_end = check_diagnostic_parse_end(original);
                for (uint32_t parent = token, steps = 0; parent < original->tokens.count && steps < original->tokens.count; steps += 1)
                {
                    if (original->tree.roles[parent] == PARSE_ROLE_CLOSE_BODY && original->tokens.kinds[parent] == PARSE_KIND_R_BRACE && parent + 1u < original_end)
                    {
                        publication = check_driver_publication_position(original, pp, parent + 1u, pp->output.count);
                        phase = PP_DIAGNOSTIC_DEFERRED;
                        break;
                    }
                    parent = original->tree.parents[parent];
                }
                uint32_t width = 0;
                bool sign = false;
                check_action_integer_type(types, event->value.type, &width, &sign);
                const bool negative = sign && (event->value.integer >> (width - 1)) != 0;
                unsigned __int128 magnitude = negative ? (0 - event->value.integer) & check_action_mask(width) : event->value.integer;
                char digits[42];
                char* text = digits + sizeof(digits) - 1;
                *text = 0;
                do
                {
                    *--text = '0' + magnitude % 10;
                    magnitude /= 10;
                }
                while (magnitude != 0);
                if (negative)
                {
                    *--text = '-';
                }
                const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? (event->default_error ? PP_SEVERITY_FATAL : PP_SEVERITY_MAPPED_FATAL) :
                    severity == CHECK_WARNING_ERROR ? (event->default_error ?
                        (phase == PP_DIAGNOSTIC_DEFERRED ? PP_SEVERITY_DEFERRED_ERROR : PP_SEVERITY_ERROR) : PP_SEVERITY_MAPPED_ERROR) : PP_SEVERITY_WARNING;
                pp_diagnose_output_phase(pp, pp_severity, parser->tokens.sources[token], publication, phase,
                                   "argument value %s is outside the valid range [%u, %u]", text, event->low, event->high);
            }
        }
    }
}

// The unit's diagnostics in token order: at each token, its floating-point pragmas, its warnings by flag, the addresses
// of packed members it takes, and its first report (the reader's, then the expressions', then the driver's own). Few
// tokens have any (#592), so the walk visits only those, from the token after the last it visited: the next warning's,
// the next report's, the next pragma's (unless the walk has passed it: then, as a walk of every token would, it never
// reports it) and the next packed member's address. Packed records behind that address are consumed in the same
// iteration without a report, as a walk of every token would. The warnings are gathered as keys
// (check_driver_warning_key) from the tokens' attributes 8 at a time, their constants' warnings 64 at a time and the
// expressions' nodes 16 at a time, where they have any (the scalar path, parser->options.scalar's, takes every token
// and node), and from the initializers' excess elements; then sorted by token (c_bindings_sort) and combined. The
// reports are gathered as keys of their token and their place in that order, and sorted: each token's first is the
// first of its keys, but for a pragma's report of the reader's whose place comes before that one's (#619).
static bool check_driver_reports(const Parser* parser, const Parser* original, Preprocessor* pp, uint32_t publication_end,
                                  const CheckInput* input, const CheckStream* stream,
                                  const CheckRepair* repair, const CheckInput* original_pragmas, const CheckDriverDiagnostic* extra, uint32_t extra_count,
                                  const uint32_t* excess_tokens, uint32_t excess_count, bool quiet, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const bool scalar = parser->options.scalar;
    const uint32_t count = parser->tokens.count;
    // Every token key is at most count, so all of its possible radix digits are known without gather-loop state.
    const uint32_t varying = count != 0 ? UINT32_MAX >> __builtin_clz(count) : 0;
    const uint32_t stream_reports = stream != NULL ? stream->report_count : 0;
    const uint32_t scanned = count < input->token_count ? count : input->token_count;
    const uint32_t nodes = stream != NULL ? stream->count : 0;
    const bool attribute_warnings = !input->asm_warnings_prepared || input->ignored_asm_count != 0;
    const uint32_t constant_warnings = input->constant_warnings_prepared ? input->constant_warning_count : scanned;
    const uint32_t node_warnings = stream != NULL && stream->warnings_prepared ? stream->warning_count : nodes;
    const bool show_warnings = !quiet;
    // Room for every report or warning a token may have, and a key past them, which no token's reaches: the walk's end.
    const uint64_t report_room = (uint64_t)input->report_count + stream_reports + extra_count + input->implicit_function_count + 1;
    const uint64_t warning_room = show_warnings ? (uint64_t)constant_warnings + (attribute_warnings ? scanned : 0) + node_warnings + excess_count + 1 : 0;
    const uint32_t completion_count = show_warnings ? input->warning_events.count : 0;
    const bool reports = report_room > 1;
    const bool fits = count < UINT32_MAX && report_room <= UINT32_MAX && warning_room <= UINT32_MAX;
    uint64_t* report_keys = fits && reports ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, report_room) : NULL;
    uint64_t* report_sorting = report_keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, report_room) : NULL;
    uint64_t* warning_keys = fits && show_warnings ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, warning_room) : NULL;
    uint64_t* warning_sorting = warning_keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, warning_room) : NULL;
    uint64_t* completion_keys = completion_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, completion_count) : NULL;
    uint64_t* completion_sorting = completion_keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, completion_count) : NULL;
    const bool ready = fits && (!reports || report_sorting != NULL) && (!show_warnings || warning_sorting != NULL) &&
                       (completion_count == 0 || (completion_sorting != NULL && input->warning_events.events != NULL));
    if (ready)
    {
        for (uint32_t i = 0; i < completion_count; i += 1)
        {
            completion_keys[i] = (uint64_t)input->warning_events.events[i].token << 32 | i;
        }
        const uint64_t* completions = completion_count != 0 ? c_bindings_sort(completion_keys, completion_sorting, completion_count, !scalar, varying) : NULL;
        uint32_t report_total = 0;
        for (uint32_t i = 0; i < input->report_count; i += 1)
        {
            const uint32_t token = input->reports[i].token < count ? input->reports[i].token : count;
            if (repair == NULL || check_repair_input_report(repair, input->reports + i) || check_repair_loop_report(repair, input, input->reports + i))
            {
                report_keys[report_total++] = (uint64_t)token << 32 | i;
            }
        }
        for (uint32_t i = 0; i < stream_reports; i += 1)
        {
            const uint32_t token = stream->reports[i].token < count ? stream->reports[i].token : count;
            if (repair == NULL || check_repair_graph_report(repair, &input->graph, stream->reports + i))
            {
                report_keys[report_total++] = (uint64_t)token << 32 | (input->report_count + i);
            }
        }
        for (uint32_t i = 0; i < extra_count; i += 1)
        {
            const uint32_t token = extra[i].position < count ? extra[i].position : count;
            report_keys[report_total++] = (uint64_t)token << 32 | (input->report_count + stream_reports + i);
        }
        for (uint32_t i = 0; i < input->implicit_function_count; i += 1)
        {
            const uint32_t token = input->implicit_function_tokens[i];
            const CheckWarningSeverity severity = check_diagnostic_implicit_function(input, token);
            if (token < count && severity != CHECK_WARNING_IGNORE && (!quiet || (input->implicit_function_c99 && severity >= CHECK_WARNING_ERROR)) &&
                (repair == NULL || check_repair_warning_token(repair, token)))
            {
                report_keys[report_total++] = (uint64_t)token << 32 | (input->report_count + stream_reports + extra_count + i);
            }
        }
        uint64_t* firsts = reports ? c_bindings_sort(report_keys, report_sorting, report_total, !scalar, varying) : NULL;
        uint32_t first_count = 0;
        for (uint32_t i = 0; i < report_total; i += 1)
        {
            if (first_count == 0 || (firsts[first_count - 1] >> 32) != (firsts[i] >> 32))
            {
                firsts[first_count++] = firsts[i];
            }
            else
            {
                // Of two of the reader's reports at one token, the one whose place comes first: a pragma's, before the
                // token, comes first even when the walk reported the token first (#619's section conflicts, found after
                // it).
                const uint32_t kept = (uint32_t)firsts[first_count - 1];
                const uint32_t later = (uint32_t)firsts[i];
                const bool earlier = kept < input->report_count && later < input->report_count && input->reports[later].pragma != 0 &&
                                     check_diagnostic_input_report_source(input, parser, input->reports + later) <
                                     check_diagnostic_input_report_source(input, parser, input->reports + kept);
                firsts[first_count - 1] = earlier ? firsts[i] : firsts[first_count - 1];
            }
        }
        if (reports)
        {
            firsts[first_count] = UINT64_MAX;
        }
        uint32_t warning_total = 0;
        for (uint32_t first = 0; show_warnings && attribute_warnings && input->attributes != NULL && first < scanned; first += 8)
        {
            const uint32_t lanes = check_input_asm_attributed(input, first, scanned, scalar);
            for (uint32_t selected = lanes; selected != 0; selected &= selected - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
                const uint32_t flags = check_diagnostic_attribute_warnings(input, token);
                if (flags != 0)
                {
                    warning_keys[warning_total++] = check_driver_warning_key(token, flags, flags);
                }
            }
        }
        for (uint32_t first = 0; show_warnings && constant_warnings != 0 && input->constant_warnings != NULL && first < scanned; first += 64)
        {
            const uint64_t lanes = scalar ? check_input_scan_all(first, scanned) : check_input_scan_nonzero(input->constant_warnings, first, scanned);
            for (uint64_t selected = lanes; selected != 0; selected &= selected - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
                const uint32_t flags = input->constant_warnings[token];
                const uint32_t ignored = input->constant_warning_ignored != NULL ? input->constant_warning_ignored[token] : 0;
                if (flags != 0)
                {
                    warning_keys[warning_total++] = check_driver_warning_key(token, flags, flags & ~ignored);
                }
            }
        }
        for (uint32_t first = 0; show_warnings && node_warnings != 0 && first < nodes; first += 16)
        {
            const uint32_t lanes = scalar ? (uint32_t)check_input_scan_all(first, nodes) & 0xffff : check_input_scan_nonzero32(stream->warnings, first, nodes);
            for (uint32_t selected = lanes; selected != 0; selected &= selected - 1)
            {
                const uint32_t node = first + (uint32_t)__builtin_ctz(selected);
                const uint32_t token = input->graph.tokens[node];
                const uint32_t flags = stream->warnings[node];
                if (token < count && flags != 0)
                {
                    const uint32_t ignored = stream->warning_ignored != NULL ? stream->warning_ignored[node] : 0;
                    warning_keys[warning_total++] = check_driver_warning_key(token, flags, flags & ~ignored);
                }
            }
        }
        for (uint32_t i = 0; show_warnings && i < excess_count; i += 1)
        {
            if (excess_tokens[i] < count)
            {
                warning_keys[warning_total++] = check_driver_warning_key(excess_tokens[i], CHECK_WARNING_EXCESS_INITIALIZERS, CHECK_WARNING_EXCESS_INITIALIZERS);
            }
        }
        uint64_t* warnings = show_warnings ? c_bindings_sort(warning_keys, warning_sorting, warning_total, !scalar, varying) : NULL;
        uint32_t warning_count = 0;
        for (uint32_t i = 0; i < warning_total; i += 1)
        {
            if (warning_count != 0 && (warnings[warning_count - 1] >> 32) == (warnings[i] >> 32))
            {
                warnings[warning_count - 1] |= warnings[i];
            }
            else
            {
                warnings[warning_count++] = warnings[i];
            }
        }
        if (show_warnings)
        {
            warnings[warning_count] = UINT64_MAX;
        }
        const CheckInput* pragma_input = original_pragmas != NULL ? original_pragmas : input;
        const CheckInputVisibilityPragmas* visibility_history = pragma_input->visibility_pragmas;
        uint32_t visibility = 0;
        uint32_t pragma = 0;
        uint32_t packed = 0;
        uint32_t warning = 0;
        uint32_t report = 0;
        uint64_t position = 0;
        bool walking = true;
        while (walking && !pp->fatal)
        {
            uint64_t next = show_warnings ? warnings[warning] >> 32 : UINT64_MAX;
            next = reports && (firsts[report] >> 32) < next ? firsts[report] >> 32 : next;
            if (pragma < pragma_input->fp_pragma_count && pragma_input->fp_pragmas[pragma].token >= position && pragma_input->fp_pragmas[pragma].token < next)
            {
                next = pragma_input->fp_pragmas[pragma].token;
            }
            if (visibility_history != NULL && visibility < visibility_history->count && visibility_history->records[visibility].token >= position &&
                visibility_history->records[visibility].token < next)
            {
                next = visibility_history->records[visibility].token;
            }
            if (packed < input->packed_member_count)
            {
                const uint64_t address = input->packed_members[packed].address;
                next = address < next ? address : next;
            }
            walking = next <= count;
            const uint32_t token = walking ? (uint32_t)next : 0;
            while (walking && !pp->fatal && ((pragma < pragma_input->fp_pragma_count && pragma_input->fp_pragmas[pragma].token == token) ||
                (visibility_history != NULL && visibility < visibility_history->count && visibility_history->records[visibility].token == token)))
            {
                const bool retained = original_pragmas != NULL || repair == NULL || token < check_repair_pragma_end(repair);
                const uint32_t fp_source = pragma < pragma_input->fp_pragma_count && pragma_input->fp_pragmas[pragma].token == token ?
                                           pragma_input->fp_pragmas[pragma].source_begin : UINT32_MAX;
                const uint32_t visibility_source = visibility_history != NULL && visibility < visibility_history->count &&
                    visibility_history->records[visibility].token == token ? visibility_history->records[visibility].source : UINT32_MAX;
                if (fp_source < visibility_source)
                {
                    if (retained)
                    {
                        check_driver_pragma_report(pp, pragma_input, pragma_input->fp_pragmas + pragma, quiet);
                    }
                    pragma += 1;
                }
                else
                {
                    if (retained)
                    {
                        check_driver_visibility_report(original, pp, pragma_input, visibility_history->records + visibility, quiet);
                    }
                    visibility += 1;
                }
            }
            const bool warned = walking && show_warnings && (warnings[warning] >> 32) == token;
            uint32_t flags = warned && (repair == NULL || check_repair_warning_token(repair, token)) ? (uint32_t)(warnings[warning] >> 16) & 0xffff : 0;
            const uint32_t enabled = warned ? (uint32_t)warnings[warning] & 0xffff : 0;
            warning += warned;
            while (!pp->fatal && flags != 0)
            {
                const uint32_t warning_flag = flags & (0u - flags);
                const bool ignored = (enabled & warning_flag) == 0;
                const CheckWarningSeverity severity = check_diagnostic_severity_default(input, token, warning_flag, ignored);
                if (severity != CHECK_WARNING_IGNORE)
                {
                    const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                                   severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
                    PPDiagnosticPhase phase = PP_DIAGNOSTIC_SEMANTIC;
                    const uint32_t publication = check_driver_warning_position(parser, pp, input, repair, completions, completion_count, token, warning_flag, publication_end, original, &phase);
                    pp_diagnose_output_phase(pp, pp_severity, parser->tokens.sources[token], publication, phase, "%s", check_diagnostic_message(warning_flag));
                }
                flags &= flags - 1;
            }
            // Addresses are sparse and ordered by the '&' event. A generic selection can put the diagnosed
            // member after another address in its control expression, so its location is not the iteration key.
            // Keep these records out of the warnings' keys.
            while (walking && !pp->fatal && packed < input->packed_member_count && input->packed_members[packed].address <= token)
            {
                const CheckInputPackedMember* member = input->packed_members + packed;
                if (show_warnings && !member->suppressed && member->address == token &&
                    (repair == NULL || check_repair_warning_token(repair, member->token)))
                {
                    check_driver_packed_report(parser, pp, input, member);
                }
                packed += 1;
            }
            if (walking && !pp->fatal && reports && (firsts[report] >> 32) == token)
            {
                const uint32_t sequence = (uint32_t)firsts[report];
                const uint32_t offset = input->report_count + stream_reports + extra_count;
                if (sequence >= offset)
                {
                    const CheckWarningSeverity severity = check_diagnostic_implicit_function(input, token);
                    const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? (input->implicit_function_c99 ? PP_SEVERITY_FATAL : PP_SEVERITY_MAPPED_FATAL) :
                                                   severity == CHECK_WARNING_ERROR ? (input->implicit_function_c99 ? PP_SEVERITY_ERROR : PP_SEVERITY_MAPPED_ERROR) : PP_SEVERITY_WARNING;
                    const uint32_t name = parser->tokens.names[token];
                    const uint32_t publication = check_driver_publication_position(parser, pp, token + 1u, publication_end);
                    if (input->implicit_function_c99)
                    {
                        pp_diagnose_output_at(pp, pp_severity, parser->tokens.sources[token], publication,
                            "call to undeclared function '%.*s'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]",
                            (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                    }
                    else
                    {
                        pp_diagnose_output_at(pp, pp_severity, parser->tokens.sources[token], publication, "implicit declaration of function '%.*s' [-Wimplicit-function-declaration]",
                                           (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                    }
                }
                else
                {
                    const uint32_t phase = sequence < input->report_count ? 0 : sequence - input->report_count < stream_reports ? 1 : 2;
                    const uint32_t index = phase == 0 ? sequence : phase == 1 ? sequence - input->report_count : sequence - input->report_count - stream_reports;
                    const char* message = phase == 2 ? extra[index].message : phase == 1 ? check_driver_report_message(stream->reports + index) :
                                                                                         check_driver_input_report_message(input, input->reports + index);
                    const uint32_t located = phase == 2 ? extra[index].token : token;
                    const uint32_t requested = phase == 0 ? check_diagnostic_input_report_source(input, parser, input->reports + index) : BUSTER_CHECK_NONE;
                    const uint32_t source = requested != BUSTER_CHECK_NONE ? requested : located < count ? parser->tokens.sources[located] : BUSTER_CHECK_NONE;
                    const uint32_t node = phase == 1 ? stream->reports[index].node :
                                          phase == 2 && located < input->token_count && input->token_nodes != NULL ? input->token_nodes[located] : BUSTER_CHECK_NONE;
                    const uint32_t completion = phase == 0 ? input->reports[index].completion :
                                                phase == 2 && extra[index].completion != 0 ? extra[index].completion :
                                                phase == 2 && extra[index].position != located ? extra[index].position :
                                                node < input->graph.count && input->graph.source_positions != NULL ? input->graph.source_positions[node] : 0;
                    if (source != BUSTER_CHECK_NONE)
                    {
                        const uint32_t publication = completion != 0 ? check_driver_publication_position(parser, pp, completion, publication_end) : source;
                        if (phase == 0 && input->reports[index].message == CHECK_INPUT_UNDECLARED && token < count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER)
                        {
                            const uint32_t name = parser->tokens.names[token];
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "use of undeclared identifier '%.*s'",
                                                 (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                        }
                        else
                        {
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "%s", message);
                        }
                    }
                    else
                    {
                        check_driver_diagnose(parser, pp, located, message);
                    }
                }
                report += 1;
            }
            position = next + 1;
        }
        // A directive in an unfinished expression can lie beyond every safely checked prefix. Its original
        // source spelling and warning policy are independent of that expression and precede the parser failure.
        if (original_pragmas != NULL)
        {
            check_driver_remaining_pragmas(original, pp, pragma_input, &pragma, &visibility, quiet);
        }
    }
    arena_pop_to(scratch, mark);
    return ready;
}

static void check_driver_dump(const CheckGraph* graph, const CheckStream* stream, TypeTable* types, Arena* scratch)
{
    const TypeNames names = { 0 };
    uint32_t effect = 0;
    uint32_t varargs = 0;
    for (uint32_t node = 0; node < stream->count; node += 1)
    {
        const uint64_t mark = scratch->length;
        const CheckValue* value = stream->values + node;
        const String8 type = value->type != 0 ? type_print(types, scratch, value->type, (String8){ 0 }, &names, 0) : (String8){ 0 };
        printf("%u token=%u op=%u type=%.*s category=%u", node, graph->tokens[node], graph->ops[node], (int)type.length, type.pointer != 0 ? type.pointer : "", value->category);
        if (value->constant_kind == CHECK_CONSTANT_INTEGER)
        {
            printf(" integer=0x%016llx%016llx", (unsigned long long)(value->integer >> 64), (unsigned long long)value->integer);
        }
        else if (value->constant_kind == CHECK_CONSTANT_FLOATING)
        {
            printf(" floating=%La", value->floating);
        }
        else if (value->constant_kind == CHECK_CONSTANT_COMPLEX)
        {
            printf(" complex=(floating:%La,imaginary:%La,integer:0x%016llx%016llx)", value->floating, value->imaginary,
                   (unsigned long long)(value->integer >> 64), (unsigned long long)value->integer);
        }
        else if (value->constant_kind == CHECK_CONSTANT_ADDRESS)
        {
            printf(" address=%u%+lld", value->object, (long long)value->offset);
        }
        printf(" flags=0x%x fault=%u warnings=0x%x", value->flags, value->fault, stream->warnings[node]);
        for (uint32_t i = 0; i < 3; i += 1)
        {
            const CheckConversion conversion = stream->conversions[node].operands[i];
            if (conversion.flags != 0)
            {
                printf(" operand%u=(type:%u,conversions:0x%x)", i, conversion.type, conversion.flags);
            }
        }
        const CheckConversion store = stream->conversions[node].store;
        if (store.flags != 0)
        {
            printf(" store=(type:%u,conversions:0x%x)", store.type, store.flags);
        }
        if (effect < stream->effect_count && stream->effects[effect].node == node)
        {
            const CheckStoreEffect* write = stream->effects + effect++;
            printf(" effect=(store-type:%u,destination-type:%u,value-kind:%u,runtime-builtin:%u,evaluated:0x%x,warnings:0x%x,warning-operands:0x%x,unsupported:0x%x)",
                   write->value.type, write->destination.type, write->value.constant_kind, write->runtime_builtin, write->evaluated_operands,
                   write->warnings, write->warning_operands, write->unsupported);
        }
        if (varargs < stream->varargs_count && stream->varargs[varargs].node == node)
        {
            const CheckVarargsEffect* action = stream->varargs + varargs++;
            printf(" varargs=(type:%u,evaluated:0x%x,warnings:0x%x,mutates:%u)", action->requested_type, action->evaluated_operands,
                   action->warnings, action->mutates_list);
        }
        putchar('\n');
        arena_pop_to(scratch, mark);
    }
}

static bool check_driver_dump_link(const Link* link, const CheckDataResult* emission)
{
    printf("static-data: %u objects, %u fixups\n", emission->atom_count, emission->fixup_count);
    for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
    {
        const AtomId atom = link->symbols.atom[symbol];
        if (atom != 0 && link->atoms.class[atom] != BUSTER_LINK_CLASS_TEXT)
        {
            const NameId name = link->symbols.name[symbol];
            printf("object=%u name=%.*s class=%u align=%" PRIu64 " size=%" PRIu64 " bytes=", symbol,
                   (int)link->names.length[name], link->names.bytes[name], link->atoms.class[atom],
                   UINT64_C(1) << link->atoms.alignment_log2[atom], link->atoms.size[atom]);
            if (link->atoms.bytes[atom] == 0)
            {
                printf("zero");
            }
            else
            {
                for (uint64_t byte = 0; byte < link->atoms.size[atom]; byte += 1)
                {
                    printf("%02x", link->atoms.bytes[atom][byte]);
                }
            }
            for (uint32_t i = 0; i < link->atoms.fixup_count[atom]; i += 1)
            {
                const FixupId fixup = link->atoms.fixup_begin[atom] + i;
                const SymbolId target = link->fixups.target[fixup];
                const NameId target_name = link->symbols.name[target];
                printf(" fixup=(offset:%u,target:%.*s,addend:%" PRId64 ")", link->fixups.offset[fixup],
                       (int)link->names.length[target_name], link->names.bytes[target_name], link_fixup_addend(link, fixup));
            }
            putchar('\n');
        }
    }
    return ferror(stdout) == 0;
}

static bool check_driver_dump_static(const CheckInput* input, const CheckStream* stream, const CheckInitializers* initializers, const Parser* parser,
                                      Preprocessor* pp, TypeTable* types, Arena* scratch)
{
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    CheckDataInput data = { .token = BUSTER_CHECK_NONE, .status = CHECK_DATA_INPUT_RESOURCE };
    bool valid = output.base != 0 && bytes.base != 0 && check_data_input_prepared(&data, input, stream, initializers, parser, pp->table, types, &output, &bytes, scratch);
    if (!valid)
    {
        const char* message = data.status == CHECK_DATA_INPUT_RESOURCE ? "static-data output capacity exhausted" :
                              data.status == CHECK_DATA_INPUT_INVALID ? "invalid static-data input" : "static-data emission for this object is not yet supported";
        check_driver_diagnose(parser, pp, data.token, message);
    }
    else
    {
        valid = check_driver_dump_link(&data.link, &data.emission);
    }
    arena_release(&bytes);
    arena_release(&output);
    return valid;
}

// Why check_program_emit failed, at its token: an unsupported construct names the feature it needs (#145), and the
// others are the compiler's own failures, with what the backend reported.
static void check_driver_emission_failed(const Parser* parser, Preprocessor* pp, const CheckProgramCode* code)
{
    char message[256];
    if (code->status == CHECK_PROGRAM_RESOURCE)
    {
        snprintf(message, sizeof(message), "code output capacity exhausted%s%s", code->reason != NULL ? ": " : "", code->reason != NULL ? code->reason : "");
    }
    else if (code->status == CHECK_PROGRAM_UNSUPPORTED)
    {
        snprintf(message, sizeof(message), "unsupported: %s", code->reason != NULL ? code->reason : "this C type or operation");
    }
    else if (code->status == CHECK_PROGRAM_INVALID && code->reason != NULL)
    {
        snprintf(message, sizeof(message), "internal compiler error: the lowered function breaks a rule of Tier 0's (%s) at entry %u", code->reason, code->backend_node);
    }
    else if (code->status == CHECK_PROGRAM_BACKEND && code->reason != NULL)
    {
        snprintf(message, sizeof(message), "%s", code->reason);
    }
    else if (code->status == CHECK_PROGRAM_BACKEND)
    {
        snprintf(message, sizeof(message), "internal compiler error: Tier 0 failed (error %u) at entry %u", (uint32_t)code->backend_error, code->backend_node);
    }
    else
    {
        snprintf(message, sizeof(message), "internal compiler error: the checked program could not be emitted");
    }
    check_driver_diagnose(parser, pp, code->token, message);
}

// Writes the checked program's object at `output_path`, its functions compiled on `lanes` (check_program_emit_lanes).
// With `debug` (-g), the object carries DWARF: the unit the executable's would (c_check_debug.c), relocated against the
// functions' sections (ObjectOptions.debug); and with `unwind`, its functions' unwind tables (check_program_emit's). With
// `stage_clock` (--time-stages), its passes' times.
// The share is of evaluated expression occurrences, including repeated graph uses, rather than all checked nodes
// (which include unused functions and unevaluated expressions). Only --time-stages asks for the report.
static void check_driver_lower_report(const CheckProgramCode* code, const CheckClock* stage_clock)
{
    if (stage_clock != NULL && code->status == CHECK_PROGRAM_OK)
    {
        const uint64_t nodes = code->lowered.expression_nodes;
        const uint64_t batch = code->lowered.batch_nodes;
        fprintf(stderr, "lowering-nodes\tevaluated=%" PRIu64 "\tbatched=%" PRIu64 "\tshare=%.2f%%\tclassified=%" PRIu64 "\n", nodes, batch,
                nodes != 0 ? 100.0 * (double)batch / (double)nodes : 0.0, code->lowered.classified_nodes);
    }
}

// A unit's link kept for the link of several units (c_checker_units.c), which check_driver_object fills.
typedef struct CheckUnitsKept CheckUnitsKept;
static bool check_units_keep(CheckUnitsKept* kept, const Link* link, const CheckProgram* program, const Parser* parser, Preprocessor* pp,
                             const ObjectDebug* debug);

// The checked program's code and data, as -c writes them: its object at `output_path`, or with `kept` (one of several
// units that link into one executable, c_checker_units.c), its link, kept for theirs. Its functions compile on `lanes`
// (check_program_emit_lanes). With `debug` (-g), the object carries DWARF: the unit the executable's would
// (c_check_debug.c), relocated against the functions' sections (ObjectOptions.debug); and with `unwind`, its functions'
// unwind tables (check_program_emit's). With `stage_clock` (--time-stages), its passes' times. The driver's own errors go
// to `errors`.
static bool check_driver_object(const CheckProgram* program, const CheckDataPrepared* data, const Parser* parser,
                                 Preprocessor* pp, TypeTable* types, const char* source_path, const char* output_path,
                                 bool scalar, bool verify, bool dump_static, bool debug, bool unwind, CheckProgramLanes* lanes, Arena* scratch,
                                 CheckClock* stage_clock, FILE* errors, CheckUnitsKept* kept)
{
    (void)scalar;

    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    const uint64_t mark = scratch->length;
    CheckProgramCode code = { .status = CHECK_PROGRAM_RESOURCE, .token = BUSTER_CHECK_NONE };
    CheckDataResult emission = { .status = CHECK_DATA_RESOURCE, .failed_token = BUSTER_CHECK_NONE };
    CheckProgramRows rows = { 0 };
    CheckDebug dwarf = { 0 };
    check_clock_mark(stage_clock, CHECK_STAGE_EMIT_SETUP);
    bool valid = output.base != 0 && bytes.base != 0 &&
                 check_program_emit_timed(&code, program, parser, pp->table, data->capacity, BUSTER_SCALAR_PATH(scalar), &output, &bytes, scratch, NULL, debug ? &rows : NULL,
                                          unwind, verify, lanes, stage_clock);
    check_driver_lower_report(&code, stage_clock);
    if (!valid)
    {
        check_driver_emission_failed(parser, pp, &code);
    }
    if (valid && debug)
    {
        valid = check_debug_build(&dwarf, program, &rows, parser, pp, BUSTER_SCALAR_PATH(scalar), &output, scratch);
        if (!valid)
        {
            fprintf(errors, "error: can't make the debug information: no memory, or a source name or file name DWARF can't hold\n");
        }
    }
    check_clock_mark(stage_clock, CHECK_STAGE_DEBUG);
    if (valid)
    {
        valid = check_data_publish(data, types, program->stream, program->object_symbols, program->object_count,
                                   &code.link, 0, &bytes, scratch, &emission);
        if (!valid)
        {
            check_driver_diagnose(parser, pp, emission.failed_token, "checked static data could not be emitted");
        }
    }
    // The definitions' named sections and the file-scope assembly's (#619), each atom's section in the object.
    ObjectAtomSection* sections = 0;
    if (valid && !check_program_sections(&code, program, &bytes, &output, &sections))
    {
        check_driver_emission_failed(parser, pp, &code);
        valid = false;
    }
    check_clock_mark(stage_clock, CHECK_STAGE_PUBLISH_DATA);
    if (valid && kept != NULL)
    {
        const LinkError checked = link_check(&code.link);
        valid = checked == LINK_ERROR_NONE && check_units_keep(kept, &code.link, program, parser, pp, debug ? &dwarf.object : NULL);
        if (checked != LINK_ERROR_NONE)
        {
            fprintf(errors, "internal compiler error: the unit's link isn't valid (link error %u)\n", (uint32_t)checked);
        }
        else if (!valid)
        {
            fprintf(errors, "fatal error: can't keep the unit's link: no memory\n");
        }
    }
    else if (valid)
    {
        ObjectUnits units = { 0 };
        ObjectSymbols symbols = { 0 };
        valid = link_check(&code.link) == LINK_ERROR_NONE && object_prepare(&units, &code.link, 1, scratch) &&
                object_map_symbols(&symbols, &code.link, scratch);
        // The jump tables' symbols and their labels' (CheckProgramLowered.case_symbol_base) are the lowering's
        // temporaries, as clang's .LJTI and .LBB labels are the assembler's: the object leaves them out, and its
        // relocations name their sections' symbols instead (ObjectOptions.temporary_symbols).
        const uint32_t switch_symbols = code.lowered.table_count + code.lowered.case_symbol_count;
        uint8_t* temporary = valid && switch_symbols != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, code.link.symbols.count) : 0;
        valid = valid && (switch_symbols == 0 || temporary != 0);
        if (temporary != 0)
        {
            memset(temporary, 0, code.link.symbols.count);
            memset(temporary + code.lowered.case_symbol_base, 1, switch_symbols);
        }
        if (valid)
        {
            const String8 source = { .pointer = (char*)source_path, .length = strlen(source_path) };
            const ObjectOptions options = {
                .atom_sections = sections,
                .atom_section_count = sections != 0 ? code.link.atoms.count : 0,
                .temporary_symbols = temporary,
                .temporary_symbol_count = temporary != 0 ? code.link.symbols.count : 0,
                .debug = debug ? &dwarf.object : NULL,
                .debug_count = debug ? 1 : 0,
            };
            const Object object = object_build(&code.link, &units, &symbols, 0, options, source, scratch);
            // The optional dump consumes the same resolved Link as the object writer. Validate and render it
            // before publishing the file, so a later diagnostic cannot leave a newly emitted rejected object.
            valid = object.error == OBJECT_ERROR_NONE && (!dump_static || check_driver_dump_link(&code.link, &emission));
            check_clock_mark(stage_clock, CHECK_STAGE_OBJECT_BUILD);
            valid = valid && object_write_file(output_path, object.bytes, object.size) == OBJECT_ERROR_NONE;
            check_clock_mark(stage_clock, CHECK_STAGE_OBJECT_WRITE);
        }
        if (!valid)
        {
            fprintf(errors, "error: could not write object file %s\n", output_path);
        }
    }
    check_clock_mark(stage_clock, CHECK_STAGE_OBJECT_BUILD);
    arena_pop_to(scratch, mark);
    check_program_lanes_release(lanes);
    arena_release(&bytes);
    arena_release(&output);
    check_clock_mark(stage_clock, CHECK_STAGE_RELEASE);
    return valid;
}

// executable_write's passes are a lane group's collectives: an executable is written by a group of one lane, or of
// several units' lanes (check_driver_executable), whose lane 0 says how it went.
typedef struct CheckDriverWrite CheckDriverWrite;
struct CheckDriverWrite
{
    Link* link;
    ExecutableOptions options;
    ExecutableResult result;
    bool succeeded;
};

// link_check_lanes's run on a group of lanes (several units' link, #778), whose lane 0 says what it found.
typedef struct CheckDriverCheck CheckDriverCheck;
struct CheckDriverCheck
{
    Link* link;
    LinkError result;
};

static void check_driver_check_main(void* argument)
{
    CheckDriverCheck* check = argument;
    const LinkError result = link_check_lanes(check->link);
    if (lane_index() == 0)
    {
        check->result = result;
    }
}

// The bound link's link_check: on `lanes` lanes (link_check_lanes, the same answer), or on the calling thread when there
// is one, or the lanes can't start.
static LinkError check_driver_link_check(Link* link, uint32_t lanes)
{
    CheckDriverCheck check = { .link = link, .result = LINK_ERROR_NONE };
    const bool ran = lanes > 1 && lane_run(lanes, check_driver_check_main, &check, 0);
    return ran ? check.result : link_check(link);
}

_Static_assert(EXECUTABLE_PHASES == BUSTER_CHECK_CLOCK_WRITE_PHASES, "--time-stages names every phase of executable_write");

static void check_driver_write_main(void* argument)
{
    CheckDriverWrite* write = argument;
    const bool succeeded = executable_write(write->link, &write->options, &write->result);
    if (lane_index() == 0)
    {
        write->succeeded = succeeded;
    }
}

// A binding's failure, about the name `name` (empty for none).
static void check_driver_bind_failed(BindError error, const char* name)
{
    if (error == BIND_ERROR_UNDEFINED)
    {
        fprintf(stderr, "error: undefined reference to '%s'\n", name);
    }
    else if (error == BIND_ERROR_DUPLICATE)
    {
        fprintf(stderr, "error: duplicate definition of '%s': the C runtime defines it too\n", name);
    }
    else if (error == BIND_ERROR_SYMBOLS)
    {
        fprintf(stderr, "error: two global symbols are named '%s'\n", name);
    }
    else if (error == BIND_ERROR_DISCARDED)
    {
        fprintf(stderr, "error: a reference to '%s', a symbol of a discarded COMDAT group\n", name);
    }
    else if (error == BIND_ERROR_SNAPSHOT)
    {
        fprintf(stderr, "error: the C runtime's snapshot is damaged\n");
    }
    else
    {
        fprintf(stderr, "fatal error: binding capacity exhausted\n");
    }
}

// Whether the link's name `name` is a symbol of the program's whose declaration a system header wrote: an undefined one
// is then a library's function that the C library doesn't have (libm's sin, linked by -lm).
static bool check_driver_system_declared(const CheckProgram* program, const Parser* parser, const Preprocessor* pp, const Link* link, NameId name)
{
    bool system = false;
    for (uint32_t s = 0; name != 0 && s < program->symbol_count && s + 1 < link->symbols.count; s += 1)
    {
        const uint32_t object = program->symbol_objects[s];
        const uint32_t declaration = object < program->object_count ? program->object_declarations[object] : BUSTER_CHECK_NONE;
        if (link->symbols.name[s + 1] == name && declaration < parser->tokens.count)
        {
            system = check_lazy_system(pp, parser->tokens.sources[declaration]);
        }
    }
    return system;
}

// A name of the link's, for a diagnostic, as a string in `buffer`.
static const char* check_driver_link_name(const Link* link, NameId name, char* buffer, uint64_t capacity)
{
    buffer[0] = 0;
    if (name != 0 && name < link->names.count)
    {
        snprintf(buffer, capacity, "%.*s", (int)link->names.length[name], (const char*)link->names.bytes[name]);
    }
    return buffer;
}

// Where a fixup of the link that binding joined to `runtime` (from unit `unit_base` on) comes from, as GNU ld names an
// input: a member of the runtime's archives as archive(member), or "the program" for the unit's own code, and the name
// of its target, into `buffer`. A pass over the atoms, on a failed write alone.
static const char* check_driver_fixup_origin(const Link* link, const Runtime* runtime, uint32_t unit_base, FixupId fixup, char* buffer, uint64_t capacity)
{
    AtomId found = 0;
    for (AtomId atom = 1; found == 0 && atom < link->atoms.count; atom += 1)
    {
        const bool inside = link->atoms.fixup_count[atom] != 0 && fixup >= link->atoms.fixup_begin[atom] && fixup - link->atoms.fixup_begin[atom] < link->atoms.fixup_count[atom];
        found = inside ? atom : 0;
    }
    const uint32_t unit = found != 0 ? link->atoms.unit[found] : 0;
    const bool member = found != 0 && unit >= unit_base + runtime->member_unit && unit - unit_base - runtime->member_unit < runtime->member_count;
    const uint32_t m = member ? unit - unit_base - runtime->member_unit : 0;
    const String8 archive = member ? runtime_member_archive(runtime, m)->path : (String8){ 0 };
    const String8 name = member ? runtime_member_name(runtime, m) : (String8){ 0 };
    const SymbolId target = fixup < link->fixups.count ? link->fixups.target[fixup] : 0;
    const NameId target_name = target != 0 && target < link->symbols.count ? link->symbols.name[target] : 0;
    char target_bytes[256];
    check_driver_link_name(link, target_name, target_bytes, sizeof(target_bytes));
    if (member)
    {
        snprintf(buffer, capacity, "%.*s(%.*s)'s fixup to '%s'", (int)archive.length, archive.pointer, (int)name.length, name.pointer, target_bytes);
    }
    else
    {
        snprintf(buffer, capacity, "%s fixup to '%s'", found != 0 && unit < unit_base ? "the program's" : "a", target_bytes);
    }
    return buffer;
}

// The libraries the command line names, -lNAME (or -l NAME), and the directories it adds to their search, -LDIR (or
// -L DIR), each in its order, for the direct path to link (runtime_libraries_import).
typedef struct CheckDriverLibraries CheckDriverLibraries;
struct CheckDriverLibraries
{
    String8* names;
    uint32_t count;
    String8* directories;
    uint32_t directory_count;
};

// Says why the -l libraries didn't import (runtime_libraries_import's error).
static void check_driver_libraries_failed(const RuntimeLibraries* libraries, RuntimeError error)
{
    if (error == RUNTIME_ERROR_MISSING)
    {
        fprintf(stderr, "error: cannot find %s\n", libraries->failed);
    }
    else if (error == RUNTIME_ERROR_ARCHIVE)
    {
        fprintf(stderr, "error: unsupported: static libraries: %s is an archive, which the direct path doesn't link yet (-c and a system linker "
                        "take it)\n", libraries->failed);
    }
    else if (error == RUNTIME_ERROR_FILE)
    {
        fprintf(stderr, "error: can't read %s: %s\n", libraries->failed, strerror(libraries->system_error));
    }
    else if (error == RUNTIME_ERROR_IMPORT)
    {
        fprintf(stderr, "error: %s isn't a shared library or a linker script the direct path reads (import error %u)\n", libraries->failed,
                (uint32_t)libraries->import.error);
    }
    else
    {
        fprintf(stderr, "fatal error: can't reserve memory for the libraries\n");
    }
}

// The units of a command that links several (c_checker_units.c): their kept links, which check_units_merge joins into
// one, in the units' order, binding them as an emission's (check_program_emit) binds its link.
typedef struct CheckUnitsUnit CheckUnitsUnit;
static bool check_units_merge(Link* link, const CheckUnitsUnit* units, uint32_t count, CheckProgramBinding* binding, ExecutableDebug* debug, Arena* output,
                              Arena* bytes, Arena* scratch);
static bool check_units_merge_lanes(Link* link, const CheckUnitsUnit* units, uint32_t count, CheckProgramBinding* binding, ExecutableDebug* debug, Arena* output,
                                    Arena* bytes, Arena* scratch, uint32_t lanes);
static bool check_units_system_declared(const CheckUnitsUnit* units, uint32_t count, const Link* link, NameId name);

// Links the checked program straight into an executable at `output_path` (#157's direct path, #145's slice): a dynamic
// PIE against the system's C runtime, which comes from its snapshot in the cache directory (imported and written there
// when it is missing or stale) and which binding joins between the program's symbols and its atoms. A binding that finds
// a piece of the snapshot damaged (BIND_ERROR_SNAPSHOT) discards the snapshot and emits once more from a fresh import.
// With `debug` (-g), the executable carries DWARF: a unit of the program's functions with their source rows (#395,
// c_check_debug.c); and with `unwind`, its functions' unwind tables, which .eh_frame_hdr indexes with the runtime's. The
// functions compile on `lanes` (check_program_emit_lanes). With `units` (#152), the program is theirs: their kept links,
// merged (check_units_merge), take the place of `program`'s emission, which is null, as are `data`, `parser`, `pp` and
// `types`, and the executable is written on `lanes`' count of lanes; with `debug`, its DWARF is their units', one each,
// which the merge joins (#779).
static bool check_driver_executable(const CheckProgram* program, const CheckDataPrepared* data, const Parser* parser,
                                     Preprocessor* pp, TypeTable* types, const char* output_path, bool scalar, bool verify, bool debug,
                                     bool unwind, const CheckDriverLibraries* libraries, CheckProgramLanes* lanes, Arena* scratch, CheckClock* stage_clock,
                                     const CheckUnitsUnit* units, uint32_t unit_count)
{
    (void)scalar;

    char cache[PATH_MAX];
    const bool cached = snapshot_directory(cache, sizeof(cache));
    bool valid = false;
    bool again = true;
    for (uint32_t attempt = 0; again && attempt < 2; attempt += 1)
    {
        again = false;
        Runtime runtime;
        const SnapshotResult opened = runtime_open(&runtime, cached ? cache : NULL);
        // The libraries -l names, imported for this runtime, whose version indices theirs follow.
        static RuntimeLibraries named;
        const bool naming = libraries->count != 0 && opened.error == RUNTIME_ERROR_NONE;
        const RuntimeError imported = naming ? runtime_libraries_import(&named, &runtime, libraries->names, libraries->count, libraries->directories,
                                                                        libraries->directory_count) : RUNTIME_ERROR_NONE;
        Arena output = arena_reserve(UINT64_C(1) << 34);
        Arena bytes = arena_reserve(UINT64_C(1) << 34);
        Arena arena = arena_reserve(UINT64_C(1) << 32);
        const uint64_t mark = scratch->length;
        Bind bind = { 0 };
        CheckProgramBinding binding = {
            .runtime = &runtime,
            .bind = &bind,
            .path = BUSTER_SCALAR_PATH(scalar) ? BIND_PATH_EXACT : BIND_PATH_VECTOR,
            .arena = &arena,
            .data_objects = data != NULL ? data->objects : NULL,
            .data_count = data != NULL ? data->count : 0,
        };
        CheckProgramCode code = { .status = CHECK_PROGRAM_RESOURCE, .token = BUSTER_CHECK_NONE };
        CheckDataResult emission = { .status = CHECK_DATA_RESOURCE, .failed_token = BUSTER_CHECK_NONE };
        CheckProgramRows rows = { 0 };
        CheckDebug dwarf = { 0 };
        char name[256] = { 0 };
        valid = opened.error == RUNTIME_ERROR_NONE && imported == RUNTIME_ERROR_NONE && output.base != 0 && bytes.base != 0 && arena.base != 0;
        if (opened.error != RUNTIME_ERROR_NONE)
        {
            fprintf(stderr, "error: can't import the C runtime%s%s\n", runtime.failed_file[0] != 0 ? ": " : "", runtime.failed_file);
        }
        else if (imported != RUNTIME_ERROR_NONE)
        {
            check_driver_libraries_failed(&named, imported);
        }
        else if (!valid)
        {
            fprintf(stderr, "fatal error: can't reserve linker memory\n");
        }
        check_clock_mark(stage_clock, CHECK_STAGE_RUNTIME);
        // The units' links merged report their own failures (check_units_merge), but binding's: on the units' lanes by the
        // vector path (#778), and by its twin on one lane or the scalar path.
        const bool merged_on_lanes = units != NULL && lanes->lanes > 1 && !BUSTER_SCALAR_PATH(scalar);
        const bool emitted = !valid || (merged_on_lanes ? check_units_merge_lanes(&code.link, units, unit_count, &binding, debug ? &dwarf.attach : NULL, &output, &bytes,
                                                                                  scratch, lanes->lanes) :
                                        units != NULL   ? check_units_merge(&code.link, units, unit_count, &binding, debug ? &dwarf.attach : NULL, &output, &bytes, scratch) :
                                                        check_program_emit_timed(&code, program, parser, pp->table, data->capacity, BUSTER_SCALAR_PATH(scalar), &output,
                                                                                 &bytes, scratch, &binding, debug ? &rows : NULL, unwind, verify, lanes, stage_clock));
        if (units != NULL)
        {
            check_clock_mark(stage_clock, CHECK_STAGE_CODE_ATOMS);
        }
        if (!emitted)
        {
            valid = false;
            again = bind.error == BIND_ERROR_SNAPSHOT;
            if (bind.error != BIND_ERROR_NONE && !again)
            {
                check_driver_bind_failed(bind.error, binding.failed_name);
            }
            else if (!again && units == NULL)
            {
                check_driver_emission_failed(parser, pp, &code);
            }
        }
        check_driver_lower_report(&code, stage_clock);
        // The units' DWARF is in the merge's (check_units_merge_debug).
        if (valid && debug && units == NULL)
        {
            valid = check_debug_build(&dwarf, program, &rows, parser, pp, BUSTER_SCALAR_PATH(scalar), &output, scratch);
            if (!valid)
            {
                fprintf(stderr, "error: can't make the debug information: no memory, or a source name or file name DWARF can't hold\n");
            }
        }
        check_clock_mark(stage_clock, CHECK_STAGE_DEBUG);
        // The units' static data is in their links already.
        if (valid && units == NULL)
        {
            valid = check_data_publish(data, types, program->stream, program->object_symbols, program->object_count, &code.link, 0, &bytes, scratch, &emission);
            if (!valid)
            {
                check_driver_diagnose(parser, pp, emission.failed_token, "checked static data could not be emitted");
            }
        }
        // The definitions' named sections (#619): the executable's classes merge them, as a linker's .text.* and .data.*
        // do, and a zero-filled variable in one is data. The units' links had theirs before they were kept
        // (check_driver_object's).
        ObjectAtomSection* sections = 0;
        if (valid && units == NULL && !check_program_sections(&code, program, &bytes, &output, &sections))
        {
            check_driver_emission_failed(parser, pp, &code);
            valid = false;
        }
        check_clock_mark(stage_clock, CHECK_STAGE_PUBLISH_DATA);
        if (valid)
        {
            // Binding takes the libraries between bind_begin, which check_program_emit ran, and bind_end.
            bind.libraries = naming ? &named : 0;
            valid = bind_end(&bind, &code.link);
            again = !valid && bind.error == BIND_ERROR_SNAPSHOT;
            if (!valid && !again)
            {
                check_driver_link_name(&code.link, bind.name, name, sizeof(name));
                const bool system = bind.error == BIND_ERROR_UNDEFINED && (units != NULL ? check_units_system_declared(units, unit_count, &code.link, bind.name) :
                                                                                           check_driver_system_declared(program, parser, pp, &code.link, bind.name));
                if (system)
                {
                    fprintf(stderr, "error: undefined reference to '%s', which a system header declares: its library isn't linked (-lm links "
                                    "libm's functions)\n", name);
                }
                else if (bind.error == BIND_ERROR_LIBRARY_UNDEFINED)
                {
                    // A needed library's own reference that nothing defines, as GNU ld words it, with the version it requires.
                    const String8 path = named.paths[bind.library];
                    const ImportReferences* references = &named.libraries[bind.library].references;
                    const String8 version = named.reference_versions[bind.library][bind.reference];
                    fprintf(stderr, "error: %.*s: undefined reference to '%.*s%s%.*s'\n", (int)path.length, path.pointer, (int)references->length[bind.reference],
                            (const char*)references->name[bind.reference], version.length != 0 ? "@" : "", (int)version.length, version.pointer);
                }
                else if (bind.error == BIND_ERROR_MEMBER_UNDEFINED)
                {
                    // A member that joined refers to a name nothing defines, as GNU ld words it, with the archive's
                    // path and the member's name: on GCC's link line only libgcc_s.so.1 could define it, which the
                    // driver names as needed, and this system has none, or one without the name (morestack.o's
                    // unwinder, for split-stack code).
                    const bool known = bind.member != UINT32_MAX;
                    const String8 archive = known ? runtime_member_archive(&runtime, bind.member)->path : (String8){ 0 };
                    const String8 member = known ? runtime_member_name(&runtime, bind.member) : (String8){ 0 };
                    fprintf(stderr, "error: %.*s%s%.*s%s: undefined reference to '%s'; GCC's driver takes it from libgcc_s.so.1, %s\n", (int)archive.length,
                            archive.pointer, known ? "(" : "a member of the C runtime's archives", (int)member.length, member.pointer, known ? ")" : "", name,
                            runtime.gcc_s_path.length != 0 ? "which doesn't define it here" : "which this system doesn't have");
                }
                else if (bind.error == BIND_ERROR_LIBRARY_VERSION)
                {
                    // A version a loaded library needs of another that that one doesn't define, as glibc's loader words it: the
                    // named library's path, or libc.so.6's or the loader's soname.
                    const RuntimeVersionNeeds* needs = &named.needs[bind.library];
                    const String8 path = named.paths[bind.library];
                    const String8 provider = bind.provider != UINT32_MAX ? named.paths[bind.provider] : needs->file[bind.need];
                    const String8 version = needs->name[bind.need];
                    fprintf(stderr, "error: %.*s: version '%.*s' not found (required by %.*s)\n", (int)provider.length, provider.pointer, (int)version.length,
                            version.pointer, (int)path.length, path.pointer);
                }
                else
                {
                    check_driver_bind_failed(bind.error, name);
                }
            }
        }
        ExecutableDynamic dynamic = { 0 };
        if (valid)
        {
            // Several units' bound link is checked on their lanes (#778).
            const LinkError checked = check_driver_link_check(&code.link, units != NULL && !BUSTER_SCALAR_PATH(scalar) ? lanes->lanes : 1);
            valid = checked == LINK_ERROR_NONE && dynamic_inputs(&dynamic, &runtime, &bind, &arena);
            // An import the code reaches other than through the GOT, by a call or from data, which a shared library's
            // data needs without copy relocations, which the direct path makes none of (#84). Tier 0 reaches every symbol
            // the unit doesn't define through the GOT (check_program_emit's access column), so this is the compiler's
            // error.
            SymbolId direct = 0;
            for (FixupId f = 1; checked == LINK_ERROR_BINDING && direct == 0 && f < code.link.fixups.count; f += 1)
            {
                const SymbolId target = code.link.fixups.target[f];
                const uint8_t kind = code.link.fixups.kind[f] & BUSTER_LINK_FIXUP_KIND;
                if (target != 0 && code.link.symbols.source[target] == BUSTER_LINK_SOURCE_RUNTIME_SHARED && kind != BUSTER_LINK_FIXUP_CALL &&
                    kind != BUSTER_LINK_FIXUP_CALL_GOT && kind != BUSTER_LINK_FIXUP_LOAD_GOT && kind != BUSTER_LINK_FIXUP_GOTPCREL && kind != BUSTER_LINK_FIXUP_ABS64)
                {
                    direct = target;
                }
            }
            if (direct != 0)
            {
                fprintf(stderr, "internal compiler error: '%s', a shared library's symbol, reached directly rather than through the GOT\n",
                        check_driver_link_name(&code.link, code.link.symbols.name[direct], name, sizeof(name)));
            }
            else if (!valid)
            {
                fprintf(stderr, "internal compiler error: the bound link isn't valid (link error %u)\n", (uint32_t)checked);
            }
        }
        // The program's symbols in the executable's symbol table, as a linker writes its objects' (#152): every named one
        // of the program's (the link's below bind_begin's start), but the compiler's local labels (.L), which an
        // assembler leaves out of its objects, and a TLS variable of no size, which the writer has no section for. -g's
        // unit writes its functions' instead (ExecutableOptions.debug).
        SymbolId* selected = valid && !debug ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, bind.program_symbols) : NULL;
        ExecutableSymbols symbols = { .ids = selected };
        for (SymbolId s = 1; selected != NULL && s < bind.program_symbols; s += 1)
        {
            const NameId symbol_name = code.link.symbols.name[s];
            const bool label = symbol_name != 0 && code.link.names.length[symbol_name] >= 2 && code.link.names.bytes[symbol_name][0] == '.' &&
                               code.link.names.bytes[symbol_name][1] == 'L';
            const bool empty_tls = (code.link.symbols.flags[s] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS && code.link.symbols.size[s] == 0;
            if (symbol_name != 0 && !label && !empty_tls)
            {
                selected[symbols.count++] = s;
            }
        }
        check_clock_mark(stage_clock, CHECK_STAGE_EXECUTABLE);
        if (valid)
        {
            CheckDriverWrite write = {
                .link = &code.link,
                .options = {
                    .path = { .pointer = (char*)output_path, .length = strlen(output_path) },
                    .entry = dynamic_global(&runtime, &bind, "_start"),
                    .dynamic = &dynamic,
                    .debug = debug ? &dwarf.attach : NULL,
                    .symbols = selected != NULL ? &symbols : NULL,
                    .exact = BUSTER_SCALAR_PATH(scalar),
                },
            };
            // Several units' executable is written on their lanes (`lanes`' count), one unit's by its calling thread.
            const uint32_t write_lanes = units != NULL && lanes->lanes > 1 ? lanes->lanes : 1;
            valid = lane_run(write_lanes, check_driver_write_main, &write, 0) && write.succeeded;
            // The write's phases, for --time-stages (#778): each from its start or the phase before it.
            uint64_t phase_begin = write.result.start_time;
            for (uint32_t phase = 0; stage_clock != NULL && valid && phase < EXECUTABLE_PHASES; phase += 1)
            {
                const uint64_t end = write.result.phase_times[phase] >= phase_begin ? write.result.phase_times[phase] : phase_begin;
                stage_clock->write_nanoseconds[phase] = end - phase_begin;
                phase_begin = end;
            }
            if (stage_clock != NULL)
            {
                stage_clock->wrote = valid;
            }
            const ExecutableError error = write.result.error;
            if (!valid && error == EXECUTABLE_ERROR_UNDEFINED && write.result.symbol < code.link.symbols.count)
            {
                fprintf(stderr, "error: undefined reference to '%s'\n",
                        check_driver_link_name(&code.link, code.link.symbols.name[write.result.symbol], name, sizeof(name)));
            }
            else if (!valid && error == EXECUTABLE_ERROR_FILE)
            {
                fprintf(stderr, "error: can't write %s: %s: %s\n", output_path, write.result.call != NULL ? write.result.call : "write",
                        strerror(write.result.system_error));
            }
            else if (!valid && (error == EXECUTABLE_ERROR_RELOCATION || error == EXECUTABLE_ERROR_FIXUP) && write.result.fixup != 0)
            {
                // The fixup's origin and target, a runtime member's as libgcc.a(generic-morestack.o) names one.
                char origin[BUSTER_RUNTIME_PATH_BYTES + 512];
                fprintf(stderr, "error: can't link %s: %s (executable error %u)\n", output_path,
                        check_driver_fixup_origin(&code.link, &runtime, units != NULL ? unit_count : 1, write.result.fixup, origin, sizeof(origin)), (uint32_t)error);
            }
            else if (!valid)
            {
                fprintf(stderr, "error: can't link %s (executable error %u)\n", output_path, (uint32_t)error);
            }
        }
        check_clock_mark(stage_clock, CHECK_STAGE_WRITE);
        if (again && cached)
        {
            snapshot_discard(cache, RUNTIME_KIND_DYNAMIC);
        }
        arena_pop_to(scratch, mark);
        check_program_lanes_release(lanes);
        arena_release(&arena);
        arena_release(&bytes);
        arena_release(&output);
        if (naming)
        {
            runtime_libraries_release(&named);
        }
        runtime_release(&runtime);
        check_clock_mark(stage_clock, CHECK_STAGE_RELEASE);
    }
    return valid;
}

// How many CPUs the process may run on (sched_getaffinity's), at most the lanes an emission runs: the functions' lanes'
// default, of which the bodies' tokens then decide how many pay (check_program_emit_lanes).
static uint32_t check_driver_cpus(void)
{
    uint64_t mask[16] = { 0 };
    const long written = syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask);
    uint32_t cpus = 0;
    for (long word = 0; word < 16 && word * 8 < written; word += 1)
    {
        cpus += (uint32_t)__builtin_popcountll(mask[word]);
    }
    return cpus == 0 ? 1 : cpus < BUSTER_CHECK_EMIT_MAX_LANES ? cpus : BUSTER_CHECK_EMIT_MAX_LANES;
}

static const char* check_driver_object_name(Arena* arena, const char* source)
{
    const char* slash = strrchr(source, '/');
    const char* name = slash != 0 ? slash + 1 : source;
    const char* dot = strrchr(name, '.');
    const uint64_t length = dot != 0 && dot != name ? (uint64_t)(dot - name) : strlen(name);
    char* result = length <= UINT64_MAX - 3 ? BUSTER_ARENA_PUSH_ARRAY(arena, char, length + 3) : 0;
    if (result != 0)
    {
        memcpy(result, name, length);
        memcpy(result + length, ".o", 3);
    }
    return result;
}

// --id-order N, #153's test option: the unit's identifiers get their ids in another order, on which nothing the command
// writes may depend (test-check-lanes and test-c-testsuite compare what it writes with the plain command's, and
// test-checker-deferred checks its paths so). The unit is preprocessed first, printing nothing, into a table of its own,
// whose names then prime `table` by N (intern_prime_reordered): the command's own run finds every other one of them in
// another order, and gives the rest ids after them.
static bool check_driver_id_order(InternTable* table, const PPOptions* options, const char* path, uint64_t seed)
{
    PPOptions silent = *options;
    silent.quiet = true;
    silent.timing = false;
    silent.defer_history = false;
    silent.no_history = true;
    // The preparatory table owns its IDs; a caller's cache belongs to the table the real compile uses.
    silent.files = NULL;
    InternTable names = { 0 };
    Arena arena = arena_reserve(sizeof(Preprocessor) + 64);
    Preprocessor* pp = arena.base != 0 ? BUSTER_ARENA_PUSH_ARRAY(&arena, Preprocessor, 1) : NULL;
    bool result = pp != NULL && intern_initialize(&names) && pp_initialize(pp, &silent, &names);
    if (result)
    {
        // The names it found, whether or not the unit preprocesses without errors: the command's own run reports them.
        pp_run(pp, path);
    }
    if (pp != NULL)
    {
        pp_release(pp);
    }
    result = result && intern_prime_reordered(table, &names, seed);
    intern_release(&names);
    arena_release(&arena);
    return result;
}

// What every translation unit of a command reads, and none writes: run_check_command_mode's options.
typedef struct CheckDriverCommand CheckDriverCommand;
struct CheckDriverCommand
{
    // The preprocessor's, from -E's options on the command line, for every unit.
    PPOptions options;
    // --scalar, --verify, --dump-typed, --dump-static, -w, -flazy-system-headers and --lower-all.
    bool scalar;
    bool verify;
    bool dump;
    bool dump_static;
    bool quiet;
    bool lazy_system_headers;
    bool lower_all;
    // -Wreturn-type's: whether to warn, whether -Werror promotes it, and -Werror=return-type's (1) or
    // -Wno-error=return-type's (-1) say.
    bool return_warning;
    bool warning_errors;
    int32_t return_error;
    // --id-order N (check_driver_id_order).
    bool id_ordered;
    uint64_t id_order;
    // --poison-arenas, #152's test option: the lanes of several units overwrite what each unit left in their arenas
    // with a pattern, before the next unit reuses them (c_checker_units.c).
    bool poison_arenas;
    // --shared-parts, #777's test option: a unit's emission that shares its functions' parts on the units' board pauses
    // after each part it takes itself, so that the lanes without units take some, and the command reports how many
    // they took, after the units' diagnostics (c_checker_units.c).
    bool shared_parts;
    // --units-walk, #776's experiment: with several source files, #150's walk prepares the units' files into one cache
    // and table before the units, and each lane's starts as a copy of them (c_checker_units.c).
    bool units_walk;
    // -c, and the default C command's link, unless -c, -fsyntax-only or a dump says otherwise.
    bool emit_object;
    bool link;
    // -g's source rows, with -c or a link, and the functions' unwind tables.
    bool source_rows;
    bool unwind_tables;
    // -o's file, or null.
    const char* output;
    // The direct path's -l libraries and -L directories.
    const CheckDriverLibraries* libraries;
};

typedef struct CheckUnitsKept CheckUnitsKept;
static int check_driver_units(const CheckDriverCommand* command, char** inputs, uint32_t count, CheckProgramLanes* lanes, CheckClock* stage_clock);

// One translation unit, `path`, with `options` (the command's, with the unit's file cache and diagnostics' stream):
// preprocessed, parsed and checked, its diagnostics printed through the preprocessor's stream and the driver's own to
// `errors`; then, as the command says, its object written, its executable linked or, with `kept` (#152: one of several
// units that link into one executable), its link kept for theirs. `identifiers` is the unit's intern table, which its
// caller initialized (null if it couldn't), `pp` and `parser` its state, and `arena`, `scratch` and `output_arena` its
// memory; its functions compile on `lanes`. The preprocessor's phases' ticks go to `pp_ticks`, and with `stage_clock`
// (--time-stages), each stage's time. With `release`, it releases the parser's, the preprocessor's and the type table's
// memory before it returns. Returns the command's exit status: 0 for a unit without errors whose outputs were written.
static int check_driver_unit(const CheckDriverCommand* command, const PPOptions* options, const char* path, InternTable* identifiers, Preprocessor* pp,
                             Parser* parser, Arena* arena, Arena* scratch, Arena* output_arena, CheckProgramLanes* lanes, CheckClock* stage_clock,
                             uint64_t* pp_ticks, FILE* errors, CheckUnitsKept* kept, bool release)
{
    int result = 1;
    Arena type_arena = { 0 };
    Arena index_arena = { 0 };
    const bool scalar = command->scalar;
    const bool verify = command->verify;
    const bool dump = command->dump;
    const bool dump_static = command->dump_static;
    const bool quiet = command->quiet;
    const bool lazy_system_headers = command->lazy_system_headers;
    const bool lower_all = command->lower_all;
    const bool return_warning = command->return_warning;
    const bool warning_errors = command->warning_errors;
    const int32_t return_error = command->return_error;
    const bool id_ordered = command->id_ordered;
    const uint64_t id_order = command->id_order;
    const bool emit_object = command->emit_object;
    const bool link = command->link;
    const bool source_rows = command->source_rows;
    const bool unwind_tables = command->unwind_tables;
    const char* output = command->output;
    // The parse's temporaries in the checker's scratch, whose reader then takes their pages again (#528), and the tree
    // alone: the reader derives the traversal it reads into those pages (check_input_run).
    const ParseOptions parser_options = { .scalar = scalar, .tree_only = true, .scratch = scratch };
    bool ready = identifiers != NULL && (!id_ordered || check_driver_id_order(identifiers, options, path, id_order)) && pp_initialize(pp, options, identifiers) &&
                 parse_initialize(parser, &parser_options, options->language);
    if (!ready || scratch->base == 0 || output_arena->base == 0)
    {
        fprintf(errors, "fatal error: can't reserve checker memory\n");
    }
    else
    {
        CheckPrefix prefix = { 0 };
        CheckRepair repair = { 0 };
        const Parser* checking = parser;
        Preprocessor* checking_pp = pp;
        bool prefix_only = false;
        bool repairing = false;
        bool parse_failed = false;
        CheckInput original_pragmas = { 0 };
        bool original_pragmas_ready = false;
        bool original_pragmas_printed = false;
        check_clock_mark(stage_clock, CHECK_STAGE_SETUP);
        pp->options.collect_diagnostics = true;
        ready = pp_run(pp, path);
        if (!ready && pp->errors != 0 && !pp->diagnostic_collection_failed && !pp->history_failed)
        {
            // Only the produced source prefix is parsed after a real preprocessing fatal. Nonfatal errors
            // leave the whole stream. Either remains a rejected unit, independent of publication's cutoff.
            pp->fatal = false;
            ready = true;
        }
        pp->diagnostic_source = true;
        check_clock_mark(stage_clock, CHECK_STAGE_PREPROCESS);
        // The phases of pp_run alone: a diagnostic's later replay of the unit (pp_history_ensure) adds to them.
        memcpy(pp_ticks, pp->ticks, sizeof(pp->ticks));
        if (ready)
        {
            ready = parse_translation_unit(parser, pp);
            parse_failed = !ready;
            if (ready)
            {
                parse_print_diagnostics(parser, pp);
            }
            else if (check_repair_make(&repair, parser, pp, arena, scratch) == CHECK_REPAIR_READY)
            {
                // Only completed original expressions can contribute diagnostics. The synthetic closure
                // belongs to a private checking view and can never produce a successful source result.
                checking = &repair.parser;
                checking_pp = repair.preprocessor;
                prefix_only = true;
                repairing = true;
                ready = true;
            }
            else if (check_prefix_make(&prefix, parser, arena, scratch) == CHECK_PREFIX_READY)
            {
                // Only complete earlier declarations survive this view. Its checking suppresses obligations
                // that belong to the full translation-unit end; the original parser rejection stays final.
                checking = &prefix.parser;
                prefix_only = true;
                ready = true;
            }
        }
        if (parse_failed)
        {
            original_pragmas_ready = check_diagnostic_original_pragmas(&original_pragmas, parser, pp,
                                                                       check_diagnostic_parse_end(parser), arena, scratch);
            if (!original_pragmas_ready)
            {
                fprintf(errors, "fatal error: original pragma diagnostic capacity exhausted\n");
                ready = false;
            }
        }
        check_clock_mark(stage_clock, CHECK_STAGE_PARSE);
        const uint32_t tokens = checking->tokens.count;
        // A complete declaration prefix ends before the original parser's lookahead, not at the whole PP EOF.
        // Repairs retain their synthetic/end boundary; completed original bodies are handled independently.
        const uint32_t publication_end = !repairing && tokens < parser->tokens.count ? parser->tokens.sources[tokens] : pp->output.count;
        const TypeCapacity capacity = {
            .types = tokens <= (1u << 27) ? tokens * 4 + 64 : 0,
            .parameters = tokens,
            .tags = tokens,
            .members = tokens,
        };
        TypeTable types = { 0 };
        if (ready)
        {
            // The table's columns in base pages: each has room for every type the unit could make, far apart, of which
            // a unit makes few (sqlite3.c's 716,380 tokens, 3,434 types), at each column's start. In huge pages each
            // column's first touch cleared a 2 MB page: 39 MB of sqlite3.c's peak RSS (#528).
            const uint64_t type_bytes = type_capacity_bytes(capacity);
            type_arena = arena_reserve_small(type_bytes, type_bytes);
            index_arena = arena_reserve(type_index_capacity_bytes(capacity));
            ready = capacity.types != 0 && type_initialize(&types, &type_arena, &index_arena, capacity);
            if (!ready)
            {
                fprintf(errors, "fatal error: can't reserve type table\n");
            }
        }
        check_clock_mark(stage_clock, CHECK_STAGE_TYPE_TABLE);
        CheckLazy lazy = { 0 };
        if (ready && lazy_system_headers && !prefix_only)
        {
            ready = check_lazy_plan(&lazy, checking, checking_pp, arena, scratch, scalar);
            if (!ready)
            {
                fprintf(errors, "fatal error: system-header checking selection failed\n");
            }
        }
        if (ready)
        {
            CheckInput input;
            // A repaired fragment (check_input_fragment), a prefix (check_input_prefix), a unit with its system headers'
            // bodies deferred (check_input_selected) or the whole unit (check_input).
            const bool input_valid = check_input_compile(&input, checking, checking_pp, &types, arena, scratch, !prefix_only,
                                                         repairing ? repair.original_end : checking->tokens.count,
                                                         lazy_system_headers && !prefix_only ? lazy.deferred_tokens : 0);
            check_clock_mark(stage_clock, CHECK_STAGE_CHECK_INPUT);
            if (input.resource)
            {
                fprintf(errors, "fatal error: checker input capacity exhausted\n");
            }
            // A rejected reader may retain complete earlier expressions solely for diagnostics. Their typed
            // reports share source order with declaration reports, but this fragment can never be emitted.
            if (input_valid || input.diagnostic_graph)
            {
                const CheckOptions check_options = { .scalar = scalar, .verify = verify };
                CheckStream stream;
                ready = check_graph_run(&types, &input.graph, output_arena, scratch, &check_options, &stream);
                ready = ready && check_input_packed_finalize(&input, checking, &types, &stream, scratch);
                check_clock_mark(stage_clock, CHECK_STAGE_CHECK_EXPRESSIONS);
                if (ready)
                {
                    CheckDriverDiagnostic additional[3] = { 0 };
                    uint32_t additional_count = 0;
                    const uint32_t* excess_tokens = NULL;
                    uint32_t excess_count = 0;
                    CheckAsms asms = { 0 };
                    CheckInitializers initializers = { 0 };
                    const bool expression_valid = input_valid && stream.report_count == 0;
                    if (expression_valid && !repairing)
                    {
                        if (!check_asms_run(&asms, &input, &stream, &types, output_arena, scratch))
                        {
                            additional[additional_count++] = (CheckDriverDiagnostic){ asms.token,
                                asms.status == CHECK_ASM_RESOURCE ? "assembly operand checking capacity exhausted" : "invalid assembly operand or constraint", asms.token };
                        }
                        if (check_initializers_run(&initializers, &input, &stream, checking, &types, output_arena, scratch))
                        {
                            excess_tokens = initializers.warning_tokens;
                            excess_count = initializers.warning_count;
                        }
                    }
                    if (!expression_valid || repairing || initializers.status != CHECK_INIT_OK)
                    {
                        CheckInitDiagnostic earlier = { 0 };
                        const uint32_t before = repairing ? repair.record_begin : BUSTER_CHECK_NONE;
                        CheckInitWarningSink warnings = { .capacity = !quiet ? tokens : 0 };
                        warnings.tokens = warnings.capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output_arena, uint32_t, warnings.capacity) : NULL;
                        const bool storage = warnings.capacity == 0 || warnings.tokens != NULL;
                        const bool diagnosed = storage && initializers.status != CHECK_INIT_RESOURCE &&
                                               check_initializers_diagnose_warnings(&earlier, &input, &stream, checking, &types, before,
                                                                                     output_arena, scratch, !quiet ? &warnings : NULL);
                        if (!storage || initializers.status == CHECK_INIT_RESOURCE)
                        {
                            earlier.status = CHECK_INIT_RESOURCE;
                            earlier.token = initializers.token < tokens ? initializers.token : 0;
                        }
                        else if (!diagnosed && initializers.status != CHECK_INIT_OK)
                        {
                            earlier.status = initializers.status;
                            earlier.token = initializers.token;
                        }
                        if ((diagnosed && earlier.status != CHECK_INIT_OK) || earlier.status == CHECK_INIT_RESOURCE ||
                            (!diagnosed && initializers.status != CHECK_INIT_OK))
                        {
                            additional[additional_count++] = (CheckDriverDiagnostic){ earlier.token, check_driver_initializer_message(earlier.status), earlier.token };
                        }
                        if (diagnosed)
                        {
                            excess_tokens = warnings.tokens;
                            excess_count = warnings.count;
                        }
                    }
                    if (!expression_valid || repairing)
                    {
                        CheckAsmDiagnostic assembly = { 0 };
                        const uint32_t before = repairing ? repair.record_begin : BUSTER_CHECK_NONE;
                        const bool asm_diagnosed = check_asms_diagnose(&assembly, &input, &stream, &types, before, scratch);
                        if ((asm_diagnosed && assembly.status != CHECK_ASM_OK) || assembly.status == CHECK_ASM_RESOURCE)
                        {
                            additional[additional_count++] = (CheckDriverDiagnostic){ assembly.token,
                                assembly.status == CHECK_ASM_RESOURCE ? "assembly operand checking capacity exhausted" : "invalid assembly operand or constraint", assembly.token };
                        }
                    }
                    uint32_t promoted_token = BUSTER_CHECK_NONE;
                    bool promoted = !quiet && check_diagnostic_first_error(&input, &stream, &promoted_token);
                    promoted |= check_driver_warning_promoted(pp, &input, &input.warning_events, repairing ? &repair : NULL);
                    promoted |= check_driver_warning_promoted(pp, &input, &stream.warning_events, repairing ? &repair : NULL);
                    uint32_t promoted_source = BUSTER_CHECK_NONE;
                    const CheckInput* pragma_input = original_pragmas_ready ? &original_pragmas : &input;
                    const uint32_t pragma_end = original_pragmas_ready || !repairing ? BUSTER_CHECK_NONE : check_repair_pragma_end(&repair);
                    promoted |= check_diagnostic_first_pragma_error_mode(pragma_input, pragma_end, &promoted_token, &promoted_source, quiet);
                    bool visibility_diagnostics = false;
                    const CheckInputVisibilityPragmas* visibility_history = pragma_input->visibility_pragmas;
                    for (uint32_t i = 0; visibility_history != NULL && i < visibility_history->count; i += 1)
                    {
                        const CheckInputVisibilityPragma* record = visibility_history->records + i;
                        visibility_diagnostics |= record->error || record->warning != 0;
                    }
                    for (uint32_t i = 0; !quiet && !promoted && i < excess_count; i += 1)
                    {
                        promoted = check_diagnostic_severity(&input, excess_tokens[i], CHECK_WARNING_EXCESS_INITIALIZERS) >= CHECK_WARNING_ERROR;
                    }
                    if (!expression_valid || repairing || additional_count != 0 || promoted || visibility_diagnostics || pp->errors != 0)
                    {
                        CheckProgramDiagnostic control = { 0 };
                        const uint32_t control_before = repairing ? repair.record_begin : BUSTER_CHECK_NONE;
                        const bool control_diagnosed = check_program_diagnose(&control, &input, &stream, checking, &types, control_before, scratch);
                        if ((control_diagnosed && control.status != CHECK_PROGRAM_OK) || control.status == CHECK_PROGRAM_RESOURCE)
                        {
                            additional[additional_count++] = (CheckDriverDiagnostic){ control.token,
                                control.status == CHECK_PROGRAM_RESOURCE ? "checker program capacity exhausted" : "invalid C statement or control flow", control.position,
                                control.position < tokens ? control.position + 1u : tokens };
                        }
                    }
                    if (!check_driver_reports(checking, parser, pp, publication_end, &input, &stream, repairing ? &repair : NULL,
                                              original_pragmas_ready ? &original_pragmas : NULL, additional, additional_count,
                                              excess_tokens, excess_count, quiet, scratch) ||
                        !check_driver_warning_events_at(checking, pp, &input, &types, &input.warning_events, repairing ? &repair : NULL, publication_end, scratch) ||
                        !check_driver_warning_events_at(checking, pp, &input, &types, &stream.warning_events, repairing ? &repair : NULL, publication_end, scratch))
                    {
                        fprintf(errors, "fatal error: checker diagnostic capacity exhausted\n");
                        ready = false;
                    }
                    original_pragmas_printed = true;
                    if (input_valid && dump && !prefix_only)
                    {
                        check_driver_dump(&input.graph, &stream, &types, scratch);
                    }
                    result = ready && expression_valid && additional_count == 0 && !repairing && pp->errors == 0 &&
                             !pp->diagnostic_collection_failed && !pp->history_failed ? 0 : 1;
                    CheckProgram program;
                    CheckDataPrepared data = { 0 };
                    check_clock_mark(stage_clock, CHECK_STAGE_CHECK_DECLARATIONS);
                    if (result == 0 && (emit_object || link) && !prefix_only && !check_data_prepare(&data, &input, &stream, &initializers, &types, scalar, output_arena, scratch))
                    {
                        check_driver_diagnose(checking, pp, data.token, "static-data preparation failed");
                        result = 1;
                    }
                    check_clock_mark(stage_clock, CHECK_STAGE_STATIC_DATA);
                    const CheckProgramOptions program_options = {
                        .initializers = &initializers,
                        .asms = &asms,
                        .prepared_data = result == 0 && (emit_object || link) && !prefix_only ? &data : NULL,
                        .identifiers = pp->table,
                        .initializer_references = data.references,
                        .initializer_reference_count = data.reference_count,
                        .scalar = scalar,
                        .target = options->target,
                        .predefines = &options->predefines,
                    };
                    const bool program_diagnostics = result == 0 ||
                        (ready && expression_valid && additional_count == 0 && !repairing && !prefix_only && pp->errors != 0 &&
                         !pp->diagnostic_collection_failed && !pp->history_failed);
                    CheckInput diagnostic_input;
                    const CheckInput* program_input = &input;
                    if (program_diagnostics && result != 0 && input.visibility_error)
                    {
                        diagnostic_input = input;
                        diagnostic_input.visibility_error = false;
                        program_input = &diagnostic_input;
                    }
                    const bool program_checked = program_diagnostics &&
                        check_program(&program, checking, program_input, &stream, &types, program_options, output_arena, scratch);
                    if (program_diagnostics && !program_checked)
                    {
                        const char* message = program.status == CHECK_PROGRAM_RESOURCE ? "checker program capacity exhausted" :
                                              program.status == CHECK_PROGRAM_UNSUPPORTED ? "unsupported C statement" : "invalid C statement or control flow";
                        check_driver_diagnose(checking, pp, program.token, message);
                        result = 1;
                    }
                    check_driver_x86_reports(checking, parser, pp, &input, &stream, &types, quiet);
                    if (program_checked)
                    {
                        const CheckWarningSeverity baseline = !return_warning ? CHECK_WARNING_IGNORE :
                            return_error > 0 || (return_error == 0 && warning_errors) ? CHECK_WARNING_ERROR : CHECK_WARNING_WARNING;
                        check_driver_return_reports(checking, parser, pp, &program, baseline, quiet);
                    }
                    result = pp->errors != 0 ? 1 : result;
                    check_clock_mark(stage_clock, CHECK_STAGE_CHECK_PROGRAM);
                    // -c's object, or a kept link (one of several units that link), are written alike: the unit's code and
                    // data.
                    if (result == 0 && (emit_object || kept != NULL) && !prefix_only)
                    {
                        CheckProgramSelection selected;
                        const bool selection = check_program_select_mode(&selected, &program, &data, lower_all ? C_LOWERING_ALL : C_LOWERING_REACHABLE, scalar,
                                                                         output_arena, scratch);
                        check_clock_mark(stage_clock, CHECK_STAGE_SELECT);
                        if (selection)
                        {
                            const char* object_path = output != 0 ? output : check_driver_object_name(arena, path);
                            result = object_path != 0 && check_driver_object(&selected.program, &selected.data, checking, pp, &types, path,
                                                                           object_path, scalar, verify, dump_static, source_rows, unwind_tables, lanes,
                                                                           scratch, stage_clock, errors, kept) ? 0 : 1;
                        }
                        else
                        {
                            check_driver_diagnose(checking, pp, selected.token, "checked definition selection failed");
                            result = 1;
                        }
                    }
                    if (result == 0 && link && kept == NULL && !prefix_only)
                    {
                        CheckProgramSelection selected;
                        const bool selection = check_program_select_mode(&selected, &program, &data, lower_all ? C_LOWERING_ALL : C_LOWERING_REACHABLE, scalar,
                                                                         output_arena, scratch);
                        check_clock_mark(stage_clock, CHECK_STAGE_SELECT);
                        if (selection)
                        {
                            result = check_driver_executable(&selected.program, &selected.data, checking, pp, &types, output != 0 ? output : "a.out", scalar, verify,
                                                             source_rows, unwind_tables, command->libraries, lanes, scratch, stage_clock, NULL, 0) ? 0 : 1;
                        }
                        else
                        {
                            check_driver_diagnose(checking, pp, selected.token, "checked definition selection failed");
                            result = 1;
                        }
                    }
                    if (result == 0 && dump_static && !emit_object && !prefix_only)
                    {
                        result = check_driver_dump_static(&input, &stream, &initializers, checking, pp, &types, scratch) ? 0 : 1;
                    }
                }
                else
                {
                    fprintf(errors, "fatal error: %s\n", stream.status == CHECK_STATUS_RESOURCE ? "checker output capacity exhausted" : "invalid checker input graph");
                }
            }
            else
            {
                if (!check_driver_reports(checking, parser, pp, publication_end, &input, NULL, repairing ? &repair : NULL,
                                          original_pragmas_ready ? &original_pragmas : NULL, NULL, 0, NULL, 0, quiet, scratch) ||
                    !check_driver_warning_events_at(checking, pp, &input, &types, &input.warning_events, repairing ? &repair : NULL, publication_end, scratch))
                {
                    fprintf(errors, "fatal error: checker diagnostic capacity exhausted\n");
                }
                original_pragmas_printed = true;
            }
        }
        if (parse_failed)
        {
            if (original_pragmas_ready && !original_pragmas_printed)
            {
                uint32_t fp = 0;
                uint32_t visibility = 0;
                check_driver_remaining_pragmas(parser, pp, &original_pragmas, &fp, &visibility, quiet);
            }
            if (!pp->fatal)
            {
                parse_print_diagnostics(parser, pp);
            }
            result = 1;
        }
    }
    // What follows the last stage (a dump, diagnostics after a failed parse) is charged to the release.
    check_clock_mark(stage_clock, CHECK_STAGE_RELEASE);
    pp_diagnostic_publish(pp);
    pp_diagnostic_summary(pp);
    result = pp->errors != 0 ? 1 : result;
    if (release)
    {
        parse_release(parser);
        pp_release(pp);
        arena_release(&index_arena);
        arena_release(&type_arena);
    }
    return result;
}

// The semantic driver: `compiler check` (`executable` false) checks, and with -c writes an object; the default C command
// (`executable` true) also links an executable at -o (a.out by default) unless -c or -fsyntax-only says otherwise. With
// several source files (#152), it compiles each of them (check_driver_units): -c writes each one's object, and the link
// one executable of them all.
// Only main's terminal routes pass release false; every reusable caller releases its mappings before returning.
static int run_check_command_mode(int count, char** arguments, bool executable, bool release)
{
    WorkerBudget worker_budget;
    // --time-stages's clock (c_check_clock.c), started first so that its span covers the whole command, and only with the
    // option: a compile without it reads neither the clocks nor its resource use. A value that spells the option (`-o
    // --time-stages`) starts the clock too, which the arguments' loop below doesn't then hand to the stages.
    bool time_stages = false;
    for (int i = 0; i < count; i += 1)
    {
        time_stages |= strcmp(arguments[i], "--time-stages") == 0;
    }
    CheckClock clock_storage = { 0 };
    if (time_stages)
    {
        check_clock_start(&clock_storage);
    }
    time_stages = false;
    worker_budget_initialize(&worker_budget, getenv("MAKEFLAGS"), BUSTER_CHECK_EMIT_MAX_LANES);
    int result = 1;
    Arena arena = arena_reserve(1ull << 36);
    Arena scratch = arena_reserve(1ull << 36);
    Arena output_arena = arena_reserve(1ull << 36);
    char** filtered = arena_push_array(&arena, (uint64_t)count + 1, sizeof(char*), _Alignof(char*));
    uint32_t filtered_count = 0;
    // The source files: every argument that is neither an option nor an option's value (#152). pp_parse_options reads the
    // first, as it reads -E's one file, and the others are each a unit too.
    char** inputs = arena_push_array(&arena, (uint64_t)count + 1, sizeof(char*), _Alignof(char*));
    uint32_t input_count = 0;
    bool scalar = false;
    // --verify checks every function's records with x86_validate (the c-testsuite harness passes it).
    bool verify = false;
    bool dump = false;
    bool dump_static = false;
    bool emit_object = false;
    bool syntax_only = false;
    bool quiet = false;
    bool return_warning = true;
    bool warning_errors = false;
    int32_t return_error = 0;
    bool lazy_system_headers = false;
    // --lower-all lowers every definition that may be emitted, referenced or not: #129's test option, to measure what
    // lowering only the referenced ones saves.
    bool lower_all = false;
    // --id-order N hands the unit's identifiers their ids in another order (check_driver_id_order): #153's test option.
    bool id_ordered = false;
    // --poison-arenas and --shared-parts: #152's and #777's test options (CheckDriverCommand's).
    bool poison_arenas = false;
    bool shared_parts = false;
    // --units-walk: #776's experiment (CheckDriverCommand.units_walk).
    bool units_walk = false;
    bool invalid_id_order = false;
    uint64_t id_order = 0;
    // --time-stages reports each stage's time, the preprocessor's phases and the process's resource use on standard
    // error after the command (#518).
    // The direct path's -l libraries and -L directories, which every other mode leaves alone.
    CheckDriverLibraries libraries = {
        .names = arena_push_array(&arena, (uint64_t)count + 1, sizeof(String8), _Alignof(String8)),
        .directories = arena_push_array(&arena, (uint64_t)count + 1, sizeof(String8), _Alignof(String8)),
    };
    bool missing_value = false;
    // The lanes the functions compile on (#151, check_program_emit_lanes): by default up to one per CPU the process may
    // run on, as many as the bodies' tokens pay for; --lanes N runs N, as long as there are bodies for them. With several
    // source files, the lanes their units compile on, one unit at a time each (check_driver_units).
    CheckProgramLanes lanes = { .lanes = check_driver_cpus() };
    // The caller is one worker. A build system can cap this process explicitly; GNU make supplies shared ownership
    // of additional workers at emission time. No external budget changes the body's profitability decision itself.
    lanes.budget = &worker_budget;
    bool invalid_lanes = false;
    for (int i = 0; filtered != 0 && inputs != 0 && libraries.names != 0 && libraries.directories != 0 && i < count; i += 1)
    {
        scalar |= strcmp(arguments[i], "--scalar") == 0;
        quiet |= strcmp(arguments[i], "-w") == 0;
        // An option that takes the next argument as its value (pp_option_takes_value: pp_parse_options' and
        // c_predefines.c's) passes on with it, unread: `-o -lresult` writes -lresult, as clang's driver takes it.
        const bool valued = i + 1 < count && pp_option_takes_value(arguments[i]);
        if (!valued)
        {
            const char* argument = arguments[i];
            if (strcmp(argument, "-Wreturn-type") == 0 || strcmp(argument, "-Werror=return-type") == 0)
            {
                return_warning = true;
            }
            else if (strcmp(argument, "-Wno-return-type") == 0)
            {
                return_warning = false;
            }
            if (strcmp(argument, "-Werror") == 0 || strcmp(argument, "-Wno-error") == 0)
            {
                warning_errors = strcmp(argument, "-Werror") == 0;
            }
            else if (strcmp(argument, "-Werror=return-type") == 0 || strcmp(argument, "-Wno-error=return-type") == 0)
            {
                return_error = strcmp(argument, "-Werror=return-type") == 0 ? 1 : -1;
            }
        }
        const bool library = !valued && arguments[i][0] == '-' && arguments[i][1] == 'l';
        const bool directory = !valued && arguments[i][0] == '-' && arguments[i][1] == 'L';
        if (valued)
        {
            // The option and its value, however the value is spelled, for pp_parse_options.
            filtered[filtered_count++] = arguments[i];
            filtered[filtered_count++] = arguments[i + 1];
            i += 1;
        }
        else if (library || directory)
        {
            const char* value = arguments[i][2] != 0 ? arguments[i] + 2 : i + 1 < count ? arguments[++i] : 0;
            const String8 text = {
                .pointer = (char*)value,
                .length = value != 0 ? strlen(value) : 0,
            };
            missing_value |= text.length == 0;
            if (library)
            {
                libraries.names[libraries.count++] = text;
            }
            else
            {
                libraries.directories[libraries.directory_count++] = text;
            }
        }
        else if (strcmp(arguments[i], "--verify") == 0)
        {
            verify = true;
        }
        else if (strcmp(arguments[i], "--lanes") == 0 || strncmp(arguments[i], "--lanes=", 8) == 0)
        {
            const char* value = arguments[i][7] == '=' ? arguments[i] + 8 : i + 1 < count ? arguments[++i] : 0;
            char* end = 0;
            const unsigned long lane_count = value != 0 ? strtoul(value, &end, 10) : 0;
            invalid_lanes |= value == 0 || end == value || *end != 0 || lane_count == 0 || lane_count > BUSTER_CHECK_EMIT_MAX_LANES;
            lanes.lanes = (uint32_t)lane_count;
            lanes.every_lane = true;
        }
        else if (strcmp(arguments[i], "--worker-budget") == 0 || strncmp(arguments[i], "--worker-budget=", 16) == 0)
        {
            const char* value = arguments[i][15] == '=' ? arguments[i] + 16 : i + 1 < count ? arguments[++i] : NULL;
            char* end = NULL;
            const unsigned long workers = value != NULL ? strtoul(value, &end, 10) : 0;
            invalid_lanes |= value == NULL || end == value || *end != 0 || workers == 0 || workers > BUSTER_CHECK_EMIT_MAX_LANES;
            worker_budget.maximum = (uint32_t)workers;
        }
        else if (strcmp(arguments[i], "-flazy-system-headers") == 0)
        {
            lazy_system_headers = true;
        }
        else if (strcmp(arguments[i], "--lower-all") == 0)
        {
            lower_all = true;
        }
        else if (strcmp(arguments[i], "--poison-arenas") == 0)
        {
            poison_arenas = true;
        }
        else if (strcmp(arguments[i], "--shared-parts") == 0)
        {
            shared_parts = true;
        }
        else if (strcmp(arguments[i], "--units-walk") == 0)
        {
            units_walk = true;
        }
        else if (strcmp(arguments[i], "--id-order") == 0 || strncmp(arguments[i], "--id-order=", 11) == 0)
        {
            const char* value = arguments[i][10] == '=' ? arguments[i] + 11 : i + 1 < count ? arguments[++i] : NULL;
            char* end = NULL;
            id_order = value != NULL ? strtoull(value, &end, 10) : 0;
            invalid_id_order |= value == NULL || end == value || *end != 0;
            id_ordered = true;
        }
        else if (strcmp(arguments[i], "--time-stages") == 0)
        {
            time_stages = true;
        }
        else if (strcmp(arguments[i], "-fno-lazy-system-headers") == 0)
        {
            lazy_system_headers = false;
        }
        else if (strcmp(arguments[i], "-c") == 0)
        {
            emit_object = true;
        }
        else if (strcmp(arguments[i], "-fsyntax-only") == 0)
        {
            syntax_only = true;
        }
        else if (strcmp(arguments[i], "--dump-typed") == 0)
        {
            dump = true;
        }
        else if (strcmp(arguments[i], "--dump-static") == 0)
        {
            dump_static = true;
        }
        else
        {
            // A source file: the first goes to pp_parse_options in its place, and each is a unit.
            const bool source = arguments[i][0] != '-';
            if (!source || input_count == 0)
            {
                filtered[filtered_count++] = arguments[i];
            }
            if (source)
            {
                inputs[input_count++] = arguments[i];
            }
        }
    }
    // The default C command links unless -c, -fsyntax-only or a dump says otherwise.
    const bool link = executable && !emit_object && !syntax_only && !dump && !dump_static;
    // Zero until pp_parse_options fills them, which a failed allocation or an option without its value skips.
    PPOptions options = { 0 };
    const char* path = 0;
    const char* output = 0;
    bool markers = false;
    const bool parsed = filtered != 0 && !missing_value && !invalid_lanes && !invalid_id_order && pp_parse_options((int)filtered_count, filtered, &options, &arena, &path, &output, &markers);
    options.semantic_pragmas = true;
    options.timing = time_stages;
    CheckClock* stage_clock = time_stages ? &clock_storage : NULL;
    // The preprocessor's phases' ticks in pp_run, for the report.
    uint64_t pp_ticks[PP_PHASE_COUNT] = { 0 };
    // The expansion history is recorded only when something asks for it (PPOptions.defer_history): a diagnostic, and the
    // system-header judgment of a unit whose runs can't make it (check_input_system_tokens). --scalar judges by the
    // history, and -flazy-system-headers reads every declaration's, so they record it as the unit runs; so does a link or
    // an object with -g, whose rows read every function's tokens' expansion places (c_check_debug.c), rather than
    // preprocessing the unit again for them.
    const bool source_rows = parsed && (link || emit_object) && (options.predefines.debug || options.predefines.debug_modules);
    // The functions' unwind tables, as clang has them: by default, unless freestanding or an option turns them off.
    const bool unwind_tables = parsed && predefines_unwind_tables(&options.predefines, options.freestanding);
    options.defer_history = !lazy_system_headers && !scalar && !source_rows;
    const CheckDriverCommand command = {
        .options = options,
        .scalar = scalar,
        .verify = verify,
        .dump = dump,
        .dump_static = dump_static,
        .quiet = quiet,
        .lazy_system_headers = lazy_system_headers,
        .lower_all = lower_all,
        .return_warning = return_warning,
        .warning_errors = warning_errors,
        .return_error = return_error,
        .id_ordered = id_ordered,
        .id_order = id_order,
        .poison_arenas = poison_arenas,
        .shared_parts = shared_parts,
        .units_walk = units_walk,
        .emit_object = emit_object,
        .link = link,
        .source_rows = source_rows,
        .unwind_tables = unwind_tables,
        .output = output,
        .libraries = &libraries,
    };
    if (worker_budget.dry_run)
    {
        result = 0;
    }
    else if (!parsed)
    {
        fprintf(stderr, "usage: compiler [check] [--scalar] [--verify] [--dump-typed] [--dump-static] [-flazy-system-headers] [--lower-all] [--poison-arenas] [--shared-parts] [--units-walk] [--lanes N] "
                        "[--worker-budget N] [--time-stages] [--id-order N] [-fsyntax-only | -c] [-o file] [-lNAME] [-LDIR] [preprocessor options] file.c...\n");
    }
    else if (!check_input_target_supported(&options))
    {
        fprintf(stderr, "error: checker requires C, 32-bit wchar_t, x87 long double and source-precision floating evaluation\n");
    }
    else if (input_count > 1)
    {
        result = check_driver_units(&command, inputs, input_count, &lanes, stage_clock);
    }
    else
    {
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        const bool interned = intern_initialize(&identifiers);
        result = check_driver_unit(&command, &command.options, path, interned ? &identifiers : NULL, &pp, &parser, &arena, &scratch, &output_arena, &lanes,
                                   stage_clock, pp_ticks, stderr, NULL, release);
        if (release)
        {
            intern_release(&identifiers);
        }
    }
    if (release)
    {
        arena_release(&output_arena);
        arena_release(&scratch);
        arena_release(&arena);
    }
    check_clock_mark(stage_clock, CHECK_STAGE_RELEASE);
    if (stage_clock != NULL)
    {
        check_clock_report(stage_clock, pp_ticks, PP_PHASE_COUNT, stderr);
        if (worker_budget.requested != 0)
        {
            const char* mode = worker_budget.mode == WORKER_BUDGET_JOBSERVER ? "jobserver" :
                               worker_budget.mode == WORKER_BUDGET_UNAVAILABLE ? "unavailable" : "none";
            fprintf(stderr, "worker-budget\tmode=%s\tlimit=%u\trequested=%u\tgranted=%u\tactive=%u\tfallback=%u\n", mode,
                    worker_budget.maximum, worker_budget.requested, worker_budget.granted, lanes.active, (uint32_t)lanes.again);
        }
    }
    worker_budget_close(&worker_budget);
    return result;
}

// Tests and other reusable callers release all owned mappings before returning. Only main's terminal source
// commands select the other mode: the kernel tears their mappings down once when the process exits, after the
// stage report and stdio flush, rather than through each subsystem's individual munmap calls first.
static int run_check_command(int count, char** arguments, bool executable)
{
    return run_check_command_mode(count, arguments, executable, true);
}
