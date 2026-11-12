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
#define CHECK_WARNING_RETURN_TYPE 0x40000u
#define CHECK_WARNING_IMPLICIT_FUNCTION_DECLARATION 0x80000u
#define CHECK_WARNING_VISIBILITY_PRAGMA 0x100000u

#include "c_check_packed_diagnostic.c"
#include "c_check_warning_ids.c"
#include "c_check_warning_graph.c"
#include "c_check_warning_undefined.c"

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
    { "-Wgnu-folding-constant", "-Wgnu", "expression is not an integer constant expression; folding it to a constant is a GNU extension" },
    { "-Wenum-too-large", NULL, "enumeration values exceed the available integer range" },
    { "-Winteger-overflow", NULL, "integer constant expression overflows its type" },
    { "-Wshift-count-negative", NULL, "shift count is negative" },
    { "-Wshift-count-overflow", NULL, "shift count >= width of type" },
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
    { "-Wimplicit-function-declaration", "-Wimplicit", "call to undeclared function" },
    { "-Wignored-pragmas", "-Wpragmas", "ignored pragma visibility directive" },
    { "-Wdeprecated-non-prototype", NULL, "a function definition without a prototype is deprecated in all versions of C and is not supported in C23" },
    { "-Wdeprecated-non-prototype", NULL, "passing arguments to a function without a prototype is deprecated in all versions of C and is not supported in C23" },
    { NULL, NULL, "too few arguments in call" },
    { NULL, NULL, "too many arguments in call" },
    { "-Wnon-literal-null-conversion", NULL, "expression which evaluates to zero treated as a null pointer constant" },
    { "-Wliteral-range", NULL, "floating literal is outside the range of its type" },
    { NULL, NULL, "ordered comparison between pointer and integer" },
    { "-Wmissing-declarations", NULL, "declaration does not declare anything" },
    { NULL, NULL, "plain '_Complex' requires a type specifier; assuming '_Complex double'" },
    { "-Wgnu-variable-sized-type-not-at-end", "-Wgnu", "field with variable sized type is not at the end of a struct or class" },
    { "-Wstatic-local-in-inline", NULL, "non-constant static local variable in inline function may be different in different files" },
    { "-Wmany-braces-around-scalar-init", NULL, "too many braces around scalar initializer" },
    { "-Winitializer-overrides", NULL, "initializer overrides prior initialization of this subobject" },
    { "-Wundefined-internal", NULL, "function has internal linkage but is not defined" },
    { "-Wc23-extensions", "-Wc2x-extensions", "omitting the parameter name in a function definition is a C23 extension" },
    { "-Wunknown-pragmas", "-Wpragmas", "unknown pragma ignored" },
    { "-Wignored-pragmas", "-Wpragmas", "pragma ignored" },
    { "-Wsource-uses-openmp", "-Wopenmp", "unexpected OpenMP directive; OpenMP is not enabled" },
    { "-Wincompatible-library-redeclaration", NULL, "incompatible redeclaration of library function" },
    { "-Wbuiltin-requires-header", NULL, "declaration of built-in function requires inclusion of its header" },
    { "-Wunknown-attributes", "-Wattributes", "unknown attribute ignored" },
    { "-Wdivision-by-zero", NULL, "division by zero is undefined" },
    { "-Wconstant-conversion", NULL, "implicit conversion changes value" },
    { "-Warray-bounds", NULL, "array index is past the end of the array" },
    { "-Wsign-compare", NULL, "comparison of integers of different signs" },
    { "-Wunused-comparison", "-Wunused-value", "comparison result unused" },
    { "-Wunused-value", NULL, "expression result unused" },
    { "-Wlogical-not-parentheses", NULL, "logical not is only applied to the left hand side of this comparison" },
    { "-Wparentheses", NULL, "using the result of an assignment as a condition without parentheses" },
    { "-Wempty-body", NULL, "empty body" },
    { "-Wself-assign", NULL, "explicitly assigning value of variable to itself" },
    { "-Wreturn-stack-address", NULL, "address of stack memory associated with local variable returned" },
    { "-Wformat", NULL, "format string does not match the arguments" },
    { "-Wformat-security", "-Wformat", "format string is not a string literal (potentially insecure)" },
    { "-Wunused-variable", NULL, "unused variable" },
    { "-Wunused-function", NULL, "unused function" },
    { "-Wunused-parameter", NULL, "unused parameter" },
    { "-Wuninitialized", NULL, "variable is used uninitialized" },
    { "-Wswitch", NULL, "enumeration value not handled in switch" },
    { "-Wdangling-else", NULL, "add explicit braces to avoid dangling else" },
    { "-Wunused-but-set-parameter", NULL, "parameter set but not used" },
    { "-Wunused-const-variable", "-Wunused-variable", "unused variable" },
    { "-Wformat-extra-args", "-Wformat", "data argument not used by format string" },
    { "-Wformat-insufficient-args", "-Wformat", "more '%' conversions than data arguments" },
    { "-Wformat-invalid-specifier", "-Wformat", "invalid conversion specifier" },
    { NULL, NULL, "empty case range specified" },
    { "-Wgcc-compat", NULL, "GCC does not allow attribute in this position on a function definition" },
    { "-Wgnu-alignof-expression", "-Wgnu", "alignof applied to an expression is a GNU extension" },
    { "-Wdeprecated-declarations", "-Wdeprecated", "use of a deprecated declaration" },
    { "-WCFString-literal", NULL, "input conversion stopped due to an input byte that does not belong to the input codeset UTF-8" },
    { "-Wunsupported-availability-guard", NULL, "__builtin_available does not guard availability here; use if (__builtin_available) instead" },
    { "-Wavailability", NULL, "use same version number separators '_' or '.'; as in 'major[.minor[.subminor]]'" },
    { "-Wbounds-safety-counted-by-elt-type-unknown-size", NULL, "'counted_by' should not be applied to an array with element of unknown size" },
    { "-Wextern-initializer", NULL, "'extern' variable has an initializer" },
    { "-Wdeprecated-attributes", "-Wdeprecated", "specifying vector types with the 'mode' attribute is deprecated" },
    { "-Wunknown-sanitizers", NULL, "unknown sanitizer ignored" },
    { "-Woverflow-behavior-attribute-ignored", NULL, "'overflow_behavior' attribute is ignored because it is not enabled" },
    { "-WNSObject-attribute", NULL, "'NSObject' attribute may be put on a typedef only; attribute is ignored" },
    { "-WIndependentClass-attribute", NULL, "'objc_independent_class' attribute may be put on a typedef only; attribute is ignored" },
    { "-Wassume", NULL, "assumption is ignored because it contains (potential) side-effects" },
    { "-Wduplicate-decl-specifier", NULL, "duplicate declaration specifier" },
    { "-Winvalid-noreturn", NULL, "function declared 'noreturn' should not return" },
    { "-Wprivate-extern", NULL, "use of __private_extern__ on a declaration may not produce external symbol private to the linkage unit and is deprecated" },
    { "-Wliteral-conversion", "-Wconversion", "implicit conversion of a floating literal changes value" },
    { NULL, NULL, "weak identifier never declared" },
    { "-Wpragmas", NULL, "#pragma redefine_extname is applicable to external C declarations only" },
    { "-Wthread-safety-attributes", "-Wthread-safety", "invalid thread-safety attribute" },
    { "-Wnoderef", NULL, "dereferencing expression marked as 'noderef'" },
    { "-Wexcessive-regsave", NULL, "interrupt service routine should only call a function with attribute 'no_caller_saved_registers'" },
    { "-Wtcb-enforcement", NULL, "calling a function outside the trusted computing base" },
    { "-Winvalid-version-availability", NULL, "invalid version in availability attribute" },
    { "-Wunknown-escape-sequence", NULL, "unknown escape sequence" },
    { "-Wvoid-ptr-dereference", NULL, "ISO C does not allow indirection on operand of type 'void *'" },
    { "-Wmultichar", NULL, "multi-character character constant" },
    { "-Wfour-char-constants", NULL, "multi-character character constant" },
    { NULL, NULL, "character constant too long for its type" },
    { "-Wnull-dereference", NULL, "indirection of non-volatile null pointer will be deleted, not trap" },
    { "-Wuser-defined-warnings", NULL, "user-defined warning" },
    { "-Wfunction-multiversion", NULL, "CPU list contains duplicate entries; attribute ignored" },
    { "-Wunused-result", "-Wunused-value", "ignoring return value of function declared with 'warn_unused_result' attribute" },
    { "-Wint-in-bool-context", NULL, "converting the result of '<<' to a boolean; did you mean to compare with '0'?" },
    { "-Wtautological-constant-compare", "-Wtautological-compare", "converting the result of '?:' with integer constants to a boolean always evaluates to 'true'" },
    { "-Wbitfield-constant-conversion", "-Wconstant-conversion", "implicit truncation to bit-field changes value" },
    { "-Wsingle-bit-bitfield-constant-conversion", "-Wbitfield-constant-conversion", "implicit truncation to a one-bit wide bit-field changes value" },
    { "-Wenum-compare", NULL, "comparison of different enumeration types" },
    { "-Wenum-enum-conversion", "-Wenum-conversion", "arithmetic between different enumeration types" },
    { "-Wenum-compare-conditional", "-Wenum-conversion", "conditional expression between different enumeration types" },
    { "-Wenum-float-conversion", "-Wenum-conversion", "arithmetic between enumeration type and floating-point type" },
    { "-Wanon-enum-enum-conversion", NULL, "arithmetic between different enumeration types" },
    { "-Wfor-loop-analysis", "-Wloop-analysis", "variable used in loop condition not modified in loop body" },
    { "-Wimplicit-enum-enum-cast", "-Wenum-conversion", "implicit conversion from enumeration type to different enumeration type" },
    { "-Wvisibility", NULL, "declaration will not be visible outside of this function" },
    { "-Wodr", NULL, "type has an attribute which currently causes the types to be treated as though they are incompatible" },
    { "-Winvalid-unevaluated-string", NULL, "encoding prefix on an unevaluated string literal has no effect" },
    { "-Wpedantic", NULL, "use of non-standard escape character" },
    { "-Wdelimited-escape-sequence-extension", "-Wpedantic", "delimited escape sequences are a C2y extension" },
    // (#1838: its ID assigned past the ones other pull requests take, which their rows fill.)
    [CHECK_WARNING_ID_TARGET_CLONES_MIXED_SPECIFIERS] = { "-Wtarget-clones-mixed-specifiers", "-Wfunction-multiversion", "mixing 'target_clones' specifier mechanisms is permitted for GCC compatibility" },
    // (#1843: its ID assigned past the ones other pull requests take, which their rows fill.)
    [CHECK_WARNING_ID_INVALID_CPU_SUPPORTS] = { NULL, NULL, "invalid cpu feature string for builtin" },
    // (#1849: its ID assigned past the ones other pull requests take, which their rows fill.)
    [CHECK_WARNING_ID_ATTRIBUTE_ALIAS] = { "-Wattribute-alias", NULL, "alias and aliasee have different types" },
    [CHECK_WARNING_ID_TYPE_SAFETY] = { "-Wtype-safety", NULL, "argument type doesn't match specified type tag" },
    [CHECK_WARNING_ID_UNEVALUATED_EXPRESSION] =
        { "-Wunevaluated-expression", "-Wunused-value", "expression with side effects has no effect in an unevaluated context" },
    [CHECK_WARNING_ID_UNDERLYING_CV_QUALIFIER] = { "-Wunderlying-cv-qualifier-ignored", NULL, "qualifier in enumeration underlying type ignored" },
    [CHECK_WARNING_ID_PRAGMA_PACK] = { "-Wpragma-pack", "-Wpragmas", "unterminated '#pragma pack (push, ...)' at end of file" },
    [CHECK_WARNING_ID_PRAGMA_PACK_NOTICE] = { NULL, NULL, "value of #pragma pack(show)" },
    [CHECK_WARNING_ID_POINTER_TO_INT_CAST] = { "-Wpointer-to-int-cast", NULL, "cast to smaller integer type" },
    [CHECK_WARNING_ID_VOID_POINTER_TO_INT_CAST] = { "-Wvoid-pointer-to-int-cast", "-Wpointer-to-int-cast", "cast to smaller integer type" },
    [CHECK_WARNING_ID_POINTER_TO_ENUM_CAST] = { "-Wpointer-to-enum-cast", NULL, "cast to smaller integer type" },
    [CHECK_WARNING_ID_VOID_POINTER_TO_ENUM_CAST] = { "-Wvoid-pointer-to-enum-cast", "-Wpointer-to-enum-cast", "cast to smaller integer type" },
    [CHECK_WARNING_ID_TENTATIVE_ARRAY] = { "-Wtentative-definition-array", NULL, "tentative array definition assumed to have one element" },
    // (#1447: its ID assigned past the ones other pull requests take, which their rows fill.)
    [CHECK_WARNING_ID_SWIFT_NAME_ATTRIBUTE] = { "-Wswift-name-attribute", NULL, "'swift_name' attribute has invalid identifier for the base name" },
    [CHECK_WARNING_ID_UNREACHABLE_GENERIC_ASSOC] = { "-Wunreachable-code-generic-assoc", "-Wunreachable-code",
                                                     "association will never be selected" },
    // (#1953: its ID past -Wunreachable-code-generic-assoc's, 131.)
    [CHECK_WARNING_ID_UNSEQUENCED] = { "-Wunsequenced", "-Wsequence-point", "multiple unsequenced modifications" },
    [CHECK_WARNING_ID_DEFAULT_CONST_INIT_VAR_UNSAFE] = { "-Wdefault-const-init-var-unsafe", "-Wdefault-const-init-unsafe",
                                                         "default initialization of an object of const type leaves the object uninitialized" },
    [CHECK_WARNING_ID_DEFAULT_CONST_INIT_FIELD_UNSAFE] = { "-Wdefault-const-init-field-unsafe", "-Wdefault-const-init-unsafe",
                                                           "default initialization of an object with const member leaves the object uninitialized" },
    [CHECK_WARNING_ID_DEFAULT_CONST_INIT_VAR] = { "-Wdefault-const-init-var", "-Wdefault-const-init",
                                                  "default initialization of an object of const type is incompatible with C++" },
    [CHECK_WARNING_ID_CXX_COMPAT] = { "-Wc++-compat", NULL, "-Wc++-compat is on" },
    [CHECK_WARNING_ID_GNU_UNION_CAST] = { "-Wgnu-union-cast", "-Wgnu", "cast to union type is a GNU extension" },
    [CHECK_WARNING_ID_GNU_DESIGNATOR] = { "-Wgnu-designator", "-Wgnu", "use of GNU 'missing =' extension in designator" },
    [CHECK_WARNING_ID_GNU_ARRAY_RANGE] = { "-Wgnu-designator", "-Wgnu", "use of GNU array range extension" },
    [CHECK_WARNING_ID_STRING_PLUS_INT] = { "-Wstring-plus-int", NULL, "adding an integer to a string does not append to the string" },
    [CHECK_WARNING_ID_SHIFT_OP_PARENTHESES] = { "-Wshift-op-parentheses", "-Wparentheses", "operator has lower precedence than its operand's" },
    [CHECK_WARNING_ID_SIZEOF_ARRAY_DECAY] = { "-Wsizeof-array-decay", NULL, "sizeof on pointer operation will return size of a pointer" },
    [CHECK_WARNING_ID_CHAINED_COMPARISON] = { "-Wparentheses", NULL, "chained comparison does not behave the same as a mathematical expression" },
    [CHECK_WARNING_ID_ASM_LABEL_ON_AUTO] = { NULL, NULL, "ignored asm label on automatic variable" },
    [CHECK_WARNING_ID_ALIGNMENT_NO_OP] = { "-Wtautological-compare", NULL, "aligning a value to 1 byte is a no-op" },
    [CHECK_WARNING_ID_ALIGNMENT_ALWAYS_TRUE] = { "-Wtautological-compare", NULL, "the result of checking whether a value is aligned to 1 byte is always true" },
    [CHECK_WARNING_ID_NONSCALAR_SAME_CAST] = { "-Wpedantic", NULL, "C99 forbids casting nonscalar type to the same type" },
    [CHECK_WARNING_ID_C23_ATTRIBUTES] = { "-Wc23-extensions", "-Wc2x-extensions", "[[]] attributes are a C23 extension" },
    [CHECK_WARNING_ID_ELABORATED_ENUM_BASE] = { "-Welaborated-enum-base", NULL,
                                                "non-defining declaration of enumeration with a fixed underlying type is only permitted as a standalone "
                                                "declaration; missing list of enumerators?" },
    [CHECK_WARNING_ID_PSABI] = { "-Wpsabi", NULL, "AVX vector argument of type T without 'avx' enabled changes the ABI" },
    [CHECK_WARNING_ID_GNU_EMPTY_STRUCT] = { "-Wgnu-empty-struct", "-Wgnu", "empty struct is a GNU extension" },
    [CHECK_WARNING_ID_UNUSED_BUT_SET_VARIABLE] = { "-Wunused-but-set-variable", "-Wunused", "variable set but not used" },
    [CHECK_WARNING_ID_SHIFT_SIGN_OVERFLOW] = { "-Wshift-sign-overflow", NULL, "signed shift result sets the sign bit of the shift expression's type" },
    [CHECK_WARNING_ID_ASSIGN_ENUM] = { "-Wassign-enum", NULL, "integer constant not in range of enumerated type" },
    [CHECK_WARNING_ID_UNUSED_LABEL] = { "-Wunused-label", "-Wunused", "unused label" },
    [CHECK_WARNING_ID_TAUTOLOGICAL_OUT_OF_RANGE_COMPARE] = { "-Wtautological-constant-out-of-range-compare", "-Wtautological-constant-compare",
                                                             "result of comparison of constant is always true or false" },
};

