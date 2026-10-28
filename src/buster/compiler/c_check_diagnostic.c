// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Apply source-ordered diagnostic state to warnings already established by semantic checking. A pragma cannot
// suppress an ordinary constraint report. The small warning-group relations follow Clang 23.1.1; no diagnostic
// implementation is imported. Include after c_check_input.c, whose immutable history owns the decoded strings.

#define CHECK_WARNING_EXCESS_INITIALIZERS 0x100u
#define CHECK_WARNING_UNKNOWN_PRAGMA 0x200u
#define CHECK_WARNING_IGNORED_ATTRIBUTES 0x400u
#define CHECK_WARNING_ADDRESS_OF_PACKED_MEMBER 0x10000u
#define CHECK_WARNING_ARGUMENT_OUTSIDE_RANGE 0x20000u
#define CHECK_WARNING_RETURN_TYPE 0x40000u

#include "c_check_packed_diagnostic.c"

typedef enum CheckWarningSeverity
{
    CHECK_WARNING_IGNORE,
    CHECK_WARNING_WARNING,
    CHECK_WARNING_ERROR,
    CHECK_WARNING_FATAL,
} CheckWarningSeverity;

typedef struct CheckWarningDescription CheckWarningDescription;
struct CheckWarningDescription
{
    const char* option;
    const char* parent;
    const char* message;
};

static const CheckWarningDescription check_warning_descriptions[] =
{
    { "-Wincompatible-pointer-types-discards-qualifiers", "-Wincompatible-pointer-types", "conversion discards pointer qualifiers" },
    { "-Wgnu-folding-constant", "-Wgnu", "folding an integer constant expression is a GNU extension" },
    { "-Wenum-too-large", NULL, "enumeration values exceed the available integer range" },
    { "-Winteger-overflow", NULL, "integer constant expression overflows its type" },
    { "-Wshift-count-negative", NULL, "shift count is negative" },
    { "-Wshift-count-overflow", NULL, "shift count is not smaller than the width of the type" },
    { "-Wshift-negative-value", NULL, "left shift of a negative value" },
    { "-Wshift-overflow", NULL, "left shift exceeds the range of the type" },
    { "-Wexcess-initializers", NULL, "excess elements in initializer" },
    { "-Wunknown-pragmas", "-Wpragmas", "expected 'ON' or 'OFF' or 'DEFAULT' in pragma" },
    { "-Wignored-attributes", "-Wattributes", "attribute declaration must precede definition" },
    { "-Wpointer-sign", NULL, "converts between pointers to integer types with different sign" },
    { "-Wpointer-integer-compare", NULL, "comparison between pointer and integer" },
    { "-Wcompare-distinct-pointer-types", NULL, "comparison of distinct pointer types" },
    { "-Wconditional-type-mismatch", NULL, "pointer/integer type mismatch in conditional expression" },
    { "-Wpointer-type-mismatch", NULL, "pointer type mismatch" },
    { "-Waddress-of-packed-member", NULL, "taking address of packed member may result in an unaligned pointer value" },
    { "-Wargument-outside-range", NULL, "argument value is outside the valid range" },
    { "-Wreturn-type", NULL, "non-void function does not return a value" },
};

static bool check_diagnostic_option(String8 option, const char* expected)
{
    const bool result = expected != NULL && option.length == strlen(expected) && memcmp(option.pointer, expected, option.length) == 0;
    return result;
}

static uint32_t check_diagnostic_warning_index(uint32_t warning)
{
    const uint32_t index = warning != 0 && (warning & (warning - 1)) == 0 ? (uint32_t)__builtin_ctz(warning) : UINT32_MAX;
    return index;
}

static const char* check_diagnostic_message(uint32_t warning)
{
    const uint32_t index = check_diagnostic_warning_index(warning);
    const char* result = index < BUSTER_ARRAY_LENGTH(check_warning_descriptions) ? check_warning_descriptions[index].message : "semantic warning";
    return result;
}

