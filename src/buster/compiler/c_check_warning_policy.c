// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Sparse semantic warning policy. No policy ID is converted to a flag in a dense token/node column. Command-line
// mappings and source pragmas are independent: a source mapping replaces the command-line baseline at its location.
// The group relations and extension classes are checked against Clang 23.1.4, with original differential probes.

// The warnings off by default that -pedantic (or -pedantic-errors) gives: GNU constant folding's, an unevaluated
// string literal's escapes' (#1447), GCC's cast to a union's (#2301), a GNU array range designator's (#2202) and a C23 attribute list's before C23 (#2409).
static bool check_warning_pedantic(uint32_t id)
{
    return id == CHECK_WARNING_ID_GNU_CONSTANT || id == CHECK_WARNING_ID_NONSTANDARD_ESCAPE || id == CHECK_WARNING_ID_DELIMITED_ESCAPE ||
           id == CHECK_WARNING_ID_GNU_UNION_CAST || id == CHECK_WARNING_ID_NONSCALAR_SAME_CAST || id == CHECK_WARNING_ID_GNU_ARRAY_RANGE || id == CHECK_WARNING_ID_C23_ATTRIBUTES || id == CHECK_WARNING_ID_GNU_EMPTY_STRUCT;
}

static bool check_warning_extension(uint32_t id)
{
    const bool extension = id == CHECK_WARNING_ID_GNU_CONSTANT || id == CHECK_WARNING_ID_ORDERED_POINTER_INTEGER ||
                           id == CHECK_WARNING_ID_MISSING_DECLARATIONS || id == CHECK_WARNING_ID_PLAIN_COMPLEX ||
                           id == CHECK_WARNING_ID_VARIABLE_SIZED_MEMBER || id == CHECK_WARNING_ID_MANY_SCALAR_BRACES ||
                           id == CHECK_WARNING_ID_UNDEFINED_INTERNAL || id == CHECK_WARNING_ID_C23_PARAMETER ||
                           id == CHECK_WARNING_ID_UNKNOWN_ESCAPE_SEQUENCE || id == CHECK_WARNING_ID_GNU_DESIGNATOR || check_warning_pedantic(id);
    return extension;
}

