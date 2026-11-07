// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Command-line warning names and the policy for diagnosing unknown ones (#1467). The driver vocabulary includes
// spellings outside __has_warning's group catalog. The ordered option slice is borrowed from PPOptions, just as
// the checker's warning policy borrows it. Resolve the final policy before publishing through the ordinary gate,
// so a later suppression applies to earlier misspellings, and deferred-history replay repeats the same diagnostics.

typedef enum PPWarningOptionKind
{
    PP_WARNING_OPTION_DRIVER,
    PP_WARNING_OPTION_GROUP,
    PP_WARNING_OPTION_ERROR,
    PP_WARNING_OPTION_FATAL,
    PP_WARNING_OPTION_INVALID,
} PPWarningOptionKind;

typedef struct PPWarningOption PPWarningOption;
struct PPWarningOption
{
    const char* name;
    const char* original;
    const char* prefix;
    PPWarningOptionKind kind;
    bool negative;
};

static bool pp_warning_transport(const char* argument)
{
    return strncmp(argument, "-Wa,", 4) == 0 || strncmp(argument, "-Wp,", 4) == 0 || strncmp(argument, "-Wl,", 4) == 0;
}

// Only these positive spellings are driver options with a value. In particular, do not remove an arbitrary =VALUE
// from a warning name: -Wformat=2 is a group, while -Wformat=3 and -Wno-frame-larger-than=64 are unknown names.
static bool pp_warning_driver_option(const char* argument)
{
    static const char* const prefixes[] =
    {
        "-Wlarger-than=",
        "-Wlarger-than-",
        "-Wframe-larger-than=",
        "-Wlarge-by-value-copy=",
        "-Wundef-prefix=",
        "-Wsystem-headers-in-module=",
    };
    bool known = pp_warning_transport(argument);
    for (uint32_t i = 0; !known && i < BUSTER_ARRAY_LENGTH(prefixes); i += 1)
    {
        const char* prefix = prefixes[i];
        known = strncmp(argument, prefix, strlen(prefix)) == 0;
    }
    return known;
}

// The name borrows a NUL-terminated suffix of argv, or a static alias. No fixed buffer limits a diagnostic's name.
// Clang's legacy nonportable-cfstrings options are joined options: their suffix, even empty, is the warning name.
static PPWarningOption pp_warning_option(const char* argument)
{
    PPWarningOption result =
    {
        .name = "",
        .original = "",
        .prefix = "-W",
        .kind = PP_WARNING_OPTION_DRIVER,
    };
    if (strncmp(argument, "-W", 2) == 0 && !pp_warning_driver_option(argument))
    {
        result.kind = PP_WARNING_OPTION_GROUP;
        result.name = argument + 2;
        if (strncmp(argument, "-Wnonportable-cfstrings", sizeof("-Wnonportable-cfstrings") - 1) == 0)
        {
            result.name = argument + sizeof("-Wnonportable-cfstrings") - 1;
        }
        else if (strncmp(argument, "-Wno-nonportable-cfstrings", sizeof("-Wno-nonportable-cfstrings") - 1) == 0)
        {
            result.name = argument + sizeof("-Wno-nonportable-cfstrings") - 1;
        }
        result.original = result.name;
        if (strcmp(result.name, "format=0") == 0)
        {
            result.name = "no-format";
        }
        result.negative = strncmp(result.name, "no-", 3) == 0;
        result.name += result.negative ? 3 : 0;
        result.prefix = result.negative ? "-Wno-" : "-W";
        if (strncmp(result.name, "error", 5) == 0)
        {
            const bool bare = result.name[5] == 0;
            const bool valid = bare || result.name[5] == '=' || strcmp(result.name + 5, "-implicit-function-declaration") == 0;
            result.kind = valid ? PP_WARNING_OPTION_ERROR : PP_WARNING_OPTION_INVALID;
            result.prefix = valid ? "-Werror=" : "-Werror";
            result.name += valid ? (bare ? 5 : 6) : 0;
        }
        else if (strncmp(result.name, "fatal-errors", 12) == 0)
        {
            const bool bare = result.name[12] == 0;
            const bool valid = bare || ((result.name[12] == '=' || result.name[12] == '-') && result.name[13] != 0);
            result.kind = valid ? PP_WARNING_OPTION_FATAL : PP_WARNING_OPTION_INVALID;
            result.prefix = valid ? "-Wfatal-errors=" : "-Wfatal-errors";
            result.name += valid ? (bare ? 12 : 13) : 0;
        }
    }
    return result;
}

// The existing catalog is sorted by the full -W spelling, so the suffixes have the same order. Empty is Clang's
// historical -W/-Wno- alias for -Wextra; everything and system-headers are controls, not catalog groups.
static bool pp_warning_group_known(const char* name)
{
    bool known = name[0] == 0;
    uint32_t low = 0;
    uint32_t high = BUSTER_ARRAY_LENGTH(pp_features_warning);
    while (!known && low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const int compared = strcmp(name, pp_features_warning[middle].name + 2);
        known = compared == 0;
        high = compared < 0 ? middle : high;
        low = compared > 0 ? middle + 1 : low;
    }
    return known;
}