static CheckWarningSeverity check_diagnostic_severity_from(const CheckInput* input, uint32_t token, uint32_t source, uint32_t warning, CheckWarningSeverity severity)
{
    const uint32_t index = check_diagnostic_warning_index(warning);
    const CheckInputDiagnosticPragmas* history = input->diagnostic_pragmas;
    if (history != NULL && index < BUSTER_ARRAY_LENGTH(check_warning_descriptions))
    {
        uint32_t low = 0;
        uint32_t high = history->count;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            const CheckInputDiagnosticPragma* record = history->records + middle;
            if (record->token < token || (record->token == token && record->source_begin < source))
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        uint32_t state = low != 0 ? history->records[low - 1].state_after : BUSTER_CHECK_NONE;
        bool matched = false;
        for (uint32_t visited = 0; !matched && state < history->count && visited < history->count; visited += 1)
        {
            const CheckInputDiagnosticPragma* record = history->records + state;
            const CheckWarningDescription* description = check_warning_descriptions + index;
            matched = check_diagnostic_option(record->option, description->option) || check_diagnostic_option(record->option, description->parent) ||
                      check_diagnostic_option(record->option, "-Weverything") || (index == 1 && check_diagnostic_option(record->option, "-Wpedantic"));
            if (matched)
            {
                severity = record->operation == CHECK_DIAGNOSTIC_IGNORED ? CHECK_WARNING_IGNORE :
                           record->operation == CHECK_DIAGNOSTIC_ERROR ? CHECK_WARNING_ERROR :
                           record->operation == CHECK_DIAGNOSTIC_FATAL ? CHECK_WARNING_FATAL : CHECK_WARNING_WARNING;
            }
            state = record->state_before;
        }
    }
    return severity;
}

static CheckWarningSeverity check_diagnostic_severity_at(const CheckInput* input, uint32_t token, uint32_t source, uint32_t warning, bool default_ignored)
{
    const CheckWarningSeverity severity = default_ignored ? CHECK_WARNING_IGNORE :
                                          warning == CHECK_WARNING_ARGUMENT_OUTSIDE_RANGE ? CHECK_WARNING_ERROR : CHECK_WARNING_WARNING;
    return check_diagnostic_severity_from(input, token, source, warning, severity);
}

static CheckWarningSeverity check_diagnostic_severity_default(const CheckInput* input, uint32_t token, uint32_t warning, bool default_ignored)
{
    return check_diagnostic_severity_at(input, token, BUSTER_CHECK_NONE, warning, default_ignored);
}

static CheckWarningSeverity check_diagnostic_severity(const CheckInput* input, uint32_t token, uint32_t warning)
{
    const CheckWarningSeverity severity = check_diagnostic_severity_default(input, token, warning, false);
    return severity;
}

static uint32_t check_diagnostic_attribute_warnings(const CheckInput* input, uint32_t token)
{
    const CheckInputAttributes* attributes = input->attributes != NULL && token < input->token_count ? input->attributes[token] : NULL;
    uint32_t warnings = 0;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        if (attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && attribute->ignored)
        {
            warnings |= CHECK_WARNING_IGNORED_ATTRIBUTES;
        }
    }
    return warnings;
}

static uint32_t check_diagnostic_x86_token(const CheckInput* input, uint32_t node)
{
    const uint32_t token = input->graph.tokens[node];
    const uint32_t call = input->intrinsic_ids != NULL && token < input->token_count ? input->intrinsic_ids[token] : 0;
    const uint32_t result = call != 0 && call <= input->intrinsic_call_count ? input->intrinsic_calls[call - 1].keyword : token;
    return result;
}