static bool check_warning_option(uint32_t id, String8 option)
{
    const CheckWarningDescription* description = id < BUSTER_ARRAY_LENGTH(check_warning_descriptions) ? check_warning_descriptions + id : NULL;
    bool matches = description != NULL && (check_diagnostic_option(option, description->option) || check_diagnostic_option(option, description->parent));
    matches |= check_diagnostic_option(option, "-Weverything");
    if (id == CHECK_WARNING_ID_GNU_CONSTANT || id == CHECK_WARNING_ID_GNU_UNION_CAST)
    {
        matches |= check_diagnostic_option(option, "-Wpedantic");
    }
    else if (id == CHECK_WARNING_ID_NON_LITERAL_NULL || id == CHECK_WARNING_ID_LITERAL_RANGE)
    {
        matches |= check_diagnostic_option(option, "-Wnon-gcc");
        matches |= id == CHECK_WARNING_ID_NON_LITERAL_NULL && check_diagnostic_option(option, "-Wconversion");
    }
    else if (id == CHECK_WARNING_ID_UNREACHABLE_GENERIC_ASSOC)
    {
        // -Wunreachable-code's, which -Wunreachable-code-aggressive holds (#1447).
        matches |= check_diagnostic_option(option, "-Wunreachable-code-aggressive");
    }
    else if (id == CHECK_WARNING_ID_INITIALIZER_OVERRIDES)
    {
        matches |= check_diagnostic_option(option, "-Wextra") || check_diagnostic_option(option, "-W") ||
                   check_diagnostic_option(option, "-Wc99-designator") || check_diagnostic_option(option, "-Wc99-extensions") ||
                   check_diagnostic_option(option, "-Woverride-init") || check_diagnostic_option(option, "-WCL4");
    }
    else if (id == CHECK_WARNING_ID_IMPLICIT_FUNCTION_DECLARATION || id == CHECK_WARNING_ID_UNRECOGNIZED_PRAGMA || id == CHECK_WARNING_ID_UNKNOWN_PRAGMA)
    {
        matches |= check_diagnostic_option(option, "-Wall");
    }
    // -Wdefault-const-init holds -Wdefault-const-init-unsafe's two and -Wdefault-const-init-var, and -Wc++-compat holds it
    // (#2168).
    const bool default_const = id == CHECK_WARNING_ID_DEFAULT_CONST_INIT_VAR_UNSAFE || id == CHECK_WARNING_ID_DEFAULT_CONST_INIT_FIELD_UNSAFE ||
                               id == CHECK_WARNING_ID_DEFAULT_CONST_INIT_VAR;
    matches |= default_const && (check_diagnostic_option(option, "-Wdefault-const-init") || check_diagnostic_option(option, "-Wc++-compat"));
    const bool wall = id == CHECK_WARNING_ID_UNUSED_VARIABLE || id == CHECK_WARNING_ID_UNUSED_CONST_VARIABLE ||
                      id == CHECK_WARNING_ID_UNUSED_FUNCTION ||
                      id == CHECK_WARNING_ID_UNINITIALIZED || id == CHECK_WARNING_ID_SWITCH || id == CHECK_WARNING_ID_DANGLING_ELSE ||
                      id == CHECK_WARNING_ID_SELF_ASSIGN || id == CHECK_WARNING_ID_EMPTY_BODY || id == CHECK_WARNING_ID_FORMAT_SECURITY ||
                      id == CHECK_WARNING_ID_SHIFT_OP_PARENTHESES;
    const bool wextra = id == CHECK_WARNING_ID_UNUSED_PARAMETER || id == CHECK_WARNING_ID_SIGN_COMPARE ||
                        id == CHECK_WARNING_ID_UNUSED_BUT_SET_PARAMETER;
    matches |= wall && check_diagnostic_option(option, "-Wall");
    // -Wunused-value's (with -Wunused-comparison, -Wunused-result and -Wunevaluated-expression under it) is in -Wunused, which
    // -Wmost and so -Wall hold.
    const bool unused = id == CHECK_WARNING_ID_UNUSED_VALUE || id == CHECK_WARNING_ID_UNUSED_COMPARISON || id == CHECK_WARNING_ID_UNUSED_RESULT ||
                        id == CHECK_WARNING_ID_UNEVALUATED_EXPRESSION || id == CHECK_WARNING_ID_UNUSED_BUT_SET_VARIABLE ||
                        id == CHECK_WARNING_ID_UNUSED_LABEL;
    matches |= unused && (check_diagnostic_option(option, "-Wunused") || check_diagnostic_option(option, "-Wmost") || check_diagnostic_option(option, "-Wall"));
    matches |= wextra && (check_diagnostic_option(option, "-Wextra") || check_diagnostic_option(option, "-W"));
    // -Wint-in-bool-context, -Wtautological-compare (with -Wtautological-constant-compare under it), diagnose_if's
    // -Wuser-defined-warnings (#1447) and -Wmultichar (#1970) are -Wmost's.
    const bool most = id == CHECK_WARNING_ID_INT_IN_BOOL_CONTEXT || id == CHECK_WARNING_ID_TAUTOLOGICAL_CONSTANT_COMPARE ||
                      id == CHECK_WARNING_ID_FOR_LOOP_ANALYSIS || id == CHECK_WARNING_ID_USER_DEFINED_WARNINGS || id == CHECK_WARNING_ID_MULTICHAR;
    matches |= most && (check_diagnostic_option(option, "-Wmost") || check_diagnostic_option(option, "-Wall"));
    // -Wconstant-conversion holds -Wbitfield-constant-conversion, which holds -Wsingle-bit-bitfield-constant-conversion;
    // -Wconversion holds all three, and -Wnon-gcc -Wconversion (#1447).
    const bool constant_conversion = id == CHECK_WARNING_ID_CONSTANT_CONVERSION || id == CHECK_WARNING_ID_BITFIELD_CONSTANT_CONVERSION ||
                                     id == CHECK_WARNING_ID_SINGLE_BIT_BITFIELD_CONSTANT_CONVERSION;
    matches |= id == CHECK_WARNING_ID_SINGLE_BIT_BITFIELD_CONSTANT_CONVERSION && check_diagnostic_option(option, "-Wconstant-conversion");
    matches |= constant_conversion && (check_diagnostic_option(option, "-Wconversion") || check_diagnostic_option(option, "-Wnon-gcc"));
    // -Wenum-conversion's three are -Wconversion's, and so -Wnon-gcc's (#1447).
    const bool enum_conversion = id == CHECK_WARNING_ID_ENUM_ENUM_CONVERSION || id == CHECK_WARNING_ID_ENUM_COMPARE_CONDITIONAL ||
                                 id == CHECK_WARNING_ID_ENUM_FLOAT_CONVERSION || id == CHECK_WARNING_ID_IMPLICIT_ENUM_ENUM_CAST;
    matches |= enum_conversion && (check_diagnostic_option(option, "-Wconversion") || check_diagnostic_option(option, "-Wnon-gcc"));
    // -Wpointer-to-int-cast holds -Wpointer-to-enum-cast and -Wvoid-pointer-to-int-cast, which both hold
    // -Wvoid-pointer-to-enum-cast (#1943).
    matches |= id == CHECK_WARNING_ID_VOID_POINTER_TO_ENUM_CAST &&
               (check_diagnostic_option(option, "-Wpointer-to-int-cast") || check_diagnostic_option(option, "-Wvoid-pointer-to-int-cast"));
    matches |= id == CHECK_WARNING_ID_POINTER_TO_ENUM_CAST && check_diagnostic_option(option, "-Wpointer-to-int-cast");
    return matches;
}