_Static_assert(BUSTER_ARRAY_LENGTH(check_warning_descriptions) == CHECK_WARNING_ID_COUNT, "every sparse policy ID has a description");

static bool check_diagnostic_option(String8 option, const char* expected)
{
    const bool result = expected != NULL && option.length == strlen(expected) && memcmp(option.pointer, expected, option.length) == 0;
    return result;
}

#include "c_check_warning_policy.c"

static uint8_t check_input_fp_warning_policy(Preprocessor* pp, uint32_t source, bool* quiet)
{
    const CheckWarningCommandLine command = check_warning_command_line(&pp->options, CHECK_WARNING_ID_IGNORED_PRAGMA, false, false);
    pp_history_ensure(pp);
    const bool system = pp_system_level_at(pp, pp_expansion_location(pp, source, pp_history_at(pp, source))) != BUSTER_PP_USER_FILE;
    *quiet = command.quiet || (system && !command.system_headers);
    return (uint8_t)command.severity;
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

static CheckWarningSeverity check_diagnostic_severity_id_from(const CheckInput* input, uint32_t token, uint32_t source, uint32_t index, CheckWarningSeverity severity)
{
    // -Wfatal-errors makes an error the command line or the diagnostic's default severity chose fatal before a source
    // pragma is read; a source pragma's error, warning or fatal then replaces that, as clang's pragmas do.
    severity = input->fatal_errors && severity == CHECK_WARNING_ERROR ? CHECK_WARNING_FATAL : severity;
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
            matched = check_warning_option(index, record->option);
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

// check_diagnostic_severity_id_from for a grouped diagnose_if (#1447): the source pragma in force at the event that names
// warning group `group` (its index in pp_features_warning) or a group that holds it replaces `severity`.
static __attribute__((unused)) CheckWarningSeverity check_diagnostic_group_severity_from(const CheckInput* input, uint32_t token, uint32_t source, uint32_t group, CheckWarningSeverity severity)
{
    const CheckInputDiagnosticPragmas* history = input->diagnostic_pragmas;
    if (history != NULL)
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
            char name[128];
            const bool fits = record->option.length >= 2 && record->option.length - 2 < sizeof(name) && record->option.pointer[0] == '-' && record->option.pointer[1] == 'W';
            if (fits)
            {
                memcpy(name, record->option.pointer + 2, record->option.length - 2);
                name[record->option.length - 2] = 0;
            }
            const uint32_t holder = fits ? pp_warning_group_index(name) : UINT32_MAX;
            matched = holder != UINT32_MAX && pp_warning_group_holds(holder, group);
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

static CheckWarningSeverity check_diagnostic_severity_from(const CheckInput* input, uint32_t token, uint32_t source, uint32_t warning, CheckWarningSeverity severity)
{
    return check_diagnostic_severity_id_from(input, token, source, check_diagnostic_warning_index(warning), severity);
}

static CheckWarningSeverity check_diagnostic_severity_at(const CheckInput* input, uint32_t token, uint32_t source, uint32_t warning, bool default_ignored)
{
    const CheckWarningSeverity severity = default_ignored ? CHECK_WARNING_IGNORE : CHECK_WARNING_WARNING;
    return check_diagnostic_severity_from(input, token, source, warning, severity);
}

// The x86 range diagnostics' severity: the command line's mapping of the event's intrinsic default (a warning, or
// clang's DefaultError), then a source pragma at the event's token. A CheckInput built outside the reader keeps the
// intrinsic default. Clang's -w suppresses this diagnostic over any pragma, even an error one.
static CheckWarningSeverity check_diagnostic_range_severity(const CheckInput* input, uint32_t token, bool default_error)
{
    const uint32_t index = default_error ? 1u : 0u;
    const CheckWarningSeverity baseline = input->x86_range_policy ? (CheckWarningSeverity)input->x86_range_severities[index] :
                                          default_error ? CHECK_WARNING_ERROR : CHECK_WARNING_WARNING;
    const bool quiet = input->x86_range_policy && input->x86_range_quiet;
    return quiet ? CHECK_WARNING_IGNORE : check_diagnostic_severity_id_from(input, token, BUSTER_CHECK_NONE, CHECK_WARNING_ID_ARGUMENT_OUTSIDE_RANGE, baseline);
}

// The severity of the unit's `index`th implicit declaration's diagnostic: a library function's, an unknown builtin's
// (clang's DefaultError "use of unknown builtin", #2121) or any other function's policy.
static CheckWarningSeverity check_diagnostic_implicit_function(const CheckInput* input, uint32_t index)
{
    const uint32_t token = input->implicit_function_tokens[index];
    const bool library = token < input->token_count && input->function_flags != NULL && (input->function_flags[token] & CHECK_INPUT_FUNCTION_IMPLICIT_LIBRARY) != 0;
    const bool builtin = input->implicit_function_builtins != NULL && input->implicit_function_builtins[index];
    const uint8_t policy = builtin ? input->implicit_builtin_diagnostic : library ? input->implicit_library_diagnostic : input->implicit_function_diagnostic;
    const CheckWarningSeverity baseline = policy == 1 ? CHECK_WARNING_IGNORE : policy == 2 ? CHECK_WARNING_WARNING : CHECK_WARNING_ERROR;
    const CheckWarningSeverity severity = input->implicit_function_system || !check_graph_system_token(&input->graph, token) ?
        check_diagnostic_severity_from(input, token, BUSTER_CHECK_NONE, CHECK_WARNING_IMPLICIT_FUNCTION_DECLARATION, baseline) : CHECK_WARNING_IGNORE;
    return severity;
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

// Snapshot command-line policy independently of option storage and source-ordered pragma overrides.
static void check_input_warning_policy(CheckInput* input, const PPOptions* options)
{
    _Static_assert(CHECK_WARNING_ID_KR_TOO_MANY - CHECK_WARNING_ID_KR_DEFINITION + 1 == BUSTER_ARRAY_LENGTH(input->kr_warning_severities),
                   "the K&R policy snapshot covers its four stable IDs");
    _Static_assert(BUSTER_ARRAY_LENGTH(input->x86_range_severities) == 2,
                   "the x86 range snapshot has a warning and a DefaultError baseline");
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(input->kr_warning_severities); i += 1)
    {
        const CheckWarningCommandLine command = check_warning_command_line(options, CHECK_WARNING_ID_KR_DEFINITION + i, false, false);
        input->kr_warning_severities[i] = (uint8_t)command.severity;
        input->kr_warning_quiet = command.quiet;
        input->kr_warning_system = command.system_headers;
        input->kr_warning_errors |= command.severity >= CHECK_WARNING_ERROR;
    }
    input->kr_warning_policy = true;
    for (uint32_t default_error = 0; default_error < BUSTER_ARRAY_LENGTH(input->x86_range_severities); default_error += 1)
    {
        const CheckWarningCommandLine x86_range = check_warning_command_line(options, CHECK_WARNING_ID_ARGUMENT_OUTSIDE_RANGE, false, default_error != 0);
        input->x86_range_severities[default_error] = (uint8_t)x86_range.severity;
        input->x86_range_quiet |= x86_range.quiet;
    }
    input->x86_range_policy = true;
    const CheckWarningCommandLine odr = check_warning_command_line(options, CHECK_WARNING_ID_ODR, false, true);
    input->odr_severity = (uint8_t)odr.mapped;
    input->odr_quiet = odr.quiet;
    input->odr_system = odr.system_headers;
    input->pedantic_escapes = check_warning_command_line(options, CHECK_WARNING_ID_NONSTANDARD_ESCAPE, true, false).severity != CHECK_WARNING_IGNORE;
}

_Static_assert(CHECK_WARNING_WARNING == BUSTER_CHECK_ODR_WARNING && CHECK_WARNING_ERROR == BUSTER_CHECK_ODR_ERROR, "the reader reads -Wodr's severity by value");

// clang's -Wodr at `token`: an error by default (DefaultError), which -w leaves; -Wno-error=odr demotes it to a warning,
// which -w then ignores; -Wno-odr ignores it; a source pragma replaces the command line's choice at its token. In a system
// header it is ignored, error or not, but with -Wsystem-headers (the reader's system-token map, check_input_system_tokens).
static uint32_t check_input_odr_severity(const CheckInput* input, uint32_t token)
{
    const CheckWarningSeverity mapped = (CheckWarningSeverity)input->odr_severity;
    const CheckWarningSeverity line = input->odr_quiet && mapped < CHECK_WARNING_ERROR ? CHECK_WARNING_IGNORE : mapped;
    const CheckWarningSeverity source = check_diagnostic_severity_id_from(input, token, BUSTER_CHECK_NONE, CHECK_WARNING_ID_ODR, line);
    const bool system = !input->odr_system && check_graph_system_token(&input->graph, token);
    return system || (input->odr_quiet && source < CHECK_WARNING_ERROR) ? CHECK_WARNING_IGNORE : source;
}

// K&R diagnostics retain call-site spelling independently of the definition's compatibility signature.
static uint32_t check_diagnostic_kr_call_token(const CheckInput* input, uint32_t node)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t known = graph->members != NULL ? graph->members[node] : 0;
    uint32_t callee = graph->operand0[node];
    if (callee < graph->count && graph->ops[callee] == CHECK_OP_ADDRESS)
    {
        callee = graph->operand0[callee];
    }
    return known != 0 ? known - 1 : callee < graph->count ? graph->tokens[callee] : graph->tokens[node];
}

// The name a reader's K&R record's severity and words read: a call's callee's (check_input_kr_call), else its token.
static uint32_t check_diagnostic_kr_name(const CheckInputKrDiagnostic* diagnostic)
{
    return diagnostic->warning != CHECK_WARNING_KR_DEFINITION && diagnostic->body != BUSTER_CHECK_NONE ? diagnostic->body : diagnostic->token;
}

// Where clang reports a call's K&R warning (#1994, Sema::BuildResolvedCallExpr and ConvertArgumentsForCall): passing
// arguments without a prototype at the call's `(`, a known definition's too few or too many at its `)`, the node's token.
static uint32_t check_diagnostic_kr_call_place(const CheckInput* input, const Parser* parser, uint32_t node, uint32_t warning)
{
    const uint32_t close = input->graph.tokens[node];
    uint32_t open = warning == CHECK_WARNING_NON_PROTOTYPE_CALL && close < parser->tokens.count ? close : 0;
    while (open != 0 && !(parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == close))
    {
        open -= 1;
    }
    return open != 0 ? open : close;
}

static CheckWarningSeverity check_diagnostic_kr_severity(const CheckInput* input, uint32_t token, uint32_t warning)
{
    const uint32_t declaration = input->declarations != NULL && token < input->token_count ? input->declarations[token] : BUSTER_CHECK_NONE;
    // An implicit declaration already has its own diagnostic; Clang does not add a deprecation warning to direct
    // calls to it. An explicit pointer or a dereferenced designator still has its expression's ordinary policy.
    const bool implicit = warning == CHECK_WARNING_NON_PROTOTYPE_CALL && declaration < input->token_count &&
                          (input->function_flags[declaration] & CHECK_INPUT_FUNCTION_IMPLICIT) != 0;
    const uint32_t id = check_diagnostic_warning_index(warning);
    const CheckWarningSeverity baseline = input->kr_warning_policy && id >= CHECK_WARNING_ID_KR_DEFINITION && id <= CHECK_WARNING_ID_KR_TOO_MANY ?
        (CheckWarningSeverity)input->kr_warning_severities[id - CHECK_WARNING_ID_KR_DEFINITION] : CHECK_WARNING_WARNING;
    const bool suppressed = implicit || input->kr_warning_quiet || (!input->kr_warning_system && check_graph_system_token(&input->graph, token));
    return suppressed ? CHECK_WARNING_IGNORE : check_diagnostic_severity_id_from(input, token, BUSTER_CHECK_NONE, id, baseline);
}

static uint32_t check_diagnostic_attribute_warnings(const CheckInput* input, uint32_t token)
{
    const CheckInputAttributes* attributes = check_sparse_wide_present(input->attributes) && token < input->token_count ? check_input_attributes_at(input, token) : NULL;
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

// The bytes of check_diagnostic_x86_hard's message buffer: the longest x86 builtin's name is 41 bytes, and test-checker
// checks that every one's message fits (check_test_x86_hard_every).
#define BUSTER_CHECK_DIAGNOSTIC_X86_TEXT 192u

// An x86 builtin's call whose hard immediate or target constraint failed, as clang reports it (#1066, #1029): Sema's
// BuiltinConstantArg and SemaX86's rounding, scale and 32-bit-only checks put the error at the call's begin, the builtin's name, and name the builtin
// for an immediate that isn't an integer constant expression. The report keeps its own token, the call's `)`, which
// orders it among the other diagnostics, as clang reports the call once its arguments' own diagnostics are out.
// Returns the message, written into `text` (`capacity` bytes), with the name's token in `location`; or NULL for any
// other report, whose message and place are its own. The node's effect is found by a search: the stream keeps the
// effects in their nodes' order (check_graph_validate takes the graph's x86 calls only so), as the lowering finds them,
// and this runs once per error reported, never per node.
static const char* check_diagnostic_x86_hard(const CheckInput* input, const CheckStream* stream, const CheckReport* report, char* text, uint32_t capacity,
                                             uint32_t* location)
{
    const char* result = NULL;
    const bool candidate = report->message == CHECK_MESSAGE_ACTION && report->diagnostic == CHECK_DIAG_INCOMPATIBLE && report->node < input->graph.count &&
                           input->graph.ops[report->node] == CHECK_OP_BUILTIN_X86;
    uint32_t low = 0;
    uint32_t high = candidate ? stream->x86_count : 0;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (stream->x86[middle].node < report->node)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    const CheckBuiltinX86Effect* effect = candidate && low < stream->x86_count && stream->x86[low].node == report->node ? stream->x86 + low : NULL;
    if (effect != NULL && effect->status == CHECK_BUILTIN_X86_ERROR && effect->info != NULL)
    {
        switch (effect->constraint)
        {
            case CHECK_BUILTIN_X86_CONSTRAINT_ICE:
                snprintf(text, capacity, "argument to '%.*s' must be a constant integer", (int)effect->info->name_length, effect->info->name);
                result = text;
                break;
            case CHECK_BUILTIN_X86_CONSTRAINT_RC:
            case CHECK_BUILTIN_X86_CONSTRAINT_SAE: result = "invalid rounding argument"; break;
            case CHECK_BUILTIN_X86_CONSTRAINT_SCALE: result = "scale argument must be 1, 2, 4, or 8"; break;
            case CHECK_BUILTIN_X86_CONSTRAINT_TARGET32: result = "this builtin is only available on 32-bit targets"; break;
            case CHECK_BUILTIN_X86_CONSTRAINT_NONE:
            case CHECK_BUILTIN_X86_CONSTRAINT_RANGE: break;
        }
    }
    *location = result != NULL ? check_diagnostic_x86_token(input, report->node) : report->token;
    return result;
}

// A varargs start whose function refuses it (CHECK_DIAG_CALL, check_varargs_one), as clang reports it (#148): its ABI's
// error (checkVAStartABI: __builtin_ms_va_start out of an ms_abi function, any other start in one), or else "used in
// function with fixed args", at the builtin's name, the token before the call's opening parenthesis: the token of
// the call's role whose parent is the report's `)` (the scan is over the call's arguments, once per error reported).
// Returns NULL for any other report, whose message and place are its own.
static const char* check_diagnostic_varargs(const CheckInput* input, const Parser* parser, const TypeTable* types, const CheckReport* report, uint32_t* location)
{
    const CheckGraph* graph = &input->graph;
    const char* result = NULL;
    *location = report->token;
    const bool candidate = report->message == CHECK_MESSAGE_ACTION && report->diagnostic == CHECK_DIAG_CALL && report->node < graph->count &&
                           graph->ops[report->node] == CHECK_OP_VARARGS && graph->varargs_calls != NULL && graph->argument_offsets[report->node] < graph->varargs_count;
    if (candidate)
    {
        const CheckVarargsContext* context = &graph->varargs_calls[graph->argument_offsets[report->node]].context;
        const bool function = type_is_valid(types, context->function_type) && type_kind(types, type_strip_aligned(types, context->function_type)) == BUSTER_TYPE_KIND_FUNCTION;
        const bool win64 = function && type_function_convention(types, type_strip_aligned(types, context->function_type)) == TYPE_FUNCTION_ABI_MICROSOFT;
        const bool microsoft = graph->members[report->node] == CBUILTIN_MS_VA_START;
        result = microsoft && !win64 ? "'__builtin_ms_va_start' used in System V ABI function" : !microsoft && win64 ? "'va_start' used in Win64 ABI function" :
                 "'va_start' used in function with fixed args";
        const uint32_t closing = report->token;
        uint32_t opening = closing < parser->tokens.count ? closing : 0;
        while (opening > 0 && !(parser->tree.parents[opening] == closing &&
                                (parser->tree.roles[opening] == PARSE_ROLE_OPEN_CALL || parser->tree.roles[opening] == PARSE_ROLE_OPEN_KEYWORD)))
        {
            opening -= 1;
        }
        *location = opening > 0 ? opening - 1 : report->token;
    }
    return result;
}

// __builtin_counted_by_ref's argument refused (#148, clang's BuiltinCountedByRef): no member access of a pointer or an
// array, or one with side effects, worded as clang's, at the argument's first token past its parentheses.
static const char* check_diagnostic_counted_ref(const CheckInput* input, const Parser* parser, const CheckReport* report, uint32_t* location)
{
    const CheckGraph* graph = &input->graph;
    const char* result = NULL;
    *location = report->token;
    const bool candidate = report->message == CHECK_MESSAGE_ACTION && report->node < graph->count && graph->ops[report->node] == CHECK_OP_BUILTIN_MISC &&
                           graph->members[report->node] == CBUILTIN_COUNTED_BY_REF && graph->argument_counts[report->node] == 1 &&
                           (report->diagnostic == CHECK_DIAG_INVALID_TYPE || report->diagnostic == CHECK_DIAG_INCOMPATIBLE);
    if (candidate)
    {
        result = report->diagnostic == CHECK_DIAG_INVALID_TYPE ? "'__builtin_counted_by_ref' argument must reference a member with the 'counted_by' attribute" :
                                                                 "'__builtin_counted_by_ref' argument cannot have side-effects";
        uint32_t first = graph->argument_tokens[graph->argument_offsets[report->node]];
        uint32_t end = graph->tokens[report->node];
        while (first + 1 < end && end < parser->tokens.count && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN && parser->tree.parents[first] == end - 1)
        {
            first += 1;
            end -= 1;
        }
        *location = first;
    }
    return result;
}

static bool check_diagnostic_first_error(const CheckInput* input, const CheckStream* stream, uint32_t* token)
{
    uint32_t first = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; i < input->implicit_function_count; i += 1)
    {
        const uint32_t at = input->implicit_function_tokens[i];
        if (at < first && check_diagnostic_implicit_function(input, i) >= CHECK_WARNING_ERROR)
        {
            first = at;
        }
    }
    for (uint32_t i = 0; stream != NULL && i < stream->x86_count; i += 1)
    {
        const CheckBuiltinX86Effect* effect = stream->x86 + i;
        const uint32_t at = check_diagnostic_x86_token(input, effect->node);
        for (uint32_t e = 0; e < effect->range_event_count; e += 1)
        {
            if (effect->range_events[e].disposition == CHECK_BUILTIN_X86_READY && at < first &&
                check_diagnostic_range_severity(input, at, effect->range_events[e].default_error) >= CHECK_WARNING_ERROR)
            {
                first = at;
            }
        }
    }
    const bool policies = input->diagnostic_pragmas != NULL && input->diagnostic_pragmas->count != 0;
    for (uint32_t i = 0; i < input->kr_diagnostic_count; i += 1)
    {
        const CheckInputKrDiagnostic diagnostic = input->kr_diagnostics[i];
        const uint32_t at = diagnostic.token;
        if (at < first && check_diagnostic_kr_severity(input, check_diagnostic_kr_name(&diagnostic), diagnostic.warning) >= CHECK_WARNING_ERROR)
        {
            first = at;
        }
    }
    for (uint32_t i = 0; policies && i < input->packed_member_count; i += 1)
    {
        const CheckInputPackedMember* member = input->packed_members + i;
        if (!member->suppressed && member->token < first &&
            check_diagnostic_severity(input, member->token, CHECK_WARNING_ADDRESS_OF_PACKED_MEMBER) >= CHECK_WARNING_ERROR)
        {
            first = member->token;
        }
    }
    for (uint32_t at = 0; policies && check_sparse_wide_present(input->attributes) && at < input->token_count && at < first; at += 1)
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
    for (uint32_t node = 0; (policies || input->kr_warning_errors) && stream != NULL && node < stream->count; node += 1)
    {
        uint32_t warnings = stream->warnings[node];
        while (warnings != 0)
        {
            const uint32_t warning = warnings & (0u - warnings);
            const bool kr = (warning & CHECK_WARNING_KR_CALLS) != 0;
            const uint32_t at = kr ? check_diagnostic_kr_call_token(input, node) : input->graph.tokens[node];
            const bool ignored = stream->warning_ignored != NULL && (stream->warning_ignored[node] & warning) != 0;
            const CheckWarningSeverity severity = kr ? check_diagnostic_kr_severity(input, at, warning) :
                                                      check_diagnostic_severity_default(input, at, warning, ignored);
            if (at < first && severity >= CHECK_WARNING_ERROR)
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
    const uint32_t offset = record->error != 0 ? (record->kind == CHECK_FP_PRAGMA_FENV_ACCESS ? 3 : 2) :
                            (record->warnings & CHECK_FP_PRAGMA_EMPTY_STACK) != 0 ? 2 : record->source_end - record->source_begin > 5 ? 4 : 3;
    const uint32_t source = record->error_source != 0 ? record->error_source : record->source_begin + offset;
    return source;
}

static const char* check_diagnostic_pragma_message(const CheckInputFpPragma* record)
{
    const char* result = (record->warnings & CHECK_FP_PRAGMA_INVALID_PLACEMENT) != 0 ? "floating-point pragma is not permitted within an expression" :
                         (record->warnings & CHECK_FP_PRAGMA_EMPTY_STACK) != 0 ? "#pragma float_control(pop, ...) failed: stack empty" :
                         check_diagnostic_message(CHECK_WARNING_UNKNOWN_PRAGMA);
    return result;
}

// The name of the C23 attribute whose scope is at `token`, where clang's parser reads its arguments its own way (#1447,
// ParseClangAttributeArgs: availability's, external_source_symbol's, type_tag_for_datatype's and objc_bridge_related's,
// with parentheses), or BUSTER_CHECK_NONE: clang's attribute is located at its name, not at its scope, as each of these
// parsers makes it. Only a `clang::` one is known, so only one has reports at its scope. Cold: an attribute's report.
static uint32_t check_diagnostic_custom_attribute_name(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    const uint8_t* kinds = parser->tokens.kinds;
    const bool scoped = token + 4 < count && kinds[token] == PARSE_KIND_IDENTIFIER && kinds[token + 1] == PARSE_KIND_COLON &&
                        kinds[token + 2] == PARSE_KIND_COLON && kinds[token + 4] == PARSE_KIND_L_PAREN && token >= 1 &&
                        (kinds[token - 1] == PARSE_KIND_L_SQUARE || kinds[token - 1] == PARSE_KIND_COMMA);
    const uint32_t name = scoped ? parser->tokens.names[token + 3] : 0;
    bool custom = false;
    for (uint32_t s = 0; name != 0 && s < BUSTER_PARSE_CUSTOM_ATTRIBUTES; s += 1)
    {
        custom |= parser->meanings.custom_attributes[s] == name;
    }
    return custom ? token + 3 : BUSTER_CHECK_NONE;
}

static uint32_t check_diagnostic_input_report_source(const CheckInput* input, const Parser* parser, const CheckInputReport* report)
{
    uint32_t source = check_input_report_source(parser, report);
    const uint32_t custom = report->pragma == 0 && report->token < parser->tokens.count ? check_diagnostic_custom_attribute_name(parser, report->token) : BUSTER_CHECK_NONE;
    source = custom != BUSTER_CHECK_NONE ? parser->tokens.sources[custom] : source;
    if (input->naked_diagnostic != NULL && report->pragma == 0 && report->token == input->naked_diagnostic->position)
    {
        source = parser->tokens.sources[input->naked_diagnostic->token];
    }
    if (input->loop_diagnostic != 0 && report->pragma == input->loop_diagnostic->pragma && report->token == input->loop_diagnostic->position)
    {
        source = input->loop_diagnostic->source;
    }
    const CheckInputSectionDiagnostic* section = check_input_section_report(input, report);
    source = section != NULL ? section->source : source;
    const CheckInputOptimizeDiagnostic* optimize = check_input_optimize_report(input, report);
    source = optimize != NULL ? optimize->source : source;
    const CheckInputMsPragma* layout = check_input_ms_report(input, report);
    source = layout != NULL ? layout->source + 2 : source;
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
        const bool unknown = (record->warnings & CHECK_FP_PRAGMA_UNKNOWN_STATE) != 0 &&
                             check_diagnostic_severity_at(input, record->token, record->source_begin, CHECK_WARNING_UNKNOWN_PRAGMA, false) >= CHECK_WARNING_ERROR;
        const bool empty = (record->warnings & CHECK_FP_PRAGMA_EMPTY_STACK) != 0 && !record->warning_quiet &&
                           check_diagnostic_severity_id_from(input, record->token, record->source_begin, CHECK_WARNING_ID_IGNORED_PRAGMA,
                                                             (CheckWarningSeverity)record->warning_severity) >= CHECK_WARNING_ERROR;
        const bool promoted = !quiet && (unknown || empty);
        if (record->token < before_token && record->token < first && (invalid || promoted))
        {
            first = record->token;
            origin = check_diagnostic_pragma_source(record);
        }
    }
    const CheckInputMsPragmas* ms = input->ms_pragmas;
    for (uint32_t i = 0; ms != NULL && i < ms->count; i += 1)
    {
        const CheckInputMsPragma* record = ms->records + i;
        if (record->error && record->token < before_token && record->token < first)
        {
            first = record->token;
            origin = record->source + 2;
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
    *result = (CheckInput){ .fatal_errors = pp->options.fatal_errors };
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
        valid = valid && check_input_visibility_pragmas(&reader, handled, false);
        valid = valid && check_input_symbol_pragmas(&reader, handled, false);
        valid = valid && check_input_ms_pragmas(&reader, handled, false);
        // Visibility retains its own recovery-aware history. A later pop must still diagnose an unmatched push
        // when the parser skipped that push's annotation while recovering from a missing semicolon.
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
        valid = check_input_ignored_pragmas(&reader, handled);
        valid = valid && check_input_symbol_syntax_warnings(&reader);
        valid = valid && check_input_ms_warnings(&reader);
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