static bool check_diagnostic_first_error(const CheckInput* input, const CheckStream* stream, uint32_t* token)
{
    uint32_t first = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; stream != NULL && i < stream->x86_count; i += 1)
    {
        const CheckBuiltinX86Effect* effect = stream->x86 + i;
        const uint32_t at = check_diagnostic_x86_token(input, effect->node);
        for (uint32_t e = 0; e < effect->range_event_count; e += 1)
        {
            if (effect->range_events[e].disposition == CHECK_BUILTIN_X86_READY && at < first &&
                check_diagnostic_severity(input, at, CHECK_WARNING_ARGUMENT_OUTSIDE_RANGE) >= CHECK_WARNING_ERROR)
            {
                first = at;
            }
        }
    }
    const bool policies = input->diagnostic_pragmas != NULL && input->diagnostic_pragmas->count != 0;
    for (uint32_t i = 0; policies && i < input->packed_member_count; i += 1)
    {
        const CheckInputPackedMember* member = input->packed_members + i;
        if (!member->suppressed && member->token < first &&
            check_diagnostic_severity(input, member->token, CHECK_WARNING_ADDRESS_OF_PACKED_MEMBER) >= CHECK_WARNING_ERROR)
        {
            first = member->token;
        }
    }
    for (uint32_t at = 0; policies && input->attributes != NULL && at < input->token_count && at < first; at += 1)
    {
        const uint32_t warnings = check_diagnostic_attribute_warnings(input, at);
        if (warnings != 0 && check_diagnostic_severity(input, at, warnings) >= CHECK_WARNING_ERROR)
        {
            first = at;
        }
    }
    for (uint32_t at = 0; policies && input->constant_warnings != NULL && at < input->token_count; at += 1)
    {
        uint32_t warnings = input->constant_warnings[at];
        while (at < first && warnings != 0)
        {
            const uint32_t warning = warnings & (0u - warnings);
            const bool ignored = input->constant_warning_ignored != NULL && (input->constant_warning_ignored[at] & warning) != 0;
            if (check_diagnostic_severity_default(input, at, warning, ignored) >= CHECK_WARNING_ERROR)
            {
                first = at;
            }
            warnings &= warnings - 1;
        }
    }
    for (uint32_t node = 0; policies && stream != NULL && node < stream->count; node += 1)
    {
        const uint32_t at = input->graph.tokens[node];
        uint32_t warnings = stream->warnings[node];
        while (at < first && warnings != 0)
        {
            const uint32_t warning = warnings & (0u - warnings);
            const bool ignored = stream->warning_ignored != NULL && (stream->warning_ignored[node] & warning) != 0;
            if (check_diagnostic_severity_default(input, at, warning, ignored) >= CHECK_WARNING_ERROR)
            {
                first = at;
            }
            warnings &= warnings - 1;
        }
    }
    *token = first;
    const bool result = first != BUSTER_CHECK_NONE;
    return result;
}

// A retained pragma has its own source point even when no ordinary token lies before the following policy.
// Keep that point separate from its adjacent token, including directives at the end of a translation unit.
static uint32_t check_diagnostic_pragma_source(const CheckInputFpPragma* record)
{
    // The canonical header's '#' and 'pragma' are synthetic. The original state operand, or the setting name
    // when no state was supplied, retains _Pragma's expansion history and therefore its invocation location.
    const uint32_t offset = record->source_end - record->source_begin > 5 ? 4 : 3;
    const uint32_t source = record->source_begin + offset;
    return source;
}

static uint32_t check_diagnostic_input_report_source(const CheckInput* input, const Parser* parser, const CheckInputReport* report)
{
    uint32_t source = check_input_report_source(parser, report);
    if (input->naked_diagnostic != NULL && report->pragma == 0 && report->token == input->naked_diagnostic->position)
    {
        source = parser->tokens.sources[input->naked_diagnostic->token];
    }
    if (input->loop_diagnostic != 0 && report->pragma == input->loop_diagnostic->pragma && report->token == input->loop_diagnostic->position)
    {
        source = input->loop_diagnostic->source;
    }
    for (uint32_t i = 0; report->pragma != 0 && i < input->fp_pragma_count; i += 1)
    {
        const CheckInputFpPragma* record = input->fp_pragmas + i;
        if (record->pragma == report->pragma - 1)
        {
            source = check_diagnostic_pragma_source(record);
            break;
        }
    }
    return source;
}

static bool check_diagnostic_first_pragma_error_mode(const CheckInput* input, uint32_t before_token, uint32_t* token, uint32_t* source, bool quiet)
{
    uint32_t first = BUSTER_CHECK_NONE;
    uint32_t origin = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; i < input->fp_pragma_count; i += 1)
    {
        const CheckInputFpPragma* record = input->fp_pragmas + i;
        const bool invalid = (record->warnings & CHECK_FP_PRAGMA_INVALID_PLACEMENT) != 0;
        const bool promoted = !quiet && (record->warnings & CHECK_FP_PRAGMA_UNKNOWN_STATE) != 0 &&
                              check_diagnostic_severity_at(input, record->token, record->source_begin, CHECK_WARNING_UNKNOWN_PRAGMA, false) >= CHECK_WARNING_ERROR;
        if (record->token < before_token && record->token < first && (invalid || promoted))
        {
            first = record->token;
            origin = check_diagnostic_pragma_source(record);
        }
    }
    *token = first;
    *source = origin;
    return first != BUSTER_CHECK_NONE;
}