typedef struct CheckWarningCommandLine CheckWarningCommandLine;
struct CheckWarningCommandLine
{
    CheckWarningSeverity severity;
    // The severity before -w, which leaves a default error that no option demoted (clang's -Wodr, #1447).
    CheckWarningSeverity mapped;
    bool quiet;
    bool system_headers;
};

// A diagnostic's command-line mapping starts from its intrinsic default: ignored, error (clang's DefaultError, as
// the x86 range events are) or warning. `-Wno-error=NAME` demotes a default error and keeps it a warning through a
// later `-Werror`; `-Werror=NAME` after it wins; `-WNAME` re-enables a disabled diagnostic as a warning (#1080).
static CheckWarningCommandLine check_warning_command_line(const PPOptions* options, uint32_t id, bool default_ignored, bool default_error)
{
    CheckWarningSeverity mapped = default_ignored ? CHECK_WARNING_IGNORE :
                                  default_error ? CHECK_WARNING_ERROR : CHECK_WARNING_WARNING;
    bool explicit_mapping = false;
    bool no_error = false;
    bool errors = false;
    bool pedantic_errors = false;
    bool everything = false;
    CheckWarningCommandLine result = { .quiet = options->suppress_warnings, .system_headers = options->system_header_warnings };
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
            if (check_warning_pedantic(id) && mapped == CHECK_WARNING_IGNORE && !explicit_mapping)
            {
                mapped = CHECK_WARNING_WARNING;
            }
        }
        else if (strcmp(argument, "-pedantic") == 0)
        {
            if (check_warning_pedantic(id) && mapped == CHECK_WARNING_IGNORE && !explicit_mapping)
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
    result.mapped = mapped;
    return result;
}

// A grouped diagnose_if's command-line mapping (#1447): clang's custom diagnostic of warning group `group` (its index in
// pp_features_warning), a warning by default or, for an "error", an error. Each option naming the group or a group that
// holds it (pp_warning_group_holds) maps it in turn: -W a warning, -Wno- ignored, -Werror= an error, -Wfatal-errors= fatal,
// -Wno-error= an error a warning. clang keeps none of these as the user's: -Weverything enables an ignored one wherever
// it is, and -Wno-everything leaves it. Its severity before a source pragma; -Werror (*errors), which makes a warning an
// error through any -Wno-error=, and -w (quiet), which ignores it unless it is an error by default and still one, apply
// after one.
static __attribute__((unused)) CheckWarningCommandLine check_warning_group_command_line(const PPOptions* options, uint32_t group, bool default_error, bool* errors_out)
{
    CheckWarningSeverity mapped = default_error ? CHECK_WARNING_ERROR : CHECK_WARNING_WARNING;
    bool errors = false;
    bool everything = false;
    CheckWarningCommandLine result = { .quiet = options->suppress_warnings, .system_headers = options->system_header_warnings };
    for (uint32_t i = 0; i < options->semantic_warning_option_count; i += 1)
    {
        const char* argument = options->semantic_warning_options[i];
        const bool error = strncmp(argument, "-Werror=", 8) == 0;
        const bool demote = strncmp(argument, "-Wno-error=", 11) == 0;
        const bool fatal = strncmp(argument, "-Wfatal-errors=", 15) == 0;
        const bool disable = !demote && strncmp(argument, "-Wno-", 5) == 0;
        const uint32_t prefix = error ? 8 : demote ? 11 : fatal ? 15 : disable ? 5 : 2;
        const bool named = strncmp(argument, "-W", 2) == 0;
        const uint32_t holder = named ? pp_warning_group_index(argument + prefix) : UINT32_MAX;
        const bool matches = holder != UINT32_MAX && pp_warning_group_holds(holder, group);
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
        else if (strcmp(argument, "-Weverything") == 0)
        {
            everything = true;
        }
        else if (matches)
        {
            mapped = error ? CHECK_WARNING_ERROR : fatal ? CHECK_WARNING_FATAL : disable ? CHECK_WARNING_IGNORE :
                     demote ? (mapped == CHECK_WARNING_ERROR ? CHECK_WARNING_WARNING : mapped) : CHECK_WARNING_WARNING;
        }
    }
    result.severity = mapped == CHECK_WARNING_IGNORE && everything ? CHECK_WARNING_WARNING : mapped;
    *errors_out = errors;
    return result;
}
