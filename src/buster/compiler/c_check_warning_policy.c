// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Sparse semantic warning policy. No policy ID is converted to a flag in a dense token/node column. Command-line
// mappings and source pragmas are independent: a source mapping replaces the command-line baseline at its location.
// The group relations and extension classes are checked against Clang 23.1.4, with original differential probes.

static bool check_warning_extension(uint32_t id)
{
    const bool extension = id == CHECK_WARNING_ID_GNU_CONSTANT || id == CHECK_WARNING_ID_ORDERED_POINTER_INTEGER ||
                           id == CHECK_WARNING_ID_MISSING_DECLARATIONS || id == CHECK_WARNING_ID_PLAIN_COMPLEX ||
                           id == CHECK_WARNING_ID_VARIABLE_SIZED_MEMBER || id == CHECK_WARNING_ID_MANY_SCALAR_BRACES ||
                           id == CHECK_WARNING_ID_UNDEFINED_INTERNAL || id == CHECK_WARNING_ID_C23_PARAMETER;
    return extension;
}

static bool check_warning_option(uint32_t id, String8 option)
{
    const CheckWarningDescription* description = id < BUSTER_ARRAY_LENGTH(check_warning_descriptions) ? check_warning_descriptions + id : NULL;
    bool matches = description != NULL && (check_diagnostic_option(option, description->option) || check_diagnostic_option(option, description->parent));
    matches |= check_diagnostic_option(option, "-Weverything");
    if (id == CHECK_WARNING_ID_GNU_CONSTANT)
    {
        matches |= check_diagnostic_option(option, "-Wpedantic");
    }
    else if (id == CHECK_WARNING_ID_NON_LITERAL_NULL || id == CHECK_WARNING_ID_LITERAL_RANGE)
    {
        matches |= check_diagnostic_option(option, "-Wnon-gcc");
        matches |= id == CHECK_WARNING_ID_NON_LITERAL_NULL && check_diagnostic_option(option, "-Wconversion");
    }
    else if (id == CHECK_WARNING_ID_INITIALIZER_OVERRIDES)
    {
        matches |= check_diagnostic_option(option, "-Wextra") || check_diagnostic_option(option, "-W") ||
                   check_diagnostic_option(option, "-Wc99-designator") || check_diagnostic_option(option, "-Wc99-extensions") ||
                   check_diagnostic_option(option, "-Woverride-init") || check_diagnostic_option(option, "-WCL4");
    }
    else if (id == CHECK_WARNING_ID_IMPLICIT_FUNCTION_DECLARATION)
    {
        matches |= check_diagnostic_option(option, "-Wall");
    }
    return matches;
}

typedef struct CheckWarningCommandLine CheckWarningCommandLine;
struct CheckWarningCommandLine
{
    CheckWarningSeverity severity;
    bool quiet;
    bool system_headers;
};

static CheckWarningCommandLine check_warning_command_line(const PPOptions* options, uint32_t id, bool default_ignored)
{
    CheckWarningSeverity mapped = default_ignored ? CHECK_WARNING_IGNORE : CHECK_WARNING_WARNING;
    bool explicit_mapping = false;
    bool no_error = false;
    bool errors = false;
    bool pedantic_errors = false;
    bool everything = false;
    CheckWarningCommandLine result = { .quiet = options->suppress_warnings };
    for (uint32_t i = 0; i < options->semantic_warning_option_count; i += 1)
    {
        const char* argument = options->semantic_warning_options[i];
        const uint64_t length = strlen(argument);
        const bool error = strncmp(argument, "-Werror=", 8) == 0;
        const bool demote = strncmp(argument, "-Wno-error=", 11) == 0;
        const bool disable = !demote && strncmp(argument, "-Wno-", 5) == 0;
        const uint32_t prefix = error ? 8 : demote ? 11 : disable ? 5 : 2;
        char name[128];
        const bool group = length >= prefix && length - prefix <= sizeof(name) - 3 && argument[0] == '-' && argument[1] == 'W';
        if (group)
        {
            name[0] = '-';
            name[1] = 'W';
            memcpy(name + 2, argument + prefix, length - prefix);
            name[length - prefix + 2] = 0;
        }
        const String8 option = { .pointer = name, .length = group ? length - prefix + 2 : 0 };
        const bool all = group && check_diagnostic_option(option, "-Weverything");
        if (strcmp(argument, "-w") == 0)
        {
            result.quiet = true;
        }
        else if (strcmp(argument, "-Wsystem-headers") == 0 || strcmp(argument, "-Wno-system-headers") == 0)
        {
            result.system_headers = strcmp(argument, "-Wsystem-headers") == 0;
        }
        else if (strcmp(argument, "-Werror") == 0 || strcmp(argument, "-Wno-error") == 0)
        {
            errors = strcmp(argument, "-Werror") == 0;
        }
        else if (strcmp(argument, "-pedantic-errors") == 0)
        {
            pedantic_errors = true;
            if (id == CHECK_WARNING_ID_GNU_CONSTANT && mapped == CHECK_WARNING_IGNORE && !explicit_mapping)
            {
                mapped = CHECK_WARNING_WARNING;
            }
        }
        else if (strcmp(argument, "-pedantic") == 0)
        {
            if (id == CHECK_WARNING_ID_GNU_CONSTANT && mapped == CHECK_WARNING_IGNORE && !explicit_mapping)
            {
                mapped = CHECK_WARNING_WARNING;
            }
        }
        else if (all && !error && !demote)
        {
            everything |= !disable;
            if (disable)
            {
                mapped = CHECK_WARNING_IGNORE;
                explicit_mapping = true;
            }
        }
        else if (group && check_warning_option(id, option))
        {
            if (demote)
            {
                mapped = mapped == CHECK_WARNING_ERROR ? CHECK_WARNING_WARNING : mapped;
                no_error = true;
            }
            else
            {
                mapped = error ? CHECK_WARNING_ERROR : disable ? CHECK_WARNING_IGNORE :
                         mapped == CHECK_WARNING_IGNORE ? CHECK_WARNING_WARNING : mapped;
                explicit_mapping = true;
                no_error = error ? false : no_error;
            }
        }
    }
    if (!explicit_mapping && everything)
    {
        mapped = mapped == CHECK_WARNING_IGNORE ? CHECK_WARNING_WARNING : mapped;
    }
    if (mapped == CHECK_WARNING_WARNING && !no_error && (errors || (pedantic_errors && check_warning_extension(id))))
    {
        mapped = CHECK_WARNING_ERROR;
    }
    result.severity = result.quiet ? CHECK_WARNING_IGNORE : mapped;
    return result;
}