static bool check_diagnostic_first_pragma_error(const CheckInput* input, uint32_t before_token, uint32_t* token, uint32_t* source)
{
    const bool result = check_diagnostic_first_pragma_error_mode(input, before_token, token, source, false);
    return result;
}

// A malformed declaration may have consumed a directive before the token which exposes its syntax error. Its
// diagnostic location can point backwards (for a missing semicolon), but directive order follows the lookahead.
static uint32_t check_diagnostic_parse_end(const Parser* parser)
{
    uint32_t end = parser->tokens.count + 1;
    for (uint32_t i = 0; i < parser->diagnostic_count; i += 1)
    {
        const ParseDiagnostic* diagnostic = parser->diagnostics + i;
        if (diagnostic->error && diagnostic->token < end)
        {
            end = diagnostic->token + 1;
        }
    }
    return end;
}

// Unknown STDC states have no semantic effect and need no valid tree. Retain their original insertion points,
// independently of the completed expression prefix used for semantic diagnostics. Recognized states also reject
// insertion inside an unfinished expression. This syntax-only boundary proof never approves a setting's placement
// or changes floating policy; those remain owned by the ordinary floating-environment reader.
static bool check_diagnostic_original_pragmas(CheckInput* result, const Parser* parser, Preprocessor* pp,
                                              uint32_t before_token, Arena* arena, Arena* scratch)
{
    *result = (CheckInput){ 0 };
    const uint64_t retained = arena->length;
    const uint64_t temporary = scratch->length;
    const uint32_t count = parser->tokens.pragma_count;
    bool valid = arena != scratch && (count == 0 || (parser->tokens.pragma_sources != NULL && parser->tokens.pragma_positions != NULL));
    uint8_t* handled = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    CheckInputFpPragma* records = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, CheckInputFpPragma, count) : NULL;
    valid &= count == 0 || (handled != NULL && records != NULL);
    CheckInputReader reader = { .output = result, .parser = parser, .pp = pp, .arena = arena, .scratch = scratch };
    if (valid && count != 0)
    {
        memset(handled, 0, count);
        valid = check_input_diagnostic_pragmas(&reader, handled);
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const uint32_t source = parser->tokens.pragma_sources[i];
        const uint32_t token = parser->tokens.pragma_positions[i];
        uint32_t end = source;
        const bool complex = check_input_fp_name(&reader, source + 3, "CX_LIMITED_RANGE");
        const bool contract = check_input_fp_name(&reader, source + 3, "FP_CONTRACT");
        if (token < before_token && check_input_fp_name(&reader, source + 2, "STDC") &&
            (complex || contract || check_input_fp_name(&reader, source + 3, "FENV_ACCESS")) && parse_pragma_at(&pp->output, source, &end))
        {
            const bool recognized = check_input_fp_name(&reader, source + 4, "ON") || check_input_fp_name(&reader, source + 4, "OFF") ||
                                    check_input_fp_name(&reader, source + 4, "DEFAULT");
            const uint8_t previous = token != 0 && token <= parser->tokens.count ? parser->tokens.kinds[token - 1] : 0;
            const bool impossible = recognized && token != 0 && previous != PARSE_KIND_L_BRACE && previous != PARSE_KIND_R_BRACE &&
                                    previous != PARSE_KIND_SEMI;
            if (!recognized || impossible)
            {
                records[result->fp_pragma_count++] = (CheckInputFpPragma)
                {
                    .pragma = i,
                    .source_begin = source,
                    .source_end = end + 1,
                    .token = token,
                    .kind = complex ? CHECK_FP_PRAGMA_CX_LIMITED_RANGE : contract ? CHECK_FP_PRAGMA_FP_CONTRACT : CHECK_FP_PRAGMA_FENV_ACCESS,
                    .warnings = impossible ? CHECK_FP_PRAGMA_INVALID_PLACEMENT : CHECK_FP_PRAGMA_UNKNOWN_STATE,
                };
            }
        }
    }
    if (valid)
    {
        result->fp_pragmas = records;
        result->token_count = parser->tokens.count;
    }
    else
    {
        arena_pop_to(arena, retained);
        *result = (CheckInput){ .resource = true };
    }
    arena_pop_to(scratch, temporary);
    return valid;
}