static bool pp_warning_option_known(PPWarningOption option)
{
    const bool control = option.kind == PP_WARNING_OPTION_GROUP &&
                         (strcmp(option.name, "everything") == 0 || strcmp(option.name, "system-headers") == 0);
    return option.kind == PP_WARNING_OPTION_DRIVER || control ||
           (option.kind != PP_WARNING_OPTION_INVALID && pp_warning_group_known(option.name));
}

typedef enum PPWarningOptionSeverity
{
    PP_WARNING_OPTION_IGNORED,
    PP_WARNING_OPTION_WARNING,
    PP_WARNING_OPTION_AS_ERROR,
    PP_WARNING_OPTION_AS_FATAL,
} PPWarningOptionSeverity;

// Unknown-option diagnostics are default warnings. The global promotion switches and explicit group mappings are
// independent: -Wno-error=NAME survives a later global -Werror, but an explicit -Werror=NAME still sets an error.
// -w and dependency-only preprocessing suppress this default-warning diagnostic even after explicit promotion.
static PPWarningOptionSeverity pp_warning_option_severity(const PPOptions* options)
{
    PPWarningOptionSeverity mapped = PP_WARNING_OPTION_WARNING;
    bool errors = false;
    bool fatal = false;
    bool no_error = false;
    bool no_fatal = false;
    bool quiet = options->suppress_warnings;
    const uint32_t count = options->semantic_warning_option_count;
    const char* const* arguments = options->semantic_warning_options;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const char* argument = arguments[i];
        const PPWarningOption option = pp_warning_option(argument);
        const bool matches = strcmp(option.name, "unknown-warning-option") == 0;
        quiet |= strcmp(argument, "-w") == 0;
        if (option.kind == PP_WARNING_OPTION_ERROR && option.name[0] == 0)
        {
            errors = !option.negative;
        }
        else if (option.kind == PP_WARNING_OPTION_FATAL && option.name[0] == 0)
        {
            fatal = !option.negative;
        }
        else if (option.kind == PP_WARNING_OPTION_GROUP && strcmp(option.name, "everything") == 0 && option.negative)
        {
            mapped = PP_WARNING_OPTION_IGNORED;
            no_fatal = false;
        }
        else if (matches && option.kind == PP_WARNING_OPTION_GROUP)
        {
            mapped = option.negative ? PP_WARNING_OPTION_IGNORED : mapped == PP_WARNING_OPTION_IGNORED ? PP_WARNING_OPTION_WARNING : mapped;
            no_fatal = false;
        }
        else if (matches && option.kind == PP_WARNING_OPTION_ERROR)
        {
            if (option.negative)
            {
                mapped = mapped >= PP_WARNING_OPTION_AS_ERROR ? PP_WARNING_OPTION_WARNING : mapped;
                no_error = true;
            }
            else
            {
                mapped = PP_WARNING_OPTION_AS_ERROR;
                no_fatal = false;
            }
        }
        else if (matches && option.kind == PP_WARNING_OPTION_FATAL)
        {
            if (option.negative)
            {
                mapped = mapped == PP_WARNING_OPTION_AS_FATAL ? PP_WARNING_OPTION_AS_ERROR : mapped;
                no_fatal = true;
            }
            else
            {
                mapped = PP_WARNING_OPTION_AS_FATAL;
                no_fatal = false;
            }
        }
    }
    mapped = mapped == PP_WARNING_OPTION_WARNING && errors && !no_error ? PP_WARNING_OPTION_AS_ERROR : mapped;
    mapped = mapped == PP_WARNING_OPTION_AS_ERROR && fatal && !no_fatal ? PP_WARNING_OPTION_AS_FATAL : mapped;
    return quiet ? PP_WARNING_OPTION_IGNORED : mapped;
}

// This runs before the prelude, in both recorded and deferred-history paths. Reuse the ordinary publication gate
// and counters; no source pragma can retroactively alter a command-line diagnostic. The optional typo hint is not
// emitted: __has_warning's catalog also includes empty groups, which Clang deliberately excludes from suggestions.
static void pp_warning_options_diagnose(Preprocessor* pp)
{
    const PPWarningOptionSeverity mapped = pp_warning_option_severity(&pp->options);
    const uint32_t count = pp->options.semantic_warning_option_count;
    const char* const* arguments = pp->options.semantic_warning_options;
    const PPSeverity severity = mapped == PP_WARNING_OPTION_AS_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                mapped == PP_WARNING_OPTION_AS_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING_ALWAYS;
    for (uint32_t i = 0; mapped != PP_WARNING_OPTION_IGNORED && !pp->fatal && i < count; i += 1)
    {
        const char* argument = arguments[i];
        const PPWarningOption option = pp_warning_option(argument);
        if (!pp_warning_option_known(option))
        {
            if (option.kind == PP_WARNING_OPTION_INVALID)
            {
                pp_diagnose(pp, severity, 0, "unknown %s warning specifier: '-W%s'", option.prefix, option.original);
            }
            else
            {
                pp_diagnose(pp, severity, 0, "unknown warning option '%s%s'", option.prefix, option.name);
            }
        }
    }
}
