// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Apply source-ordered diagnostic state to warnings already established by semantic checking. A pragma cannot
// suppress an ordinary constraint report. The small warning-group relations follow Clang 23.1.1; no diagnostic
// implementation is imported. Include after c_check_input.c, whose immutable history owns the decoded strings.

#define CHECK_WARNING_EXCESS_INITIALIZERS 0x100u

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

static CheckWarningSeverity check_diagnostic_severity_default(const CheckInput* input, uint32_t token, uint32_t warning, bool default_ignored)
{
    const uint32_t index = check_diagnostic_warning_index(warning);
    CheckWarningSeverity severity = default_ignored ? CHECK_WARNING_IGNORE : CHECK_WARNING_WARNING;
    const CheckInputDiagnosticPragmas* history = input->diagnostic_pragmas;
    if (history != NULL && index < BUSTER_ARRAY_LENGTH(check_warning_descriptions))
    {
        uint32_t low = 0;
        uint32_t high = history->count;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            if (history->records[middle].token <= token)
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

static CheckWarningSeverity check_diagnostic_severity(const CheckInput* input, uint32_t token, uint32_t warning)
{
    const CheckWarningSeverity severity = check_diagnostic_severity_default(input, token, warning, false);
    return severity;
}

static bool check_diagnostic_first_error(const CheckInput* input, const CheckStream* stream, uint32_t* token)
{
    uint32_t first = BUSTER_CHECK_NONE;
    const bool policies = input->diagnostic_pragmas != NULL && input->diagnostic_pragmas->count != 0;
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
