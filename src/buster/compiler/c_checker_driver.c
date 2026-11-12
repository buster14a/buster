// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source checking and object emission share one typed stream. Unsupported constructs are errors, never silently
// accepted. Diagnostics retain source/macro history and are ordered by the point their constraint is known.

#include "linker_options.c"
#include "c_driver_inputs.c"

// The closing brace of the function body around `token`, up its tree's parents, or BUSTER_CHECK_NONE.
static uint32_t check_driver_body_close(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t parent = token, steps = 0; result == BUSTER_CHECK_NONE && parent < count && steps < count; steps += 1)
    {
        result = parser->tree.roles[parent] == PARSE_ROLE_CLOSE_BODY ? parent : BUSTER_CHECK_NONE;
        parent = parser->tree.parents[parent];
    }
    return result;
}

// The candidates clang's notes name for an ambiguous label's reference, report `index` (#1844, its reader's
// check_input_report_label_ambiguous), in its lookup's order, into `candidates` (`capacity` of them), their count: the
// innermost list's entries of the name in the candidates' scope, the latest first (its note token's, then back through
// its block's lists, which begin the block), then where the function's own labels are among them (`detail` 1), in the
// order clang made them: the one a reference that was no ambiguity made (`attribute`), then the one each earlier
// ambiguous reference of the function made, at its name.
static uint32_t check_driver_label_candidates(const Parser* parser, const CheckInput* input, uint32_t index, uint32_t* candidates, uint32_t capacity)
{
    const uint32_t count = parser->tokens.count;
    const CheckInputReport* report = input->reports + index;
    const uint32_t name = parser->tokens.names[report->token];
    uint32_t found = 0;
    bool listed = report->note_token < count;
    for (uint32_t token = report->note_token; listed && found < capacity; token -= 1)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        if (kind == PARSE_KIND_IDENTIFIER && parser->tokens.names[token] == name)
        {
            candidates[found++] = token;
        }
        listed = token != 0 && (kind == PARSE_KIND_IDENTIFIER || kind == PARSE_KIND_COMMA || kind == PARSE_KIND_SEMI || kind == PARSE_KIND_LABEL);
    }
    if (report->detail == 1)
    {
        if (report->attribute < count && found < capacity)
        {
            candidates[found++] = report->attribute;
        }
        // The earlier ambiguous references of the name in the same body, in their tokens' order.
        const uint32_t close = check_driver_body_close(parser, report->token);
        const uint32_t start = found;
        for (uint32_t r = 0; r < input->report_count && found < capacity; r += 1)
        {
            const CheckInputReport* other = input->reports + r;
            if (other->message == CHECK_INPUT_LABEL_AMBIGUOUS && other->token < report->token && parser->tokens.names[other->token] == name &&
                check_driver_body_close(parser, other->token) == close)
            {
                uint32_t at = found++;
                while (at > start && candidates[at - 1] > other->token)
                {
                    candidates[at] = candidates[at - 1];
                    at -= 1;
                }
                candidates[at] = other->token;
            }
        }
    }
    return found;
}

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
        case CHECK_DIAG_REGISTER_ADDRESS: result = "address of register variable requested"; break;
        case CHECK_DIAG_MEMBER: result = "no such member in the record type"; break;
        case CHECK_DIAG_CALL: result = "called object is not a function or function pointer"; break;
        case CHECK_DIAG_ARGUMENT_COUNT: result = "incorrect number of arguments"; break;
        case CHECK_DIAG_RESOURCE: result = "type checker ran out of memory"; break;
        case CHECK_DIAG_UNSUPPORTED: result = "expression is not yet supported by the type checker"; break;
        case CHECK_DIAG_ADDRESS_SPACE: result = "conversion changes address space of pointer"; break;
        case CHECK_DIAG_ADDRESS_SPACE_OVERLAP: result = "pointers to non-overlapping address spaces"; break;
        case CHECK_DIAG_ADDRESS_SPACE_NESTED: result = "conversion changes address space of nested pointer"; break;
        case CHECK_DIAG_OVERFLOW_BIT_INT: result = "__builtin_mul_overflow does not support 'signed _BitInt' operands of more than 128 bits"; break;
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
        case CHECK_INPUT_INITIALIZER_MEMBER: result = "field designator does not refer to any field in the record type"; break;
        case CHECK_INPUT_ARRAY_LITERAL_MISMATCH: result = "cannot initialize array with array of another type"; break;
        case CHECK_INPUT_FUNCTION_RETURN: result = "function cannot return array or function type"; break;
        case CHECK_INPUT_ARRAY_NEGATIVE: result = "array size is negative"; break;
        case CHECK_INPUT_ARRAY_FUNCTIONS: result = "declared as array of functions"; break;
        case CHECK_INPUT_ARRAY_INCOMPLETE: result = "array has incomplete element type"; break;
        case CHECK_INPUT_FUNCTION_INITIALIZER: result = "illegal initializer (only variables can be initialized)"; break;
        case CHECK_INPUT_ARRAY_ELEMENT_ALIGNMENT: result = "size of array element isn't a multiple of its alignment"; break;
        case CHECK_INPUT_ARRAY_TOO_LARGE: result = "array is too large"; break;
        case CHECK_INPUT_ARRAY_NON_INTEGER: result = "size of array has non-integer type"; break;
        case CHECK_INPUT_AMBIGUOUS: result = "reference to identifier is ambiguous"; break;
        case CHECK_INPUT_ASM_LABEL_CONFLICT: result = "conflicting asm label"; break;
        case CHECK_INPUT_TYPE_SPECIFIER_MISSING: result = "type name requires a specifier or qualifier"; break;
        case CHECK_INPUT_INVALID_USE: result = "use of an invalid declaration"; break;
        // (Unreached: every such report has its attribute token, and the attribute-argument branch prints it in clang's
        // words, check_input_constant_failure_report.)
        case CHECK_INPUT_CONSTANT_EXPRESSION: result = "invalid constant expression"; break;
        case CHECK_INPUT_TAG_INCOMPATIBLE: result = "type has incompatible definitions"; break;
        case CHECK_INPUT_UNDECLARED_CORRECTED: result = "use of undeclared identifier"; break;
        case CHECK_INPUT_UNDECLARED: result = "use of undeclared identifier"; break;
        case CHECK_INPUT_LITERAL: result = "invalid or unsupported literal"; break;
        case CHECK_INPUT_RESOURCE: result = "type checker ran out of memory"; break;
        case CHECK_INPUT_REDECLARATION: result = "conflicting declaration"; break;
        case CHECK_INPUT_BREAK: result = "'break' statement not in loop or switch statement"; break;
        case CHECK_INPUT_CONTINUE: result = "'continue' statement not in loop statement"; break;
        case CHECK_INPUT_INCOMPLETE_TENTATIVE: result = "tentative definition has incomplete type"; break;
        case CHECK_INPUT_ENUMERATOR_REDEFINITION: result = "redefinition of enumerator"; break;
        case CHECK_INPUT_CONFLICTING_TYPES: result = "conflicting types"; break;
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
        case CHECK_INPUT_KR_IMPLICIT_INT: result = "parameter was not declared; ISO C99 and later do not support implicit int"; break;
        case CHECK_INPUT_CONSTANT_INITIALIZER: result = "initializer element is not a compile-time constant"; break;
        case CHECK_INPUT_DIRECT_INITIALIZER_ARRAY: result = "array initializer must be an initializer list or string literal"; break;
        case CHECK_INPUT_DIRECT_INITIALIZER_DEDUCED: result = "initializer for a variable with deduced type contains multiple expressions"; break;
        case CHECK_INPUT_SECTION: result = "invalid '#pragma clang section' or section type conflict"; break;
        case CHECK_INPUT_OPTIMIZE: result = "invalid '#pragma clang optimize'"; break;
        case CHECK_INPUT_JUMP_GOTO: result = "cannot jump from this goto statement to its label"; break;
        case CHECK_INPUT_JUMP_SWITCH: result = "cannot jump from switch statement to this case label"; break;
        case CHECK_INPUT_JUMP_INDIRECT: result = "cannot jump from this indirect goto statement to one of its possible targets"; break;
        case CHECK_INPUT_CASE_OUTSIDE_SWITCH: result = "'case' statement not in switch statement"; break;
        case CHECK_INPUT_DEFAULT_OUTSIDE_SWITCH: result = "'default' statement not in switch statement"; break;
        case CHECK_INPUT_JUMP_ASM: result = "cannot jump from this asm goto statement to one of its possible targets"; break;
        case CHECK_INPUT_CLEANUP_FUNCTION: result = "'cleanup' argument is not a function"; break;
        case CHECK_INPUT_CLEANUP_PARAMETERS: result = "'cleanup' function must take 1 parameter"; break;
        case CHECK_INPUT_CLEANUP_TYPE: result = "'cleanup' function parameter has incompatible type"; break;
        case CHECK_INPUT_CLEANUP_ARGUMENTS: result = "'cleanup' attribute takes one argument"; break;
        case CHECK_INPUT_WEAK_LINKAGE: result = "weak declaration cannot have internal linkage"; break;
        case CHECK_INPUT_ALIAS_DEFINITION: result = "definition cannot also be an alias"; break;
        case CHECK_INPUT_IFUNC_DEFINITION: result = "definition cannot also be an ifunc"; break;
        case CHECK_INPUT_ALIAS_TENTATIVE: result = "alias definition after tentative definition"; break;
        case CHECK_INPUT_WEAKREF_LINKAGE: result = "weakref declaration must have internal linkage"; break;
        case CHECK_INPUT_WEAKREF_CONTEXT: result = "weakref declaration must be in a global context"; break;
        case CHECK_INPUT_WEAKREF_ALIAS: result = "weakref declaration must also have an alias attribute"; break;
        case CHECK_INPUT_WEAKREF_SUBJECT: result = "'weakref' attribute only applies to variables and functions"; break;
        case CHECK_INPUT_LABEL_UNDECLARED: result = "use of undeclared label"; break;
        case CHECK_INPUT_LABEL_REDEFINITION: result = "redefinition of label"; break;
        case CHECK_INPUT_LABEL_AMBIGUOUS: result = "reference to label is ambiguous"; break;
        case CHECK_INPUT_AUTO_TYPE_INITIALIZER: result = "declaration with deduced type '__auto_type' requires an initializer"; break;
        case CHECK_INPUT_AUTO_TYPE_SELF: result = "variable declared with deduced type '__auto_type' cannot appear in its own initializer"; break;
        case CHECK_INPUT_AUTO_TYPE_CONTEXT: result = "'__auto_type' not allowed here"; break;
        case CHECK_INPUT_AUTO_TYPE_LIST: result = "cannot use '__auto_type' with initializer list in C"; break;
        case CHECK_INPUT_AUTO_TYPE_ARRAY: result = "cannot use '__auto_type' with array in C"; break;
        case CHECK_INPUT_AUTO_TYPE_BIT_FIELD: result = "cannot pass bit-field as __auto_type initializer in C"; break;
        case CHECK_INPUT_AUTO_DEDUCED_INITIALIZER: result = "declaration with deduced type 'auto' requires an initializer"; break;
        case CHECK_INPUT_AUTO_DEDUCED_SELF: result = "variable declared with deduced type 'auto' cannot appear in its own initializer"; break;
        case CHECK_INPUT_AUTO_DEDUCED_CONTEXT: result = "'auto' not allowed here"; break;
        case CHECK_INPUT_AUTO_DEDUCED_LIST: result = "cannot use 'auto' with initializer list in C"; break;
        case CHECK_INPUT_AUTO_DEDUCED_ARRAY: result = "cannot use 'auto' with array in C"; break;
        case CHECK_INPUT_AUTO_DEDUCED_BIT_FIELD: result = "cannot pass bit-field as auto initializer in C"; break;
        case CHECK_INPUT_AUTO_DEDUCED_MISMATCH: result = "'auto' deduced types differ within one declaration"; break;
        case CHECK_INPUT_AUTO_TYPE_MISMATCH: result = "'__auto_type' deduced types differ within one declaration"; break;
        case CHECK_INPUT_CONSTEXPR_INITIALIZER: result = "constexpr variable must be initialized by a constant expression"; break;
        case CHECK_INPUT_CONSTEXPR_POINTER: result = "constexpr pointer initializer is not null"; break;
        case CHECK_INPUT_CONSTEXPR_REPRESENTABLE: result = "constexpr initializer is not exactly representable in the declared type"; break;
        case CHECK_INPUT_CONSTEXPR_CONTEXT: result = "constexpr can only be used in variable declarations"; break;
        case CHECK_INPUT_WIDE_LITERAL: result = "bit-precise integer constants wider than 8388608 bits are not supported"; break;
        case CHECK_INPUT_PRAGMA_SEMANTICS: result = "unsupported semantic pragma"; break;
        case CHECK_INPUT_FP_EVAL_USE: result = "'__FLT_EVAL_METHOD__' cannot be expanded inside a scope containing '#pragma clang fp eval_method'"; break;
        case CHECK_INPUT_FP_EVAL_TYPE: result = "type is set according to the default eval method for the translation unit"; break;
        case CHECK_INPUT_UNEVALUATED_ESCAPE: result = "invalid escape sequence in an unevaluated string literal"; break;
        case CHECK_INPUT_MUSTTAIL_ARGUMENTS: result = "'musttail' attribute takes no arguments"; break;
        case CHECK_INPUT_MUSTTAIL_DECLARATION: result = "'musttail' attribute cannot be applied to a declaration"; break;
        case CHECK_INPUT_MUSTTAIL_STATEMENT: result = "'musttail' attribute only applies to return statements"; break;
        case CHECK_INPUT_LABEL_ATTRIBUTE: result = "attribute cannot be applied to a label"; break;
        case CHECK_INPUT_STATEMENT_ATTRIBUTE: result = "attribute cannot be applied to a statement"; break;
        case CHECK_INPUT_ATTRIBUTE_SUBJECT: result = "attribute only applies to other declarations"; break;
        case CHECK_INPUT_FALLTHROUGH_STATEMENT: result = "'fallthrough' attribute only applies to empty statements"; break;
        case CHECK_INPUT_FALLTHROUGH_SWITCH: result = "fallthrough annotation is outside switch statement"; break;
        case CHECK_INPUT_FALLTHROUGH_PLACEMENT: result = "fallthrough annotation does not directly precede switch label"; break;
        case CHECK_INPUT_VERBOSE_TRAP_STRING: result = "argument to __builtin_verbose_trap must be a pointer to a constant string"; break;
        case CHECK_INPUT_VERBOSE_TRAP_DOLLAR: result = "argument to __builtin_verbose_trap must not contain $"; break;
        case CHECK_INPUT_LONGJMP_INTEGER: result = "argument to '__builtin_longjmp' must be a constant integer"; break;
        case CHECK_INPUT_LONGJMP_ONE: result = "argument to __builtin_longjmp must be a constant 1"; break;
        case CHECK_INPUT_UNKNOWN_BUILTIN: result = "use of unknown builtin"; break;
        case CHECK_INPUT_INVOKE_ARGUMENTS: result = "too few arguments to function call, expected at least 1, have 0"; break;
        case CHECK_INPUT_BUILTIN_ARGUMENT_COUNT: result = "incorrect number of arguments"; break;
        case CHECK_INPUT_BUILTIN_NOT_CALLED: result = "builtin functions must be directly called"; break;
        case CHECK_INPUT_BUILTIN_CONFLICT: result = "conflicting types for builtin function"; break;
        case CHECK_INPUT_BUILTIN_DEFINITION: result = "definition of builtin function"; break;
        case CHECK_INPUT_BUILTIN_REDECLARE: result = "cannot redeclare builtin function"; break;
        case CHECK_INPUT_ATTRIBUTE_OUT_OF_BOUNDS: result = "attribute parameter is out of bounds"; break;
        case CHECK_INPUT_ATTRIBUTE_TYPES: result = "attribute cannot be applied to types"; break;
        case CHECK_INPUT_ATTRIBUTE_LIST_PLACE: result = "an attribute list cannot appear here"; break;
        case CHECK_INPUT_ATTRIBUTES_MISPLACED: result = "misplaced attributes; expected attributes here"; break;
        case CHECK_INPUT_ENUM_NOT_REPRESENTABLE: result = "enumerator value is not representable in the underlying type"; break;
        case CHECK_INPUT_ATTRIBUTE_CONVENTIONS: result = "calling convention attributes are not compatible"; break;
        case CHECK_INPUT_CONVENTION_CHANGE: result = "function declared here was previously declared with another calling convention"; break;
        case CHECK_INPUT_VECTOR_ELEMENT: result = "invalid vector element type"; break;
        case CHECK_INPUT_VECTOR_BIT_INT: result = "'_BitInt' vector element width must be a power of 2"; break;
        case CHECK_INPUT_VECTOR_CONSTANT: result = "vector_size attribute requires an integer constant"; break;
        case CHECK_INPUT_VECTOR_NEGATIVE: result = "vector must have non-negative size"; break;
        case CHECK_INPUT_VECTOR_TOO_LARGE: result = "vector size too large"; break;
        case CHECK_INPUT_VECTOR_ZERO: result = "zero vector size"; break;
        case CHECK_INPUT_VECTOR_MULTIPLE: result = "vector size not an integral multiple of component size"; break;
        case CHECK_INPUT_ADDRESS_SPACE_CONSTANT: result = "'address_space' attribute requires an integer constant"; break;
        case CHECK_INPUT_ADDRESS_SPACE_NEGATIVE: result = "address space is negative"; break;
        case CHECK_INPUT_ADDRESS_SPACE_TOO_HIGH: result = "address space is larger than the maximum supported (8388580)"; break;
        case CHECK_INPUT_ADDRESS_SPACE_FUNCTION: result = "function type may not be qualified with an address space"; break;
        case CHECK_INPUT_ADDRESS_SPACE_MULTIPLE: result = "multiple address spaces specified for type"; break;
        case CHECK_INPUT_ADDRESS_SPACE_AUTOMATIC: result = "automatic variable qualified with an address space"; break;
        case CHECK_INPUT_ADDRESS_SPACE_PARAMETER: result = "parameter may not be qualified with an address space"; break;
        case CHECK_INPUT_ADDRESS_SPACE_FIELD: result = "field may not be qualified with an address space"; break;
        case CHECK_INPUT_ADDRESS_SPACE_COMPOUND: result = "compound literal in function scope may not be qualified with an address space"; break;
        case CHECK_INPUT_GC_MULTIPLE: result = "multiple garbage collection attributes specified for type"; break;
        case CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT: result = "attribute takes another count of arguments"; break;
        case CHECK_INPUT_ATTRIBUTE_EXCLUSION: result = "attributes are not compatible"; break;
        case CHECK_INPUT_ATTRIBUTE_ANNOTATE_STRING: result = "expected string literal as argument of 'annotate' attribute"; break;
        case CHECK_INPUT_UNAVAILABLE_USE: result = "use of an unavailable declaration"; break;
        case CHECK_INPUT_MULTIVERSION_ATTRIBUTE: result = "attribute multiversioning cannot be combined with another attribute"; break;
        case CHECK_INPUT_MULTIVERSION_INCOMPATIBLE: result = "multiversioning attributes are not compatible"; break;
        case CHECK_INPUT_MULTIVERSION_DUPLICATE: result = "attribute cannot appear more than once on a declaration"; break;
        case CHECK_INPUT_NO_BUILTIN_DEFINITION: result = "no_builtin attribute is permitted on definitions only"; break;
        case CHECK_INPUT_SPECIFIER_COMBINE: result = "cannot combine with previous declaration specifier"; break;
        case CHECK_INPUT_SPECIFIER_SIGN: result = "type cannot be signed or unsigned"; break;
        case CHECK_INPUT_SPECIFIER_WIDTH: result = "invalid width for the type"; break;
        case CHECK_INPUT_SPECIFIER_COMPLEX: result = "invalid _Complex type"; break;
        case CHECK_INPUT_AVAILABILITY_VERSION: result = "expected a version of the form 'major[.minor[.subminor]]'"; break;
        case CHECK_INPUT_AVAILABILITY_ZERO: result = "version number must have non-zero major, minor, or sub-minor version"; break;
        case CHECK_INPUT_AVAILABILITY_PLATFORM: result = "unrecognized platform name"; break;
        case CHECK_INPUT_AVAILABILITY_APPLE: result = "invalid anyAppleOS version in availability check"; break;
        case CHECK_INPUT_AVAILABILITY_REPEATED: result = "version for the platform already specified"; break;
        case CHECK_INPUT_AVAILABILITY_STAR: result = "'*' query has already been specified"; break;
        case CHECK_INPUT_AVAILABILITY_WILDCARD: result = "must handle potential future platforms with '*'"; break;
        case CHECK_INPUT_COUNTED_UNION: result = "'counted_by' cannot be applied to a union member"; break;
        case CHECK_INPUT_COUNTED_SUBJECT: result = "'counted_by' only applies to pointers or C99 flexible array members"; break;
        case CHECK_INPUT_COUNTED_ARRAY: result = "'counted_by' on arrays only applies to C99 flexible array members"; break;
        case CHECK_INPUT_COUNTED_POINTEE: result = "'counted_by' cannot be applied to a pointer with pointee of unknown size"; break;
        case CHECK_INPUT_COUNTED_INTEGER: result = "'counted_by' requires a non-boolean integer type argument"; break;
        case CHECK_INPUT_COUNTED_REFERENCE: result = "'counted_by' argument must be a simple declaration reference"; break;
        case CHECK_INPUT_COUNTED_OUTSIDE: result = "field in 'counted_by' not inside structure"; break;
        case CHECK_INPUT_COUNTED_UNION_FIELD: result = "'counted_by' argument cannot refer to a union member"; break;
        case CHECK_INPUT_COUNTED_STRUCT: result = "'counted_by' field isn't within the same struct as the annotated flexible array"; break;
        case CHECK_INPUT_COUNTED_REQUIRED: result = "argument required after attribute"; break;
        case CHECK_INPUT_COUNTED_PAREN: result = "expected ')'"; break;
        case CHECK_INPUT_COUNTED_REF_LEAK: result = "value returned by '__builtin_counted_by_ref' cannot be assigned to a variable"; break;
        case CHECK_INPUT_COUNTED_REF_USE: result = "value returned by '__builtin_counted_by_ref' cannot be used in a binary expression"; break;
        case CHECK_INPUT_LIFETIMEBOUND_VOID:
            result = "'lifetimebound' attribute cannot be applied to a parameter of a function that returns void; did you mean 'lifetime_capture_by(X)'";
            break;
        case CHECK_INPUT_LIFETIMEBOUND_OBJECT: result = "'lifetimebound' attribute cannot be applied; non-member function has no implicit object parameter"; break;
        case CHECK_INPUT_LOADER_INITIALIZER: result = "variable with 'loader_uninitialized' attribute cannot have an initializer"; break;
        case CHECK_INPUT_LOADER_EXTERN: result = "variable cannot be declared both 'extern' and with the 'loader_uninitialized' attribute"; break;
        case CHECK_INPUT_LOADER_REDECLARATION: result = "redeclaration cannot add 'loader_uninitialized' attribute"; break;
        case CHECK_INPUT_INTERNAL_LINKAGE_FIRST: result = "attribute does not appear on the first declaration"; break;
        case CHECK_INPUT_ATTRIBUTE_STRING_ARGUMENT: result = "expected string literal as argument of attribute"; break;
        case CHECK_INPUT_ATTRIBUTE_INT_OR_BOOL: result = "attribute requires parameter 1 to be int or bool"; break;
        case CHECK_INPUT_ATTRIBUTE_INDEX_BOUNDS: result = "attribute parameter is out of bounds"; break;
        case CHECK_INPUT_ATTRIBUTE_ARGUMENT: result = "invalid attribute argument"; break;
        case CHECK_INPUT_RESTRICT_NOT_POINTER: result = "restrict requires a pointer or reference"; break;
        case CHECK_INPUT_INLINE_NOT_FUNCTION: result = "'inline' can only appear on functions"; break;
        case CHECK_INPUT_NORETURN_NOT_FUNCTION: result = "'_Noreturn' can only appear on functions"; break;
        case CHECK_INPUT_MS_STRUCT_ALIGNMENT: result = "ms_struct bit-field storage with non-power-of-two size is not supported"; break;
        case CHECK_INPUT_INTERRUPT_HANDLER: result = "x86-64 'interrupt' attribute only applies to functions that have a 'void' return type"; break;
        case CHECK_INPUT_INTERRUPT_CALLED: result = "interrupt service routine cannot be called directly"; break;
        case CHECK_INPUT_ASM_CONSTRAINT: result = "invalid constraint in asm"; break;
        case CHECK_INPUT_FILE_REGISTER: result = "illegal storage class on file-scoped variable"; break;
        case CHECK_INPUT_REGISTER_NAME: result = "unknown register name in asm"; break;
        case CHECK_INPUT_REGISTER_TYPE: result = "unsupported type for named register variable"; break;
        case CHECK_INPUT_BLOCK_EXTERN_INITIALIZER: result = "declaration of block scope identifier with linkage cannot have an initializer"; break;
        case CHECK_INPUT_DIAGNOSE_IF_ERROR: result = "diagnose_if attribute error"; break;
        case CHECK_INPUT_ENABLE_IF_CALL: result = "no matching function for call"; break;
        case CHECK_INPUT_ENABLE_IF_ADDRESS: result = "cannot take address of function because it has one or more non-tautological enable_if conditions"; break;
        case CHECK_INPUT_CPU_DISPATCH: result = "invalid cpu_specific or cpu_dispatch declaration"; break;
        case CHECK_INPUT_VLA_FILE_SCOPE: result = "variable length array declaration not allowed at file scope"; break;
        case CHECK_INPUT_VM_FILE_SCOPE: result = "variably modified type declaration not allowed at file scope"; break;
        case CHECK_INPUT_VLA_STATIC: result = "variable length array declaration cannot have 'static' storage duration"; break;
        case CHECK_INPUT_VLA_INITIALIZER: result = "variable-sized object may not be initialized"; break;
        case CHECK_INPUT_VLA_EXTERN: result = "variable length array declaration cannot have 'extern' linkage"; break;
        case CHECK_INPUT_VM_EXTERN: result = "variably modified type declaration cannot have 'extern' linkage"; break;
        case CHECK_INPUT_VLA_FIELD: result = "fields must have a constant size: 'variable length array in structure' extension will never be supported"; break;
        case CHECK_INPUT_DROPPED_SUPPRESS_DECLARATION: result = "expected identifier or '('"; break;
    }
    return result;
}

// The spelling of the attribute whose name token is `token`: as written, or clang's normalized spelling (without the
// double underscores around GNU's name).
static String8 check_driver_attribute_spelling(const Parser* parser, const Preprocessor* pp, uint32_t token, bool normalized)
{
    String8 result = { 0 };
    if (token < parser->tokens.count && parser->tokens.names[token] != 0)
    {
        const uint32_t name = parser->tokens.names[token];
        if (normalized)
        {
            CheckInputReader names = { .pp = (Preprocessor*)pp };
            result = check_input_attribute_name(&names, name);
            // clang's messages name the attribute record, not every spelling of it.
            result = check_input_attribute_equal(result, "no_sanitize_address") || check_input_attribute_equal(result, "no_address_safety_analysis") ||
                     check_input_attribute_equal(result, "no_sanitize_thread") ? (String8){ .pointer = "no_sanitize", .length = 11 } : result;
            result = check_input_attribute_equal(result, "weakref") ? (String8){ .pointer = "alias", .length = 5 } : result;
        }
        else
        {
            result = (String8){ .pointer = (char*)intern_bytes(pp->table, name), .length = intern_length(pp->table, name) };
        }
    }
    return result;
}

static String8 check_warning_attribute_normalized(String8 name);

// This argument error has no source spelling to interpolate. The corpus's first-error record and the driver's
// complete diagnostic stream share it, including when a later parse error requires a resumed view.
static const char check_driver_alignment_power[] = "requested alignment is not a power of 2";

// An attribute argument's error (#147, CHECK_INPUT_ATTRIBUTE_ARGUMENT: c_check_attribute.c's CheckAttributeArgumentError
// in `detail`'s low byte, a parameter's place above it) as clang words it, the attribute named from its first token
// (`first`) as each message names it: an applied attribute's normalized name (aligned's), the parsed attribute's (a GNU
// one as written, a C23 one's scope and name normalized), or the parser's name as written, without its scope (a string
// literal's); `literal` the string's text where the message quotes it (model's). Into `text`, `capacity` bytes.
static void check_driver_attribute_argument(const Parser* parser, const Preprocessor* pp, uint32_t first, uint32_t detail, uint64_t value, String8 literal,
                                            const CBuiltinLibraryNamed* named_types, char* text, uint32_t capacity)
{
    const uint32_t count = parser->tokens.count;
    const bool scoped = first + 2 < count && parser->tokens.kinds[first + 1] == PARSE_KIND_COLON;
    const uint32_t at = !scoped ? first : parser->tokens.kinds[first + 2] == PARSE_KIND_COLON && first + 3 < count ? first + 3 : first + 2;
    uint32_t opener = first;
    while (opener > 0 && parser->tree.depths[opener - 1] >= parser->tree.depths[first])
    {
        opener -= 1;
    }
    const bool standard = opener > 0 && parser->tokens.kinds[opener - 1] == PARSE_KIND_L_SQUARE;
    const String8 written = check_driver_attribute_spelling(parser, pp, at, false);
    const String8 bare = check_warning_attribute_normalized(written);
    String8 scope = scoped ? check_warning_attribute_normalized(check_driver_attribute_spelling(parser, pp, first, false)) : (String8){ "", 0 };
    scope = check_input_attribute_equal(scope, "_Clang") ? (String8){ "clang", 5 } : scope;
    char parsed[160];
    snprintf(parsed, sizeof(parsed), "%.*s%s%.*s", (int)scope.length, scope.pointer, scoped ? "::" : "", (int)(standard ? bare : written).length,
             (standard ? bare : written).pointer);
    const uint32_t number = detail >> 8;
    // mode's name, or a type argument's, as written, after the attribute's first token by `number`.
    const String8 argument = first + number < count ? check_driver_attribute_spelling(parser, pp, first + number, false) : (String8){ "", 0 };
    switch ((CheckAttributeArgumentError)(detail & 0xffu))
    {
        case CHECK_ARGUMENT_INTEGER_CONSTANT: snprintf(text, capacity, "'%.*s' attribute requires integer constant", (int)bare.length, bare.pointer); break;
        case CHECK_ARGUMENT_AN_INTEGER_CONSTANT: snprintf(text, capacity, "'%s' attribute requires an integer constant", parsed); break;
        case CHECK_ARGUMENT_PARAMETER_INTEGER: snprintf(text, capacity, "'%s' attribute requires parameter %u to be an integer constant", parsed, number); break;
        case CHECK_ARGUMENT_EXT_VECTOR_INTEGER: snprintf(text, capacity, "ext_vector_type attribute requires an integer constant"); break;
        case CHECK_ARGUMENT_ALIGNMENT_POWER: snprintf(text, capacity, "%s", check_driver_alignment_power); break;
        case CHECK_ARGUMENT_ALIGNAS_INTEGER_CONSTANT: snprintf(text, capacity, "'aligned' attribute requires integer constant"); break;
        case CHECK_ARGUMENT_ALIGNMENT_TOO_LARGE: snprintf(text, capacity, "requested alignment must be 4294967296 bytes or smaller"); break;
        case CHECK_ARGUMENT_TYPE_TAG_IDENTIFIER: snprintf(text, capacity, "expected identifier"); break;
        case CHECK_ARGUMENT_TYPE_TAG_FLAG:
            snprintf(text, capacity, "invalid comparison flag '%.*s'; use 'layout_compatible' or 'must_be_null'", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_OBJC_CLASS: snprintf(text, capacity, "parameter of '%s' attribute must be a single name of an Objective-C class", parsed); break;
        case CHECK_ARGUMENT_OBJC_TYPEDEF_ID: snprintf(text, capacity, "parameter of '%s' attribute must be 'id' when used on a typedef", parsed); break;
        case CHECK_ARGUMENT_OBJC_TYPEDEF_POINTER: snprintf(text, capacity, "'objc_bridge(id)' is only allowed on structs and typedefs of void pointers"); break;
        case CHECK_ARGUMENT_OBJC_RELATED_CLASS: snprintf(text, capacity, "expected a related Objective-C class name, e.g., 'NSColor'"); break;
        case CHECK_ARGUMENT_OBJC_RELATED_SELECTOR:
            snprintf(text, capacity, "expected a class method selector with single argument, e.g., 'colorWithCGColor:'");
            break;
        case CHECK_ARGUMENT_EXPECTED_COMMA: snprintf(text, capacity, "expected ','"); break;
        case CHECK_ARGUMENT_REQUIRED_AFTER: snprintf(text, capacity, "argument required after attribute"); break;
        case CHECK_ARGUMENT_NON_KR: snprintf(text, capacity, "'%s' attribute only applies to non-K&R-style functions", parsed); break;
        case CHECK_ARGUMENT_SWIFT_ERROR_PARAMETER:
            snprintf(text, capacity, "'%s' attribute can only be applied to a function with an error parameter", parsed);
            break;
        case CHECK_ARGUMENT_SWIFT_ERROR_RESULT:
        {
            const uint32_t convention = value < count && parser->tokens.kinds[value] == PARSE_KIND_IDENTIFIER ? parser->tokens.names[(uint32_t)value] : 0;
            snprintf(text, capacity, "'%s' attribute with '%.*s' convention can only be applied to a function returning %s", parsed,
                     convention != 0 ? (int)intern_length(pp->table, convention) : 0, convention != 0 ? (const char*)intern_bytes(pp->table, convention) : "",
                     number != 0 ? "a pointer" : "an integral type");
            break;
        }
        case CHECK_ARGUMENT_SWIFT_ASYNC_NONE:
            snprintf(text, capacity, "'swift_async' attribute must be applied to a function annotated with non-'none' attribute 'swift_async'");
            break;
        case CHECK_ARGUMENT_TYPE_TAG_INITIALIZER:
            snprintf(text, capacity, "'type_tag_for_datatype' attribute requires the initializer to be an integer constant expression");
            break;
        case CHECK_ARGUMENT_ZERO_VECTOR: snprintf(text, capacity, "zero vector size"); break;
        case CHECK_ARGUMENT_REGPARM_RANGE: snprintf(text, capacity, "'regparm' parameter must be between 0 and 6 inclusive"); break;
        case CHECK_ARGUMENT_OUT_OF_BOUNDS: snprintf(text, capacity, "'%s' attribute parameter %u is out of bounds", parsed, number); break;
        case CHECK_ARGUMENT_SENTINEL_NEGATIVE: snprintf(text, capacity, "'sentinel' parameter 1 less than zero"); break;
        case CHECK_ARGUMENT_SENTINEL_NULL: snprintf(text, capacity, "'sentinel' parameter 2 not 0 or 1"); break;
        case CHECK_ARGUMENT_PARAMETER_TYPE: snprintf(text, capacity, "'%s' attribute argument may only refer to a function parameter of integer type", parsed); break;
        case CHECK_ARGUMENT_STRING_LITERAL: snprintf(text, capacity, "expected string literal as argument of '%.*s' attribute", (int)written.length, written.pointer); break;
        case CHECK_ARGUMENT_STRING: snprintf(text, capacity, "'%s' attribute requires a string", parsed); break;
        case CHECK_ARGUMENT_IDENTIFIER: snprintf(text, capacity, "'%s' attribute requires an identifier", parsed); break;
        case CHECK_ARGUMENT_PARAMETER_IDENTIFIER: snprintf(text, capacity, "'%s' attribute requires parameter %u to be an identifier", parsed, number); break;
        case CHECK_ARGUMENT_UNKNOWN_MODE: snprintf(text, capacity, "unknown machine mode '%.*s'", (int)argument.length, argument.pointer); break;
        case CHECK_ARGUMENT_UNSUPPORTED_MODE: snprintf(text, capacity, "unsupported machine mode '%.*s'", (int)argument.length, argument.pointer); break;
        case CHECK_ARGUMENT_MODE_ENUM_VECTOR: snprintf(text, capacity, "mode '%.*s' is not supported for enumeration types", (int)argument.length, argument.pointer); break;
        case CHECK_ARGUMENT_MODE_PRIMITIVE: snprintf(text, capacity, "mode attribute only supported for integer and floating-point types"); break;
        case CHECK_ARGUMENT_MODE_TYPE: snprintf(text, capacity, "type of machine mode does not match type of base type"); break;
        case CHECK_ARGUMENT_MODE_COMPLEX_VECTOR: snprintf(text, capacity, "type of machine mode does not support base vector types"); break;
        case CHECK_ARGUMENT_TLS_MODEL: snprintf(text, capacity, "tls_model must be \"global-dynamic\", \"local-dynamic\", \"initial-exec\" or \"local-exec\""); break;
        case CHECK_ARGUMENT_FORMAT_STRING: snprintf(text, capacity, "format argument not a string type"); break;
        case CHECK_ARGUMENT_FORMAT_RESULT: snprintf(text, capacity, "function does not return string type"); break;
        case CHECK_ARGUMENT_STRFTIME: snprintf(text, capacity, "strftime format attribute requires 3rd parameter to be 0"); break;
        case CHECK_ARGUMENT_UNKNOWN_TYPE: snprintf(text, capacity, "unknown type name '%.*s'", (int)argument.length, argument.pointer); break;
        case CHECK_ARGUMENT_EXPECTED_TYPE: snprintf(text, capacity, "expected a type"); break;
        case CHECK_ARGUMENT_EXPECTED_PAREN: snprintf(text, capacity, "expected ')'"); break;
        case CHECK_ARGUMENT_PCS_TYPE: snprintf(text, capacity, "invalid PCS type"); break;
        case CHECK_ARGUMENT_CONVENTION_UNSUPPORTED: snprintf(text, capacity, "'%s' calling convention is not supported for this target", parsed); break;
        case CHECK_ARGUMENT_FUNCTIONS_METHODS: snprintf(text, capacity, "'%s' attribute only applies to functions and methods", parsed); break;
        case CHECK_ARGUMENT_DESTROY_STORAGE:
            snprintf(text, capacity, "%s attribute can only be applied to a variable with static or thread storage duration", number != 0 ? "always_destroy" : "no_destroy");
            break;
        case CHECK_ARGUMENT_PERSONALITY: snprintf(text, capacity, "'personality' argument is not a function"); break;
        case CHECK_ARGUMENT_CODE_MODEL: snprintf(text, capacity, "code model '%.*s' is not supported on this target", (int)literal.length, literal.pointer); break;
        case CHECK_ARGUMENT_INVALID_ON_DECLARATION: snprintf(text, capacity, "'%s' attribute cannot be applied to a declaration", parsed); break;
        case CHECK_ARGUMENT_EFFECT_CONSTANT: snprintf(text, capacity, "%.*s attribute requires an integer constant", (int)bare.length, bare.pointer); break;
        case CHECK_ARGUMENT_NON_NEGATIVE: snprintf(text, capacity, "'%s' attribute requires a non-negative integral compile time constant expression", parsed); break;
        case CHECK_ARGUMENT_GREATER_ZERO: snprintf(text, capacity, "'%s' attribute must be greater than 0", parsed); break;
        case CHECK_ARGUMENT_PARAMETER_INTEGER_APPLIED:
            snprintf(text, capacity, "'%.*s' attribute requires parameter %u to be an integer constant", (int)bare.length, bare.pointer, number);
            break;
        case CHECK_ARGUMENT_INVALID_MAX_ZERO:
            snprintf(text, capacity, "'%.*s' attribute argument is invalid: max must be 0 since min is 0", (int)bare.length, bare.pointer);
            break;
        case CHECK_ARGUMENT_INVALID_MIN_MAX:
            snprintf(text, capacity, "'%.*s' attribute argument is invalid: min must not be greater than max", (int)bare.length, bare.pointer);
            break;
        case CHECK_ARGUMENT_OPENCL_KERNEL: snprintf(text, capacity, "attribute '%.*s' can only be applied to an OpenCL kernel function", (int)bare.length, bare.pointer); break;
        case CHECK_ARGUMENT_KERNEL_ONLY: snprintf(text, capacity, "'%.*s' attribute only applies to kernel functions", (int)bare.length, bare.pointer); break;
        case CHECK_ARGUMENT_ALIGNAS_DECLARATION:
            snprintf(text, capacity, "'%.*s' attribute cannot be applied to %s", (int)written.length, written.pointer,
                     number == 1 ? "a variable with 'register' storage class" : number == 3 ? "a bit-field" : "a function parameter");
            break;
        case CHECK_ARGUMENT_ALIGNAS_SUBJECT: snprintf(text, capacity, "'%.*s' attribute only applies to variables and fields", (int)written.length, written.pointer); break;
        case CHECK_ARGUMENT_ALIGNAS_UNDERALIGNED:
            snprintf(text, capacity, "requested alignment is less than minimum alignment of %u for type %.*s", number, (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_OUTPUT_PARAMETER: snprintf(text, capacity, "attribute only applies to output parameters"); break;
        case CHECK_ARGUMENT_TYPES_NAMESPACES: snprintf(text, capacity, "'%s' attribute only applies to types and namespaces", parsed); break;
        case CHECK_ARGUMENT_NO_CAPTURE: snprintf(text, capacity, "'%s' attribute specifies no capturing entity", parsed); break;
        case CHECK_ARGUMENT_CAPTURE_UNKNOWN:
            // The argument as written (the report's literal).
            snprintf(text, capacity, "'%s' attribute argument '%.*s' is not a known function parameter; must be a function parameter, 'this', 'global' or 'unknown'",
                     parsed, (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_CAPTURE_ITSELF: snprintf(text, capacity, "'lifetime_capture_by' argument references itself"); break;
        case CHECK_ARGUMENT_CAPTURE_THIS: snprintf(text, capacity, "'lifetime_capture_by' argument references unavailable implicit 'this'"); break;
        case CHECK_ARGUMENT_IMPLICIT_OBJECT: snprintf(text, capacity, "'%s' attribute requires an implicit object parameter", parsed); break;
        case CHECK_ARGUMENT_OWNERSHIP_RETURN: snprintf(text, capacity, "'%s' attribute only applies to functions that return a pointer", parsed); break;
        case CHECK_ARGUMENT_OWNERSHIP_TYPE: snprintf(text, capacity, "'%s' attribute only applies to %s arguments", parsed, number == 1 ? "integer" : "pointer"); break;
        case CHECK_ARGUMENT_OWNERSHIP_CONFLICT:
        {
            const String8 other = check_driver_attribute_spelling(parser, pp, (uint32_t)value, false);
            snprintf(text, capacity, "'%s' and '%.*s' attributes are not compatible", parsed, (int)other.length, other.pointer);
            break;
        }
        case CHECK_ARGUMENT_NSOBJECT_POINTER: snprintf(text, capacity, "'NSObject' attribute is for pointer types only"); break;
        case CHECK_ARGUMENT_PRECISE_LIFETIME:
            snprintf(text, capacity, "objc_precise_lifetime only applies to retainable types; type here is %.*s", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_TARGET_FEATURE:
            snprintf(text, capacity, "'%s' attribute is not supported on targets missing '%s'; specify an appropriate -march= or -mcpu=", parsed,
                     number == 1 ? "zve32x" : "sve");
            break;
        case CHECK_ARGUMENT_BUILTIN_ALIAS: snprintf(text, capacity, "'%s' attribute can only be applied to a ARM, HLSL, SPIR-V or RISC-V builtin", parsed); break;
        case CHECK_ARGUMENT_BLOCK_NONLOCAL: snprintf(text, capacity, "'__block' is not allowed on a %s variable", number == 1 ? "static local" : "nonlocal"); break;
        case CHECK_ARGUMENT_NOT_ICE: snprintf(text, capacity, "expression is not an integer constant expression"); break;
        case CHECK_ARGUMENT_CODE_ALIGN:
            snprintf(text, capacity, "'%s' attribute requires an integer argument which is a constant power of two between 1 and 4096 inclusive; provided argument was %" PRId64,
                     parsed, (int64_t)value);
            break;
        case CHECK_ARGUMENT_LOOP_ONLY: snprintf(text, capacity, "'%s' attribute only applies to 'for', 'while', and 'do' statements", parsed); break;
        case CHECK_ARGUMENT_POSITIVE: snprintf(text, capacity, "'%s' attribute requires a positive integral compile time constant expression", parsed); break;
        case CHECK_ARGUMENT_ATOMIC_OPTION:
            snprintf(text, capacity, "invalid argument '%.*s' to atomic attribute; valid options are: 'remote_memory', 'fine_grained_memory', 'ignore_denormal_mode' "
                                     "(optionally prefixed with 'no_')", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_COMPOUND_ONLY: snprintf(text, capacity, "'%s' attribute only applies to compound statements", parsed); break;
        case CHECK_ARGUMENT_LOOP_CONFLICT: snprintf(text, capacity, "conflicting loop attribute '%s'", parsed); break;
        case CHECK_ARGUMENT_EMPTY_ONLY: snprintf(text, capacity, "'%s' attribute only applies to empty statements", parsed); break;
        case CHECK_ARGUMENT_ASSUME_PARENTHESES: snprintf(text, capacity, "use of this expression in an 'assume' attribute requires parentheses"); break;
        case CHECK_ARGUMENT_NOT_BOOL: snprintf(text, capacity, "value of type %.*s is not contextually convertible to 'bool'", (int)literal.length, literal.pointer); break;
        case CHECK_ARGUMENT_VEC_TYPE_HINT: snprintf(text, capacity, "a non-vector or non-vectorizable scalar type is an invalid argument to attribute '%s'", parsed); break;
        case CHECK_ARGUMENT_INCOMPLETE_TYPE: snprintf(text, capacity, "incomplete type %.*s where a complete type is required", (int)literal.length, literal.pointer); break;
        case CHECK_ARGUMENT_EXPECTED_EXPRESSION: snprintf(text, capacity, "expected expression"); break;
        case CHECK_ARGUMENT_EXPRESSION_MEMBER_BASE:
            snprintf(text, capacity, "member reference base type %.*s is not a structure or union", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_EXPRESSION_MEMBER_NAME:
            snprintf(text, capacity, "no member named '%.*s' in %.*s", (int)intern_length(pp->table, (uint32_t)value), intern_bytes(pp->table, (uint32_t)value),
                     (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_EXPRESSION_SIZEOF:
            snprintf(text, capacity, "invalid application of 'sizeof' to an incomplete type %.*s", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_EXPRESSION_CALL_COUNT:
        {
            // The value: the count, the variadic bit (32) and the named only parameter's token plus 1 (from 33), whose
            // name clang's single-argument words give (check_constant_failure_error).
            const uint32_t parameter = (uint32_t)(value >> 33);
            const bool variadic = ((value >> 32) & 1) != 0;
            const bool few = (uint32_t)value < number;
            const uint32_t name = parameter != 0 && parameter - 1 < parser->tokens.count ? parser->tokens.names[parameter - 1] : 0;
            const int length = (int)intern_length(pp->table, name);
            const char* bytes = (const char*)intern_bytes(pp->table, name);
            if (name != 0 && few)
            {
                snprintf(text, capacity, "too few arguments to function call, %s argument '%.*s' %s", variadic ? "at least" : "single", length, bytes,
                         variadic ? "must be specified" : "was not specified");
            }
            else if (name != 0)
            {
                snprintf(text, capacity, "too many arguments to function call, expected single argument '%.*s', have %u arguments", length, bytes, (uint32_t)value);
            }
            else
            {
                snprintf(text, capacity, "too %s arguments to function call, expected %s%u, have %u", few ? "few" : "many", variadic ? "at least " : "", number,
                         (uint32_t)value);
            }
            break;
        }
        case CHECK_ARGUMENT_EXPRESSION_BINARY:
            snprintf(text, capacity, "invalid operands to binary expression (%.*s)", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_EXPRESSION_ARROW_POINTER:
            snprintf(text, capacity, "member reference type %.*s is not a pointer", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_EXPRESSION_INCOMPLETE_ARITHMETIC:
            snprintf(text, capacity, "arithmetic on a pointer to an incomplete type %.*s", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_ANNOTATE_CONSTANT:
            snprintf(text, capacity, "'%s' attribute requires parameter %u to be a constant expression", parsed, number);
            break;
        case CHECK_ARGUMENT_TARGET_CLONES_DEFAULT:
            snprintf(text, capacity, "'target_clones' multiversioning requires a default target");
            break;
        case CHECK_ARGUMENT_SELECTANY_LINKAGE:
            snprintf(text, capacity, "'selectany' can only be applied to data items with external linkage");
            break;
        case CHECK_ARGUMENT_TOO_LARGE:
            snprintf(text, capacity, "integer constant expression evaluates to value %" PRIu64 " that cannot be represented in a 32-bit unsigned integer type", value);
            break;
        case CHECK_ARGUMENT_TCB_CONFLICT:
        {
            // Both attributes by their names as written, and the TCB (the report's literal).
            const String8 other = check_driver_attribute_spelling(parser, pp, (uint32_t)value, false);
            snprintf(text, capacity, "attributes '%.*s(\"%.*s\")' and '%.*s(\"%.*s\")' are mutually exclusive", (int)written.length, written.pointer, (int)literal.length,
                     literal.pointer, (int)other.length, other.pointer, (int)literal.length, literal.pointer);
            break;
        }
        case CHECK_ARGUMENT_CALLBACK_NO_CALLEE: snprintf(text, capacity, "'callback' attribute specifies no callback callee"); break;
        case CHECK_ARGUMENT_CALLBACK_UNKNOWN:
            snprintf(text, capacity, "'callback' attribute argument '%.*s' is not a known function parameter", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_CALLBACK_THIS: snprintf(text, capacity, "'callback' argument at position %u references unavailable implicit 'this'", number); break;
        case CHECK_ARGUMENT_CALLBACK_INVALID_CALLEE: snprintf(text, capacity, "'callback' attribute specifies invalid callback callee"); break;
        case CHECK_ARGUMENT_CALLBACK_NOT_FUNCTION: snprintf(text, capacity, "'callback' attribute callee does not have function type"); break;
        case CHECK_ARGUMENT_CALLBACK_COUNT:
            snprintf(text, capacity, "'%s' attribute references function of type %.*s which expects %u argument%s but attribute specifies %u parameter index argument%s",
                     parsed, (int)literal.length, literal.pointer, number, number == 1 ? "" : "s", (uint32_t)value, value == 1 ? "" : "s");
            break;
        case CHECK_ARGUMENT_CALLBACK_VARIADIC: snprintf(text, capacity, "'callback' attribute callee may not be variadic"); break;
        case CHECK_ARGUMENT_CALLBACK_MULTIPLE: snprintf(text, capacity, "multiple 'callback' attributes specified"); break;
        case CHECK_ARGUMENT_SWIFT_ASYNC_KIND:
            snprintf(text, capacity, "first argument to 'swift_async' must be either 'none', 'swift_private', or 'not_swift_private'");
            break;
        case CHECK_ARGUMENT_SWIFT_ASYNC_BLOCK:
            snprintf(text, capacity, "'swift_async' completion handler parameter must have block type returning 'void', type here is %.*s", (int)literal.length,
                     literal.pointer);
            break;
        case CHECK_ARGUMENT_ERROR_DOMAIN:
            snprintf(text, capacity, "domain argument %s%.*s%sdoes not refer to global constant", number != 0 ? "'" : "", (int)literal.length, literal.pointer,
                     number != 0 ? "' " : "");
            break;
        case CHECK_ARGUMENT_DUPLICATE_ASPECT:
            snprintf(text, capacity, "duplicate aspect '%.*s' in 'modular_format' attribute", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_DIFFERENT_ARGUMENTS: snprintf(text, capacity, "attribute '%s' is already applied with different arguments", parsed); break;
        case CHECK_ARGUMENT_REQUIRES_FORMAT: snprintf(text, capacity, "'modular_format' attribute requires 'format' attribute"); break;
        case CHECK_ARGUMENT_AVAILABILITY_PLATFORM: snprintf(text, capacity, "expected a platform name, e.g., 'macos'"); break;
        case CHECK_ARGUMENT_AVAILABILITY_COMMA: snprintf(text, capacity, "expected ','"); break;
        case CHECK_ARGUMENT_AVAILABILITY_CHANGE: snprintf(text, capacity, "expected 'introduced', 'deprecated', or 'obsoleted'"); break;
        case CHECK_ARGUMENT_AVAILABILITY_REDUNDANT:
            snprintf(text, capacity, "redundant '%.*s' availability change; only the last specified change will be used", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_AVAILABILITY_EQUAL: snprintf(text, capacity, "expected '=' after '%.*s'", (int)literal.length, literal.pointer); break;
        case CHECK_ARGUMENT_AVAILABILITY_MESSAGE: snprintf(text, capacity, "expected string literal for optional message in 'availability' attribute"); break;
        case CHECK_ARGUMENT_AVAILABILITY_ENVIRONMENT_NAME: snprintf(text, capacity, "expected an environment name, e.g., 'compute'"); break;
        case CHECK_ARGUMENT_AVAILABILITY_VERSION: snprintf(text, capacity, "expected a version of the form 'major[.minor[.subminor]]'"); break;
        case CHECK_ARGUMENT_AVAILABILITY_ZERO: snprintf(text, capacity, "version number must have non-zero major, minor, or sub-minor version"); break;
        case CHECK_ARGUMENT_AVAILABILITY_STAGE:
            snprintf(text, capacity, "'%.*s' is not an availability stage; use 'introduced', 'deprecated', or 'obsoleted'", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_AVAILABILITY_ANY_APPLE:
            snprintf(text, capacity, "invalid anyAppleOS version '%.*s' in availability attribute", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_AVAILABILITY_ENVIRONMENT:
            snprintf(text, capacity, "unexpected parameter 'environment' in availability attribute, not permitted in C/C++");
            break;
        case CHECK_ARGUMENT_SOURCE_KEYWORD: snprintf(text, capacity, "expected 'language', 'defined_in', 'generated_declaration', or 'USR'"); break;
        case CHECK_ARGUMENT_SOURCE_DUPLICATE:
            snprintf(text, capacity, "duplicate '%.*s' clause in an 'external_source_symbol' attribute", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_SOURCE_EQUAL: snprintf(text, capacity, "expected '=' after %.*s", (int)literal.length, literal.pointer); break;
        case CHECK_ARGUMENT_UNEXPECTED_SEMI: snprintf(text, capacity, "unexpected ';' before ')'"); break;
        case CHECK_ARGUMENT_OBJC_VARIABLES: snprintf(text, capacity, "'%s' attribute only applies to variables", parsed); break;
        case CHECK_ARGUMENT_BETWEEN: snprintf(text, capacity, "'%s' attribute requires integer constant between 0 and %" PRIu64 " inclusive", parsed, value); break;
        case CHECK_ARGUMENT_PATCHABLE_SECTION:
            snprintf(text, capacity, "section argument to 'patchable_function_entry' attribute is not valid for this target: section must not be empty");
            break;
        case CHECK_ARGUMENT_NEON_SIZE: snprintf(text, capacity, "Neon vector size must be 64 or 128 bits"); break;
        case CHECK_ARGUMENT_BUILTIN_FUNCTION: snprintf(text, capacity, "'%s' attribute requires parameter %u to be a builtin function", parsed, number); break;
        case CHECK_ARGUMENT_BUILTIN_COUNT:
        case CHECK_ARGUMENT_BUILTIN_BOUNDS:
        case CHECK_ARGUMENT_BUILTIN_TYPES:
        {
            // diagnose_as_builtin's (#1447): its builtin by its first argument's name, the function by its name's token in
            // the value's high half, the index in its low half; and a mismatched builtin parameter's type as clang spells
            // the builtin's declaration's (cbuiltin_library_spell_parameter), with its aka where it has __size_t.
            const String8 builtin = at + 2 < count ? check_driver_attribute_spelling(parser, pp, at + 2, false) : (String8){ "", 0 };
            const uint32_t function = (uint32_t)(value >> 32);
            const String8 named = function < count ? check_driver_attribute_spelling(parser, pp, function, false) : (String8){ "", 0 };
            const uint32_t index = (uint32_t)value;
            const uint32_t error = detail & 0xffu;
            if (error == CHECK_ARGUMENT_BUILTIN_COUNT)
            {
                char takes[48];
                snprintf(takes, sizeof(takes), number == 0 ? "takes no arguments" : number == 1 ? "takes one argument" : "takes exactly %u arguments", number);
                snprintf(text, capacity, "'%s' attribute references function '%.*s', which %s", parsed, (int)builtin.length, builtin.pointer, takes);
            }
            else if (error == CHECK_ARGUMENT_BUILTIN_BOUNDS)
            {
                snprintf(text, capacity, "'%s' attribute references parameter %u, but the function '%.*s' has only %u parameters", parsed, index, (int)named.length,
                         named.pointer, number);
            }
            else
            {
                const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup(builtin.pointer, (uint32_t)builtin.length);
                const bool c99 = parser->language.standard >= 1999;
                char sugared[256];
                char plain[256];
                bool sized = false;
                bool unsugared = false;
                const bool spelled = info != NULL &&
                                     cbuiltin_library_spell_parameter(info->encoding, info->encoding_length, info->attributes, info->attributes_length, number - 1, true, c99,
                                                                      named_types, sugared, sizeof(sugared), &sized) &&
                                     cbuiltin_library_spell_parameter(info->encoding, info->encoding_length, info->attributes, info->attributes_length, number - 1, false, c99,
                                                                      named_types, plain, sizeof(plain), &unsugared);
                // A vector's clang describes after its spelling: "(vector of 8 'short' values)".
                char vector[96];
                const bool described = spelled && cbuiltin_library_vector_description(info->encoding, info->encoding_length, number - 1, vector, sizeof(vector));
                char builtin_type[700];
                snprintf(builtin_type, sizeof(builtin_type), sized ? "'%s' (aka '%s')" : described ? "'%s' %s" : "'%s'", spelled ? sugared : "", sized ? plain : vector);
                snprintf(text, capacity, "'%s' attribute parameter types do not match: parameter %u of function '%.*s' has type %.*s, but parameter %u of function '%.*s' has type %s",
                         parsed, index, (int)named.length, named.pointer, (int)literal.length, literal.pointer, number, (int)builtin.length, builtin.pointer, builtin_type);
            }
            break;
        }
        case CHECK_ARGUMENT_DIAGNOSTIC_TYPE: snprintf(text, capacity, "invalid diagnostic type for '%s'; use \"error\" or \"warning\" instead", parsed); break;
        case CHECK_ARGUMENT_WARNING_GROUP: snprintf(text, capacity, "unknown warning group '%.*s'", (int)literal.length, literal.pointer); break;
        case CHECK_ARGUMENT_NEVER_CONSTANT: snprintf(text, capacity, "'%s' attribute expression never produces a constant expression", parsed); break;
        case CHECK_ARGUMENT_SOURCE_STRING:
            snprintf(text, capacity, "expected string literal for %s in 'external_source_symbol' attribute",
                     number == 0 ? "language name" : number == 1 ? "source container name" : "USR");
            break;
        case CHECK_ARGUMENT_MATRIX_DISABLED: snprintf(text, capacity, "matrix types extension is disabled. Pass -fenable-matrix to enable it"); break;
        case CHECK_ARGUMENT_KERNEL_FUNCTIONS: snprintf(text, capacity, "'%s' attribute only applies to functions", parsed); break;
        case CHECK_ARGUMENT_VARIADIC_CONVENTION: snprintf(text, capacity, "variadic function cannot use %s calling convention", check_input_convention_name(number)); break;
        case CHECK_ARGUMENT_NO_PROTOTYPE_CONVENTION:
            snprintf(text, capacity, "function with no prototype cannot use the %s calling convention", check_input_convention_name(number));
            break;
        case CHECK_ARGUMENT_SWIFT_PARAMETER_TYPE:
            snprintf(text, capacity, "'%s' parameter must have pointer%s type; type here is %.*s", check_input_parameter_abi_name(number & 0xfu),
                     (number & BUSTER_CHECK_SWIFT_POINTER_POINTER) != 0 ? " to unqualified pointer" : "", (int)literal.length, literal.pointer);
            break;
        case CHECK_ARGUMENT_SWIFT_PARAMETER_CONVENTION:
            snprintf(text, capacity, "'%s' parameter can only be used with swiftcall%s calling convention%s", check_input_parameter_abi_name(number & 0xfu),
                     (number & BUSTER_CHECK_SWIFT_ONLY) != 0 ? "" : " or swiftasynccall", (number & BUSTER_CHECK_SWIFT_ONLY) != 0 ? "s" : "");
            break;
        case CHECK_ARGUMENT_SWIFT_INDIRECT_FIRST: snprintf(text, capacity, "'swift_indirect_result' parameters must be first parameters of function"); break;
        case CHECK_ARGUMENT_SWIFT_ERROR_AFTER_CONTEXT: snprintf(text, capacity, "'swift_error_result' parameter must follow 'swift_context' parameter"); break;
        case CHECK_ARGUMENT_SWIFT_PARAMETER_CONFLICT:
            snprintf(text, capacity, "%s and '%s' attributes are not compatible", check_input_parameter_abi_name(number & 0xffu), check_input_parameter_abi_name(number >> 8 & 0xffu));
            break;
        default: snprintf(text, capacity, "invalid attribute argument"); break;
    }
}

// clang's musttail attribute (#147) as clang's messages spell it, from its first token (`token`: GNU's name, or the
// scope's of clang::musttail): 'musttail' or 'clang::musttail', or as it is `written`, its name's own spelling
// (__musttail__ too), which clang's reading of an attribute's arguments says. Into `text`, `capacity` bytes.
static void check_driver_musttail_spelling(const Parser* parser, const Preprocessor* pp, uint32_t token, bool written, char* text, uint32_t capacity)
{
    const bool scoped = token + 3 < parser->tokens.count && parser->tokens.kinds[token + 1] == PARSE_KIND_COLON;
    const uint32_t name = token < parser->tokens.count ? parser->tokens.names[scoped ? token + 3 : token] : 0;
    const bool spelled = written && name != 0;
    snprintf(text, capacity, "%s%.*s", scoped ? "clang::" : "", spelled ? (int)intern_length(pp->table, name) : 8,
             spelled ? (const char*)intern_bytes(pp->table, name) : "musttail");
}

// The message of a musttail return's error (c_check_musttail.c), clang's, in `arena`: the attribute spelled as clang's
// messages spell it, and the callee's name where the message names one. Null where the arena is full.
static const char* check_driver_musttail_message(const Parser* parser, const Preprocessor* pp, const CheckInput* input,
                                                 const CheckMusttailDiagnostic* diagnostic, Arena* arena)
{
    char spelling[96];
    check_driver_musttail_spelling(parser, pp, input->musttails[diagnostic->musttail].name, false, spelling, sizeof(spelling));
    const uint32_t name = diagnostic->callee < parser->tokens.count ? parser->tokens.names[diagnostic->callee] : 0;
    const int name_length = name != 0 ? (int)intern_length(pp->table, name) : 0;
    const char* name_bytes = name != 0 ? (const char*)intern_bytes(pp->table, name) : "";
    const uint64_t capacity = 192 + (uint64_t)name_length;
    char* text = arena_push(arena, capacity, 1);
    if (text != NULL)
    {
        switch (diagnostic->error)
        {
            case CHECK_MUSTTAIL_CALL: snprintf(text, capacity, "'%s' attribute requires that the return value is the result of a function call", spelling); break;
            case CHECK_MUSTTAIL_BUILTIN: snprintf(text, capacity, "construct is not yet supported by the type checker"); break;
            case CHECK_MUSTTAIL_PROTOTYPE: snprintf(text, capacity, "'%s' attribute requires that both caller and callee functions have a prototype", spelling); break;
            case CHECK_MUSTTAIL_CONVENTION:
                snprintf(text, capacity, "cannot perform a tail call to function%s%.*s%s because it uses an incompatible calling convention", name != 0 ? " '" : "",
                         name_length, name_bytes, name != 0 ? "'" : "");
                break;
            case CHECK_MUSTTAIL_VARIADIC: snprintf(text, capacity, "'%s' attribute may not be used with variadic functions", spelling); break;
            case CHECK_MUSTTAIL_NORETURN: snprintf(text, capacity, "'%s' attribute may not be used with no-return-attribute functions", spelling); break;
            case CHECK_MUSTTAIL_SIGNATURE:
                snprintf(text, capacity, "cannot perform a tail call to function%s%.*s%s because its signature is incompatible with the calling function",
                         name != 0 ? " '" : "", name_length, name_bytes, name != 0 ? "'" : "");
                break;
            case CHECK_MUSTTAIL_SCOPE: snprintf(text, capacity, "cannot perform a tail call from this return statement"); break;
        }
    }
    return text;
}

// x86's interrupt's errors (#147, clang's err_anyx86_interrupt_attribute on x86-64), by which of the handler's rules the
// declaration breaks (CHECK_INPUT_INTERRUPT_HANDLER's detail, check_input_interrupt_handler).
static const char* check_driver_interrupt_message(uint32_t detail)
{
    static const char* const messages[] =
    {
        "x86-64 'interrupt' attribute only applies to functions that have a 'void' return type",
        "x86-64 'interrupt' attribute only applies to functions that have only a pointer parameter optionally followed by an integer parameter",
        "x86-64 'interrupt' attribute only applies to functions that have a pointer as the first parameter",
        "x86-64 'interrupt' attribute only applies to functions that have a 'unsigned long' type as the second parameter",
    };
    const uint32_t rule = detail & ~BUSTER_CHECK_INTERRUPT_UNLOCATED;
    return messages[rule < BUSTER_ARRAY_LENGTH(messages) ? rule : 0];
}

static const char* check_driver_input_report_message(const CheckInput* input, const CheckInputReport* report)
{
    const CheckInputLoopDiagnostic* loop = input->loop_diagnostic;
    const CheckInputSectionDiagnostic* section = check_input_section_report(input, report);
    const CheckInputOptimizeDiagnostic* optimize = check_input_optimize_report(input, report);
    const CheckInputFpPragma* fp = check_input_fp_report(input, report);
    const CheckInputMsPragma* layout = check_input_ms_report(input, report);
    return loop != NULL && report->pragma == loop->pragma && report->token == loop->position ? loop->text :
           section != NULL ? section->text.pointer : optimize != NULL ? optimize->text.pointer : fp != NULL ? check_input_fp_error_message(fp->error) :
           layout != NULL ? "this pragma cannot appear in struct declaration" :
           report->message == CHECK_INPUT_INTERRUPT_HANDLER ? check_driver_interrupt_message(report->detail) :
           report->message == CHECK_INPUT_DIRECT_INITIALIZER_ARRAY && report->detail == CHECK_INPUT_ARRAY_LIST_WIDE ?
           "array initializer must be an initializer list or wide string literal" :
           report->message == CHECK_INPUT_DIRECT_INITIALIZER_ARRAY && report->detail == CHECK_INPUT_ARRAY_LIST_ONLY ?
           "array initializer must be an initializer list" :
           report->message == CHECK_INPUT_TYPE_SPECIFIER_MISSING && report->detail == 0 ?
           "type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int [-Wimplicit-int]" :
           report->message == CHECK_INPUT_TYPE_SPECIFIER_MISSING && report->detail == 1 ? "a type specifier is required for all declarations" :
           report->message == CHECK_INPUT_ATTRIBUTE_ARGUMENT && (report->detail & 0xffu) == CHECK_ARGUMENT_ALIGNMENT_POWER ?
           check_driver_alignment_power : check_driver_input_message(report->message);
}

// The function whose body holds `token` as clang's qualified names spell it before a block's name (#1826): its name and
// parameter types, then `::` (`g(int, char *)::`, `k()::` for a `(void)` prototype), from the function's type as printed
// with its name; empty at file scope or where the type doesn't print so. Diagnostics only.
static String8 check_driver_function_scope(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types, uint32_t token, Arena* scratch)
{
    String8 result = { 0 };
    const uint32_t count = parser->tokens.count;
    uint32_t close = BUSTER_CHECK_NONE;
    for (uint32_t at = token, steps = 0; at < count && steps < count && close == BUSTER_CHECK_NONE; steps += 1)
    {
        close = parser->tree.roles[at] == PARSE_ROLE_CLOSE_BODY ? at : close;
        at = parser->tree.parents[at] != at ? parser->tree.parents[at] : count;
    }
    uint32_t open = BUSTER_CHECK_NONE;
    uint32_t depth = 0;
    for (uint32_t at = close; close < count && at-- > 0 && open == BUSTER_CHECK_NONE;)
    {
        const uint8_t kind = parser->tokens.kinds[at];
        depth += kind == PARSE_KIND_R_BRACE;
        open = kind == PARSE_KIND_L_BRACE && depth == 0 ? at : open;
        depth -= kind == PARSE_KIND_L_BRACE && depth != 0;
    }
    const uint32_t function = open < count ? check_sparse_get(input->body_declarations, open) : BUSTER_CHECK_NONE;
    const TypeId type = function < input->token_count && input->declared_types != NULL ? input->declared_types[function] : 0;
    const uint32_t name = function < count ? parser->tokens.names[function] : 0;
    const String8 spelled = name != 0 ? (String8){ .pointer = (char*)intern_bytes(pp->table, name), .length = intern_length(pp->table, name) } : (String8){ 0 };
    const uint32_t options = pp->options.language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0;
    const String8 printed = type != 0 && type_is_valid(types, type) && spelled.length != 0 ? type_print(types, scratch, type, spelled, NULL, options) : (String8){ 0 };
    // The name's own parameter list: the group right after the name in the declarator as printed.
    uint64_t at = 0;
    bool found = false;
    // (The printer may parenthesize the name: `void (g)(void)`.)
    for (uint64_t i = 0; !found && printed.length > spelled.length && i + spelled.length < printed.length; i += 1)
    {
        uint64_t after = i + spelled.length;
        const bool grouped = i != 0 && printed.pointer[i - 1] == '(' && after < printed.length && printed.pointer[after] == ')';
        after += grouped ? 1 : 0;
        found = memcmp(printed.pointer + i, spelled.pointer, spelled.length) == 0 && after < printed.length && printed.pointer[after] == '(' &&
                (i == 0 || !(printed.pointer[i - 1] == '_' || (printed.pointer[i - 1] >= 'a' && printed.pointer[i - 1] <= 'z') ||
                             (printed.pointer[i - 1] >= 'A' && printed.pointer[i - 1] <= 'Z') || (printed.pointer[i - 1] >= '0' && printed.pointer[i - 1] <= '9')));
        at = found ? after : at;
    }
    uint64_t end = at;
    uint32_t nesting = 0;
    for (uint64_t i = at; found && i < printed.length && end == at; i += 1)
    {
        nesting += printed.pointer[i] == '(';
        nesting -= printed.pointer[i] == ')';
        end = nesting == 0 ? i + 1 : end;
    }
    if (found && end > at)
    {
        const bool bare = end - at == 6 && memcmp(printed.pointer + at, "(void)", 6) == 0;
        const uint64_t length = spelled.length + (bare ? 2 : end - at) + 2;
        char* text = BUSTER_ARENA_PUSH_ARRAY(scratch, char, length + 1);
        if (text != NULL)
        {
            snprintf(text, length + 1, "%.*s%.*s::", (int)spelled.length, spelled.pointer, bare ? 2 : (int)(end - at), bare ? "()" : printed.pointer + at);
            result = (String8){ .pointer = text, .length = length };
        }
    }
    return result;
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
        case CHECK_MESSAGE_STATIC_INITIALIZER: result = "initializer element is not a compile-time constant"; break;
    }
    return result;
}

static const char* check_driver_initializer_message(CheckInitStatus status)
{
    const char* result = "invalid initializer";
    switch (status)
    {
        case CHECK_INIT_MEMBER: result = "field designator does not refer to any field in the record type"; break;
        case CHECK_INIT_NON_CONSTANT: result = "initializer element is not a compile-time constant"; break;
        case CHECK_INIT_UNSUPPORTED: result = "initializer construct is not supported"; break;
        case CHECK_INIT_RESOURCE: result = "initializer checking capacity exhausted"; break;
        case CHECK_INIT_INVALID:
        case CHECK_INIT_CHECK_FAILED:
        case CHECK_INIT_ARRAY_DESIGNATOR:
        case CHECK_INIT_FIELD_DESIGNATOR:
        case CHECK_INIT_NON_AGGREGATE_DESIGNATOR:
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
    // An error clang reports again at its place, where the reports' walk keeps every one rather than a token's first: a
    // musttail return's for each cleanup scope it leaves, a braced static subobject's non-constant value after its
    // conversion's (#147).
    bool repeated;
    // A note at `note_token`, where `note` isn't null: a duplicate case's previous case (#1447).
    uint32_t note_token;
    const char* note;
    // One of clang's warnings that default to errors (check_driver_report_default_error, #1969): an initializer's
    // subobject's conversion between an integer and a pointer or between pointers, which its scope doesn't count; its
    // source's node plus one (0 for none) and its target.
    uint32_t default_source;
    TypeId default_target;
};

// A conversion clang warns of with an error by default and makes (#1969, check_driver_report_default_error), whose source a
// rejected unit's view casts (CHECK_DRIVER_CAST_*) rather than leaving its part out: the error's token, the source's
// first and last tokens, and whether its target is a pointer.
typedef struct CheckDriverConversionSite CheckDriverConversionSite;
struct CheckDriverConversionSite
{
    uint32_t token;
    uint32_t first;
    uint32_t last;
    bool pointer;
};

// Room for `more` diagnostics after the `count` of `*list`, which `*capacity` holds, and for the few single ones the
// driver adds after a list (BUSTER_CHECK_DRIVER_ADDITIONAL_SINGLES): a bigger list in `arena` with them copied, where it
// hasn't. False, and the list as it was, where the arena has none.
#define BUSTER_CHECK_DRIVER_ADDITIONAL_SINGLES 8u
static bool check_driver_additional_room(CheckDriverDiagnostic** list, uint32_t* capacity, uint32_t count, uint64_t more, Arena* arena)
{
    const uint64_t needed = (uint64_t)count + more + BUSTER_CHECK_DRIVER_ADDITIONAL_SINGLES;
    bool result = needed <= *capacity;
    if (!result && needed <= UINT32_MAX)
    {
        const uint32_t grown = (uint32_t)needed;
        CheckDriverDiagnostic* pushed = BUSTER_ARENA_PUSH_ARRAY(arena, CheckDriverDiagnostic, grown);
        if (pushed != NULL)
        {
            memcpy(pushed, *list, (uint64_t)count * sizeof(**list));
            *list = pushed;
            *capacity = grown;
            result = true;
        }
    }
    return result;
}

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

// A warning at a declarator or an operator `token` whose operand holds a typo's corrected use (#1447): clang gives the
// correction's error as it reads the operand, and the warning once the declarator's initializer or the operator is
// complete, so it is published after that use. The operand runs to the first `,` or `;` outside the brackets opened
// after the token, or to the bracket that encloses it. Only a unit with a correction looks.
static uint32_t check_driver_after_corrections(const Parser* parser, const Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t publication,
                                               uint32_t publication_end)
{
    const uint32_t count = parser->tokens.count;
    uint32_t after = 0;
    for (uint32_t i = 0; token < count && i < input->corrected_count; i += 1)
    {
        const uint32_t used = input->corrected_tokens[i];
        bool inside = used > token && used < count;
        int32_t depth = 0;
        for (uint32_t t = token + 1; inside && t < used; t += 1)
        {
            const uint8_t kind = parser->tokens.kinds[t];
            depth += kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE ? 1 :
                     kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE ? -1 : 0;
            inside = depth > 0 || (depth == 0 && kind != PARSE_KIND_COMMA && kind != PARSE_KIND_SEMI);
        }
        after = inside ? used + 1 : after;
    }
    const uint32_t later = after != 0 ? check_driver_publication_position(parser, pp, after, publication_end) : 0;
    return later > publication ? later : publication;
}

// Clang defers runtime-behavior warnings until the function is complete, including the lookahead after its closing
// brace. Required constant evaluation diagnoses immediately instead. Follow only proven parser parents for a
// sparse warning; no clean unit acquires a token-wide completion column.
// The end of the file-scope declarator initializer `token` is in (#1447), or BUSTER_CHECK_NONE: the parse tree's climb
// reaches the declarator's `=` from its right, past a brace initializer's `}` or a scalar one's root, whose graph node's
// end it is, outside a function body and an enumeration (whose values clang evaluates as constants, warning of no count).
// An assignment's `=` shares the role; outside a function, one is only in an unevaluated operand, which warns of none.
static uint32_t check_driver_file_initializer_end(const Parser* parser, const CheckInput* input, uint32_t token)
{
    uint32_t end = BUSTER_CHECK_NONE;
    uint32_t child = BUSTER_CHECK_NONE;
    bool outside = true;
    uint32_t at = token;
    for (uint32_t steps = 0; outside && at < parser->tokens.count && steps < parser->tokens.count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[at];
        outside = role != PARSE_ROLE_CLOSE_BODY && role != PARSE_ROLE_CLOSE_ENUM && role != PARSE_ROLE_OPEN_BODY;
        const uint32_t parent = parser->tree.parents[at];
        const bool top = parent >= parser->tokens.count || parent == at;
        if (child == BUSTER_CHECK_NONE && !top && at > parent && parser->tokens.kinds[parent] == PARSE_KIND_EQUAL)
        {
            child = at;
        }
        at = top ? parser->tokens.count : parent;
    }
    if (outside && child != BUSTER_CHECK_NONE)
    {
        const uint32_t node = child < input->token_count && input->token_nodes != NULL ? input->token_nodes[child] : BUSTER_CHECK_NONE;
        end = parser->tree.roles[child] == PARSE_ROLE_CLOSE_INITIALIZER ? child + 1u :
              node < input->graph.count && input->graph.source_positions != NULL && input->graph.source_positions[node] <= parser->tokens.count ?
              input->graph.source_positions[node] : BUSTER_CHECK_NONE;
    }
    return end;
}

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
    const uint32_t counts = CHECK_WARNING_SHIFT_COUNT_NEGATIVE | CHECK_WARNING_SHIFT_COUNT_OVERFLOW;
    const uint32_t declarator = !completed && (warning & counts) != 0 ?
                                check_driver_file_initializer_end(parser, input, token) : BUSTER_CHECK_NONE;
    if (declarator != BUSTER_CHECK_NONE)
    {
        // A file-scope initializer's folded shift count (#1447): clang's DiagRuntimeBehavior queues it to the declarator's
        // end, after that initializer's literal conversions and before its constant conversions.
        boundary = declarator;
        *phase = PP_DIAGNOSTIC_RUNTIME;
    }
    else if (!completed && (warning & runtime) != 0 && (constant & warning) == 0)
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
#include "c_check_semantic_diagnostic.c"

// Each failing asm statement's error (check_asms_diagnose_all, #1447) in clang's words, after the unit's `*count`
// additional diagnostics, before `before`; false where the diagnosis lists none, for the caller's generic one.
static bool check_driver_asm_errors(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, TypeTable* types,
                                    uint32_t before, CheckDriverDiagnostic** additional, uint32_t* capacity, uint32_t* count, Arena* output, Arena* scratch)
{
    CheckAsmDiagnostic diagnostic = { 0 };
    const bool result = check_asms_diagnose_all(&diagnostic, input, stream, types, before, output, scratch) && diagnostic.failure_count != 0 &&
                        check_driver_additional_room(additional, capacity, *count, diagnostic.failure_count, output);
    for (uint32_t i = 0; result && i < diagnostic.failure_count; i += 1)
    {
        const CheckAsmFailure* failure = diagnostic.failures + i;
        PPOutputDiagnosticNote note = { 0 };
        const char* message = check_semantic_asm(parser, pp, input, types, failure, &note, output);
        // A cast's at the lvalue it casts, or at the `(` of the cast that changes its value.
        const CheckGraph* graph = &input->graph;
        uint32_t located = failure->token;
        if (failure->error == CHECK_ASM_ERROR_CAST && failure->node < graph->count)
        {
            located = graph->tokens[failure->node];
            const uint32_t close = located;
            while (graph->ops[failure->node] == CHECK_OP_CAST && located != 0 &&
                   !(parser->tokens.kinds[located] == PARSE_KIND_L_PAREN && parser->tree.parents[located] == close))
            {
                located -= 1;
            }
        }
        (*additional)[(*count)++] = (CheckDriverDiagnostic){
            .token = located,
            .message = message,
            .position = failure->token,
            .note_token = note.message != NULL ? note.token : BUSTER_CHECK_NONE,
            .note = note.message,
        };
    }
    return result;
}
#include "c_check_literal_diagnostic.c"
#include "c_check_common_warnings.c"

// Initializer failures retain the actual selected record, not merely the root declaration's type. The same
// payload reaches here from early array inference and from final/damaged-unit initializer planning. A designator of the
// wrong kind (`status` CHECK_INIT_ARRAY_DESIGNATOR, CHECK_INIT_FIELD_DESIGNATOR or CHECK_INIT_NON_AGGREGATE_DESIGNATOR, #2202)
// names its current object's type, spelled by the typedef at `spelling` where it isn't 0.
static const char* check_driver_initializer_field_message(const Parser* parser, Preprocessor* pp, const CheckInput* input,
                                                         const TypeTable* types, CheckInitStatus status, TypeId type, uint32_t spelling, uint32_t token,
                                                         Arena* scratch)
{
    CheckWarningSpelling spell = { 0 };
    const String8 printed = check_driver_warning_type(parser, pp, input, types, &spell, scratch, type, spelling);
    const uint32_t name = token < parser->tokens.count ? parser->tokens.names[token] : 0;
    const char* result = NULL;
    if (printed.length == 0)
    {
        result = NULL;
    }
    else if (status == CHECK_INIT_ARRAY_DESIGNATOR)
    {
        result = check_warning_text(scratch, "array designator cannot initialize non-array type %.*s", (int)printed.length, printed.pointer);
    }
    else if (status == CHECK_INIT_NON_AGGREGATE_DESIGNATOR)
    {
        result = check_warning_text(scratch, "initialization of non-aggregate type %.*s with a designated initializer list", (int)printed.length, printed.pointer);
    }
    else if (status == CHECK_INIT_FIELD_DESIGNATOR)
    {
        result = check_warning_text(scratch, "field designator cannot initialize a non-struct, non-union type %.*s", (int)printed.length, printed.pointer);
    }
    else
    {
        result = check_warning_text(scratch, "field designator '%.*s' does not refer to any field in type %.*s", (int)intern_length(pp->table, name),
                                    intern_bytes(pp->table, name), (int)printed.length, printed.pointer);
    }
    return result;
}

static bool check_driver_literal(Preprocessor* pp, const Parser* parser, uint32_t token, uint32_t source, uint32_t publication, Arena* scratch);
static bool check_driver_literal_escapes(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t publication, Arena* scratch);

// One escape sequence's diagnostic (check_literal_unevaluated_escapes', check_literal_evaluated_escapes'), at its
// backslash in `token`'s spelling, published where `publication` puts it: an unknown escape's -Wunknown-escape-sequence
// warning as the warning options and `input`'s pragmas map it, or an error.
static void check_driver_escape(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, String8 spelling, const CheckEscapeDiagnostic* diagnostic,
                                uint32_t publication)
{
    const uint32_t source = parser->tokens.sources[token];
    const uint32_t offset = diagnostic->offset;
    const char character = (char)diagnostic->character;
    // A macro argument's literal: its escape's diagnostics show the argument (#1447, Preprocessor.diagnostic_arguments).
    pp->diagnostic_arguments = true;
    switch (diagnostic->kind)
    {
        case CHECK_ESCAPE_DIAGNOSTIC_UNKNOWN:
        {
            const CheckWarningSeverity severity = check_common_severity(pp, parser, input, token, CHECK_WARNING_ID_UNKNOWN_ESCAPE_SEQUENCE, false);
            // "[-Werror,-Wflag]" while the promoted warning is an ordinary error, "[-Wflag]" once it is fatal.
            const bool error = severity == CHECK_WARNING_ERROR;
            const PPSeverity disposition = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL : error ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
            // clang prints a character that isn't printable as its value, sign-extended from char, in hexadecimal.
            const bool printable = diagnostic->character >= 0x20 && diagnostic->character < 0x7f;
            char escaped[16];
            if (printable)
            {
                snprintf(escaped, sizeof(escaped), "%c", character);
            }
            else
            {
                snprintf(escaped, sizeof(escaped), "x%X", (unsigned)(int32_t)(int8_t)diagnostic->character);
            }
            if (severity != CHECK_WARNING_IGNORE)
            {
                pp_diagnose_output_offset_at(pp, disposition, source, offset, publication, "unknown escape sequence '\\%s' [%s-Wunknown-escape-sequence]", escaped,
                                             error ? "-Werror," : "");
            }
            break;
        }
        case CHECK_ESCAPE_DIAGNOSTIC_NONSTANDARD:
        case CHECK_ESCAPE_DIAGNOSTIC_DELIMITED:
        case CHECK_ESCAPE_DIAGNOSTIC_NAMED:
        {
            // -pedantic's warnings (#1447), off by default.
            const bool nonstandard = diagnostic->kind == CHECK_ESCAPE_DIAGNOSTIC_NONSTANDARD;
            const CheckWarningSeverity severity = check_common_severity(pp, parser, input, token, nonstandard ? CHECK_WARNING_ID_NONSTANDARD_ESCAPE :
                                                                                                                CHECK_WARNING_ID_DELIMITED_ESCAPE, true);
            const bool error = severity == CHECK_WARNING_ERROR;
            const PPSeverity disposition = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL : error ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
            if (severity != CHECK_WARNING_IGNORE && nonstandard)
            {
                pp_diagnose_output_offset_at(pp, disposition, source, offset, publication, "use of non-standard escape character '\\%c' [%s-Wpedantic]", character,
                                             error ? "-Werror," : "");
            }
            else if (severity != CHECK_WARNING_IGNORE)
            {
                pp_diagnose_output_offset_at(pp, disposition, source, offset, publication, "%s escape sequences are a %s extension [%s-Wdelimited-escape-sequence-extension]",
                                             diagnostic->kind == CHECK_ESCAPE_DIAGNOSTIC_NAMED ? "named" : "delimited",
                                             diagnostic->kind == CHECK_ESCAPE_DIAGNOSTIC_NAMED ? "Clang" : "C2y", error ? "-Werror," : "");
            }
            break;
        }
        case CHECK_ESCAPE_DIAGNOSTIC_HEX_DIGITS:
            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "\\x used with no following hex digits");
            break;
        case CHECK_ESCAPE_DIAGNOSTIC_HEX_RANGE:
        case CHECK_ESCAPE_DIAGNOSTIC_OCTAL_RANGE:
            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "%s escape sequence out of range",
                                         diagnostic->kind == CHECK_ESCAPE_DIAGNOSTIC_HEX_RANGE ? "hex" : "octal");
            break;
        case CHECK_ESCAPE_DIAGNOSTIC_EMPTY:
            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "delimited escape sequence cannot be empty");
            break;
        case CHECK_ESCAPE_DIAGNOSTIC_DIGIT:
            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "invalid digit '%c' in escape sequence", character);
            break;
        case CHECK_ESCAPE_DIAGNOSTIC_OPEN_BRACE:
            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "expected '{' after '\\%c' escape sequence", character);
            break;
        case CHECK_ESCAPE_DIAGNOSTIC_CLOSE_BRACE:
            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "expected '}'");
            break;
        case CHECK_ESCAPE_DIAGNOSTIC_UCN_DIGITS:
            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "\\%c used with no following hex digits", character);
            break;
        case CHECK_ESCAPE_DIAGNOSTIC_UCN_INCOMPLETE:
            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "incomplete universal character name");
            break;
        case CHECK_ESCAPE_DIAGNOSTIC_UCN_INVALID:
            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "invalid universal character");
            break;
        case CHECK_ESCAPE_DIAGNOSTIC_UCN_CONTROL:
            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "universal character name refers to a control character");
            break;
        case CHECK_ESCAPE_DIAGNOSTIC_UCN_NAME:
        {
            // The name, from past its "\\N{" to its '}' (which check_literal_unevaluated_ucn found before a line break).
            const uint8_t* bytes = (const uint8_t*)spelling.pointer;
            const uint32_t name = offset + 3;
            uint32_t close = name;
            while (close < spelling.length && bytes[close] != '}')
            {
                close += 1;
            }
            UnicodeNameDiagnosis unicode;
            unicode_name_diagnose(bytes + name, close - name, true, &unicode);
            PPOffsetNote notes[BUSTER_UNICODE_NAME_NOTES];
            for (uint32_t n = 0; n < unicode.note_count; n += 1)
            {
                notes[n] = (PPOffsetNote){ name + unicode.notes[n].offset, unicode.notes[n].message };
            }
            pp_diagnose_output_offset_notes(pp, PP_SEVERITY_ERROR, source, name, publication, notes, unicode.note_count, "%s", unicode.error);
            break;
        }
        case CHECK_ESCAPE_DIAGNOSTIC_UCN_BASIC:
            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "character '%c' cannot be specified by a universal character name",
                                         character);
            break;
        default:
            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "invalid escape sequence '%.*s' in an unevaluated string literal",
                                         (int)diagnostic->length, spelling.pointer + offset);
            break;
    }
    pp->diagnostic_arguments = false;
}

// The encoding prefixes of an unevaluated string literal's tokens from `first` to `end` (#1447): each one's
// -Winvalid-unevaluated-string warning at it, as the warning options and `input`'s pragmas map it, with the macro argument
// the token came from.
static void check_driver_unevaluated_prefixes(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t first, uint32_t end, uint32_t publication,
                                              Arena* scratch)
{
    for (uint32_t token = first; token < end && token < parser->tokens.count && !pp->fatal; token += 1)
    {
        const uint64_t mark = scratch->length;
        const String8 spelling = check_warning_string_spelling(parser, pp, token, scratch);
        const uint32_t prefix = check_literal_encoding_prefix((const uint8_t*)spelling.pointer, (uint32_t)spelling.length);
        const char* written = prefix == 2 ? "u8" : prefix != 0 && spelling.pointer[0] == 'L' ? "L" : prefix != 0 && spelling.pointer[0] == 'U' ? "U" : "u";
        arena_pop_to(scratch, mark);
        const CheckWarningSeverity severity = prefix != 0 ? check_common_severity(pp, parser, input, token, CHECK_WARNING_ID_INVALID_UNEVALUATED_STRING, false) :
                                                            CHECK_WARNING_IGNORE;
        const bool error = severity == CHECK_WARNING_ERROR;
        const PPSeverity disposition = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL : error ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
        if (severity != CHECK_WARNING_IGNORE)
        {
            pp->diagnostic_arguments = true;
            pp_diagnose_output_offset_at(pp, disposition, parser->tokens.sources[token], 0, publication,
                                         "encoding prefix '%s' on an unevaluated string literal has no effect [%s-Winvalid-unevaluated-string]", written,
                                         error ? "-Werror," : "");
            pp->diagnostic_arguments = false;
        }
    }
}

// The escape sequences' diagnostics of an unevaluated string literal's tokens from `first` to `end` (#147), each at its
// escape's backslash, in clang's order, published where `publication` puts them: an unknown escape's
// -Wunknown-escape-sequence warning as the warning options and `input`'s pragmas map it, then the errors
// (check_literal_unevaluated_escapes). The tokens before `first` have no escape clang refuses: none set its error state.
static void check_driver_escapes(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t first, uint32_t end, uint32_t publication, Arena* scratch)
{
    // Clang's unevaluated string literal warns of each token's encoding prefix first (#1447), its tokens before `first`
    // (adjacent string literals, which concatenate) too.
    uint32_t begin = first;
    while (begin > 0 && begin <= parser->tokens.count && parser->tokens.kinds[begin - 1] == PARSE_KIND_STRING)
    {
        begin -= 1;
    }
    check_driver_unevaluated_prefixes(parser, pp, input, begin, end, publication, scratch);
    bool had_error = false;
    for (uint32_t token = first; token < end && token < parser->tokens.count && !pp->fatal; token += 1)
    {
        const uint64_t mark = scratch->length;
        const String8 spelling = check_warning_string_spelling(parser, pp, token, scratch);
        const uint32_t length = (uint32_t)spelling.length;
        CheckEscapeDiagnostic* diagnostics = length != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckEscapeDiagnostic, BUSTER_CHECK_ESCAPE_DIAGNOSTICS(length)) : NULL;
        const uint32_t count = diagnostics != NULL ? check_literal_unevaluated_escapes((const uint8_t*)spelling.pointer, length, pp->options.language.standard >= 2023, true, diagnostics, &had_error) : 0;
        for (uint32_t i = 0; i < count && !pp->fatal; i += 1)
        {
            check_driver_escape(parser, pp, input, token, spelling, diagnostics + i, publication);
        }
        arena_pop_to(scratch, mark);
    }
}

// A CHECK_INPUT_UNEVALUATED_ESCAPE report's diagnostics (#147): its literal's escapes', from its token to its `detail`
// (check_driver_escapes). Then, where its `value` gives a C23 standard attribute's `(` plus one, clang's parser's error
// there of the attribute before it (check_input_attribute_standard_previous), naming the dropped one (the report's
// `attribute`) as written: "attribute 'X' cannot have an argument list" with bit 32, else "parentheses must be omitted
// if 'X' attribute's argument list is empty".
static void check_driver_unevaluated_escapes(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckInputReport* report, uint32_t publication,
                                             Arena* scratch)
{
    check_driver_escapes(parser, pp, input, report->token, report->detail, publication, scratch);
    const uint32_t open = (uint32_t)report->value - 1;
    if (report->value != 0 && open < parser->tokens.count && report->attribute < parser->tokens.count && !pp->fatal)
    {
        const uint32_t name = parser->tokens.names[report->attribute];
        const int length = (int)intern_length(pp->table, name);
        const char* written = (const char*)intern_bytes(pp->table, name);
        if ((report->value >> 32) != 0)
        {
            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, parser->tokens.sources[open], publication, "attribute '%.*s' cannot have an argument list", length, written);
        }
        else
        {
            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, parser->tokens.sources[open], publication,
                                  "parentheses must be omitted if '%.*s' attribute's argument list is empty", length, written);
        }
    }
}

// A bit-precise constant's suffix before C23 among the numeric constants a failed parse left unchecked (#1447): the
// warning the checked ones' reader gives (check_input_bit_int_suffix), as the command line maps it.
static void check_driver_bit_int_suffix(Preprocessor* pp, const Parser* parser, uint32_t token, Arena* scratch)
{
    static const CheckInput none = { 0 };
    const uint32_t source = parser->tokens.sources[token];
    PPTokens* output = &pp->output;
    if (parser->language.standard < 2023 && source < output->count && output->kinds[source] != BUSTER_PP_KIND_EMBED && output->kinds[source] != BUSTER_PP_KIND_INTEGER)
    {
        const uint64_t mark = scratch->length;
        const uint64_t capacity = (uint64_t)output->values[source] - output->locations[source] + 1;
        uint8_t* buffer = arena_push(scratch, capacity, 1);
        const uint8_t* bytes = 0;
        const uint32_t length = buffer != 0 ? pp_spelling(pp, output->kinds[source], output->values[source], output->locations[source], buffer, &bytes) : 0;
        CheckLiteral literal = { 0 };
        if (length != 0)
        {
            check_literal_number(bytes, length, scratch, &literal);
        }
        const CheckWarningSeverity severity = literal.valid && literal.bit_width != 0 ? check_common_severity(pp, parser, &none, token, CHECK_WARNING_ID_C23_PARAMETER, false) :
                                                                                         CHECK_WARNING_IGNORE;
        const bool error = severity == CHECK_WARNING_ERROR;
        const PPSeverity disposition = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL : error ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
        if (severity != CHECK_WARNING_IGNORE)
        {
            pp_diagnose_output_at(pp, disposition, source, source, "'_BitInt' suffix for literals is a C23 extension [%s-Wc23-extensions]", error ? "-Werror," : "");
        }
        arena_pop_to(scratch, mark);
    }
}

// The numeric constants a failed parse left unchecked, from `begin` (where the checked prefix or repair ends) to its first
// error: clang diagnoses each as it parses it, before that error, which is where buster's checker never reaches them
// (#1127). Each one's publication is its own token, so it sorts before the parse's diagnostics. Numbers in a C23
// attribute list, whose tokens clang skips, have none.
static void check_driver_unparsed_literals(const Parser* parser, Preprocessor* pp, uint32_t begin, Arena* scratch)
{
    uint32_t end = parser->tokens.count;
    for (uint32_t d = 0; d < parser->diagnostic_count && end == parser->tokens.count; d += 1)
    {
        end = parser->diagnostics[d].error && parser->diagnostics[d].token < end ? parser->diagnostics[d].token : end;
    }
    uint32_t t = begin;
    while (t < end)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        if (kind == PARSE_KIND_L_SQUARE && t + 1 < end && parser->tokens.kinds[t + 1] == PARSE_KIND_L_SQUARE)
        {
            // Past the attribute list's brackets, balanced.
            uint32_t depth = 0;
            do
            {
                depth += parser->tokens.kinds[t] == PARSE_KIND_L_SQUARE;
                depth -= depth != 0 && parser->tokens.kinds[t] == PARSE_KIND_R_SQUARE;
                t += 1;
            } while (t < end && depth != 0);
        }
        else
        {
            if (kind == PARSE_KIND_NUMBER && !check_driver_literal(pp, parser, t, parser->tokens.sources[t], parser->tokens.sources[t], scratch))
            {
                check_driver_bit_int_suffix(pp, parser, t, scratch);
            }
            t += 1;
        }
    }
}

static void check_driver_unparsed_strings(const Parser* parser, Preprocessor* pp, uint32_t begin, Arena* scratch);

// The literals a rejected unit's reader left unread (#1447): from where its error stopped the walk
// (CheckInput.walk_end), and past its last report's token and the string literals adjacent to it, read as the unchecked
// rest of a failed parse is (check_driver_unparsed_literals, check_driver_unparsed_strings): each literal's errors and
// warnings, which clang's literal parsers give whatever came before.
static void check_driver_unread_literals(const Parser* parser, Preprocessor* pp, const CheckInput* input, Arena* scratch)
{
    uint32_t begin = input->walk_end;
    for (uint32_t r = 0; r < input->report_count; r += 1)
    {
        begin = input->reports[r].token < parser->tokens.count && input->reports[r].token + 1 > begin ? input->reports[r].token + 1 : begin;
    }
    while (begin > 0 && begin < parser->tokens.count && parser->tokens.kinds[begin] == PARSE_KIND_STRING && parser->tokens.kinds[begin - 1] == PARSE_KIND_STRING)
    {
        begin += 1;
    }
    if (begin < parser->tokens.count)
    {
        check_driver_unparsed_literals(parser, pp, begin, scratch);
        check_driver_unparsed_strings(parser, pp, begin, scratch);
    }
}

// Whether the tokens from `first` to `end`, any parentheses around them peeled, are string literals whose escape sequences
// clang refuses in an unevaluated string literal (#147): then their diagnostics (check_driver_escapes), from the first
// token that has one, published at it, the warning's severity the command line's (`input` holds no pragmas).
static bool check_driver_unparsed_escapes(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t first, uint32_t end, Arena* scratch)
{
    const uint8_t* kinds = parser->tokens.kinds;
    while (first + 2 < end && kinds[first] == PARSE_KIND_L_PAREN && kinds[end - 1] == PARSE_KIND_R_PAREN)
    {
        first += 1;
        end -= 1;
    }
    bool strings = first < end;
    for (uint32_t token = first; strings && token < end; token += 1)
    {
        strings = kinds[token] == PARSE_KIND_STRING;
    }
    uint32_t refused = end;
    for (uint32_t token = first; strings && refused == end && token < end; token += 1)
    {
        const uint64_t mark = scratch->length;
        const String8 spelling = check_warning_string_spelling(parser, pp, token, scratch);
        bool had_error = false;
        refused = spelling.length != 0 && check_literal_unevaluated_escapes((const uint8_t*)spelling.pointer, (uint32_t)spelling.length, pp->options.language.standard >= 2023, false, NULL, &had_error) != 0 ? token : end;
        arena_pop_to(scratch, mark);
    }
    if (refused != end)
    {
        check_driver_escapes(parser, pp, input, refused, end, parser->tokens.sources[refused], scratch);
    }
    else if (strings)
    {
        check_driver_unevaluated_prefixes(parser, pp, input, first, end, parser->tokens.sources[first], scratch);
    }
    return refused != end;
}

// An attribute's arguments in parentheses, from the `(` at `k`, split at their top-level commas, as
// check_driver_unparsed_strings reads them: the escapes of each argument its name's `row` reads as an unevaluated string
// literal, to the first whose escape clang refuses (which drops the attribute, its later arguments unread). Returns the
// token past the `)`.
static uint32_t check_driver_unparsed_arguments(const Parser* parser, Preprocessor* pp, const CheckInputArgumentsRow* row, uint32_t k, uint32_t end, Arena* scratch)
{
    static const CheckInput none = { 0 };
    const uint8_t* kinds = parser->tokens.kinds;
    const uint32_t identifiers = BUSTER_CHECK_ARGUMENTS_VARIADIC_IDENTIFIER | BUSTER_CHECK_ARGUMENTS_STRICT_IDENTIFIER;
    uint32_t argument = k + 1;
    uint32_t index = 0;
    uint32_t depth = 0;
    bool refused = false;
    k = argument;
    while (k < end && !(depth == 0 && kinds[k] == PARSE_KIND_R_PAREN))
    {
        const uint8_t at = kinds[k];
        depth += at == PARSE_KIND_L_PAREN || at == PARSE_KIND_L_SQUARE || at == PARSE_KIND_L_BRACE;
        depth -= depth != 0 && (at == PARSE_KIND_R_PAREN || at == PARSE_KIND_R_SQUARE || at == PARSE_KIND_R_BRACE);
        const bool last = k + 1 < end && depth == 0 && kinds[k + 1] == PARSE_KIND_R_PAREN;
        if (depth == 0 && (at == PARSE_KIND_COMMA || last))
        {
            const uint32_t stop = at == PARSE_KIND_COMMA ? k : k + 1;
            const bool lone = stop == argument + 1 && kinds[argument] == PARSE_KIND_IDENTIFIER;
            const bool identifier = row != NULL && lone && ((index == 0 && (row->flags & BUSTER_CHECK_ARGUMENTS_IDENTIFIER) != 0) || (row->flags & identifiers) != 0);
            const bool string = row != NULL && index < 32 && ((row->strings >> index) & 1u) != 0 && (row->flags & (identifiers | BUSTER_CHECK_ARGUMENTS_TYPE)) == 0;
            refused = refused || (string && !identifier && check_driver_unparsed_escapes(parser, pp, &none, argument, stop, scratch));
            index += 1;
            argument = stop + 1;
        }
        k += 1;
    }
    return k + (k < end ? 1 : 0);
}

// The unevaluated string literals a failed parse left unchecked, from `begin` to its first error, as
// check_driver_unparsed_literals reads its constants (#147): a GNU attribute's string arguments (its name's row; the
// first whose escape sequence clang refuses drops the attribute, its later ones unread), a known C23 attribute's (a
// standard one's, deprecated's and nodiscard's, or one of gnu::'s or clang::'s, the GNU name's row) and a static
// assertion's message, each wholly before that error. clang diagnoses their escapes as it parses them. The unknown
// escape's warning is the command line's: the pragmas are the checked view's.
static void check_driver_unparsed_strings(const Parser* parser, Preprocessor* pp, uint32_t begin, Arena* scratch)
{
    static const CheckInput none = { 0 };
    const uint8_t* kinds = parser->tokens.kinds;
    uint32_t end = parser->tokens.count;
    for (uint32_t d = 0; d < parser->diagnostic_count && end == parser->tokens.count; d += 1)
    {
        end = parser->diagnostics[d].error && parser->diagnostics[d].token < end ? parser->diagnostics[d].token : end;
    }
    uint32_t t = begin;
    while (t < end && !pp->fatal)
    {
        const uint8_t kind = kinds[t];
        const bool gnu = kind == PARSE_KIND_ATTRIBUTE && t + 2 < end && kinds[t + 1] == PARSE_KIND_L_PAREN && kinds[t + 2] == PARSE_KIND_L_PAREN;
        const bool standard = kind == PARSE_KIND_L_SQUARE && t + 1 < end && kinds[t + 1] == PARSE_KIND_L_SQUARE;
        const bool assertion = kind == PARSE_KIND_STATIC_ASSERT && t + 1 < end && kinds[t + 1] == PARSE_KIND_L_PAREN;
        uint32_t k = gnu ? t + 3 : standard || assertion ? t + 2 : t + 1;
        // A list's items, to its inner `)` or its `]`: a name (a C23 one's after its scope and `::`), and its arguments in
        // parentheses, split at their commas.
        while ((gnu || standard) && k < end && kinds[k] != (gnu ? PARSE_KIND_R_PAREN : PARSE_KIND_R_SQUARE))
        {
            const bool scoped = standard && k + 3 < end && kinds[k] == PARSE_KIND_IDENTIFIER && kinds[k + 1] == PARSE_KIND_COLON &&
                                kinds[k + 2] == PARSE_KIND_COLON && (parser->tokens.flags[k + 2] & BUSTER_PARSE_COLONCOLON_SECOND) != 0;
            const uint32_t name = scoped ? k + 3 : k;
            const String8 scope = scoped ? check_input_attribute_spelling(pp->table, parser->tokens.names[k]) : (String8){ "", 0 };
            const String8 spelled = kinds[name] != PARSE_KIND_COMMA && parser->tokens.names[name] != 0 ?
                                    check_input_attribute_spelling(pp->table, parser->tokens.names[name]) : (String8){ "", 0 };
            const bool known = gnu || (scoped ? check_input_attribute_equal(scope, "gnu") || check_input_attribute_equal(scope, "__gnu__") ||
                                                check_input_attribute_equal(scope, "clang") || check_input_attribute_equal(scope, "_Clang") :
                                                check_input_attribute_equal(spelled, "deprecated") || check_input_attribute_equal(spelled, "nodiscard") ||
                                                check_input_attribute_equal(spelled, "__deprecated__") || check_input_attribute_equal(spelled, "__nodiscard__"));
            const CheckInputArgumentsRow* row = known && spelled.length != 0 ? check_input_arguments_row(spelled) : NULL;
            k = name + 1;
            if (kinds[name] != PARSE_KIND_COMMA && k < end && kinds[k] == PARSE_KIND_L_PAREN)
            {
                k = check_driver_unparsed_arguments(parser, pp, row, k, end, scratch);
            }
        }
        // A static assertion's message, after the condition's top-level comma, to its `)`.
        uint32_t depth = 0;
        uint32_t comma = end;
        while (assertion && k < end && !(depth == 0 && kinds[k] == PARSE_KIND_R_PAREN))
        {
            const uint8_t at = kinds[k];
            comma = depth == 0 && at == PARSE_KIND_COMMA && comma == end ? k : comma;
            depth += at == PARSE_KIND_L_PAREN || at == PARSE_KIND_L_SQUARE || at == PARSE_KIND_L_BRACE;
            depth -= depth != 0 && (at == PARSE_KIND_R_PAREN || at == PARSE_KIND_R_SQUARE || at == PARSE_KIND_R_BRACE);
            k += 1;
        }
        if (assertion && k < end && comma < k)
        {
            check_driver_unparsed_escapes(parser, pp, &none, comma + 1, k, scratch);
        }
        // Another string literal, with those adjacent to it, or a character constant: an evaluated one, whose escapes'
        // diagnostics and refusal clang gives as it gives the checked ones' (#1447, check_driver_literal_escapes).
        const bool evaluated = !gnu && !standard && !assertion && (kind == PARSE_KIND_STRING || kind == PARSE_KIND_CHARACTER);
        if (evaluated)
        {
            check_driver_literal_escapes(parser, pp, &none, t, parser->tokens.sources[t], scratch);
            k = t + 1;
            while (kind == PARSE_KIND_STRING && k < end && kinds[k] == PARSE_KIND_STRING)
            {
                k += 1;
            }
        }
        t = gnu || standard || assertion || evaluated ? k : t + 1;
    }
}

// What clang refuses of the character constant `bytes` (#1447) past its escapes' errors, which come first: a character
// past what its type holds (1, "character too large for enclosing character literal type": a universal character name's
// or a UTF-8 character's past 0x7F in an ordinary or u8 one, past 0xFFFF in a u one), or more than one character in a
// wide one (2, "wide character literals may not contain multiple characters") or a Unicode one (3, "Unicode character
// literals ..."); 0 for none.
static uint8_t check_driver_character_refusal(const uint8_t* bytes, uint32_t length, bool c23)
{
    const bool wide = length != 0 && bytes[0] == 'L';
    const bool unicode = length != 0 && (bytes[0] == 'u' || bytes[0] == 'U');
    const uint32_t most = length > 1 && bytes[0] == 'u' && bytes[1] != '8' ? 0xFFFFu : length != 0 && (bytes[0] == 'U' || wide) ? 0x10FFFFu : 0x7Fu;
    uint32_t position = 0;
    while (position < length && bytes[position] != '\'')
    {
        position += 1;
    }
    position += 1;
    uint8_t result = 0;
    uint32_t characters = 0;
    while (result == 0 && position + 1 < length)
    {
        const uint32_t begin = position;
        uint32_t value = 0;
        const bool decoded = check_literal_character_unit_by(bytes, length - 1, &position, &value, c23, true);
        const bool numeric = bytes[begin] == '\\' && (bytes[begin + 1] == 'x' || bytes[begin + 1] == 'o' || (bytes[begin + 1] >= '0' && bytes[begin + 1] <= '7'));
        result = decoded && !numeric && value > most ? 1 : 0;
        characters += 1;
        position = decoded && position > begin ? position : length;
    }
    return result != 0 ? result : characters > 1 && wide ? 2 : characters > 1 && unicode ? 3 : 0;
}

// The characters of the character constant `bytes` as clang's CharLiteralParser counts them (#1447), its errors or not:
// each escape sequence and universal character name one (as far as clang reads it: check_literal_evaluated_escape),
// and each run between them its UTF-8 characters, or where it isn't well-formed UTF-8 its bytes (an ordinary
// constant's, which alone this counts for).
static uint32_t check_driver_character_count(const uint8_t* bytes, uint32_t length, bool c23)
{
    uint32_t at = 0;
    while (at < length && bytes[at] != '\'')
    {
        at += 1;
    }
    at += 1;
    const uint32_t end = length > at && bytes[length - 1] == '\'' ? length - 1 : length;
    uint32_t count = 0;
    uint32_t ignored = 0;
    bool had_error = false;
    while (at < end)
    {
        if (bytes[at] == '\\')
        {
            at = check_literal_evaluated_escape(bytes, at, end, c23, false, UINT32_MAX, NULL, &ignored, &had_error);
            count += 1;
        }
        else
        {
            uint32_t run = at;
            while (run < end && bytes[run] != '\\')
            {
                run += 1;
            }
            uint32_t characters = 0;
            bool decoded = true;
            for (uint32_t k = at; decoded && k < run;)
            {
                uint32_t code_point = 0;
                const uint32_t size = unicode_utf8_sequence(bytes + k, run - k, &code_point);
                decoded = size != 0;
                characters += 1;
                k += size;
            }
            count += decoded ? characters : run - at;
            at = run;
        }
    }
    return count;
}

// A string literal or a character constant the checker refused (#1447), worded and placed as clang does: its escape
// sequences' diagnostics (check_literal_evaluated_escapes, check_driver_escape), from the token at `token` on through the
// string literals adjacent to it, its warnings among them; or a character constant's refusal at the constant
// (check_driver_character_refusal); then, as clang counts an ordinary constant's characters whatever their errors
// (check_driver_character_count), more than one's -Wmultichar warning, or for four -Wfour-char-constants'. False where
// clang has no error for it, which leaves the checker's own message.
static bool check_driver_literal_escapes(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t publication, Arena* scratch)
{
    const bool c23 = pp->options.language.standard >= 2023;
    const bool character = token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_CHARACTER;
    const bool string = token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_STRING;
    uint32_t end = token + (character || string ? 1 : 0);
    while (string && end < parser->tokens.count && parser->tokens.kinds[end] == PARSE_KIND_STRING)
    {
        end += 1;
    }
    // Whether any is an error, then each in turn.
    bool errors = false;
    uint8_t refusal = 0;
    for (uint32_t pass = 0; pass < 2 && (pass == 0 || errors) && !pp->fatal; pass += 1)
    {
        bool had_error = false;
        for (uint32_t t = token; t < end && !pp->fatal; t += 1)
        {
            const uint64_t mark = scratch->length;
            const String8 spelling = check_warning_string_spelling(parser, pp, t, scratch);
            const uint32_t length = (uint32_t)spelling.length;
            CheckEscapeDiagnostic* diagnostics = length != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckEscapeDiagnostic, BUSTER_CHECK_ESCAPE_DIAGNOSTICS(length)) : NULL;
            const uint32_t count = diagnostics != NULL ? check_literal_evaluated_escapes((const uint8_t*)spelling.pointer, length, c23, true, diagnostics, &had_error) : 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                // (-Wunknown-escape-sequence's and -pedantic's are warnings.)
                errors |= diagnostics[i].kind != CHECK_ESCAPE_DIAGNOSTIC_UNKNOWN && diagnostics[i].kind != CHECK_ESCAPE_DIAGNOSTIC_NONSTANDARD &&
                          diagnostics[i].kind != CHECK_ESCAPE_DIAGNOSTIC_DELIMITED && diagnostics[i].kind != CHECK_ESCAPE_DIAGNOSTIC_NAMED;
                if (pass == 1)
                {
                    check_driver_escape(parser, pp, input, t, spelling, diagnostics + i, publication);
                }
            }
            if (pass == 0 && character && !errors)
            {
                refusal = check_driver_character_refusal((const uint8_t*)spelling.pointer, length, c23);
                errors = refusal != 0;
            }
            arena_pop_to(scratch, mark);
        }
    }
    if (refusal != 0)
    {
        pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, parser->tokens.sources[token], publication, "%s",
                              refusal == 1 ? "character too large for enclosing character literal type" :
                              refusal == 2 ? "wide character literals may not contain multiple characters" :
                                             "Unicode character literals may not contain multiple characters");
    }
    if (errors && character && !pp->fatal)
    {
        const uint64_t mark = scratch->length;
        const String8 spelling = check_warning_string_spelling(parser, pp, token, scratch);
        const bool ordinary = spelling.length != 0 && spelling.pointer[0] == '\'';
        const uint32_t characters = ordinary ? check_driver_character_count((const uint8_t*)spelling.pointer, (uint32_t)spelling.length, c23) : 0;
        arena_pop_to(scratch, mark);
        if (characters > 1)
        {
            const bool four = characters == 4;
            const CheckWarningSeverity severity = check_common_severity(pp, parser, input, token, four ? CHECK_WARNING_ID_FOUR_CHAR_CONSTANTS : CHECK_WARNING_ID_MULTICHAR, four);
            const bool error = severity == CHECK_WARNING_ERROR;
            const PPSeverity disposition = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL : error ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
            if (severity != CHECK_WARNING_IGNORE)
            {
                pp_diagnose_output_at(pp, disposition, parser->tokens.sources[token], publication, "multi-character character constant [%s-W%s]", error ? "-Werror," : "",
                                      four ? "four-char-constants" : "multichar");
            }
        }
    }
    return errors;
}

// A numeric constant the checker refused, worded and placed as clang does (c_check_literal_diagnostic.c, #1127): false if
// clang has no error for it, which leaves the checker's own message.
static bool check_driver_literal(Preprocessor* pp, const Parser* parser, uint32_t token, uint32_t source, uint32_t publication, Arena* scratch)
{
    bool result = false;
    PPTokens* output = &pp->output;
    if (token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_NUMBER && source < output->count && output->kinds[source] != BUSTER_PP_KIND_EMBED &&
        output->kinds[source] != BUSTER_PP_KIND_INTEGER)
    {
        const uint64_t mark = scratch->length;
        const uint64_t capacity = (uint64_t)output->values[source] - output->locations[source] + 1;
        uint8_t* buffer = arena_push(scratch, capacity, 1);
        const uint8_t* bytes = 0;
        const uint32_t length = buffer != 0 ? pp_spelling(pp, output->kinds[source], output->values[source], output->locations[source], buffer, &bytes) : 0;
        CheckLiteralDiagnostic diagnostic;
        result = length != 0 && check_literal_diagnose(bytes, length, parser->language.standard >= 2023, &diagnostic);
        if (result)
        {
            const char* radix = diagnostic.radix == 2 ? "binary" : diagnostic.radix == 8 ? "octal" : "decimal";
            const uint32_t offset = diagnostic.offset;
            switch (diagnostic.kind)
            {
                case CHECK_LITERAL_DIAGNOSTIC_EXPONENT:
                    pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "exponent has no digits");
                    break;
                case CHECK_LITERAL_DIAGNOSTIC_DIGIT:
                    pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "invalid digit '%c' in %s constant", diagnostic.character, radix);
                    break;
                case CHECK_LITERAL_DIAGNOSTIC_SUFFIX:
                    pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "invalid suffix '%.*s' on %s constant", (int)(length - offset),
                                                 (const char*)bytes + offset, diagnostic.floating ? "floating" : "integer");
                    break;
                case CHECK_LITERAL_DIAGNOSTIC_HEX_EXPONENT:
                case CHECK_LITERAL_DIAGNOSTIC_HEX_SIGNIFICAND:
                    pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "hexadecimal floating constant requires %s",
                                                 diagnostic.kind == CHECK_LITERAL_DIAGNOSTIC_HEX_EXPONENT ? "an exponent" : "a significand");
                    break;
                case CHECK_LITERAL_DIAGNOSTIC_SEPARATOR:
                    pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, offset, publication, "digit separator cannot appear at %s of digit sequence",
                                                 diagnostic.floating ? "end" : "start");
                    break;
                case CHECK_LITERAL_DIAGNOSTIC_TOO_LARGE:
                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "integer literal is too large to be represented in any integer type");
                    break;
                default:
                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'size_t' suffix for literals is a C++23 feature");
                    break;
            }
        }
        arena_pop_to(scratch, mark);
    }
    return result;
}

// Tier 1's selection state tables (tier1_select_burs.c), which main.c includes after this file: --tier1 builds them before
// any lane compiles a unit.
static bool tier1_select_initialize(void);

// The old excess-element sink and policy remain available to other consumers. Its sparse MESSAGE witness only
// preserves initializer semantic order alongside new facts; the generic sparse renderer still ignores MESSAGE.
// The initializers' walk's warnings, and its unavailable members' errors (CHECK_WARNING_EVENT_UNAVAILABLE_USE, #147), which
// `quiet` (-w) keeps alone: the walk stages them all, whatever the warning options.
static bool check_driver_initializer_warning_events(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                                      const CheckWarningEvents* view, const CheckRepair* repair, bool quiet, Arena* scratch)
{
    bool valid = true;
    for (uint32_t i = 0; valid && !pp->fatal && i < view->count; i += 1)
    {
        const CheckWarningEvent* event = view->events + i;
        const bool shown = !quiet || event->kind == CHECK_WARNING_EVENT_UNAVAILABLE_USE;
        if (shown && event->kind == CHECK_WARNING_EVENT_MESSAGE && event->id == CHECK_WARNING_ID_EXCESS_INITIALIZERS)
        {
            valid = event->token < parser->tokens.count && event->completion <= parser->tokens.count &&
                    (event->payload.data & 0xffff) == CHECK_WARNING_EXCESS_INITIALIZERS;
            if (valid && (repair == NULL || check_repair_warning_token(repair, event->token)))
            {
                const CheckWarningSeverity severity = check_diagnostic_severity(input, event->token, CHECK_WARNING_EXCESS_INITIALIZERS);
                if (severity != CHECK_WARNING_IGNORE)
                {
                    const bool source_error = pp->options.fatal_errors && severity == CHECK_WARNING_ERROR;
                    const PPSeverity disposition = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_FATAL :
                                                   severity >= CHECK_WARNING_ERROR ? (source_error ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_ERROR) : PP_SEVERITY_WARNING;
                    const uint32_t publication = check_driver_publication_position(parser, pp, event->completion, pp->output.count);
                    const TypeId object = event->payload.first_type != 0 ? type_unqualified(type_strip_aligned(types, event->payload.first_type)) : 0;
                    const uint8_t object_kind = object != 0 ? type_kind(types, object) : 0;
                    const char* subject = object_kind == BUSTER_TYPE_KIND_ARRAY || object_kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ? "array" :
                                          object_kind == BUSTER_TYPE_KIND_STRUCT ? "struct" :
                                          object_kind == BUSTER_TYPE_KIND_UNION ? "union" : "scalar";
                    const bool string = (event->payload.data & CHECK_INIT_EXCESS_STRING) != 0;
                    const char* message = string ? "initializer-string for char array is too long" :
                                          object != 0 ? check_warning_text(scratch, "excess elements in %s initializer", subject) :
                                          check_diagnostic_message(CHECK_WARNING_EXCESS_INITIALIZERS);
                    pp_diagnose_output_at(pp, disposition, parser->tokens.sources[event->token], publication, "%s [-W%sexcess-initializers]",
                                          message, severity >= CHECK_WARNING_ERROR ? "error," : "");
                    valid = !pp->diagnostic_collection_failed;
                }
            }
        }
        else if (shown && event->kind == CHECK_WARNING_EVENT_UNAVAILABLE_USE)
        {
            // An unavailable member's use (check_init_member_use, #147): clang's error, whatever the warning options.
            const CheckWarningNote* note = event->note_count == 1 && event->note_first < view->note_count ? view->notes + event->note_first : NULL;
            valid = event->token < parser->tokens.count && event->completion <= parser->tokens.count && note != NULL && note->token < parser->tokens.count;
            if (valid && (repair == NULL || check_repair_warning_token(repair, event->token)))
            {
                const uint64_t mark = scratch->length;
                CheckWarningSpelling spell = { 0 };
                const char* message = check_driver_warning_message(parser, pp, input, types, &spell, event, scratch);
                const char* text = check_driver_warning_note(parser, pp, input, types, &spell, note, scratch);
                valid = message != NULL && text != NULL;
                if (valid)
                {
                    const PPOutputDiagnosticNote noted = { .token = parser->tokens.sources[note->token], .message = text };
                    const uint32_t publication = check_driver_publication_position(parser, pp, event->completion, pp->output.count);
                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, parser->tokens.sources[event->token], publication, &noted, 1, "%s", message);
                    valid = !pp->diagnostic_collection_failed;
                }
                arena_pop_to(scratch, mark);
            }
        }
        else if (shown)
        {
            const CheckWarningEvents one = { .events = event, .count = 1, .notes = view->notes, .note_count = view->note_count };
            valid = check_driver_warning_events(parser, pp, input, types, &one, repair, scratch);
        }
    }
    return valid;
}

static void check_driver_pragma_report(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckInputFpPragma* record, bool quiet)
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
    else if (!quiet && !record->warning_quiet && (record->warnings & CHECK_FP_PRAGMA_EMPTY_STACK) != 0)
    {
        const CheckWarningSeverity severity = check_diagnostic_severity_id_from(input, record->token, record->source_begin,
            CHECK_WARNING_ID_IGNORED_PRAGMA, (CheckWarningSeverity)record->warning_severity);
        if (severity != CHECK_WARNING_IGNORE)
        {
            const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                           severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
            // The parser consumes this annotation after preprocessing the following ordinary token. Malformed
            // retained pragmas before that lookahead publish first, even when they follow the pop in the source.
            const uint32_t publication = check_driver_publication_position(parser, pp, record->token, pp->output.count);
            pp_diagnose_output_phase(pp, pp_severity, check_diagnostic_pragma_source(record), publication, PP_DIAGNOSTIC_SEMANTIC,
                "%s [%s-Wignored-pragmas]", check_diagnostic_pragma_message(record), check_driver_warning_error_prefix(severity == CHECK_WARNING_ERROR));
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
                               (int)record->name.length, record->name.pointer,
                               check_driver_warning_error_prefix(!record->error && severity == CHECK_WARNING_ERROR));
        }
        else
        {
            char promoted[256];
            const char* message = record->message;
            const char* prefix = check_driver_warning_error_prefix(!record->error && severity == CHECK_WARNING_ERROR);
            const char* option = prefix[0] != 0 ? strstr(message, "[-W") : NULL;
            if (option != NULL)
            {
                snprintf(promoted, sizeof(promoted), "%.*s[%s%s", (int)(option - message), message, prefix, option + 1);
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

// The publication position of the unit's first error so far, its phase's too (PP_DIAGNOSTIC_PHASE_COUNT a source position),
// or UINT64_MAX: its preprocessor's records, the checker's reports published among them, and the original parse's errors.
static uint64_t check_driver_first_error(const Parser* original, const Preprocessor* pp)
{
    uint64_t first_error = UINT64_MAX;
    for (const PPDiagnosticRecord* record = pp->diagnostic_first; record != NULL && pp->errors != 0; record = record->next)
    {
        if (record->severity == PP_SEVERITY_ERROR || record->severity == PP_SEVERITY_FATAL || record->severity == PP_SEVERITY_DEFERRED_ERROR)
        {
            const uint64_t position = record->severity == PP_SEVERITY_DEFERRED_ERROR ?
                                      record->position - record->position % PP_DIAGNOSTIC_PHASE_COUNT + PP_DIAGNOSTIC_SEMANTIC : record->position;
            first_error = position < first_error ? position : first_error;
        }
    }
    for (uint32_t i = 0; i < original->diagnostic_count; i += 1)
    {
        const ParseDiagnostic* diagnostic = original->diagnostics + i;
        if (diagnostic->error)
        {
            const uint32_t source = diagnostic->token < original->tokens.count ? original->tokens.sources[diagnostic->token] : pp->output.count;
            const uint64_t position = (uint64_t)source * PP_DIAGNOSTIC_PHASE_COUNT + PP_DIAGNOSTIC_SEMANTIC;
            first_error = position < first_error ? position : first_error;
        }
    }
    return first_error;
}

// Whether clang ends the body whose `}` is `close` after the unit's first error (`first_error`, check_driver_first_error):
// its AnalysisBasedWarnings::IssueWarnings, at the body's end, where -Wreturn-type's place is, then finds an uncompilable
// error, builds no CFG and gives no analysis warning (-Wreturn-type, -Wuninitialized), but every runtime-behavior one the
// body queued (#1447's semantic recovery, phase 3, check_common_flush).
static bool check_driver_body_flushed(const Parser* original, Preprocessor* pp, uint32_t close, uint64_t first_error)
{
    const uint32_t publication = check_driver_publication_position(original, pp, close + 1u, pp->output.count);
    return (uint64_t)publication * PP_DIAGNOSTIC_PHASE_COUNT + PP_DIAGNOSTIC_RETURN >= first_error;
}

// The tokens of the program's bodies clang ends after the unit's first error (check_driver_body_flushed), from each one's
// `{` to its `}`, in `scratch`; or null: in a unit with no error, or no such body, which is every error-free unit's case and
// costs it nothing past the preprocessor's error count and the original parse's.
static const uint8_t* check_driver_flushed_bodies(const Parser* parser, const Parser* original, Preprocessor* pp, const CheckProgram* program,
                                                  Arena* scratch)
{
    const uint64_t first_error = pp->errors != 0 || original->diagnostic_count != 0 ? check_driver_first_error(original, pp) : UINT64_MAX;
    uint8_t* flushed = NULL;
    for (uint32_t b = 0; first_error != UINT64_MAX && b < program->body_count; b += 1)
    {
        const uint32_t opening = program->bodies[b].opening;
        const uint32_t close = opening < parser->tokens.count ? parser->tree.parents[opening] : BUSTER_CHECK_NONE;
        if (close < parser->tokens.count && close > opening && check_driver_body_flushed(original, pp, close, first_error))
        {
            if (flushed == NULL)
            {
                flushed = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, parser->tokens.count);
                if (flushed != NULL)
                {
                    memset(flushed, 0, parser->tokens.count);
                }
            }
            if (flushed != NULL)
            {
                memset(flushed + opening, 1, (uint64_t)close - opening + 1);
            }
        }
    }
    return flushed;
}

static void check_driver_return_reports(const Parser* parser, const Parser* original, Preprocessor* pp, const CheckProgram* program, CheckWarningSeverity baseline, bool quiet)
{
    const uint64_t first_error = quiet ? UINT64_MAX : check_driver_first_error(original, pp);
    for (uint32_t b = 0; !quiet && !pp->fatal && b < program->body_count; b += 1)
    {
        const CheckProgramBody* body = program->bodies + b;
        const uint32_t token = parser->tree.parents[body->opening];
        // A cpu_dispatch function's body may fall off its end (#1447: clang's CheckFallThroughForBody permits it).
        if (body->return_warning != 0 && token < parser->tokens.count &&
            (pp->options.system_header_warnings || !check_graph_system_token(&program->input->graph, token)) &&
            check_cpu_dispatch_kind(check_input_attributes_at(program->input, body->declaration)) != CHECK_CPU_DISPATCH_DISPATCH)
        {
            // The body's analysis boundary: the queued diagnostics' boundary is the `}` lookahead, and the
            // return warning's RETURN phase keeps it after them, before the uninit SEMANTICs.
            const uint32_t publication = check_driver_publication_position(original, pp, token + 1u, pp->output.count);
            const CheckWarningEvent event = { .id = CHECK_WARNING_ID_INVALID_NORETURN, .kind = CHECK_WARNING_EVENT_NORETURN_FALLS,
                                               .token = token, .source = BUSTER_CHECK_WARNING_NO_SOURCE };
            const CheckWarningSeverity severity = body->return_warning == 3 ? check_driver_warning_severity(pp, program->input, &event) :
                check_diagnostic_severity_from(program->input, token, BUSTER_CHECK_NONE, CHECK_WARNING_RETURN_TYPE, baseline);
            if (severity != CHECK_WARNING_IGNORE && (uint64_t)publication * PP_DIAGNOSTIC_PHASE_COUNT + PP_DIAGNOSTIC_RETURN < first_error)
            {
                const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                               severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
                if (body->return_warning == 3)
                {
                    pp_diagnose_output_phase(pp, pp_severity, parser->tokens.sources[token], publication, PP_DIAGNOSTIC_RETURN,
                                             "function declared 'noreturn' should not return [%s-Winvalid-noreturn]",
                                             check_driver_warning_error_prefix(severity == CHECK_WARNING_ERROR));
                }
                else
                {
                    pp_diagnose_output_phase(pp, pp_severity, parser->tokens.sources[token], publication, PP_DIAGNOSTIC_RETURN,
                                             "non-void function does not return a value%s [%s-Wreturn-type]",
                                             body->return_warning == 2 ? " in all control paths" : "",
                                             check_driver_warning_error_prefix(severity == CHECK_WARNING_ERROR));
                }
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
            check_driver_pragma_report(parser, pp, input, input->fp_pragmas + *fp, quiet);
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
            const CheckBuiltinX86RangeEvent* event = effect->range_events + e;
            const CheckWarningSeverity severity = check_diagnostic_range_severity(input, token, event->default_error);
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
                // A source pragma's error is an ordinary error under -Wfatal-errors; a command-line or default error
                // arrives already fatal from check_diagnostic_severity.
                const bool source_error = pp->options.fatal_errors && severity == CHECK_WARNING_ERROR;
                const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? (event->default_error ? PP_SEVERITY_FATAL : PP_SEVERITY_MAPPED_FATAL) :
                    severity == CHECK_WARNING_ERROR ? (event->default_error && !source_error ?
                        (phase == PP_DIAGNOSTIC_DEFERRED ? PP_SEVERITY_DEFERRED_ERROR : PP_SEVERITY_ERROR) : PP_SEVERITY_MAPPED_ERROR) : PP_SEVERITY_WARNING;
                // Clang's warn_argument_invalid_range is an error by default, so its printer never adds `-Werror,`: the
                // option alone, whether it is a warning, an error or a fatal error (#1079).
                pp_diagnose_output_phase(pp, pp_severity, parser->tokens.sources[token], publication, phase,
                                   "argument value %s is outside the valid range [%u, %u] [-Wargument-outside-range]", text, event->low, event->high);
            }
        }
    }
}

// A K&R warning at `place` of the call whose callee's name is `token`.
static void check_driver_kr_report_at(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t place, uint32_t warning)
{
    const CheckWarningSeverity severity = token < parser->tokens.count ? check_diagnostic_kr_severity(input, token, warning) : CHECK_WARNING_IGNORE;
    if (token < parser->tokens.count && place < parser->tokens.count && severity != CHECK_WARNING_IGNORE)
    {
        const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                       severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
        if (warning == CHECK_WARNING_KR_TOO_FEW || warning == CHECK_WARNING_KR_TOO_MANY)
        {
            const uint32_t name = parser->tokens.names[token];
            // These warnings have no flag of their own: -Werror's error says only that (#1994).
            pp_diagnose_output(pp, pp_severity, parser->tokens.sources[place], "%s to '%.*s'%s",
                               check_diagnostic_message(warning), (int)intern_length(pp->table, name), intern_bytes(pp->table, name),
                               severity == CHECK_WARNING_ERROR ? " [-Werror]" : "");
        }
        else
        {
            pp_diagnose_output(pp, pp_severity, parser->tokens.sources[place], "%s [%s-Wdeprecated-non-prototype]", check_diagnostic_message(warning),
                               check_driver_warning_error_prefix(severity == CHECK_WARNING_ERROR));
        }
    }
}

// A call's K&R warning from the graph's node (#1994): at check_diagnostic_kr_call_place, its severity at the callee's
// name, as check_driver_kr_report's. Passing arguments without a prototype names the function where the callee is a
// function's own name (through parentheses and `&`), as clang's FDecl, and is "a function" otherwise.
// `token` is the callee's name and `place` the warning's (the reader's records' too, check_input_kr_call).
static void check_driver_kr_call_at(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t place, uint32_t warning)
{
    const CheckWarningSeverity severity = token < parser->tokens.count ? check_diagnostic_kr_severity(input, token, warning) : CHECK_WARNING_IGNORE;
    if (warning != CHECK_WARNING_NON_PROTOTYPE_CALL)
    {
        check_driver_kr_report_at(parser, pp, input, token, place, warning);
    }
    else if (place < parser->tokens.count && severity != CHECK_WARNING_IGNORE)
    {
        const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                       severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
        const uint32_t declaration = input->declarations != NULL && token < input->token_count ? input->declarations[token] : BUSTER_CHECK_NONE;
        const bool direct = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && declaration < input->token_count && input->declaration_kinds != NULL &&
                            input->declaration_kinds[declaration] == C_BINDING_FUNCTION;
        const uint32_t name = direct ? parser->tokens.names[token] : 0;
        pp_diagnose_output(pp, pp_severity, parser->tokens.sources[place],
                           "passing arguments to %s%.*s%s without a prototype is deprecated in all versions of C and is not supported in C23 [%s-Wdeprecated-non-prototype]",
                           direct ? "'" : "a function", direct ? (int)intern_length(pp->table, name) : 0, direct ? (const char*)intern_bytes(pp->table, name) : "", direct ? "'" : "",
                           check_driver_warning_error_prefix(severity == CHECK_WARNING_ERROR));
    }
}

static void check_driver_kr_call(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t node, uint32_t warning)
{
    check_driver_kr_call_at(parser, pp, input, check_diagnostic_kr_call_token(input, node), check_diagnostic_kr_call_place(input, parser, node, warning), warning);
}

// Clang completes a K&R definition's warning when the function's checking does, including the lookahead past its
// closing brace: the body's own diagnostics publish first, then this warning ahead of the boundary's deferred and
// return ones (#992). A repaired tree proves that boundary only when its brace and the lookahead are both original;
// a genuinely unfinished body publishes at the repaired unit's end instead.
static void check_driver_kr_definition(const Parser* parser, const Parser* original, Preprocessor* pp, const CheckInput* input,
                                       const CheckInputKrDiagnostic* diagnostic, uint32_t publication_end)
{
    const uint32_t token = diagnostic->token;
    const CheckWarningSeverity severity = check_diagnostic_kr_severity(input, token, diagnostic->warning);
    if (token < parser->tokens.count && severity != CHECK_WARNING_IGNORE)
    {
        const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                       severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
        const Parser* publication_parser = parser;
        const uint32_t original_end = original != parser ? check_diagnostic_parse_end(original) : 0;
        const uint32_t walk_end = original != parser && original_end < original->tokens.count ? original_end : original->tokens.count;
        uint32_t boundary = parser->tokens.count + 1u;
        for (uint32_t parent = diagnostic->body, steps = 0; parent < walk_end && steps < original->tokens.count; steps += 1)
        {
            if (original->tree.roles[parent] == PARSE_ROLE_CLOSE_BODY && original->tokens.kinds[parent] == PARSE_KIND_R_BRACE &&
                (original == parser || parent + 1u < original_end))
            {
                boundary = parent + 1u;
                publication_parser = original;
                break;
            }
            parent = original->tree.parents[parent];
        }
        const uint32_t publication = check_driver_publication_position(publication_parser, pp, boundary,
                                                                       publication_parser == parser ? publication_end : pp->output.count);
        pp_diagnose_output_phase(pp, pp_severity, parser->tokens.sources[token], publication, PP_DIAGNOSTIC_LEXICAL,
                                 "%s [%s-Wdeprecated-non-prototype]", check_diagnostic_message(diagnostic->warning),
                                 check_driver_warning_error_prefix(severity == CHECK_WARNING_ERROR));
    }
}

// A type of the unit as clang's TypePrinter spells it, NUL-terminated in `scratch`, with " *" after it where `decay` says
// (an array's element's, as a parameter decays the array), or NULL.
static const char* check_driver_library_type(const Preprocessor* pp, const TypeTable* types, const TypeNames* names, TypeId type, bool decay, Arena* scratch)
{
    const TypeId base = type_strip_aligned(types, type);
    const bool array = decay && type_kind_is_array(type_kind(types, base));
    const String8 printed = type_is_valid(types, type) ? type_print(types, scratch, array ? type_element(types, base) : type, (String8){ 0 }, names,
                                                                    pp->options.language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0) :
                                                         (String8){ 0 };
    const bool star = printed.length != 0 && printed.pointer[printed.length - 1] == '*';
    char* text = printed.length != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, char, printed.length + 3) : NULL;
    if (text != NULL)
    {
        memcpy(text, printed.pointer, printed.length);
        memcpy(text + printed.length, array ? (star ? "*" : " *") : "", array ? (star ? 2 : 3) : 1);
    }
    return text;
}

// The unit's FILE, jmp_buf, sigjmp_buf and ucontext_t as a library builtin's spelling names them (CBuiltinLibraryNamed), for
// those a prototype took (CheckInput.implicit_library_types), in `scratch`.
static CBuiltinLibraryNamed check_driver_library_named(Preprocessor* pp, const TypeTable* types, const CheckInput* input, Arena* scratch)
{
    CBuiltinLibraryNamed named = { 0 };
    const TypeId* taken = input != NULL ? input->implicit_library_types : NULL;
    if (types != NULL && taken != NULL && (taken[0] != 0 || taken[1] != 0 || taken[2] != 0 || taken[3] != 0))
    {
        // The names' spellings, as the warnings' renderer builds them (check_driver_spelling_build), for these few types.
        const uint32_t count = pp->table->count;
        const uint8_t** bytes = BUSTER_ARENA_PUSH_ARRAY(scratch, const uint8_t*, count);
        uint32_t* lengths = bytes != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
        for (uint32_t i = 0; lengths != NULL && i < count; i += 1)
        {
            bytes[i] = intern_bytes(pp->table, i);
            lengths[i] = intern_length(pp->table, i);
        }
        const TypeNames names = { .bytes = bytes, .lengths = lengths, .count = lengths != NULL ? count : 0 };
        named = (CBuiltinLibraryNamed){
            .file = taken[0] != 0 ? check_driver_library_type(pp, types, &names, taken[0], false, scratch) : NULL,
            .jmp_buf = taken[1] != 0 ? check_driver_library_type(pp, types, &names, taken[1], true, scratch) : NULL,
            .sigjmp_buf = taken[2] != 0 ? check_driver_library_type(pp, types, &names, taken[2], true, scratch) : NULL,
            .ucontext = taken[3] != 0 ? check_driver_library_type(pp, types, &names, taken[3], false, scratch) : NULL,
        };
    }
    return named;
}

// An implicit declaration of a plain library builtin (#148, CHECK_INPUT_FUNCTION_IMPLICIT_LIBRARY), as clang reports it:
// C99's and later's "call to undeclared library function", or before C99 "implicitly declaring library function", each
// with the builtin's prototype (cbuiltin_library_spell: with size_t's __size_t and FILE and ucontext_t by their typedefs,
// and then its spelling without them, the unit's types spelled from `types`: CheckInput.implicit_library_types), and a
// note naming the header that declares it, where its row has one. `promoted` is "-Werror," where -Werror made the warning
// before C99 an error, else "".
static void check_driver_implicit_library(Preprocessor* pp, const TypeTable* types, const CheckInput* input, PPSeverity severity, uint32_t source,
                                          uint32_t publication, const uint8_t* name, uint32_t length, bool c99, const char* promoted, Arena* scratch)
{
    const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup((const char*)name, length);
    const uint64_t mark = scratch->length;
    const bool needs = info != NULL && (memchr(info->encoding, 'P', info->encoding_length) != NULL || memchr(info->encoding, 'J', info->encoding_length) != NULL ||
                                        memchr(info->encoding, 'K', info->encoding_length) != NULL);
    const CBuiltinLibraryNamed named = needs ? check_driver_library_named(pp, types, input, scratch) : (CBuiltinLibraryNamed){ 0 };
    char sugared[256];
    char plain[256];
    bool sized = false;
    const bool spelled = info != NULL && cbuiltin_library_spell(info->encoding, info->encoding_length, info->attributes, info->attributes_length, true, c99,
                                                                 &named, sugared, sizeof(sugared), &sized) &&
                         cbuiltin_library_spell(info->encoding, info->encoding_length, info->attributes, info->attributes_length, false, c99, &named,
                                                plain, sizeof(plain), &sized);
    scratch->length = mark;
    char type[560];
    snprintf(type, sizeof(type), sized ? "'%s' (aka '%s')" : "'%s'", spelled ? sugared : "int ()", plain);
    const char* header = info != NULL ? cbuiltin_library_header(info->header_token) : NULL;
    char note[192];
    snprintf(note, sizeof(note), "include the header <%s> or explicitly provide a declaration for '%.*s'", header != NULL ? header : "", (int)length, name);
    const PPOutputDiagnosticNote notes[1] = { { .token = source, .message = note } };
    if (c99)
    {
        pp_diagnose_output_notes_at(pp, severity, source, publication, notes, header != NULL,
                                    "call to undeclared library function '%.*s' with type %s; ISO C99 and later do not support implicit function declarations "
                                    "[-Wimplicit-function-declaration]",
                                    (int)length, name, type);
    }
    else
    {
        pp_diagnose_output_notes_at(pp, severity, source, publication, notes, header != NULL,
                                    "implicitly declaring library function '%.*s' with type %s [%s-Wimplicit-function-declaration]", (int)length, name, type,
                                    promoted);
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
// A pointer expression keeps the typedef spelling of the value it derives from. Address, indirection, subscript and
// pointer arithmetic wrap that type without replacing its base specifier; a cast supplies a new spelling itself.
static uint32_t check_driver_pointer_spelling(const Parser* parser, const CheckInput* input, const CheckStream* stream,
                                              const TypeTable* types, uint32_t node)
{
    const CheckGraph* graph = stream->graph;
    uint32_t spelling = 0;
    for (uint32_t steps = 0; spelling == 0 && node < graph->count && steps < graph->count; steps += 1)
    {
        const CheckOp op = graph->ops[node];
        // A member's own declarator's specifiers (#1837): its typedef or typeof (`&s.m` of a `typeof(long) m`).
        const bool member = op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW;
        const uint32_t selected = member ? check_semantic_member_of(parser, input, stream, types, node) : 0;
        const uint32_t declarator = selected != 0 ? check_semantic_member_token(parser, input, types, selected) : BUSTER_CHECK_NONE;
        spelling = !member ? check_input_operand_type_spelling(input, parser, node) :
                   declarator < input->token_count ? check_input_specifier_spelling(input, parser, declarator) : 0;
        uint32_t next = !member && (op == CHECK_OP_ADDRESS || op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT) ? graph->operand0[node] : BUSTER_CHECK_NONE;
        if (op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT)
        {
            const uint32_t first = graph->operand0[node];
            const TypeId type = first < stream->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, first))) : 0;
            const uint8_t kind = type != 0 ? type_kind(types, type) : BUSTER_TYPE_KIND_NONE;
            next = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) ? first : graph->operand1[node];
        }
        node = next;
    }
    return spelling;
}

// Revisit only an already diagnosed conversion. Array decay and atomic loads expose their pointees directly,
// without allocating semantic types or a per-argument warning column.
static CheckActionResult check_driver_pointer_warning(const TypeTable* types, const CheckGraph* graph, uint32_t node,
                                                       TypeId destination, const CheckValue* value, bool* nested)
{
    CheckActionResult result = { 0 };
    const CheckAction action = { .node = node, .context = check_graph_context(graph, node, false) };
    const TypeId target = type_unqualified(type_strip_aligned(types, destination));
    TypeId source = type_unqualified(type_strip_aligned(types, value->type));
    if (type_kind(types, source) == BUSTER_TYPE_KIND_ATOMIC)
    {
        source = type_unqualified(type_strip_aligned(types, type_element(types, source)));
    }
    const uint8_t kind = type_kind(types, source);
    *nested = false;
    if (type_kind(types, target) == BUSTER_TYPE_KIND_POINTER && (kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind)) &&
        !check_action_null(types, value, action.context.extensions))
    {
        check_action_pointer_assignable(types, type_element(types, target), type_element(types, source), &action, &result, false, nested);
    }
    return result;
}

static const char* check_driver_pointer_wording(const TypeTable* types, uint32_t warning, bool nested, TypeId destination, TypeId source)
{
    destination = type_unqualified(type_strip_aligned(types, destination));
    source = type_unqualified(type_strip_aligned(types, source));
    const TypeId left = type_kind(types, destination) == BUSTER_TYPE_KIND_POINTER ?
                        type_unqualified(type_strip_aligned(types, type_element(types, destination))) : 0;
    const TypeId right = type_kind(types, source) == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(type_kind(types, source)) ?
                         type_unqualified(type_strip_aligned(types, type_element(types, source))) : 0;
    const bool plain_char = left == BUSTER_TYPE_CHAR || right == BUSTER_TYPE_CHAR;
    return warning == CHECK_WARNING_DISCARDED_QUALIFIERS ?
           (nested ? "discards qualifiers in nested pointer types" : "discards qualifiers") : plain_char ?
           "converts between pointers to integer types where one is of the unique plain 'char' type and the other is not" :
           "converts between pointers to integer types with different sign";
}

// A conditional's -Wpointer-type-mismatch or -Wconditional-type-mismatch (#2292): clang's words name its arms' types as
// written, "pointer type mismatch ('int *' and 'long *')", at the conditional's `?` (`*located`), as clang's
// checkConditionalPointerCompatibility and checkPointerIntegerMismatch give them; a GNU `?:`'s left arm is its condition.
// NULL where the conditional's arms aren't known, for the generic words.
static const char* check_driver_conditional_mismatch_message(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                                             const TypeTable* types, CheckWarningSpelling* spell, uint32_t token, uint32_t warning,
                                                             uint32_t* located, Arena* scratch)
{
    const uint32_t node = input->token_nodes != NULL && token < input->token_count ? input->token_nodes[token] : BUSTER_CHECK_NONE;
    const CheckGraph* graph = stream != NULL ? stream->graph : NULL;
    const bool conditional = types != NULL && graph != NULL && node < stream->count && graph->ops[node] == CHECK_OP_CONDITIONAL;
    const uint32_t condition = conditional ? graph->operand0[node] : BUSTER_CHECK_NONE;
    const uint32_t left = conditional ? graph->operand1[node] : BUSTER_CHECK_NONE;
    const uint32_t right = conditional ? graph->operand2[node] : BUSTER_CHECK_NONE;
    const char* result = NULL;
    if (condition < stream->count && left < stream->count && right < stream->count)
    {
        String8 names[2] = { 0 };
        const uint32_t arms[2] = { left, right };
        for (uint32_t i = 0; i < 2; i += 1)
        {
            // An arm's type after its lvalue conversion: an array's or a function's pointer, else as written.
            const TypeId type = check_stream_type(stream, arms[i]);
            const uint8_t kind = type_kind(types, type_strip_aligned(types, type));
            const bool decays = type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_FUNCTION;
            const TypeId pointee = type_kind_is_array(kind) ? type_element(types, type_strip_aligned(types, type)) : type;
            const TypeId decayed = decays ? type_find_pointer(types, pointee) : 0;
            const uint32_t spelling = decays ? 0 : check_driver_pointer_spelling(parser, input, stream, types, arms[i]);
            names[i] = check_driver_warning_type(parser, pp, input, types, spell, scratch, decayed != 0 ? decayed : type, spelling);
        }
        const char* words = warning == CHECK_WARNING_CONDITIONAL_POINTER_TYPES ? "pointer type mismatch" : "pointer/integer type mismatch in conditional expression";
        result = names[0].length != 0 && names[1].length != 0 ?
                 check_warning_text(scratch, "%s (%.*s and %.*s)", words, (int)names[0].length, names[0].pointer, (int)names[1].length, names[1].pointer) : NULL;
        // At its `?`, the `:`'s child that opens it.
        uint32_t question = graph->tokens[node];
        while (question != 0 && !(parser->tokens.kinds[question] == PARSE_KIND_QUESTION && parser->tree.parents[question] == graph->tokens[node]))
        {
            question -= 1;
        }
        *located = result != NULL && parser->tokens.kinds[question] == PARSE_KIND_QUESTION ? question : *located;
    }
    return result;
}

static const char* check_driver_pointer_conversion_message(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                                           const TypeTable* types, CheckWarningSpelling* spell, uint32_t token, uint32_t warning,
                                                           uint32_t* located, Arena* scratch)
{
    const uint32_t node = input->token_nodes != NULL && token < input->token_count ? input->token_nodes[token] : BUSTER_CHECK_NONE;
    const char* result = check_diagnostic_message(warning);
    const CheckGraph* graph = stream != NULL ? stream->graph : NULL;
    const CheckOp op = graph != NULL && node < stream->count ? graph->ops[node] : CHECK_OP_COUNT;
    const bool returning = op == CHECK_OP_CONVERT && parser->tokens.kinds[token] == PARSE_KIND_RETURN;
    if (types != NULL && graph != NULL && (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || returning))
    {
        if (op == CHECK_OP_INITIALIZE && check_sparse_present(input->initializer_begins))
        {
            // Initializers diagnose their declarator, whose sparse range starts just after this '='. Search
            // only this declaration; parameter names in a pointer declarator cannot match that range.
            bool found = false;
            for (uint32_t at = token; !found && at != 0;)
            {
                at -= 1;
                found = check_sparse_get(input->initializer_begins, at) == token + 1;
                if (found)
                {
                    *located = at;
                }
                found = found || check_input_declaration_boundary(parser->tree.roles[at]);
            }
        }
        else if (returning && token + 1 < parser->tokens.count)
        {
            *located = token + 1;
        }
        const uint32_t right = op == CHECK_OP_ASSIGN ? graph->operand1[node] : graph->operand0[node];
        const uint32_t left = op == CHECK_OP_ASSIGN ? graph->operand0[node] : BUSTER_CHECK_NONE;
        const uint32_t left_token = left < stream->count ? graph->tokens[left] : *located;
        const TypeId declared = op == CHECK_OP_INITIALIZE && graph->types != NULL ? graph->types[node] : 0;
        uint32_t left_spelling = 0;
        const TypeId left_type = returning ? check_stream_type(stream, node) :
                                 check_input_object_spelling(types, input, parser, left_token, declared, check_stream_type(stream, node), &left_spelling);
        const uint32_t right_spelling = right < stream->count ? check_driver_pointer_spelling(parser, input, stream, types, right) : 0;
        const String8 left_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, left_type, left_spelling);
        const String8 right_name = right < stream->count ?
                                   check_driver_warning_type(parser, pp, input, types, spell, scratch, check_stream_type(stream, right), right_spelling) : (String8){ 0 };
        if (left_name.length != 0 && right_name.length != 0)
        {
            const char* action = op == CHECK_OP_INITIALIZE ? "initializing %.*s with an expression of type %.*s %s" :
                                 returning ? "returning %.*s from a function with result type %.*s %s" : "assigning to %.*s from %.*s %s";
            const String8 first = returning ? right_name : left_name;
            const String8 second = returning ? left_name : right_name;
            bool nested = false;
            const CheckValue value = check_stream_value(stream, right);
            check_driver_pointer_warning(types, graph, node, check_stream_type(stream, node), &value, &nested);
            const char* conversion = check_driver_pointer_wording(types, warning, nested, check_stream_type(stream, node), value.type);
            result = check_warning_text(scratch, action, (int)first.length, first.pointer, (int)second.length, second.pointer, conversion);
        }
    }
    return result;
}

// A folded left shift's -Wshift-negative-value and -Wshift-overflow as clang's DiagnoseBadShiftValues words them: a
// negative left operand's undefined shift, or the significant bits the nonnegative result needs, ResultBits =
// significant bits of the left operand (its bit length and a sign bit) plus the count, printed as an unsigned
// hexadecimal of that width, against the bits of the left operand's (promoted) type, its typedef spelled as written.
// A shift whose node or operands the stream doesn't hold keeps the generic text.
static const char* check_driver_shift_message(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                              const TypeTable* types, CheckWarningSpelling* spell, uint32_t token, uint32_t warning, Arena* scratch)
{
    const char* result = check_diagnostic_message(warning);
    const uint32_t node = input->token_nodes != NULL && token < input->token_count ? input->token_nodes[token] : BUSTER_CHECK_NONE;
    const CheckGraph* graph = stream != NULL ? stream->graph : NULL;
    const bool shift = types != NULL && graph != NULL && node < stream->count && graph->ops[node] == CHECK_OP_SHIFT_LEFT &&
                       graph->operand0[node] < stream->count && graph->operand1[node] < stream->count;
    if (warning == CHECK_WARNING_SHIFT_NEGATIVE_VALUE)
    {
        result = "shifting a negative signed value is undefined";
    }
    else if (shift)
    {
        const uint32_t left = graph->operand0[node];
        const uint32_t right = graph->operand1[node];
        const TypeId type = check_stream_type(stream, node);
        uint32_t width = 0;
        bool is_signed = false;
        uint32_t count_width = 0;
        bool count_signed = false;
        if (check_action_integer_type(types, type, &width, &is_signed) && width != 0 && width <= 128 &&
            check_action_integer_type(types, check_stream_type(stream, right), &count_width, &count_signed) &&
            check_stream_constant_kind(stream, left) == CHECK_CONSTANT_INTEGER && check_stream_constant_kind(stream, right) == CHECK_CONSTANT_INTEGER)
        {
            const unsigned __int128 value = check_stream_value(stream, left).integer & check_action_mask(width);
            const uint32_t count = (uint32_t)(check_stream_value(stream, right).integer & 0xffu);
            const uint32_t length = value == 0 ? 0 : (uint64_t)(value >> 64) != 0 ? 128u - (uint32_t)__builtin_clzll((uint64_t)(value >> 64)) :
                                                                                   64u - (uint32_t)__builtin_clzll((uint64_t)value);
            // The hexadecimal digits of value << count: those of value << (count % 4), three 64-bit limbs wide, then
            // count / 4 zeros.
            const uint32_t nibble = count % 4u;
            uint64_t limbs[3] = { (uint64_t)value, (uint64_t)(value >> 64), 0 };
            limbs[2] = nibble != 0 ? limbs[1] >> (64u - nibble) : 0;
            limbs[1] = nibble != 0 ? limbs[1] << nibble | limbs[0] >> (64u - nibble) : limbs[1];
            limbs[0] = limbs[0] << nibble;
            char digits[64 + 1];
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
            digits[used] = 0;
            const uint32_t spelling = check_stream_type(stream, left) == type ? check_input_operand_type_spelling(input, parser, left) : 0;
            const String8 name = check_driver_warning_type(parser, pp, input, types, spell, scratch, type, spelling);
            if (name.length != 0)
            {
                result = check_warning_text(scratch, "signed shift result (0x%s%.*s) requires %u bits to represent, but %.*s only has %u bits", digits,
                                            (int)(count / 4u), "00000000000000000000000000000000000000000000000000000000000000000000",
                                            length + count + 1u, (int)name.length, name.pointer, width);
            }
        }
    }
    return result;
}

// A legacy warning's command-line and source policy: the same sparse group mapping the event warnings use, so
// -Wno-NAME, -Werror=NAME and -Wno-everything reach the dense warning bits too.
static CheckWarningSeverity check_driver_legacy_severity(Preprocessor* pp, const CheckInput* input, uint32_t token, uint32_t warning, bool default_ignored)
{
    const uint32_t index = check_diagnostic_warning_index(warning);
    const CheckWarningCommandLine command = check_warning_command_line(&pp->options, index, default_ignored, false);
    CheckWarningSeverity severity = command.quiet ? CHECK_WARNING_IGNORE :
        check_diagnostic_severity_id_from(input, token, BUSTER_CHECK_NONE, index, command.severity);
    if (severity != CHECK_WARNING_IGNORE && !command.system_headers && check_graph_system_token(&input->graph, token))
    {
        severity = CHECK_WARNING_IGNORE;
    }
    return severity;
}

// The reader's warning events to publish on a unit with errors (#1447), as clang leaves out those its errors suppress:
// - ActOnEndOfTranslationUnit gives no unused file-scope declaration's warning once an error has occurred, so a rejected
//   unit's unused functions' and file-scope objects' warnings, which complete at the unit's end, are left out;
// - ActOnPopScope gives no unused-variable warning of a scope in which an error occurred (Scope's
//   hasUnrecoverableErrorOccurred), from its opening to its end, nested scopes' errors among them; a for initializer's
//   scope runs from its header through the body's end, and a function body's scope opens at its definition's start,
//   as ActOnStartOfFunctionDef handles the definition's declarator inside it.
// `rejected` says the unit has an error; `errors`, where its reports locate some, counts them before each token (a prefix
// count, check_driver_error_counts), else is NULL. A unit without an error publishes its events as they are, with no copy;
// another publishes a copy in `scratch`. Whether there was room.
static bool check_driver_rejected_warnings(const Parser* parser, const CheckWarningEvents* events, bool rejected, const uint32_t* errors, Arena* scratch,
                                           CheckWarningEvents* view)
{
    *view = *events;
    bool valid = true;
    const uint32_t count = parser->tokens.count;
    if (rejected && events->count != 0)
    {
        CheckWarningEvent* kept = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckWarningEvent, events->count);
        valid = kept != NULL;
        uint32_t total = 0;
        for (uint32_t i = 0; valid && i < events->count; i += 1)
        {
            const CheckWarningEvent* event = events->events + i;
            const bool unused = event->kind == CHECK_WARNING_EVENT_UNUSED_VARIABLE || event->kind == CHECK_WARNING_EVENT_UNUSED_FUNCTION;
            const bool unit_end = unused && event->completion_kind == CHECK_WARNING_COMPLETE_TU_END;
            const uint32_t close = unused && event->completion_kind == CHECK_WARNING_COMPLETE_AFTER_DELIMITER ? event->completion : count;
            // The scope's opening delimiter, before the declaration. A for initializer retains its header's `)`
            // separately from completion at the statement's end; a later error in the enclosing block isn't its own.
            const uint32_t anchor = event->payload.token;
            const bool for_scope = event->kind == CHECK_WARNING_EVENT_UNUSED_VARIABLE && anchor < count && parser->tree.roles[anchor] == PARSE_ROLE_CLOSE_FOR;
            const uint32_t delimiter = for_scope ? anchor : close;
            const uint8_t opening_kind = for_scope ? PARSE_KIND_L_PAREN : PARSE_KIND_L_BRACE;
            uint32_t open = close < count && event->token < close ? event->token : 0;
            while (open > 0 && !(parser->tree.parents[open] == delimiter && parser->tokens.kinds[open] == opening_kind))
            {
                open -= 1;
            }
            // A body's scope from its definition's start: after the previous declaration's `;` or body's `}`.
            uint32_t start = open;
            while (close < count && parser->tree.roles[open] == PARSE_ROLE_OPEN_BODY && start > 0 && parser->tokens.kinds[start - 1] != PARSE_KIND_SEMI &&
                   parser->tree.roles[start - 1] != PARSE_ROLE_CLOSE_BODY)
            {
                start -= 1;
            }
            const bool scoped = errors != NULL && close < count && errors[close + 1] != errors[start];
            kept[total] = *event;
            total += !(unit_end || scoped);
        }
        view->events = valid ? kept : events->events;
        view->count = valid ? total : events->count;
    }
    return valid;
}

// The class of a reader's warning event for check_driver_unit_end_split: 0 published where it stands, 1 a tentative
// definition's (#2238's array, #2168's const object), 2 an unused file-scope declaration's.
static uint32_t check_driver_unit_end_class(const CheckWarningEvent* event)
{
    const bool unit_end = event->completion_kind == CHECK_WARNING_COMPLETE_TU_END;
    const bool unused = (event->kind == CHECK_WARNING_EVENT_UNUSED_VARIABLE || event->kind == CHECK_WARNING_EVENT_UNUSED_FUNCTION) && unit_end;
    const bool tentative = event->kind == CHECK_WARNING_EVENT_TENTATIVE_ARRAY || (event->kind == CHECK_WARNING_EVENT_DEFAULT_CONST_INIT && unit_end);
    return tentative ? 1u : unused ? 2u : 0u;
}

// Splits a reader's warning events (`events`) into those published where they stand (`kept`) and the unit-end ones
// (`late`), which clang's ActOnEndOfTranslationUnit gives after every other diagnostic (#2238): late[0] the tentative
// arrays' warnings, late[1] the unused file-scope declarations', which it leaves out once any error has occurred, a
// warning -Werror promotes included. Each keeps its order. `kept` may be `events`; a unit with unit-end events copies
// them into `arena`. Whether there was room.
static bool check_driver_unit_end_split(const CheckWarningEvents* events, Arena* arena, CheckWarningEvents* kept, CheckWarningEvents late[2])
{
    const CheckWarningEvents source = *events;
    uint32_t counts[3] = { 0 };
    for (uint32_t i = 0; i < source.count; i += 1)
    {
        counts[check_driver_unit_end_class(source.events + i)] += 1;
    }
    late[0] = (CheckWarningEvents){ .notes = source.notes, .note_count = source.note_count };
    late[1] = late[0];
    CheckWarningEvent* copy = counts[1] + counts[2] != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, CheckWarningEvent, source.count) : NULL;
    const bool valid = counts[1] + counts[2] == 0 || copy != NULL;
    if (copy != NULL)
    {
        uint32_t cursors[3] = { 0, counts[0], counts[0] + counts[1] };
        for (uint32_t i = 0; i < source.count; i += 1)
        {
            const uint32_t class = check_driver_unit_end_class(source.events + i);
            copy[cursors[class]++] = source.events[i];
        }
        *kept = (CheckWarningEvents){ .events = copy, .count = counts[0], .notes = source.notes, .note_count = source.note_count };
        late[0].events = copy + counts[0];
        late[0].count = counts[1];
        late[1].events = copy + counts[0] + counts[1];
        late[1].count = counts[2];
    }
    else
    {
        *kept = source;
    }
    return valid;
}

// Whether `token` is inside a function's body, between its braces: a definition's head is under its body's `}` in the
// tree too, so the body's `{` decides. Only a recovered report's scope asks it (check_driver_error_scoped).
static bool check_driver_in_body(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t ancestor = token;
    uint32_t steps = 0;
    while (ancestor < count && parser->tree.roles[ancestor] != PARSE_ROLE_CLOSE_BODY && steps < count)
    {
        ancestor = parser->tree.parents[ancestor];
        steps += 1;
    }
    const bool closed = ancestor < count && parser->tree.roles[ancestor] == PARSE_ROLE_CLOSE_BODY;
    uint32_t open = closed ? ancestor : 0;
    while (open > 0 && !(parser->tree.roles[open] == PARSE_ROLE_OPEN_BODY && parser->tree.parents[open] == ancestor))
    {
        open -= 1;
    }
    return closed && open < token;
}

// Whether a recovered report is one whose declaration stays valid (#1835): a dropped attribute's
// (check_input_attribute_recoverable) or a dropped specifier's (check_input_specifiers_recover). A rejected unit's rounds
// read past it (check_driver_rejected_common).
static bool check_driver_report_keeps_valid(const CheckInputReport* report)
{
    const CheckInputMessage message = report->message;
    const bool specifier = message == CHECK_INPUT_SPECIFIER_COMBINE || message == CHECK_INPUT_SPECIFIER_SIGN || message == CHECK_INPUT_SPECIFIER_WIDTH ||
                           message == CHECK_INPUT_SPECIFIER_COMPLEX;
    return report->recovered && (specifier || check_input_attribute_recoverable(report));
}

// Whether `token` is inside a declarator's parameter list: an enclosing `(`, matched backward over the tokens, is one the
// parser marks a parameter list's (both paths do, #1968), before a `;` or a brace.
static bool check_driver_in_parameters(const Parser* parser, uint32_t token)
{
    uint32_t depth = 0;
    bool inside = false;
    bool scanning = token < parser->tokens.count;
    for (uint32_t at = token; scanning && at > 0;)
    {
        at -= 1;
        const uint8_t kind = parser->tokens.kinds[at];
        inside = kind == PARSE_KIND_L_PAREN && depth == 0 && parser->tree.roles[at] == PARSE_ROLE_OPEN_FUNCTION;
        depth += kind == PARSE_KIND_R_PAREN ? 1u : 0u;
        depth -= kind == PARSE_KIND_L_PAREN && depth != 0 ? 1u : 0u;
        scanning = !inside && !(depth == 0 && (kind == PARSE_KIND_SEMI || kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_R_BRACE));
    }
    return inside;
}

// Whether a reader's report counts in the scopes around it (check_driver_rejected_warnings). Clang opens a definition's
// body's scope before it handles the definition's declarator (ActOnStartOfFunctionDef: its own attributes, its
// redeclaration), whose errors then keep the body's unused variables quiet; but what it gives as it parses, before that
// (#1835), doesn't count: a specifier's (DeclSpec::Finish), a parameter's attributes' (ActOnParamDeclarator), and an
// attribute's argument that must be a string literal (the parser's err_expected_string_literal). Nor does a jump's
// (#1942): clang's JumpScopeChecker runs in ActOnFinishFunctionBody, once the body's scopes have closed.
static bool check_driver_error_scoped(const Parser* parser, const CheckInput* input, const CheckInputReport* report)
{
    const CheckInputMessage message = report->message;
    const bool specifier = message == CHECK_INPUT_SPECIFIER_COMBINE || message == CHECK_INPUT_SPECIFIER_SIGN || message == CHECK_INPUT_SPECIFIER_WIDTH ||
                           message == CHECK_INPUT_SPECIFIER_COMPLEX;
    const bool attribute = check_input_attribute_recoverable(report);
    const bool parsed = message == CHECK_INPUT_ATTRIBUTE_STRING_ARGUMENT ||
                        (message == CHECK_INPUT_ATTRIBUTE_ARGUMENT && (report->detail & 0xffu) == CHECK_ARGUMENT_STRING_LITERAL);
    const bool early = report->recovered && (specifier || (attribute && (parsed || check_driver_in_parameters(parser, report->token))));
    const bool jump = message == CHECK_INPUT_JUMP_GOTO || message == CHECK_INPUT_JUMP_SWITCH || message == CHECK_INPUT_JUMP_INDIRECT ||
                      message == CHECK_INPUT_JUMP_ASM;
    return !jump && (!early || check_driver_in_body(parser, report->token));
}

// Each parameter has its own specifiers. Unlike a declaration's comma-separated declarators, a parameter cannot
// inherit a typedef spelling from the preceding parameter or the function's return type.
static uint32_t check_driver_parameter_spelling(const Parser* parser, const CheckInput* input, uint32_t token)
{
    uint32_t result = 0;
    bool scanning = token < input->token_count;
    while (scanning && result == 0 && token != 0)
    {
        token -= 1;
        const uint8_t role = parser->tree.roles[token];
        scanning = role != PARSE_ROLE_SEPARATOR && role != PARSE_ROLE_OPEN_FUNCTION && !check_input_declaration_boundary(role);
        if (scanning && role == PARSE_ROLE_SPECIFIER && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && input->declarations != NULL)
        {
            const uint32_t declaration = input->declarations[token];
            result = declaration < input->token_count && input->declaration_kinds[declaration] == C_BINDING_TYPEDEF ? declaration : 0;
        }
    }
    return result;
}

// Whether `token`, below `end`, starts a parameter's declarator rather than continuing its declaration's specifiers: a
// `*`, its name, an abstract group, a function's parameter list or an array's bound.
static bool check_driver_declarator_start(const Parser* parser, uint32_t token)
{
    const uint8_t kind = parser->tokens.kinds[token];
    const uint8_t role = parser->tree.roles[token];
    return kind == PARSE_KIND_STAR || (kind == PARSE_KIND_IDENTIFIER && role == PARSE_ROLE_OPERAND) || role == PARSE_ROLE_OPEN_GROUP ||
           (kind == PARSE_KIND_L_PAREN && (role == PARSE_ROLE_OPEN_CALL || role == PARSE_ROLE_OPEN_FUNCTION)) ||
           (kind == PARSE_KIND_L_SQUARE && role == PARSE_ROLE_OPEN_SUBSCRIPT);
}

// The token a parameter's note names (#147), of the parameter from `begin` to `end` (its `,` or `)`): as the declarator
// reader finds it (check_input_declarator_step's frames), past the declaration's specifiers, then each frame's pointer
// prefix (`*` and the qualifiers, _Atomic and attributes after it) and the abstract group it opens: there the name, or,
// unnamed, the token where the name would be (the reader's missing_name). BUSTER_CHECK_NONE for an empty parameter and for
// an identifier list's (a K&R declaration's, whose names have no notes).
static uint32_t check_driver_parameter_declarator(const Parser* parser, uint32_t begin, uint32_t end)
{
    uint32_t result = BUSTER_CHECK_NONE;
    const bool listed = begin < end && !(parser->tokens.kinds[begin] == PARSE_KIND_IDENTIFIER && parser->tree.roles[begin] == PARSE_ROLE_OPERAND);
    uint32_t i = begin;
    while (listed && i < end && !check_driver_declarator_start(parser, i))
    {
        const uint8_t role = parser->tree.roles[i];
        const uint32_t close = parser->tree.parents[i];
        const bool group = role < PARSE_ROLE_COUNT && parse_closer_roles[role] != PARSE_ROLE_NONE && close > i && close < end;
        i = check_input_attribute_starts(parser, i, end) ? check_input_skip_attributes(parser, i, end) : group ? close + 1 : i + 1;
    }
    uint32_t frame_end = end;
    bool descending = listed;
    for (uint32_t steps = 0; descending && steps < end - begin; steps += 1)
    {
        i = check_input_skip_attributes(parser, i, frame_end);
        while (i < frame_end && parser->tokens.kinds[i] == PARSE_KIND_STAR)
        {
            i += 1;
            uint32_t qualifiers = 0;
            bool prefix = true;
            while (prefix && i < frame_end)
            {
                const bool atomic = parser->tokens.kinds[i] == PARSE_KIND_ATOMIC && (i + 1 == frame_end || parser->tokens.kinds[i + 1] != PARSE_KIND_L_PAREN);
                const bool attribute = !atomic && check_input_attribute_starts(parser, i, frame_end);
                prefix = atomic || attribute || check_input_qualifier(parser, i, &qualifiers);
                i = attribute ? check_input_skip_attributes(parser, i, frame_end) : prefix ? i + 1 : i;
            }
        }
        const uint32_t close = i < frame_end ? parser->tree.parents[i] : BUSTER_CHECK_NONE;
        const uint32_t inner = close < frame_end && close > i ? check_input_skip_attributes(parser, i + 1, close) : BUSTER_CHECK_NONE;
        const bool group = close < frame_end && close > i &&
                           (parser->tree.roles[i] == PARSE_ROLE_OPEN_GROUP ||
                            (parser->tokens.kinds[i] == PARSE_KIND_L_PAREN && inner < close && parser->tokens.kinds[inner] == PARSE_KIND_STAR));
        descending = group;
        result = group ? result : i;
        frame_end = group ? close : frame_end;
        i = group ? i + 1 : i;
    }
    return result;
}

// The note token of parameter `index` of the function declared at `declaration` (its name), as the declarator reader
// placed it (check_driver_parameter_declarator), found where a warning or an error gives the note rather than kept for
// every prototype. BUSTER_CHECK_NONE where the declaration's own list, right after its name, has no such parameter: an
// empty-list redeclaration's notes are none even where the prototype it inherits has them.
static uint32_t check_driver_parameter_source(const Parser* parser, const CheckInput* input, uint32_t declaration, uint32_t index)
{
    const uint32_t count = parser->tokens.count < input->token_count ? parser->tokens.count : input->token_count;
    uint32_t open = declaration + 1;
    while (open < count && parser->tokens.kinds[open] == PARSE_KIND_R_PAREN)
    {
        open += 1;
    }
    open = check_input_skip_attributes(parser, open, count);
    const bool listed = open < count && parser->tokens.kinds[open] == PARSE_KIND_L_PAREN;
    const uint32_t close = listed ? parser->tree.parents[open] : 0;
    uint32_t result = BUSTER_CHECK_NONE;
    uint32_t begin = open + 1;
    uint32_t ordinal = 0;
    bool done = !listed || close >= count || close <= open;
    while (!done)
    {
        uint32_t end = begin;
        while (end < close && parser->tokens.kinds[end] != PARSE_KIND_COMMA)
        {
            const uint8_t role = parser->tree.roles[end];
            const uint32_t closer = parser->tree.parents[end];
            end = role < PARSE_ROLE_COUNT && parse_closer_roles[role] != PARSE_ROLE_NONE && closer > end && closer < close ? closer + 1 : end + 1;
        }
        result = ordinal == index ? check_driver_parameter_declarator(parser, begin, end) : BUSTER_CHECK_NONE;
        done = ordinal == index || end >= close;
        ordinal += 1;
        begin = end + 1;
    }
    return result;
}

// A call's warning bits summarize its arguments. Expand those two pointer bits together, walking arguments once so mixed
// warning groups retain argument order. Function-pointer calls, including an explicit (*f), have no named declaration for
// parameter notes. A direct use resolves to the visible declaration, whose own parameter list gives the note
// (check_driver_parameter_source).
static bool check_driver_pointer_arguments(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                            const TypeTable* types, CheckWarningSpelling* spell, uint32_t token, uint32_t flags, uint32_t enabled,
                                            uint32_t publication, PPDiagnosticPhase phase, Arena* scratch)
{
    const uint32_t node = input->token_nodes != NULL && token < input->token_count ? input->token_nodes[token] : BUSTER_CHECK_NONE;
    const CheckGraph* graph = stream != NULL ? stream->graph : NULL;
    const CheckOp op = types != NULL && graph != NULL && node < stream->count ? graph->ops[node] : CHECK_OP_COUNT;
    // A builtin's call (#2189): its arguments a list's, or its operands' (`__builtin_strcpy`), each converted to its
    // prototype's parameter, which no declaration in the source names.
    const bool builtin = op != CHECK_OP_CALL && op != CHECK_OP_COUNT && check_graph_has_arguments(op);
    const bool lists = graph != NULL && graph->argument_offsets != NULL && graph->argument_counts != NULL && stream->argument_conversions != NULL;
    const bool listed = lists && (op == CHECK_OP_CALL || (builtin && graph->argument_counts[node] != 0));
    const bool operands = builtin && !listed && stream->conversions != NULL;
    const bool call = listed || operands;
    if (call)
    {
        const uint32_t callee = graph->operand0[node];
        const uint32_t name = callee < stream->count ? graph->tokens[callee] : BUSTER_CHECK_NONE;
        const uint32_t declaration = name < input->token_count && input->declarations != NULL ? input->declarations[name] : BUSTER_CHECK_NONE;
        const bool direct = op == CHECK_OP_CALL && callee < stream->count && graph->ops[callee] == CHECK_OP_VALUE &&
                            check_action_kind(types, check_stream_type(stream, callee)) == BUSTER_TYPE_KIND_FUNCTION &&
                            declaration < input->token_count && input->declaration_kinds != NULL &&
                            input->declaration_kinds[declaration] == C_BINDING_FUNCTION;
        const uint32_t offset = listed ? graph->argument_offsets[node] : 0;
        const uint32_t arguments = listed ? graph->argument_counts[node] : 3;
        for (uint32_t i = 0; !pp->fatal && i < arguments; i += 1)
        {
            const uint32_t argument = listed ? graph->argument_nodes[offset + i] : i == 0 ? graph->operand0[node] : i == 1 ? graph->operand1[node] : graph->operand2[node];
            const CheckConversion conversion = listed ? stream->argument_conversions[offset + i] : stream->conversions[node].operands[i];
            if (argument < stream->count && (conversion.flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0)
            {
                const CheckValue value = check_stream_value(stream, argument);
                bool nested = false;
                const CheckActionResult checked = check_driver_pointer_warning(types, graph, node, conversion.type, &value, &nested);
                const uint32_t warning = checked.warnings & flags & (CHECK_WARNING_POINTER_SIGN | CHECK_WARNING_DISCARDED_QUALIFIERS);
                if (checked.diagnostic == CHECK_DIAG_RESOURCE)
                {
                    pp_diagnose_output(pp, PP_SEVERITY_FATAL, parser->tokens.sources[token], "%s", check_driver_action_message(checked.diagnostic));
                }
                if (warning != 0)
                {
                    const uint32_t located = check_warning_graph_first(graph, parser, argument);
                    const CheckWarningSeverity severity = check_driver_legacy_severity(pp, input, located, warning, (enabled & warning) == 0);
                    if (severity != CHECK_WARNING_IGNORE)
                    {
                        const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                                       severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
                        // (A pointer parameter's, as a pointer conversion's warning is.)
                        const uint32_t note_token = direct ? check_driver_parameter_source(parser, input, declaration, i) : BUSTER_CHECK_NONE;
                        const uint32_t spelling = check_driver_pointer_spelling(parser, input, stream, types, argument);
                        const uint32_t target_spelling = note_token < input->token_count ? check_driver_parameter_spelling(parser, input, note_token) : 0;
                        const String8 source = check_driver_warning_type(parser, pp, input, types, spell, scratch, value.type, spelling);
                        const String8 target = check_driver_warning_type(parser, pp, input, types, spell, scratch, conversion.type, target_spelling);
                        const char* note = NULL;
                        if (note_token < input->token_count)
                        {
                            const bool named = parser->tokens.kinds[note_token] == PARSE_KIND_IDENTIFIER && input->declarations[note_token] == note_token &&
                                               input->declaration_kinds[note_token] == C_BINDING_OBJECT;
                            const uint32_t identifier = named ? parser->tokens.names[note_token] : 0;
                            const uint32_t length = named ? intern_length(pp->table, identifier) : 0;
                            const uint8_t* text = named ? intern_bytes(pp->table, identifier) : NULL;
                            note = named ? check_warning_text(scratch, "passing argument to parameter '%.*s' here", (int)length, text) :
                                           "passing argument to parameter here";
                        }
                        const PPOutputDiagnosticNote notes[1] =
                        {
                            { .token = note_token < input->token_count ? parser->tokens.sources[note_token] : 0, .message = note },
                        };
                        const uint32_t index = check_diagnostic_warning_index(warning);
                        if (source.length != 0 && target.length != 0 && (note_token == BUSTER_CHECK_NONE || note != NULL))
                        {
                            pp_diagnose_output_notes_phase(pp, pp_severity, parser->tokens.sources[located], publication, phase, notes, note != NULL,
                                "passing %.*s to parameter of type %.*s %s [%s%s]", (int)source.length, source.pointer, (int)target.length, target.pointer,
                                check_driver_pointer_wording(types, warning, nested, conversion.type, value.type),
                                severity >= CHECK_WARNING_ERROR ? "-Werror," : "", check_warning_descriptions[index].option);
                        }
                        else
                        {
                            pp_diagnose_output(pp, PP_SEVERITY_FATAL, parser->tokens.sources[located], "%s", check_driver_action_message(CHECK_DIAG_RESOURCE));
                        }
                    }
                }
            }
        }
    }
    return call;
}

// clang's kinds of a failed simple assignment's conversion (Sema::CheckAssignmentConstraints, #147), each its own message:
// an integer to a pointer (IntToPointer, -Wint-conversion's), a pointer, array or function to an integer (PointerToInt), two
// pointers of incompatible types (IncompatiblePointer, -Wincompatible-pointer-types'; two pointers to functions,
// IncompatibleFunctionPointer's -Wincompatible-function-pointer-types), and any other (Incompatible, an error of no flag).
typedef enum CheckDriverConversion
{
    CHECK_DRIVER_CONVERSION_INT_POINTER,
    CHECK_DRIVER_CONVERSION_POINTER_INT,
    CHECK_DRIVER_CONVERSION_POINTERS,
    CHECK_DRIVER_CONVERSION_FUNCTION_POINTERS,
    CHECK_DRIVER_CONVERSION_INCOMPATIBLE,
} CheckDriverConversion;

// A converted node's type as check_driver_conversion_kind takes it: an lvalue's atomic type is converted to its value's,
// and an rvalue's is kept (a cast's, #2230).
static TypeId check_driver_conversion_source(const TypeTable* types, const CheckStream* stream, uint32_t node)
{
    const TypeId type = check_stream_type(stream, node);
    return check_stream_category(stream, node) == CHECK_RVALUE ? type : type_conversion_operand(types, type);
}

static CheckDriverConversion check_driver_conversion_kind(const TypeTable* types, TypeId target, TypeId source)
{
    const TypeId to = type_conversion_operand(types, target);
    const TypeId from = type_conversion_operand(types, source);
    const uint8_t to_kind = type_kind(types, to);
    const uint8_t from_kind = type_kind(types, from);
    const bool from_pointer = from_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(from_kind) || from_kind == BUSTER_TYPE_KIND_FUNCTION;
    const bool from_integer = from_kind == BUSTER_TYPE_KIND_INTEGER || from_kind == BUSTER_TYPE_KIND_ENUM || from_kind == BUSTER_TYPE_KIND_BIT_INT;
    const bool to_integer = to_kind == BUSTER_TYPE_KIND_INTEGER || to_kind == BUSTER_TYPE_KIND_ENUM || to_kind == BUSTER_TYPE_KIND_BIT_INT;
    const bool to_function = to_kind == BUSTER_TYPE_KIND_POINTER && type_kind(types, type_conversion_operand(types, type_element(types, to))) == BUSTER_TYPE_KIND_FUNCTION;
    const bool from_function = from_kind == BUSTER_TYPE_KIND_FUNCTION ||
                               (from_kind == BUSTER_TYPE_KIND_POINTER && type_kind(types, type_conversion_operand(types, type_element(types, from))) == BUSTER_TYPE_KIND_FUNCTION);
    // An atomic rvalue, a cast's (#2230), converts to nothing but an atomic type, which clang words as incompatible.
    const bool atomic = type_kind(types, type_unqualified(type_strip_aligned(types, source))) == BUSTER_TYPE_KIND_ATOMIC;
    return atomic ? CHECK_DRIVER_CONVERSION_INCOMPATIBLE :
           to_kind == BUSTER_TYPE_KIND_POINTER && from_integer ? CHECK_DRIVER_CONVERSION_INT_POINTER :
           to_integer && from_pointer ? CHECK_DRIVER_CONVERSION_POINTER_INT :
           to_kind == BUSTER_TYPE_KIND_POINTER && from_pointer ? (to_function && from_function ? CHECK_DRIVER_CONVERSION_FUNCTION_POINTERS : CHECK_DRIVER_CONVERSION_POINTERS) :
           CHECK_DRIVER_CONVERSION_INCOMPATIBLE;
}

// Whether a graph's report is one of clang's warnings that default to errors (#1969): a failed simple assignment's
// conversion between an integer and a pointer (-Wint-conversion) or between pointers (-Wincompatible-pointer-types,
// -Wincompatible-function-pointer-types), as check_driver_conversion_error words it, or a value returned from a void
// function (-Wreturn-mismatch). Clang's DiagnosticIDs::isUnrecoverable doesn't count a warning's class, whatever its
// severity, so such an error leaves its scope's unused variables warned (Scope::hasUnrecoverableErrorOccurred).
static bool check_driver_report_default_error(const Parser* parser, const CheckStream* stream, const TypeTable* types, const CheckReport* report,
                                              uint32_t* converted_source, TypeId* converted_target)
{
    const CheckGraph* graph = stream->graph;
    const uint32_t node = report->node;
    const uint32_t token = report->token;
    const CheckOp op = node < stream->count ? graph->ops[node] : CHECK_OP_COUNT;
    const bool incompatible = report->message == CHECK_MESSAGE_ACTION && report->diagnostic == CHECK_DIAG_INCOMPATIBLE;
    const bool returning = op == CHECK_OP_CONVERT && token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_RETURN;
    const bool void_return = returning && check_action_kind(types, graph->types[node]) == BUSTER_TYPE_KIND_VOID;
    const uint32_t argument_index = op == CHECK_OP_CALL ? stream->diagnostic_operands[node] : 0;
    const bool passing = op == CHECK_OP_CALL && argument_index != 0 && graph->argument_counts != NULL && argument_index <= graph->argument_counts[node];
    uint32_t source = BUSTER_CHECK_NONE;
    TypeId target = 0;
    if (incompatible && (op == CHECK_OP_INITIALIZE || returning))
    {
        source = graph->operand0[node];
        target = graph->types[node];
    }
    else if (incompatible && op == CHECK_OP_ASSIGN)
    {
        source = graph->operand1[node];
        target = graph->operand0[node] < stream->count ? check_stream_type(stream, graph->operand0[node]) : 0;
    }
    else if (incompatible && passing)
    {
        const uint32_t index = argument_index - 1;
        source = graph->argument_nodes[graph->argument_offsets[node] + index];
        const uint32_t callee = graph->operand0[node];
        TypeId function = callee < stream->count ? type_strip_aligned(types, check_stream_type(stream, callee)) : 0;
        function = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, function)) : function;
        uint32_t parameter_count = 0;
        const TypeId* parameters = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION ? type_parameters(types, function, &parameter_count) : NULL;
        target = parameters != NULL && index < parameter_count ? parameters[index] : 0;
    }
    const TypeId value = source < stream->count ? check_driver_conversion_source(types, stream, source) : 0;
    const bool converted = incompatible && !void_return && value != 0 && target != 0 &&
                           check_driver_conversion_kind(types, target, value) != CHECK_DRIVER_CONVERSION_INCOMPATIBLE;
    *converted_source = converted ? source : BUSTER_CHECK_NONE;
    *converted_target = converted ? target : 0;
    return (incompatible && void_return) || converted;
}

// The site of a conversion that defaults to an error at `token` (#1969, CheckDriverConversionSite), of `source`'s value to
// `target`, in a body: its source's tokens. False where it has none of its own.
static bool check_driver_conversion_site(const Parser* parser, const CheckStream* stream, const TypeTable* types, uint32_t token, uint32_t source, TypeId target,
                                         CheckDriverConversionSite* site)
{
    const CheckGraph* graph = stream->graph;
    const uint32_t count = parser->tokens.count;
    // A condition's assignment keeps its part left out: clang's -Wparentheses of it follows the conversion's error, which
    // completes after its source, where the common warnings publish it at the `=` (check_common_parentheses).
    const uint32_t parent = token < count ? parser->tree.parents[token] : BUSTER_CHECK_NONE;
    const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
    const bool condition = parent_role == PARSE_ROLE_CLOSE_CONDITION || parent_role == PARSE_ROLE_CLOSE_FOR;
    const bool known = source < stream->count && target != 0 && token < count && !condition && check_driver_in_body(parser, token);
    const uint32_t operand = known ? check_warning_graph_first(graph, parser, source) : BUSTER_CHECK_NONE;
    // An array compound literal's node is its body's `}` (its decay's too), so its own first token is its type name's `(`:
    // a cast before the `}` would break the view's reading and lose the body's warnings (`char *b = (int[2]){ 1, 2 };`).
    const uint8_t operand_role = operand < count ? parser->tree.roles[operand] : PARSE_ROLE_NONE;
    const bool compound = operand_role == PARSE_ROLE_CLOSE_COMPOUND_BODY || operand_role == PARSE_ROLE_CLOSE_COMPOUND;
    const uint32_t first = compound ? check_warning_graph_start(parser, operand) : operand;
    const uint32_t end = known ? graph->source_positions[source] : 0;
    const bool valid = first < end && end <= count;
    *site = (CheckDriverConversionSite){
        .token = token,
        .first = valid ? first : BUSTER_CHECK_NONE,
        .last = valid ? end - 1 : BUSTER_CHECK_NONE,
        .pointer = valid && type_kind(types, type_conversion_operand(types, target)) == BUSTER_TYPE_KIND_POINTER,
    };
    return valid;
}

// The prefix count of the unit's errors' tokens (check_driver_rejected_warnings): its reader's reports, its graph's and the
// driver's own, but a recovered report outside every body (check_driver_error_scoped), and a warning that defaults to an
// error: the reader's missing return value (-Wreturn-mismatch), the graph's (check_driver_report_default_error) and an
// initializer's conversion's (CheckDriverDiagnostic.default_error), which clang's scopes don't count (#1969). NULL without any, or without room.
static uint32_t* check_driver_error_counts(const Parser* parser, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                           const CheckDriverDiagnostic* extra, uint32_t extra_count, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    const bool any = input->report_count != 0 || stream->report_count != 0 || extra_count != 0;
    uint32_t* errors = any ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 2) : NULL;
    if (errors != NULL)
    {
        memset(errors, 0, ((uint64_t)count + 2) * sizeof(uint32_t));
        // (An invalid declaration's quiet use is no error of its scope's, #1447: clang warns of its unused variables.)
        for (uint32_t i = 0; i < input->report_count; i += 1)
        {
            errors[(input->reports[i].token < count ? input->reports[i].token : count) + 1] +=
                check_driver_error_scoped(parser, input, input->reports + i) && input->reports[i].message != CHECK_INPUT_INVALID_USE &&
                input->reports[i].message != CHECK_INPUT_RETURN ? 1u : 0u;
        }
        for (uint32_t i = 0; i < stream->report_count; i += 1)
        {
            uint32_t source = BUSTER_CHECK_NONE;
            TypeId target = 0;
            errors[(stream->reports[i].token < count ? stream->reports[i].token : count) + 1] +=
                check_driver_report_default_error(parser, stream, types, stream->reports + i, &source, &target) ? 0u : 1u;
        }
        for (uint32_t i = 0; i < extra_count; i += 1)
        {
            errors[(extra[i].token < count ? extra[i].token : count) + 1] += extra[i].default_source != 0 ? 0u : 1u;
        }
        for (uint32_t t = 1; t < count + 2; t += 1)
        {
            errors[t] += errors[t - 1];
        }
    }
    return errors;
}

// Whether `to` is at least as qualified as `from` and, through one level of pointers where both are, of the same
// unqualified type and at least as qualified (clang's compareTypesSimple in C, of canonical types). An address space is
// a qualifier too (compatiblyIncludes): an `AS1 int` is no `int` (#1447 item 2).
static bool check_driver_conversion_same(const TypeTable* types, TypeId from, TypeId to)
{
    const uint32_t qualifiers = BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE | BUSTER_TYPE_RESTRICT;
    bool same = (type_qualifiers(from) & ~type_qualifiers(to) & qualifiers) == 0 &&
                type_address_space(type_object_qualifiers(types, from)) == type_address_space(type_object_qualifiers(types, to));
    TypeId left = type_strip_aligned(types, from);
    TypeId right = type_strip_aligned(types, to);
    if (type_kind(types, left) == BUSTER_TYPE_KIND_POINTER && type_kind(types, right) == BUSTER_TYPE_KIND_POINTER)
    {
        left = type_strip_aligned(types, type_element(types, left));
        right = type_strip_aligned(types, type_element(types, right));
    }
    same = same && type_unqualified(left) == type_unqualified(right) && (type_qualifiers(left) & ~type_qualifiers(right) & qualifiers) == 0 &&
           type_address_space(type_object_qualifiers(types, left)) == type_address_space(type_object_qualifiers(types, right));
    return same;
}

// clang's fix-it of a failed conversion of `node`'s value of `source`'s type to `target` (ConversionFixItGenerator::
// tryToFixConversion, #147), as the message's last words: a pointer whose pointee converts, dereferenced ("; dereference
// with *", or "; remove &" of an `&x`, but none of a null pointer constant); an lvalue whose address converts to a target
// pointer ("; take the address with &", or "; remove *" of a `*p`, but none of a pointer to a void pointer); else none.
static const char* check_driver_conversion_hint(const TypeTable* types, const CheckStream* stream, uint32_t node, TypeId source, TypeId target)
{
    const CheckGraph* graph = stream->graph;
    const TypeId from = type_strip_aligned(types, source);
    const TypeId to = type_strip_aligned(types, target);
    uint32_t bare = node;
    // The operand without its parentheses and casts, as clang's IgnoreParenCasts reads it for a null pointer constant.
    for (uint32_t steps = 0; bare < graph->count && steps < graph->count && (graph->ops[bare] == CHECK_OP_CAST ||
         (graph->ops[bare] == CHECK_OP_VALUE && graph->operand0[bare] < graph->count)); steps += 1)
    {
        bare = graph->operand0[bare];
    }
    const CheckValue zero = bare < stream->count ? check_stream_value(stream, bare) : (CheckValue){ 0 };
    const bool null = zero.constant_kind == CHECK_CONSTANT_INTEGER && zero.integer == 0 && check_action_kind(types, zero.type) != BUSTER_TYPE_KIND_POINTER;
    uint32_t written = node;
    for (uint32_t steps = 0; written < graph->count && steps < graph->count && graph->ops[written] == CHECK_OP_VALUE && graph->operand0[written] < graph->count;
         steps += 1)
    {
        written = graph->operand0[written];
    }
    const CheckOp op = written < graph->count ? graph->ops[written] : CHECK_OP_COUNT;
    const char* result = "";
    if (type_kind(types, from) == BUSTER_TYPE_KIND_POINTER && check_driver_conversion_same(types, type_element(types, from), to))
    {
        result = null ? "" : op == CHECK_OP_ADDRESS ? "; remove &" : "; dereference with *";
    }
    else if (type_kind(types, to) == BUSTER_TYPE_KIND_POINTER && node < stream->count && check_stream_category(stream, node) == CHECK_LVALUE &&
             !(type_kind(types, from) == BUSTER_TYPE_KIND_POINTER && type_kind(types, type_strip_aligned(types, type_element(types, to))) == BUSTER_TYPE_KIND_VOID))
    {
        const TypeId pointee = type_strip_aligned(types, type_element(types, to));
        const uint32_t qualifiers = BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE | BUSTER_TYPE_RESTRICT;
        const bool taken = type_unqualified(from) == type_unqualified(pointee) && (type_qualifiers(from) & ~type_qualifiers(pointee) & qualifiers) == 0;
        result = !taken ? "" : op == CHECK_OP_DEREFERENCE ? "; remove *" : "; take the address with &";
    }
    return result;
}

// The place of parameter `index` of the function declared at `declaration` (its name), for a note of a parameter that isn't
// a pointer, or a pointer's that check_driver_parameter_source finds none of: the parameter's own name, or, unnamed, the
// token that ends its declarator (the `,` or `)` after it), where clang's unnamed parameter is. BUSTER_CHECK_NONE where its
// declarator has no parameter list right after the name. A diagnosis's cold path.
static uint32_t check_driver_parameter_place(const Parser* parser, const CheckInput* input, uint32_t declaration, uint32_t index)
{
    const uint32_t count = parser->tokens.count < input->token_count ? parser->tokens.count : input->token_count;
    uint32_t open = declaration + 1;
    while (open < count && parser->tokens.kinds[open] == PARSE_KIND_R_PAREN)
    {
        open += 1;
    }
    const bool listed = open < count && parser->tokens.kinds[open] == PARSE_KIND_L_PAREN;
    const uint32_t close = listed ? parser->tree.parents[open] : 0;
    uint32_t result = BUSTER_CHECK_NONE;
    uint32_t ordinal = 0;
    uint32_t begin = open + 1;
    uint32_t depth = 0;
    for (uint32_t k = open + 1; listed && close < count && k <= close && result == BUSTER_CHECK_NONE; k += 1)
    {
        const uint8_t kind = parser->tokens.kinds[k];
        const bool ends = k == close || (depth == 0 && kind == PARSE_KIND_COMMA);
        if (ends && ordinal == index)
        {
            uint32_t named = BUSTER_CHECK_NONE;
            for (uint32_t j = begin; j < k; j += 1)
            {
                named = parser->tokens.kinds[j] == PARSE_KIND_IDENTIFIER && input->declarations != NULL && input->declarations[j] == j &&
                        input->declaration_kinds != NULL && input->declaration_kinds[j] == C_BINDING_OBJECT ? j : named;
            }
            result = named != BUSTER_CHECK_NONE ? named : k;
        }
        ordinal += ends ? 1 : 0;
        begin = ends ? k + 1 : begin;
        depth += kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE ? 1 : 0;
        depth -= depth != 0 && (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE) ? 1 : 0;
    }
    return result;
}

// The implicitly declared library builtin a node names (#1447 item 10, CHECK_INPUT_FUNCTION_IMPLICIT_LIBRARY), through
// conversions and `&`: its catalog row, or NULL.
static const BuiltinIntrinsicInfo* check_driver_library_named_by(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                                               uint32_t node, bool* address)
{
    const CheckGraph* graph = stream->graph;
    *address = false;
    for (uint32_t steps = 0; node < graph->count && steps < 8 && (graph->ops[node] == CHECK_OP_CONVERT || graph->ops[node] == CHECK_OP_ADDRESS); steps += 1)
    {
        *address |= graph->ops[node] == CHECK_OP_ADDRESS;
        node = graph->operand0[node];
    }
    const uint32_t token = node < graph->count && graph->ops[node] == CHECK_OP_VALUE ? graph->tokens[node] : BUSTER_CHECK_NONE;
    const uint32_t declaration = token < input->token_count && input->declarations != NULL ? input->declarations[token] : BUSTER_CHECK_NONE;
    const bool library = declaration < input->token_count && input->function_flags != NULL &&
                         (input->function_flags[declaration] & CHECK_INPUT_FUNCTION_IMPLICIT_LIBRARY) != 0;
    const uint32_t name = library ? parser->tokens.names[token] : 0;
    return library ? builtin_intrinsic_lookup((const char*)intern_bytes(pp->table, name), intern_length(pp->table, name)) : NULL;
}

// Whether a library builtin's encoding returns size_t (its first type's code `z`).
static bool check_driver_library_returns_size(const BuiltinIntrinsicInfo* info)
{
    uint32_t at = 0;
    while (info != NULL && at < info->encoding_length && (info->encoding[at] == 'L' || info->encoding[at] == 'U' || info->encoding[at] == 'S'))
    {
        at += 1;
    }
    return info != NULL && at < info->encoding_length && info->encoding[at] == 'z';
}

// Whether an integer value keeps clang's __size_t sugar (#1447 item 10): a sizeof's or an alignof's, a call's of a library
// builtin that returns size_t, and an operator's whose usual arithmetic conversions keep that sugar (clang's
// UsualArithmeticConversions): the operand of the result's type where only one has it, a common sugar where both have
// (getCommonSugaredType), a shift's left operand, a unary operator's, a comma's right one and a conditional's arms. Its
// own walk with a stack of the operands every one of which must keep it.
static bool check_driver_size_sugar(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types, uint32_t node)
{
    const CheckGraph* graph = stream->graph;
    uint32_t pending[32];
    uint32_t count = 0;
    pending[count++] = node;
    bool result = node < graph->count;
    for (uint32_t steps = 0; result && count != 0 && steps < 256; steps += 1)
    {
        count -= 1;
        uint32_t at = pending[count];
        // A parenthesized operand has no node; an implicit conversion's value is its own (the converted operand's type
        // is the conversion's, which the operator's choice below compares).
        const CheckOp op = at < graph->count ? graph->ops[at] : CHECK_OP_COUNT;
        const TypeId type = at < stream->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, at))) : 0;
        bool address = false;
        const bool binary = (op >= CHECK_OP_ADD && op <= CHECK_OP_REMAINDER) || (op >= CHECK_OP_BIT_AND && op <= CHECK_OP_BIT_XOR);
        // (A sizeof's or an alignof's value keeps it.)
        if (op == CHECK_OP_CALL)
        {
            result = check_driver_library_returns_size(check_driver_library_named_by(parser, pp, input, stream, graph->operand0[at], &address));
        }
        else if (binary || op == CHECK_OP_CONDITIONAL)
        {
            // The operands the result takes its type from: those whose own type (before the conversion to the common
            // type) is the result's.
            const uint32_t left = op == CHECK_OP_CONDITIONAL ? graph->operand1[at] : graph->operand0[at];
            const uint32_t right = op == CHECK_OP_CONDITIONAL ? graph->operand2[at] : graph->operand1[at];
            uint32_t sides[2] = { left, right };
            uint32_t chosen = 0;
            for (uint32_t k = 0; k < 2; k += 1)
            {
                uint32_t side = sides[k];
                while (side < graph->count && graph->ops[side] == CHECK_OP_CONVERT)
                {
                    side = graph->operand0[side];
                }
                const TypeId own = side < stream->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, side))) : 0;
                if (own == type && count + 1 < 32)
                {
                    pending[count++] = side;
                    chosen += 1;
                }
            }
            result = chosen != 0;
        }
        else if (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT || op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT ||
                 op == CHECK_OP_COMMA)
        {
            const uint32_t operand = op == CHECK_OP_COMMA ? graph->operand1[at] : graph->operand0[at];
            pending[count++] = operand;
        }
        else
        {
            result = op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF;
        }
    }
    return result && count == 0;
}

// A function designator of an implicitly declared library builtin, or its address (#1447 item 10), as clang prints it:
// its prototype with size_t's __size_t and FILE's and ucontext_t's typedefs, and its a.k.a. without them
// (`'void *(__size_t)' (aka 'void *(unsigned long)')`, `'__size_t (*)(const char *)' (aka ...)`); empty for another.
static CBuiltinLibraryNamed check_driver_library_named(Preprocessor* pp, const TypeTable* types, const CheckInput* input, Arena* scratch);
static String8 check_driver_library_designator(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                               uint32_t node, TypeId value, Arena* scratch)
{
    bool address = false;
    const BuiltinIntrinsicInfo* info = check_driver_library_named_by(parser, pp, input, stream, node, &address);
    const TypeId bare = value != 0 ? type_unqualified(type_strip_aligned(types, value)) : 0;
    const uint8_t kind = bare != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_NONE;
    const bool pointer = kind == BUSTER_TYPE_KIND_POINTER;
    String8 result = { 0 };
    if (info != NULL && (kind == BUSTER_TYPE_KIND_FUNCTION || pointer))
    {
        const bool c99 = pp->options.language.standard >= 1999;
        const bool needs = memchr(info->encoding, 'P', info->encoding_length) != NULL || memchr(info->encoding, 'J', info->encoding_length) != NULL ||
                           memchr(info->encoding, 'K', info->encoding_length) != NULL;
        const CBuiltinLibraryNamed named = needs ? check_driver_library_named(pp, types, input, scratch) : (CBuiltinLibraryNamed){ 0 };
        char sugared[256];
        char plain[256];
        bool sized = false;
        const bool spelled = cbuiltin_library_spell_as(info->encoding, info->encoding_length, info->attributes, info->attributes_length, true, c99, &named, pointer,
                                                       sugared, sizeof(sugared), &sized) &&
                             cbuiltin_library_spell_as(info->encoding, info->encoding_length, info->attributes, info->attributes_length, false, c99, &named,
                                                       pointer, plain, sizeof(plain), &sized);
        const char* text = spelled ? check_warning_text(scratch, sized ? "'%s' (aka '%s')" : "'%s'", sugared, plain) : NULL;
        result = text != NULL ? (String8){ .pointer = (char*)text, .length = strlen(text) } : (String8){ 0 };
    }
    return result;
}

// A failed conversion of `source`'s value to `target` (spelled with `target_spelling`'s typedef, or `target_sugar`'s
// sugar) as clang words it (#147,
// Sema::DiagnoseAssignmentResult): its kind's words (check_driver_conversion_kind), its `action`'s (0 initializing, 1
// assigning, 2 returning, 3 passing, each putting the target or the source first as clang's does), the fix-it's
// (check_driver_conversion_hint) and its flag. The source's type as written, with its typedef. NULL where a type has none.
static const char* check_driver_conversion_words(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                                 CheckWarningSpelling* spell, uint32_t source, TypeId target, uint32_t target_spelling, uint32_t target_sugar,
                                                 uint32_t target_declaration, uint32_t target_node, uint8_t action, Arena* scratch)
{
    const char* result = NULL;
    if (source < stream->count && target != 0)
    {
        const TypeId value = check_stream_type(stream, source);
        // An integer's __size_t sugar as clang's usual arithmetic conversions keep it (#1447 item 10): `n + sizeof(int)` of
        // an `unsigned long n` has none, which the pointer arithmetic's walk would find in the sizeof.
        const uint32_t walked = check_driver_pointer_spelling(parser, input, stream, types, source);
        const uint8_t value_kind = type_kind(types, type_unqualified(type_strip_aligned(types, value)));
        const bool integer = value_kind == BUSTER_TYPE_KIND_INTEGER;
        const bool sized = integer && check_driver_size_sugar(parser, pp, input, stream, types, source);
        const uint32_t source_spelling = sized ? BUSTER_CHECK_NONE : integer && walked == BUSTER_CHECK_NONE ? 0 : walked;
        // Each as its declaration's sugar writes it, where it has some (#1447), and with its declaration's own address space
        // macros (#1447 item 2, check_driver_space_scope).
        // A declaration whose parameters' specifiers name typedefs prints them, with the a.k.a. (#1447 item 5).
        CheckDriverSpaceScope scope;
        check_driver_space_scope(parser, pp, input, types, spell, target_declaration, &scope, scratch);
        const String8 placed_to = target_sugar == 0 ? check_driver_placed_type(parser, pp, input, types, spell, scratch, target, target_declaration) : (String8){ 0 };
        // A member a counted_by family attribute counts, as clang's CountAttributedType prints it (#1842's 3c).
        const String8 counted_to = target_node != BUSTER_CHECK_NONE ? check_driver_counted_type(parser, pp, input, stream, types, spell, scratch, target_node, target) :
                                   (String8){ 0 };
        const String8 to = counted_to.length != 0 ? counted_to : placed_to.length != 0 ? placed_to :
                           check_driver_warning_type_sugared(parser, pp, input, types, spell, scratch, target, target_spelling, target_sugar);
        check_driver_space_restore(spell, &scope);
        const uint32_t source_sugar = check_driver_operand_sugar(input, stream, source);
        const uint32_t source_declaration = check_driver_operand_declaration(input, stream, source);
        check_driver_space_scope(parser, pp, input, types, spell, source_declaration, &scope, scratch);
        // A library builtin's designator by its prototype (#1447 item 10).
        const String8 designator = check_driver_library_designator(parser, pp, input, stream, types, source, value, scratch);
        const String8 placed_from = designator.length == 0 && source_sugar == 0 ?
                                    check_driver_placed_type(parser, pp, input, types, spell, scratch, value, source_declaration) : (String8){ 0 };
        const String8 counted_from = check_driver_counted_type(parser, pp, input, stream, types, spell, scratch, source, value);
        const String8 from = counted_from.length != 0 ? counted_from : designator.length != 0 ? designator : placed_from.length != 0 ? placed_from :
                             check_driver_warning_type_sugared(parser, pp, input, types, spell, scratch, value, source_spelling, source_sugar);
        check_driver_space_restore(spell, &scope);
        const CheckDriverConversion kind = check_driver_conversion_kind(types, target, check_driver_conversion_source(types, stream, source));
        const uint32_t incompatible = kind == CHECK_DRIVER_CONVERSION_INCOMPATIBLE ? 1 : 0;
        static const char* const lead[] = {
            "incompatible integer to pointer conversion ",
            "incompatible pointer to integer conversion ",
            "incompatible pointer types ",
            "incompatible function pointer types ",
            "",
        };
        static const char* const flag[] = { " [-Wint-conversion]", " [-Wint-conversion]", " [-Wincompatible-pointer-types]",
                                            " [-Wincompatible-function-pointer-types]", "" };
        static const char* const actions[4][2] = {
            { "initializing %.*s with an expression of type %.*s", "initializing %.*s with an expression of incompatible type %.*s" },
            { "assigning to %.*s from %.*s", "assigning to %.*s from incompatible type %.*s" },
            { "returning %.*s from a function with result type %.*s", "returning %.*s from a function with incompatible result type %.*s" },
            { "passing %.*s to parameter of type %.*s", "passing %.*s to parameter of incompatible type %.*s" },
        };
        const char* hint = check_driver_conversion_hint(types, stream, source, value, target);
        // clang's actions put the target first where they initialize or assign, the source first where they return or pass.
        const String8 first = action < 2 ? to : from;
        const String8 second = action < 2 ? from : to;
        const char* words = to.length != 0 && from.length != 0 && action < 4 ?
                            check_warning_text(scratch, actions[action][incompatible], (int)first.length, first.pointer, (int)second.length, second.pointer) : NULL;
        result = words != NULL ? check_warning_text(scratch, "%s%s%s%s", lead[kind], words, hint, flag[kind]) : NULL;
    }
    return result;
}

// An initializer's subobject whose conversion failed (#147, CheckInitDiagnostic.failed_source): clang's words of it as an
// initialization (check_driver_conversion_words), at the subobject's expression (`token`), or at the declarator
// (`declaration`) where it is the whole initializer of no braces, the declared type then spelled with its typedef. A
// record's or an array's whole initializer of no braces that failed with no conversion of its own (`source`
// BUSTER_CHECK_NONE) is clang's too where its value is no record of the type: the record's initialization from the value,
// or an array's "array initializer must be an initializer list". NULL where it is none of those (the generic "invalid
// initializer" stays).
static const char* check_driver_initializer_conversion(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                                       const TypeTable* types, TypeId target, uint32_t source, uint32_t declaration, uint64_t offset,
                                                       uint32_t token, uint32_t* located, Arena* scratch)
{
    CheckWarningSpelling spell = { 0 };
    const bool declared = declaration < input->token_count;
    const bool whole = declared && token < parser->tokens.count && check_sparse_get(input->initializer_begins, declaration) == token &&
                       parser->tokens.kinds[token] != PARSE_KIND_L_BRACE;
    if (source == BUSTER_CHECK_NONE && whole && input->token_nodes != NULL)
    {
        // The whole initializer's value: its last token's outermost node, as check_init_node takes it.
        const uint32_t end = check_sparse_get(input->initializer_ends, declaration);
        for (uint32_t at = token; at < end && at < input->token_count; at += 1)
        {
            const uint32_t node = input->token_nodes[at];
            source = node < stream->count && (source == BUSTER_CHECK_NONE || node > source) ? node : source;
        }
        target = input->declared_types[declaration];
        const uint8_t kind = type_kind(types, type_strip_aligned(types, target));
        const bool aggregate = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || type_kind_is_array(kind);
        const TypeId value = source < stream->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, source))) : 0;
        // A string literal's array initialized from it is the walk's own (its sizes), and a record of the type its copy.
        const bool own = value == type_unqualified(type_strip_aligned(types, target)) || (type_kind_is_array(kind) && value != 0 && type_kind_is_array(type_kind(types, value)));
        source = aggregate && !own ? source : BUSTER_CHECK_NONE;
    }
    uint32_t spelling = 0;
    const TypeId spelled = whole ? check_input_object_spelling(types, input, parser, declaration, input->declared_types[declaration], target, &spelling) : target;
    // A subobject's member or element spells it (#1447, check_semantic_subobject_spelling).
    spelling = !whole && declared && source < stream->count ?
               check_semantic_subobject_spelling(parser, input, types, input->declared_types[declaration], declaration, offset, target) : spelling;
    const TypeId bare = type_strip_aligned(types, target);
    const bool array = type_kind_is_array(type_kind(types, bare));
    // An array's (clang's FK_ArrayNeedsInitList and its string literal kinds): the literal its element takes named, a
    // character type's a string, wchar_t's, char16_t's and char32_t's (int, unsigned short and unsigned int here) a wide one.
    const TypeId element = array ? type_unqualified(type_strip_aligned(types, type_element(types, bare))) : 0;
    const bool narrow = element == BUSTER_TYPE_CHAR || element == BUSTER_TYPE_SCHAR || element == BUSTER_TYPE_UCHAR;
    const bool wide = element == BUSTER_TYPE_INT || element == BUSTER_TYPE_USHORT || element == BUSTER_TYPE_UINT;
    const char* result = source >= stream->count ? NULL :
                         !array ? check_driver_conversion_words(parser, pp, input, stream, types, &spell, source, spelled, spelling,
                                                                whole ? check_input_declared_sugar(input, declaration) : 0, whole ? declaration : BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, 0,
                                                                scratch) :
                         narrow ? "array initializer must be an initializer list or string literal" :
                         wide ? "array initializer must be an initializer list or wide string literal" : "array initializer must be an initializer list";
    *located = whole ? declaration : token;
    return result;
}

// A failed assignment conversion's error as clang words it (#147, Sema::DiagnoseAssignmentResult), where `report` is one:
// an initializer's (at its declarator), an assignment's (at its `=`), a return's (at its expression) or a call's argument's
// (at the argument, with "passing argument to parameter here" at its parameter, `*note_token` and `*note`, where the callee
// is a function's own name). The types are as written, with their typedefs (an array's and a function's, a qualified
// lvalue's), the target's as declared; the kind (check_driver_conversion_kind) chooses the words and the flag, and the
// fix-it its last words (check_driver_conversion_hint). NULL for any other report, which the generic message keeps.
static const char* check_driver_conversion_error(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                                 CheckWarningSpelling* spell, const CheckReport* report, uint32_t* located, uint32_t* note_token,
                                                 const char** note, Arena* scratch)
{
    const CheckGraph* graph = stream->graph;
    const uint32_t node = report->node;
    const uint32_t token = report->token;
    const CheckOp op = node < stream->count ? graph->ops[node] : CHECK_OP_COUNT;
    // (A value returned from a void function is clang's "void function 'g' should not return a value", which the semantic
    // message gives.)
    const bool returning = op == CHECK_OP_CONVERT && token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_RETURN &&
                           check_action_kind(types, graph->types[node]) != BUSTER_TYPE_KIND_VOID;
    const uint32_t argument_index = op == CHECK_OP_CALL ? stream->diagnostic_operands[node] : 0;
    const bool passing = op == CHECK_OP_CALL && argument_index != 0 && graph->argument_counts != NULL && argument_index <= graph->argument_counts[node];
    const bool applies = report->message == CHECK_MESSAGE_ACTION && report->diagnostic == CHECK_DIAG_INCOMPATIBLE &&
                         (op == CHECK_OP_INITIALIZE || op == CHECK_OP_ASSIGN || returning || passing);
    uint32_t source = BUSTER_CHECK_NONE;
    TypeId target = 0;
    uint32_t target_spelling = 0;
    uint32_t target_sugar = 0;
    uint32_t target_declaration = BUSTER_CHECK_NONE;
    uint32_t target_node = BUSTER_CHECK_NONE;
    uint32_t at = token;
    *note_token = BUSTER_CHECK_NONE;
    *note = NULL;
    if (applies && op == CHECK_OP_INITIALIZE)
    {
        source = graph->operand0[node];
        for (uint32_t declaration = 0; declaration < input->token_count && at == token; declaration += 1)
        {
            at = check_sparse_get(input->initializer_nodes, declaration) == node ? declaration : at;
        }
        target = check_input_object_spelling(types, input, parser, at, graph->types[node], graph->types[node], &target_spelling);
        target_sugar = at < input->token_count ? check_input_declared_sugar(input, at) : 0;
        target_declaration = at;
    }
    else if (applies && op == CHECK_OP_ASSIGN)
    {
        source = graph->operand1[node];
        const uint32_t left = graph->operand0[node];
        const TypeId lvalue = left < stream->count ? check_stream_type(stream, left) : 0;
        target = left < stream->count ? check_input_object_spelling(types, input, parser, graph->tokens[left], 0, lvalue, &target_spelling) : 0;
        // A member's own declarator's specifiers (#1837): `s.lp = m` of a member `LP lp` assigns to 'LP'.
        const bool member = left < stream->count && (graph->ops[left] == CHECK_OP_MEMBER || graph->ops[left] == CHECK_OP_ARROW);
        const uint32_t selected = member ? check_semantic_member_of(parser, input, stream, types, left) : 0;
        const uint32_t declarator = selected != 0 ? check_semantic_member_token(parser, input, types, selected) : BUSTER_CHECK_NONE;
        target_spelling = declarator < input->token_count ? check_input_specifier_spelling(input, parser, declarator) : member ? 0 : target_spelling;
        target = member ? lvalue : target;
        target_sugar = member ? 0 : check_driver_operand_sugar(input, stream, left);
        target_declaration = member ? BUSTER_CHECK_NONE : check_driver_operand_declaration(input, stream, left);
        target_node = member ? left : BUSTER_CHECK_NONE;
    }
    else if (applies && returning)
    {
        source = graph->operand0[node];
        const uint32_t function = check_semantic_function(parser, input, token);
        target = graph->types[node];
        target_spelling = function < input->token_count ? check_input_typedef_spelling(input, parser, function) : 0;
        at = source < stream->count ? check_warning_graph_first(graph, parser, source) : token;
    }
    else if (applies)
    {
        // The callee's type's parameter, and its declaration's source where the callee is a function's own name
        // (check_driver_parameter_source).
        const uint32_t index = argument_index - 1;
        source = graph->argument_nodes[graph->argument_offsets[node] + index];
        const uint32_t callee = graph->operand0[node];
        TypeId function = callee < stream->count ? type_strip_aligned(types, check_stream_type(stream, callee)) : 0;
        function = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, function)) : function;
        uint32_t parameter_count = 0;
        const TypeId* parameters = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION ? type_parameters(types, function, &parameter_count) : NULL;
        target = parameters != NULL && index < parameter_count ? parameters[index] : 0;
        const uint32_t name = callee < stream->count ? graph->tokens[callee] : BUSTER_CHECK_NONE;
        const uint32_t declaration = name < input->token_count && input->declarations != NULL ? input->declarations[name] : BUSTER_CHECK_NONE;
        const bool direct = callee < stream->count && graph->ops[callee] == CHECK_OP_VALUE && declaration < input->token_count &&
                            input->declaration_kinds != NULL && input->declaration_kinds[declaration] == C_BINDING_FUNCTION;
        // A pointer parameter's note is where the declarator reader puts it (check_driver_parameter_source), any other's at
        // its name or its declarator's end (check_driver_parameter_place), as is a pointer parameter's that has none.
        uint32_t parameter = direct && target != 0 && check_action_kind(types, target) == BUSTER_TYPE_KIND_POINTER ?
                             check_driver_parameter_source(parser, input, declaration, index) : BUSTER_CHECK_NONE;
        parameter = parameter < input->token_count ? parameter : direct ? check_driver_parameter_place(parser, input, declaration, index) : BUSTER_CHECK_NONE;
        if (parameter < input->token_count)
        {
            const bool named = parser->tokens.kinds[parameter] == PARSE_KIND_IDENTIFIER && input->declarations[parameter] == parameter &&
                               input->declaration_kinds[parameter] == C_BINDING_OBJECT;
            const uint32_t identifier = named ? parser->tokens.names[parameter] : 0;
            *note_token = parameter;
            *note = named ? check_warning_text(scratch, "passing argument to parameter '%.*s' here", (int)intern_length(pp->table, identifier),
                                               intern_bytes(pp->table, identifier)) : "passing argument to parameter here";
            target_spelling = check_driver_parameter_spelling(parser, input, parameter);
            target_declaration = parameter;
        }
    }
    const uint8_t action = op == CHECK_OP_INITIALIZE ? 0 : op == CHECK_OP_ASSIGN ? 1 : returning ? 2 : 3;
    const char* result = applies ? check_driver_conversion_words(parser, pp, input, stream, types, spell, source, target, target_spelling, target_sugar,
                                                                 target_declaration, target_node, action,
                                                                 scratch) : NULL;
    *located = result != NULL ? at : *located;
    return result;
}

// __builtin_dump_struct's rest argument that doesn't convert to its printer's parameter (#2189, clang's BuiltinDumpStruct,
// whose printer calls convert as a call's arguments): the conversion's words as a call's argument's
// (check_driver_conversion_words), at the argument's first token, with clang's notes "in call to printing function with
// arguments '(rest..., "%s", "struct S")' while dumping struct" at the builtin's name, the arguments of its first line's
// call as clang's printer spells them, and "passing argument to parameter 'x' here" at the printer's parameter where it
// is a function's name. NULL for another report.
static const char* check_driver_dump_struct(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                            CheckWarningSpelling* spell, const CheckReport* report, uint32_t* located, PPOutputDiagnosticNote* notes, Arena* scratch)
{
    const CheckGraph* graph = stream->graph;
    const uint32_t node = report->node;
    const bool candidate = report->message == CHECK_MESSAGE_ACTION && report->diagnostic == CHECK_DIAG_INCOMPATIBLE && node < stream->count &&
                           graph->ops[node] == CHECK_OP_BUILTIN_MISC && graph->members[node] == CBUILTIN_DUMP_STRUCT && graph->argument_counts != NULL;
    const uint32_t operand = candidate ? stream->diagnostic_operands[node] : 0;
    const uint32_t count = candidate ? graph->argument_counts[node] : 0;
    const char* result = NULL;
    if (operand >= 2 && operand < count)
    {
        const uint32_t offset = graph->argument_offsets[node];
        const uint32_t index = operand - 2;
        const uint32_t source = graph->argument_nodes[offset + operand];
        const uint32_t printer = graph->argument_nodes[offset + 1];
        TypeId function = printer < stream->count ? type_strip_aligned(types, check_stream_type(stream, printer)) : 0;
        function = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, function)) : function;
        uint32_t parameter_count = 0;
        const TypeId* parameters = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION ? type_parameters(types, function, &parameter_count) : NULL;
        const TypeId target = parameters != NULL && index < parameter_count ? parameters[index] : 0;
        // The function the printer names, as clang's getCalleeDecl finds it: `p`, `(p)` and `&p`.
        const uint32_t called = printer < stream->count && graph->ops[printer] == CHECK_OP_ADDRESS ? graph->operand0[printer] : printer;
        const uint32_t name = called < stream->count && graph->ops[called] == CHECK_OP_VALUE ? graph->tokens[called] : BUSTER_CHECK_NONE;
        const uint32_t declaration = name < input->token_count && input->declarations != NULL ? input->declarations[name] : BUSTER_CHECK_NONE;
        const bool direct = declaration < input->token_count && input->declaration_kinds != NULL && input->declaration_kinds[declaration] == C_BINDING_FUNCTION;
        uint32_t parameter = direct && target != 0 && check_action_kind(types, target) == BUSTER_TYPE_KIND_POINTER ?
                             check_driver_parameter_source(parser, input, declaration, index) : BUSTER_CHECK_NONE;
        parameter = parameter < input->token_count ? parameter : direct ? check_driver_parameter_place(parser, input, declaration, index) : BUSTER_CHECK_NONE;
        const bool placed = parameter < input->token_count;
        const char* words = target != 0 ? check_driver_conversion_words(parser, pp, input, stream, types, spell, source, target,
                                                                         placed ? check_driver_parameter_spelling(parser, input, parameter) : 0, 0,
                                                                         placed ? parameter : BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, 3, scratch) : NULL;
        // The first line's call: the rest as written, then the record's line's format and type name.
        const uint32_t opening = graph->argument_tokens[offset] > 0 ? check_semantic_argument_begin(parser, graph, node, 0) - 1 : 0;
        const uint32_t closing = opening < parser->tokens.count ? parser->tree.parents[opening] : BUSTER_CHECK_NONE;
        const uint32_t record = graph->argument_nodes[offset];
        const TypeId pointer = record < stream->count ? type_strip_aligned(types, check_stream_type(stream, record)) : 0;
        const TypeId pointee = pointer != 0 && type_kind(types, pointer) == BUSTER_TYPE_KIND_POINTER ? type_unqualified(type_element(types, pointer)) : 0;
        String8 type = pointee != 0 ? check_driver_warning_type(parser, pp, input, types, spell, scratch, pointee, 0) : (String8){ 0 };
        // Its name as the call's string has it: the first quoted text, without the quotes.
        uint64_t end = 1;
        while (end < type.length && type.pointer[end] != '\'')
        {
            end += 1;
        }
        const String8 named = type.length > 2 && type.pointer[0] == '\'' ? (String8){ .pointer = type.pointer + 1, .length = end - 1 } : (String8){ 0 };
        const char* arguments = "";
        for (uint32_t k = 2; k < count && closing < parser->tokens.count && named.length != 0 && arguments != NULL; k += 1)
        {
            const uint32_t begin = check_semantic_argument_begin(parser, graph, node, k);
            const uint32_t stop = k + 1 < count ? check_semantic_argument_begin(parser, graph, node, k + 1) - 1 : closing;
            const char* text = begin < stop ? check_semantic_requirement(parser, pp, begin, stop, NULL, scratch) : NULL;
            arguments = text != NULL ? check_warning_text(scratch, "%s%s, ", arguments, text) : NULL;
        }
        const uint32_t callee = opening > 0 ? opening - 1 : BUSTER_CHECK_NONE;
        if (words != NULL && arguments != NULL && named.length != 0 && callee < parser->tokens.count)
        {
            result = words;
            *located = check_semantic_argument_begin(parser, graph, node, operand);
            uint32_t note_count = 0;
            check_semantic_add_note(parser, notes, &note_count, callee,
                                    check_warning_text(scratch, "in call to printing function with arguments '(%s\"%%s\", \"%.*s\")' while dumping struct", arguments,
                                                       (int)named.length, named.pointer));
            if (placed)
            {
                const bool identified = parser->tokens.kinds[parameter] == PARSE_KIND_IDENTIFIER && input->declarations[parameter] == parameter &&
                                        input->declaration_kinds[parameter] == C_BINDING_OBJECT;
                const uint32_t identifier = identified ? parser->tokens.names[parameter] : 0;
                check_semantic_add_note(parser, notes, &note_count, parameter,
                                        identified ? check_warning_text(scratch, "passing argument to parameter '%.*s' here", (int)intern_length(pp->table, identifier),
                                                                        intern_bytes(pp->table, identifier)) : "passing argument to parameter here");
            }
        }
    }
    return result;
}

// A call's wrong argument count as clang words it (#1860, #1921, Sema::ConvertArgumentsForCall): "too few/too many arguments
// to function call, expected [at least|at most] N, have M". An ordinary call's count is its callee's type's, with clang's
// "'h' declared here" note at its declaration where the callee is a function's name (through parentheses and `&`), and
// clang's single-argument words where that declaration names its only parameter; a builtin's is the one its check
// carried in the diagnostic's operand (check_action_diagnose_count), "too many" at its first extra argument, as an
// ordinary call's (check_graph_report). NULL for any other report, which keeps the generic message.
static const char* check_driver_call_count(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                           const CheckReport* report, uint32_t* located, uint32_t* note_token, const char** note, Arena* scratch)
{
    const CheckGraph* graph = stream->graph;
    const uint32_t node = report->node;
    const bool applies = report->message == CHECK_MESSAGE_ACTION && report->diagnostic == CHECK_DIAG_ARGUMENT_COUNT && node < stream->count &&
                         graph->argument_counts != NULL;
    const CheckOp op = applies ? graph->ops[node] : CHECK_OP_COUNT;
    const uint32_t operand = applies ? stream->diagnostic_operands[node] : 0;
    const uint32_t have = applies ? graph->argument_counts[node] : 0;
    uint32_t at = report->token;
    const char* result = NULL;
    if (op == CHECK_OP_CALL)
    {
        const uint32_t callee = graph->operand0[node];
        TypeId function = callee < stream->count ? type_strip_aligned(types, check_stream_type(stream, callee)) : 0;
        function = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, function)) : function;
        const bool typed = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION;
        const bool variadic = typed && (types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_VARIADIC) != 0;
        uint32_t expected = 0;
        if (typed)
        {
            type_parameters(types, function, &expected);
        }
        // The function the callee names, as clang's getCalleeDecl finds it: `h`, `(h)` and `(&h)`.
        const uint32_t named = callee < stream->count && graph->ops[callee] == CHECK_OP_ADDRESS ? graph->operand0[callee] : callee;
        const uint32_t name = named < stream->count && graph->ops[named] == CHECK_OP_VALUE ? graph->tokens[named] : BUSTER_CHECK_NONE;
        const uint32_t declaration = name < input->token_count && input->declarations != NULL ? input->declarations[name] : BUSTER_CHECK_NONE;
        const bool direct = declaration < input->token_count && input->declaration_kinds != NULL && input->declaration_kinds[declaration] == C_BINDING_FUNCTION;
        const uint32_t parameter = direct && expected == 1 ? check_driver_parameter_place(parser, input, declaration, 0) : BUSTER_CHECK_NONE;
        const bool single = parameter < input->token_count && parser->tokens.kinds[parameter] == PARSE_KIND_IDENTIFIER &&
                            input->declarations[parameter] == parameter && input->declaration_kinds[parameter] == C_BINDING_OBJECT;
        const String8 parameter_name = single ? check_semantic_name(parser, pp, parameter) : (String8){ 0 };
        const bool few = have < expected;
        if (!typed || (few ? false : variadic || have == expected))
        {
            result = NULL;
        }
        else if (single && few)
        {
            result = check_warning_text(scratch, "too few arguments to function call, %s argument '%.*s' %s", variadic ? "at least" : "single",
                                        (int)parameter_name.length, parameter_name.pointer, variadic ? "must be specified" : "was not specified");
        }
        else if (single)
        {
            result = check_warning_text(scratch, "too many arguments to function call, expected single argument '%.*s', have %u arguments",
                                        (int)parameter_name.length, parameter_name.pointer, have);
        }
        else
        {
            result = check_warning_text(scratch, "too %s arguments to function call, expected %s%u, have %u", few ? "few" : "many", variadic ? "at least " : "",
                                        expected, have);
        }
        if (result != NULL && direct)
        {
            const String8 function_name = check_semantic_name(parser, pp, name);
            *note_token = declaration;
            *note = check_warning_text(scratch, "'%.*s' declared here", (int)function_name.length, function_name.pointer);
        }
    }
    else if (applies && op != CHECK_OP_BUILTIN_X86 && (operand & 0x80u) != 0)
    {
        const uint32_t expected = operand & 0x3fu;
        const bool bound = (operand & 0x40u) != 0;
        const bool few = have < expected;
        result = check_warning_text(scratch, "too %s arguments to function call, expected %s%u, have %u", few ? "few" : "many",
                                    !bound ? "" : few ? "at least " : "at most ", expected, have);
        // Too many is at the first extra argument, but clang's "at most" is at the call's `)`, the report's token. A builtin
        // whose arguments are its operands (va_start's) has no list: the argument after the expected count's commas.
        const bool listed = check_graph_has_arguments(op) && graph->argument_tokens != NULL;
        uint32_t extra = !few && !bound && expected < have && listed ? graph->argument_tokens[graph->argument_offsets[node] + expected] : at;
        const uint32_t close = at;
        uint32_t open = !few && !bound && expected < have && !listed && close < parser->tokens.count ? close : 0;
        while (open != 0 && !(parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == close))
        {
            open -= 1;
        }
        uint32_t commas = 0;
        for (uint32_t k = open + 1; open != 0 && k < close && commas < expected; k += 1)
        {
            const uint8_t kind = parser->tokens.kinds[k];
            const bool nested = (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE) && parser->tree.parents[k] > k &&
                                parser->tree.parents[k] < close;
            commas += kind == PARSE_KIND_COMMA ? 1u : 0u;
            extra = kind == PARSE_KIND_COMMA && commas == expected ? k + 1 : extra;
            k = nested ? parser->tree.parents[k] : k;
        }
        at = extra < parser->tokens.count ? extra : at;
    }
    *located = result != NULL ? at : *located;
    return result;
}

// `from`: the first token whose reports, warnings, pragmas' diagnostics and packed members' warnings it publishes, 0 for
// all of them (check_driver_rejected_common's second reading publishes those past where the first one stopped).
static bool check_driver_reports_from(const Parser* parser, const Parser* original, Preprocessor* pp, uint32_t publication_end,
                                      const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                      const CheckRepair* repair, const CheckInput* original_pragmas, const CheckDriverDiagnostic* extra, uint32_t extra_count,
                                      const uint32_t* excess_tokens, uint32_t excess_count, bool quiet,
                                      CheckCommonShiftWarnings* shifts, uint32_t from, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const bool scalar = parser->options.scalar;
    const uint32_t count = parser->tokens.count;
    CheckWarningSpelling spell = { 0 };
    // Whether signed overflow is defined (-fwrapv, or -fno-strict-overflow without -fno-wrapv), as the predefines read it.
    const PredefinesOptions* predefines = &pp->options.predefines;
    const bool wrapv = predefines->wrapv != 0 ? predefines->wrapv == BUSTER_PREDEFINES_ON : predefines->strict_overflow == BUSTER_PREDEFINES_OFF;
    // (The parser contexts a folded shift's warning asks, check_common_context's memo, are this function's own.)
    check_common_contexts = (CheckCommonContexts){ 0 };
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
    const uint64_t kr_offset64 = (uint64_t)input->report_count + stream_reports + extra_count + input->implicit_function_count;
    const uint64_t call_offset64 = kr_offset64 + input->kr_diagnostic_count;
    const uint64_t report_room = call_offset64 + (show_warnings ? node_warnings : 0) + 1;
    const uint64_t warning_room = show_warnings ? (uint64_t)constant_warnings + (attribute_warnings ? scanned : 0) + (uint64_t)node_warnings * 2 + excess_count + 1 : 0;
    const uint32_t completion_count = show_warnings ? input->warning_events.count : 0;
    const bool reports = report_room > 1;
    const bool fits = count < UINT32_MAX && call_offset64 + nodes <= UINT32_MAX && report_room <= UINT32_MAX && warning_room <= UINT32_MAX;
    uint64_t* report_keys = fits && reports ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, report_room) : NULL;
    uint64_t* report_sorting = report_keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, report_room) : NULL;
    uint64_t* warning_keys = fits && show_warnings ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, warning_room) : NULL;
    uint64_t* warning_sorting = warning_keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, warning_room) : NULL;
    uint64_t* completion_keys = completion_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, completion_count) : NULL;
    uint64_t* completion_sorting = completion_keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, completion_count) : NULL;
    const bool ready = fits && (!reports || report_sorting != NULL) && (!show_warnings || warning_sorting != NULL) &&
                       (completion_count == 0 || (completion_sorting != NULL && input->warning_events.events != NULL));
    bool shifts_ready = true;
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
            const uint32_t located = input->reports[i].sort != 0 ? input->reports[i].sort : input->reports[i].token;
            const uint32_t token = located < count ? located : count;
            // An invalid declaration's use publishes nothing (#1447, CHECK_INPUT_INVALID_USE).
            if (input->reports[i].message != CHECK_INPUT_INVALID_USE &&
                (repair == NULL || check_repair_input_report(repair, input->reports + i) || check_repair_loop_report(repair, input, input->reports + i)))
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
            const CheckWarningSeverity severity = check_diagnostic_implicit_function(input, i);
            // An error clang makes by default (C99's, an unknown builtin's) survives -w.
            const bool default_error = input->implicit_function_c99 || input->implicit_function_builtins[i];
            if (token < count && severity != CHECK_WARNING_IGNORE && (!quiet || (default_error && severity >= CHECK_WARNING_ERROR)) &&
                (repair == NULL || check_repair_warning_token(repair, token)))
            {
                report_keys[report_total++] = (uint64_t)token << 32 | (input->report_count + stream_reports + extra_count + i);
            }
        }
        for (uint32_t i = 0; show_warnings && i < input->kr_diagnostic_count; i += 1)
        {
            const CheckInputKrDiagnostic diagnostic = input->kr_diagnostics[i];
            const uint32_t token = diagnostic.token;
            const uint32_t name = check_diagnostic_kr_name(&diagnostic);
            if (token < count && name < count && check_diagnostic_kr_severity(input, name, diagnostic.warning) != CHECK_WARNING_IGNORE &&
                (repair == NULL || check_repair_warning_token(repair, name)))
            {
                report_keys[report_total++] = (uint64_t)token << 32 | ((uint32_t)kr_offset64 + i);
            }
        }
        // Gather both warning families in one mask scan. Call diagnostics don't fit the arithmetic warnings'
        // packed 16-bit keys; sort those with the sparse source reports, without an extra pass over the nodes.
        uint32_t warning_total = 0;
        for (uint32_t first = 0; show_warnings && node_warnings != 0 && first < nodes; first += 16)
        {
            const uint32_t lanes = scalar ? (uint32_t)check_input_scan_all(first, nodes) & 0xffff : check_input_scan_nonzero32(stream->warnings, first, nodes);
            for (uint32_t selected = lanes; selected != 0; selected &= selected - 1)
            {
                const uint32_t node = first + (uint32_t)__builtin_ctz(selected);
                const uint32_t source = input->graph.tokens[node];
                const uint32_t warned = stream->warnings[node] & 0xffffu;
                // A folded non-ICE expression's warning is at its first token, as clang's ext_expr_not_ice is at the
                // expression's begin: a cast's `(`, not the node's `)`.
                const uint32_t folded = warned & CHECK_WARNING_GNU_CONSTANT;
                const uint32_t packed = warned & ~folded;
                const uint32_t ignored = stream->warning_ignored != NULL ? stream->warning_ignored[node] : 0;
                if (source < count && packed != 0)
                {
                    warning_keys[warning_total++] = check_driver_warning_key(source, packed, packed & ~ignored);
                }
                if (source < count && folded != 0)
                {
                    warning_keys[warning_total++] = check_driver_warning_key(check_common_subtree_start(parser, source), folded, folded & ~ignored);
                }
                const uint32_t flags = stream->warnings[node] & CHECK_WARNING_KR_CALLS;
                if (flags != 0)
                {
                    const uint32_t token = check_diagnostic_kr_call_token(input, node);
                    const uint32_t place = check_diagnostic_kr_call_place(input, parser, node, flags);
                    if (token < count && place < count && check_diagnostic_kr_severity(input, token, flags) != CHECK_WARNING_IGNORE &&
                        (repair == NULL || check_repair_warning_token(repair, token)))
                    {
                        report_keys[report_total++] = (uint64_t)place << 32 | ((uint32_t)call_offset64 + node);
                    }
                }
            }
        }
        // Inside a direct call's full expression, of an interrupt service routine, only its error (#1447).
        uint32_t report_kept = 0;
        for (uint32_t i = 0; input->interrupt_range_count != 0 && i < report_total; i += 1)
        {
            const uint32_t sequence = (uint32_t)report_keys[i];
            const bool called = sequence < input->report_count && input->reports[sequence].message == CHECK_INPUT_INTERRUPT_CALLED;
            report_keys[report_kept] = report_keys[i];
            report_kept += called || check_driver_interrupt_kept(input, (uint32_t)(report_keys[i] >> 32)) ? 1u : 0u;
        }
        report_total = input->interrupt_range_count != 0 ? report_kept : report_total;
        uint64_t* firsts = reports ? c_bindings_sort(report_keys, report_sorting, report_total, !scalar, varying) : NULL;
        uint32_t first_count = 0;
        for (uint32_t i = 0; i < report_total; i += 1)
        {
            const uint32_t sequence = (uint32_t)firsts[i];
            const uint32_t extra_index = sequence - input->report_count - stream_reports;
            const bool input_repeated = sequence < input->report_count && input->reports[sequence].repeated;
            const bool repeated = input_repeated ||
                                  (sequence >= input->report_count + stream_reports && extra_index < extra_count && extra[extra_index].repeated);
            // A typo's correction (#1447) is not its token's error but a name's: the expression it recovers to keeps its own
            // error at that token, as clang's does (`static int s = countr;`'s non-constant one), once.
            const uint32_t last = first_count != 0 ? (uint32_t)firsts[first_count - 1] : BUSTER_CHECK_NONE;
            const bool corrected = last < input->report_count && input->reports[last].message == CHECK_INPUT_UNDECLARED_CORRECTED;
            if (first_count == 0 || (firsts[first_count - 1] >> 32) != (firsts[i] >> 32) || repeated || corrected)
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
        uint32_t skipped = 0;
        while (from != 0 && skipped < first_count && (firsts[skipped] >> 32) < from)
        {
            skipped += 1;
        }
        if (skipped != 0)
        {
            memmove(firsts, firsts + skipped, (uint64_t)(first_count - skipped) * sizeof(*firsts));
            first_count -= skipped;
        }
        if (reports)
        {
            firsts[first_count] = UINT64_MAX;
        }
        const CheckSparseWide attributes = input->attributes;
        const bool attributed = show_warnings && attribute_warnings && check_sparse_wide_present(attributes);
        for (uint32_t first = attributed ? check_input_attributed_next(attributes, 0, scanned, scalar) : scanned; first < scanned; first = check_input_attributed_next(attributes, first + 16, scanned, scalar))
        {
            const uint32_t lanes = check_input_attributed_group(attributes, first, scanned, scalar);
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
        for (uint32_t i = 0; show_warnings && i < excess_count; i += 1)
        {
            if (excess_tokens[i] < count)
            {
                warning_keys[warning_total++] = check_driver_warning_key(excess_tokens[i], CHECK_WARNING_EXCESS_INITIALIZERS, CHECK_WARNING_EXCESS_INITIALIZERS);
            }
        }
        uint32_t warning_kept = 0;
        for (uint32_t i = 0; input->interrupt_range_count != 0 && i < warning_total; i += 1)
        {
            warning_keys[warning_kept] = warning_keys[i];
            warning_kept += check_driver_interrupt_kept(input, (uint32_t)(warning_keys[i] >> 32)) ? 1u : 0u;
        }
        warning_total = input->interrupt_range_count != 0 ? warning_kept : warning_total;
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
        uint32_t passed = 0;
        while (from != 0 && passed < warning_count && (warnings[passed] >> 32) < from)
        {
            passed += 1;
        }
        if (passed != 0)
        {
            memmove(warnings, warnings + passed, (uint64_t)(warning_count - passed) * sizeof(*warnings));
            warning_count -= passed;
        }
        if (show_warnings)
        {
            warnings[warning_count] = UINT64_MAX;
        }
        // Runtime constant-count groups will join the common arithmetic walk's new count diagnostics (#1394), and so will
        // a folded shift's negative left operand, clang's runtime -Wshift-negative-value too, which the walk drops where the
        // shift is unevaluated (check_common_shift_unreached). Keep their original records queued until that walk is ready,
        // so a failed program retains every group. Only units with these warnings allocate this sparse handoff, with room
        // for each bit at each token.
        if (shifts != NULL && pp->options.collect_diagnostics)
        {
            uint64_t capacity = 0;
            for (uint32_t i = 0; i < warning_count; i += 1)
            {
                const uint32_t flags = (uint32_t)(warnings[i] >> 16) &
                                       (CHECK_WARNING_SHIFT_COUNT_NEGATIVE | CHECK_WARNING_SHIFT_COUNT_OVERFLOW | CHECK_WARNING_SHIFT_NEGATIVE_VALUE);
                capacity += (uint32_t)__builtin_popcount(flags);
            }
            shifts->records = capacity != 0 && capacity <= UINT32_MAX ? arena_push_array(&pp->arena, capacity, sizeof(CheckCommonShiftWarning),
                                                                                         _Alignof(CheckCommonShiftWarning)) : NULL;
            shifts->capacity = shifts->records != NULL ? (uint32_t)capacity : 0;
            shifts_ready = capacity == 0 || shifts->records != NULL;
        }
        const CheckInput* pragma_input = original_pragmas != NULL ? original_pragmas : input;
        const CheckInputVisibilityPragmas* visibility_history = pragma_input->visibility_pragmas;
        uint32_t visibility = 0;
        uint32_t pragma = 0;
        uint32_t packed = 0;
        // From `from` on (a rejected unit's second reading, check_driver_rejected_common), the pragmas' records are the
        // unit's, which the first reading published whole, wherever its walk ended: none of them again.
        visibility = from != 0 && visibility_history != NULL ? visibility_history->count : 0;
        pragma = from != 0 ? pragma_input->fp_pragma_count : 0;
        while (from != 0 && packed < input->packed_member_count && input->packed_members[packed].address < from)
        {
            packed += 1;
        }
        uint32_t warning = 0;
        uint32_t report = 0;
        uint64_t position = from;
        bool walking = shifts_ready;
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
                        check_driver_pragma_report(original, pp, pragma_input, pragma_input->fp_pragmas + pragma, quiet);
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
            const uint32_t pointer_flags = flags & (CHECK_WARNING_POINTER_SIGN | CHECK_WARNING_DISCARDED_QUALIFIERS);
            if (pointer_flags != 0)
            {
                PPDiagnosticPhase phase = PP_DIAGNOSTIC_SEMANTIC;
                const uint32_t publication = check_driver_after_corrections(parser, pp, input, token,
                    check_driver_warning_position(parser, pp, input, repair, completions, completion_count, token, pointer_flags & (0u - pointer_flags),
                                                  publication_end, original, &phase), publication_end);
                if (check_driver_pointer_arguments(parser, pp, input, stream, types, &spell, token, pointer_flags, enabled, publication, phase, scratch))
                {
                    flags &= ~pointer_flags;
                }
            }
            while (!pp->fatal && flags != 0)
            {
                const uint32_t warning_flag = flags & (0u - flags);
                const bool ignored = (enabled & warning_flag) == 0;
                // Under -fwrapv (or -fno-strict-overflow) a shift's result and a negative left operand are defined, and clang's
                // DiagnoseBadShiftValues warns of neither (isSignedOverflowDefined, #1447); a count's warnings stay.
                const bool defined = wrapv && (warning_flag & (CHECK_WARNING_SHIFT_NEGATIVE_VALUE | CHECK_WARNING_SHIFT_OVERFLOW)) != 0;
                const CheckWarningSeverity severity = defined ? CHECK_WARNING_IGNORE : check_driver_legacy_severity(pp, input, token, warning_flag, ignored);
                if (severity != CHECK_WARNING_IGNORE)
                {
                    const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                                   severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
                    PPDiagnosticPhase phase = PP_DIAGNOSTIC_SEMANTIC;
                    const uint32_t publication = check_driver_after_corrections(parser, pp, input, token,
                        check_driver_warning_position(parser, pp, input, repair, completions, completion_count, token, warning_flag, publication_end, original, &phase),
                        publication_end);
                    const uint32_t index = check_diagnostic_warning_index(warning_flag);
                    const char* option = index < BUSTER_ARRAY_LENGTH(check_warning_descriptions) ? check_warning_descriptions[index].option : NULL;
                    const bool promoted = severity >= CHECK_WARNING_ERROR;
                    const char* suffix = option != NULL ? check_warning_text(scratch, " [%s%s]", promoted ? "-Werror," : "", option) :
                                         promoted ? " [-Werror]" : "";
                    if (suffix != NULL)
                    {
                        uint32_t located = token;
                        const char* conditional = warning_flag == CHECK_WARNING_CONDITIONAL_POINTER_TYPES || warning_flag == CHECK_WARNING_CONDITIONAL_POINTER_INTEGER ?
                                                  check_driver_conditional_mismatch_message(parser, pp, input, stream, types, &spell, token, warning_flag, &located, scratch) :
                                                  NULL;
                        const char* message = conditional != NULL ? conditional :
                                              warning_flag == CHECK_WARNING_POINTER_SIGN || warning_flag == CHECK_WARNING_DISCARDED_QUALIFIERS ?
                                              check_driver_pointer_conversion_message(parser, pp, input, stream, types, &spell, token, warning_flag, &located, scratch) :
                                              (warning_flag & (CHECK_WARNING_SHIFT_NEGATIVE_VALUE | CHECK_WARNING_SHIFT_OVERFLOW)) != 0 ?
                                              check_driver_shift_message(parser, pp, input, stream, types, &spell, token, warning_flag, scratch) :
                                              check_diagnostic_message(warning_flag);
                        const uint32_t node = input->token_nodes != NULL && token < input->token_count ? input->token_nodes[token] : BUSTER_CHECK_NONE;
                        const bool stashed = shifts != NULL && shifts->records != NULL && phase == PP_DIAGNOSTIC_DEFERRED &&
                                             (warning_flag & (CHECK_WARNING_SHIFT_COUNT_NEGATIVE | CHECK_WARNING_SHIFT_COUNT_OVERFLOW |
                                                              CHECK_WARNING_SHIFT_NEGATIVE_VALUE)) != 0 &&
                                             node < input->graph.count && check_common_shift_op(input->graph.ops[node]);
                        // A folded shift's runtime warning in an unevaluated operand's statement expression or an array bound,
                        // which clang's DiagRuntimeBehavior drops (#1447).
                        const bool dropped = !stashed &&
                                             (warning_flag & (CHECK_WARNING_SHIFT_COUNT_NEGATIVE | CHECK_WARNING_SHIFT_COUNT_OVERFLOW |
                                                              CHECK_WARNING_SHIFT_NEGATIVE_VALUE)) != 0 && node < input->graph.count &&
                                             (check_common_unevaluated(parser, pp, input, token, scratch) || check_common_bound(parser, pp, input, token, scratch));
                        if (stashed)
                        {
                            // The common walk reports it (CheckCommonShiftWarnings), or drops it where unevaluated.
                            const char* text = check_warning_text(scratch, "%s%s", message, suffix);
                            const uint64_t length = text != NULL ? strlen(text) : 0;
                            char* owned = text != NULL && shifts->count < shifts->capacity ? arena_push(&pp->arena, length + 1u, 1) : NULL;
                            if (owned != NULL)
                            {
                                memcpy(owned, text, length + 1u);
                                shifts->records[shifts->count++] = (CheckCommonShiftWarning){
                                    .token = token,
                                    .source = parser->tokens.sources[located],
                                    .publication = publication,
                                    .severity = (uint8_t)pp_severity,
                                    .phase = (uint8_t)phase,
                                    .message = owned,
                                };
                            }
                            else
                            {
                                shifts_ready = false;
                            }
                        }
                        else if (!dropped)
                        {
                            const PPDiagnosticRecord* previous = pp->diagnostic_last;
                            pp_diagnose_output_phase(pp, pp_severity, parser->tokens.sources[located], publication, phase, "%s%s", message, suffix);
                            // A file-scope declarator's queue keeps the order clang builds its expressions in, the shift
                            // before a comma that holds it (#1943, check_common_report_comma_left): by its node's end.
                            if (phase == PP_DIAGNOSTIC_RUNTIME && node < input->graph.count && input->graph.source_positions != NULL)
                            {
                                pp_diagnostic_suborder(pp, previous, (uint64_t)input->graph.source_positions[node] + 1);
                            }
                        }
                    }
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
            while (walking && !pp->fatal && reports && (firsts[report] >> 32) == token)
            {
                const uint32_t sequence = (uint32_t)firsts[report];
                const uint32_t offset = input->report_count + stream_reports + extra_count;
                if (sequence >= (uint32_t)call_offset64)
                {
                    const uint32_t call = sequence - (uint32_t)call_offset64;
                    check_driver_kr_call(parser, pp, input, call, stream->warnings[call] & CHECK_WARNING_KR_CALLS);
                }
                else if (sequence >= (uint32_t)kr_offset64)
                {
                    const CheckInputKrDiagnostic* diagnostic = input->kr_diagnostics + (sequence - (uint32_t)kr_offset64);
                    if (diagnostic->warning == CHECK_WARNING_KR_DEFINITION)
                    {
                        check_driver_kr_definition(parser, original, pp, input, diagnostic, publication_end);
                    }
                    else
                    {
                        check_driver_kr_call_at(parser, pp, input, check_diagnostic_kr_name(diagnostic), diagnostic->token, diagnostic->warning);
                    }
                }
                else if (sequence >= offset)
                {
                    const uint32_t implicit = sequence - offset;
                    const CheckWarningSeverity severity = check_diagnostic_implicit_function(input, implicit);
                    const bool builtin = input->implicit_function_builtins[implicit];
                    const bool default_error = input->implicit_function_c99 || builtin;
                    const bool source_error = input->fatal_errors && severity == CHECK_WARNING_ERROR;
                    const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? (default_error ? PP_SEVERITY_FATAL : PP_SEVERITY_MAPPED_FATAL) :
                                                   severity == CHECK_WARNING_ERROR ? (default_error && !source_error ? PP_SEVERITY_ERROR : PP_SEVERITY_MAPPED_ERROR) : PP_SEVERITY_WARNING;
                    const uint32_t name = parser->tokens.names[token];
                    // A warning the policy made an error is clang's "-Werror," one; a default error has no prefix.
                    const char* promoted = !default_error && severity >= CHECK_WARNING_ERROR ? "-Werror," : "";
                    const uint32_t publication = check_driver_publication_position(parser, pp, token + 1u, publication_end);
                    const bool library = (input->function_flags[token] & CHECK_INPUT_FUNCTION_IMPLICIT_LIBRARY) != 0;
                    if (library)
                    {
                        // Clang declares the builtin as its name is resolved, before the expression around it is built: where
                        // the name completes with that expression (a value's `int *q = malloc;`, #2348), its error comes first.
                        const PPDiagnosticRecord* previous = pp->diagnostic_last;
                        check_driver_implicit_library(pp, types, input, pp_severity, parser->tokens.sources[token], publication, intern_bytes(pp->table, name),
                                                      intern_length(pp->table, name), input->implicit_function_c99, promoted, scratch);
                        pp_diagnostic_suborder(pp, previous, 1);
                    }
                    else if (default_error)
                    {
                        // The function the name may have meant (#1447, c_check_typo.c), which clang's ImplicitlyDefineFunction
                        // searches only where its diagnostic is an error: a "did you mean" note at the call's name and a
                        // "declared here" one at the function's.
                        const uint32_t meant = input->implicit_function_corrections != NULL && implicit < input->implicit_function_count ?
                                               input->implicit_function_corrections[implicit] : BUSTER_CHECK_NONE;
                        const bool corrected = severity >= CHECK_WARNING_ERROR && meant < count;
                        const uint32_t other = corrected ? parser->tokens.names[meant] : 0;
                        char suggestion[512];
                        char declared[512];
                        snprintf(suggestion, sizeof(suggestion), "did you mean '%.*s'?", (int)intern_length(pp->table, other), intern_bytes(pp->table, other));
                        snprintf(declared, sizeof(declared), "'%.*s' declared here", (int)intern_length(pp->table, other), intern_bytes(pp->table, other));
                        const PPOutputDiagnosticNote notes[2] =
                        {
                            { .token = parser->tokens.sources[token], .message = suggestion },
                            { .token = corrected ? parser->tokens.sources[meant] : 0, .message = declared },
                        };
                        pp_diagnose_output_notes_at(pp, pp_severity, parser->tokens.sources[token], publication, notes, corrected ? 2u : 0u,
                            builtin ? "use of unknown builtin '%.*s' [-Wimplicit-function-declaration]" :
                            "call to undeclared function '%.*s'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]",
                            (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                    }
                    else
                    {
                        pp_diagnose_output_at(pp, pp_severity, parser->tokens.sources[token], publication, "implicit declaration of function '%.*s' [%s-Wimplicit-function-declaration]",
                                           (int)intern_length(pp->table, name), intern_bytes(pp->table, name), promoted);
                    }
                }
                else
                {
                    const uint32_t phase = sequence < input->report_count ? 0 : sequence - input->report_count < stream_reports ? 1 : 2;
                    const uint32_t index = phase == 0 ? sequence : phase == 1 ? sequence - input->report_count : sequence - input->report_count - stream_reports;
                    const char* message = phase == 2 ? extra[index].message : phase == 1 ? check_driver_report_message(stream->reports + index) :
                                                                                         check_semantic_input(parser, pp, input, types, input->reports + index, &spell, scratch);
                    const uint32_t located = phase == 2 ? extra[index].token : phase == 1 ? stream->reports[index].token : input->reports[index].token;
                    const uint32_t requested = phase == 0 ? check_diagnostic_input_report_source(input, parser, input->reports + index) : BUSTER_CHECK_NONE;
                    const CheckInputFpPragma* fp_error = phase == 0 ? check_input_fp_report(input, input->reports + index) : NULL;
                    const uint32_t source = requested != BUSTER_CHECK_NONE ? requested : located < count ? parser->tokens.sources[located] : BUSTER_CHECK_NONE;
                    const uint32_t node = phase == 1 ? stream->reports[index].node :
                                          phase == 2 && located < input->token_count && input->token_nodes != NULL ? input->token_nodes[located] : BUSTER_CHECK_NONE;
                    const uint32_t completion = phase == 0 ? input->reports[index].completion :
                                                phase == 2 && extra[index].completion != 0 ? extra[index].completion :
                                                phase == 2 && extra[index].position != located ? extra[index].position :
                                                node < input->graph.count && input->graph.source_positions != NULL ? input->graph.source_positions[node] : 0;
                    // An x86 builtin's call whose hard immediate constraint failed: clang's message at the call's begin,
                    // published where the report's own token puts it (#1066).
                    char x86_text[BUSTER_CHECK_DIAGNOSTIC_X86_TEXT];
                    uint32_t x86_location = BUSTER_CHECK_NONE;
                    PPOutputDiagnosticNote x86_note[BUSTER_CHECK_SEMANTIC_NOTES] = { 0 };
                    const char* x86_message = phase == 1 ? check_diagnostic_x86_hard(input, stream, stream->reports + index, x86_text, sizeof(x86_text), &x86_location) :
                                                           NULL;
                    if (x86_message == NULL && phase == 1)
                    {
                        x86_message = check_diagnostic_varargs(input, parser, types, stream->reports + index, &x86_location);
                    }
                    if (x86_message == NULL && phase == 1)
                    {
                        x86_message = check_diagnostic_counted_ref(input, parser, stream->reports + index, &x86_location);
                    }
                    if (x86_message == NULL && phase == 1)
                    {
                        x86_message = check_driver_dump_struct(parser, pp, input, stream, types, &spell, stream->reports + index, &x86_location, x86_note, scratch);
                    }
                    // A failed assignment conversion's clang words, with an argument's parameter note (#147).
                    uint32_t conversion_note_token = BUSTER_CHECK_NONE;
                    const char* conversion_note = NULL;
                    if (x86_message == NULL && phase == 1)
                    {
                        x86_message = check_driver_conversion_error(parser, pp, input, stream, types, &spell, stream->reports + index, &x86_location,
                                                                    &conversion_note_token, &conversion_note, scratch);
                    }
                    if (x86_message == NULL && phase == 1)
                    {
                        x86_message = check_driver_call_count(parser, pp, input, stream, types, stream->reports + index, &x86_location, &conversion_note_token,
                                                              &conversion_note, scratch);
                    }
                    if (x86_message == NULL && phase == 1)
                    {
                        x86_message = check_semantic_expression(parser, pp, input, stream, types, stream->reports + index, &spell, &x86_location, x86_note, scratch);
                    }
                    if (phase == 0 && input->reports[index].message == CHECK_INPUT_FP_EVAL_USE)
                    {
                        const CheckInputReport* report = input->reports + index;
                        pp_diagnose_eval_method_use(pp, report->subject - 1, report->detail, (uint32_t)report->value);
                    }
                    else if (source != BUSTER_CHECK_NONE)
                    {
                        const PPDiagnosticRecord* previous = pp->diagnostic_last;
                        uint32_t publication = completion != 0 ? check_driver_publication_position(parser, pp, completion, publication_end) : source;
                        // An alias or ifunc definition's error is clang's after the multiversioning error at the
                        // function's name, though its location is its earlier attribute (#839).
                        if (phase == 0 && input->reports[index].sort != 0)
                        {
                            const uint32_t sorted = check_driver_publication_position(parser, pp, input->reports[index].sort, publication_end);
                            publication = publication < sorted ? sorted : publication;
                        }
                        // The report's identifier supplies its spelling. Its sorted publication key can be a later
                        // expression's end, as for an attribute count diagnosed after an argument's call error.
                        const CheckInputMessage kind = phase == 0 ? input->reports[index].message : CHECK_INPUT_UNSUPPORTED;
                        const bool named = phase == 0 && located < count && parser->tokens.kinds[located] == PARSE_KIND_IDENTIFIER;
                        if (kind >= CHECK_INPUT_MUSTTAIL_ARGUMENTS && kind <= CHECK_INPUT_MUSTTAIL_STATEMENT && named)
                        {
                            // clang's musttail attribute where it doesn't go (#147), spelled as clang spells it there.
                            char spelling[96];
                            check_driver_musttail_spelling(parser, pp, located, kind == CHECK_INPUT_MUSTTAIL_ARGUMENTS, spelling, sizeof(spelling));
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'%s' attribute %s", spelling,
                                                 kind == CHECK_INPUT_MUSTTAIL_ARGUMENTS ? "takes no arguments" :
                                                 kind == CHECK_INPUT_MUSTTAIL_DECLARATION ? "cannot be applied to a declaration" : "only applies to return statements");
                        }
                        else if ((kind == CHECK_INPUT_LABEL_ATTRIBUTE || kind == CHECK_INPUT_STATEMENT_ATTRIBUTE || kind == CHECK_INPUT_FALLTHROUGH_STATEMENT ||
                                  kind == CHECK_INPUT_ATTRIBUTE_SUBJECT || kind == CHECK_INPUT_ATTRIBUTE_OUT_OF_BOUNDS || kind == CHECK_INPUT_ATTRIBUTE_TYPES ||
                                  kind == CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT || kind == CHECK_INPUT_ATTRIBUTE_EXCLUSION || kind == CHECK_INPUT_ATTRIBUTE_INT_OR_BOOL ||
                                  kind == CHECK_INPUT_ATTRIBUTE_INDEX_BOUNDS) && named)
                        {
                            // An attribute on a label or a statement that clang refuses there (#147), from its first token
                            // (a C23 one's scope), worded as clang's: a GNU one spelled as written, a C23 one as clang
                            // normalizes it (its name without a keyword's underscores, `gnu::` and `clang::` for theirs),
                            // which its list's opener, the nearest shallower token before it, tells (`[` or `(`); a label's
                            // subjects by its name without a GNU keyword's underscores (check_input_label_refusal).
                            // `::` is one token in C23 and two colons before it.
                            const bool scoped = located + 2 < count && parser->tokens.kinds[located + 1] == PARSE_KIND_COLON;
                            const uint32_t at = !scoped ? located : parser->tokens.kinds[located + 2] == PARSE_KIND_COLON && located + 3 < count ? located + 3 : located + 2;
                            const uint32_t scope = scoped ? parser->tokens.names[located] : 0;
                            const uint32_t name = parser->tokens.names[at];
                            uint32_t opener = located;
                            while (opener > 0 && parser->tree.depths[opener - 1] >= parser->tree.depths[located])
                            {
                                opener -= 1;
                            }
                            const bool standard = opener > 0 && parser->tokens.kinds[opener - 1] == PARSE_KIND_L_SQUARE;
                            const String8 written = { (char*)intern_bytes(pp->table, name), intern_length(pp->table, name) };
                            const String8 bare = check_warning_attribute_normalized(written);
                            const String8 spelled = standard ? bare : written;
                            String8 scope_text = scope != 0 ? check_warning_attribute_normalized((String8){ (char*)intern_bytes(pp->table, scope), intern_length(pp->table, scope) })
                                                            : (String8){ "", 0 };
                            scope_text = check_input_attribute_equal(scope_text, "_Clang") ? (String8){ "clang", 5 } : scope_text;
                            const char* refusal = kind == CHECK_INPUT_LABEL_ATTRIBUTE ? check_input_label_refusal(bare) : NULL;
                            // A declaration's subject error names the subjects of its row (check_input_attribute_subjects).
                            const CheckInputSubjectRow* row = kind == CHECK_INPUT_ATTRIBUTE_SUBJECT ? check_input_subject_row(scope_text, bare, scope != 0, standard) : NULL;
                            char subjects[224];
                            snprintf(subjects, sizeof(subjects), "only applies to %s", row != NULL ? row->message : "other declarations");
                            // An index of a function's parameters past them (#147, clang's checkFunctionOrMethodParameterIndex),
                            // by its place among the attribute's arguments.
                            char bounds[128];
                            snprintf(bounds, sizeof(bounds), "parameter %u is out of bounds", input->reports[index].detail);
                            // A thread-safety lock function's parameter index (#147, checkAttrArgsAreCapabilityObjs), with clang's
                            // extra information: the argument's place, then the parameters' count.
                            const uint32_t place = input->reports[index].detail & 0xffffu;
                            const uint32_t parameters = input->reports[index].detail >> 16;
                            if (kind == CHECK_INPUT_ATTRIBUTE_INDEX_BOUNDS)
                            {
                                snprintf(bounds, sizeof(bounds), parameters == 0 ? "parameter %u is out of bounds: no parameters to index into" :
                                         parameters == 1 ? "parameter %u is out of bounds: can only be 1, since there is one parameter" :
                                                           "parameter %u is out of bounds: must be between 1 and %u", place, parameters);
                            }
                            // An argument count other than clang's (#147, BUSTER_CHECK_COUNT_*), as its three messages word it.
                            const uint32_t detail = input->reports[index].detail;
                            const uint32_t bound = detail >> 8;
                            char arguments[64];
                            snprintf(arguments, sizeof(arguments), (detail & 0xffu) == BUSTER_CHECK_COUNT_EXACT ?
                                     (bound == 0 ? "takes no arguments" : bound == 1 ? "takes one argument" : "requires exactly %u arguments") :
                                     (detail & 0xffu) == BUSTER_CHECK_COUNT_LEAST ? (bound == 1 ? "takes at least %u argument" : "takes at least %u arguments") :
                                     (bound == 1 ? "takes no more than %u argument" : "takes no more than %u arguments"), bound);
                            // A mutual exclusion names the attribute already there too, as clang names an applied one: a
                            // GNU one without its keyword's underscores, a C23 one as above (check_input_attribute_excluding).
                            const uint32_t other = kind == CHECK_INPUT_ATTRIBUTE_EXCLUSION ? input->reports[index].note_token : count;
                            char excluded[160] = { 0 };
                            if (other < count)
                            {
                                const bool other_scoped = other + 2 < count && parser->tokens.kinds[other + 1] == PARSE_KIND_COLON;
                                const uint32_t other_at = !other_scoped ? other : parser->tokens.kinds[other + 2] == PARSE_KIND_COLON && other + 3 < count ? other + 3 : other + 2;
                                const uint32_t other_name = parser->tokens.names[other_at];
                                const String8 other_bare = check_warning_attribute_normalized((String8){ (char*)intern_bytes(pp->table, other_name), intern_length(pp->table, other_name) });
                                String8 other_scope = other_scoped ? check_warning_attribute_normalized((String8){ (char*)intern_bytes(pp->table, parser->tokens.names[other]),
                                                                                                          intern_length(pp->table, parser->tokens.names[other]) }) :
                                                                     (String8){ "", 0 };
                                other_scope = check_input_attribute_equal(other_scope, "_Clang") ? (String8){ "clang", 5 } : other_scope;
                                snprintf(excluded, sizeof(excluded), "and '%.*s%s%.*s' attributes are not compatible", (int)other_scope.length, other_scope.pointer,
                                         other_scoped ? "::" : "", (int)other_bare.length, other_bare.pointer);
                            }
                            const char* ending = kind == CHECK_INPUT_STATEMENT_ATTRIBUTE ? "cannot be applied to a statement" :
                                                 kind == CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT ? arguments :
                                                 kind == CHECK_INPUT_ATTRIBUTE_EXCLUSION ? excluded :
                                                 kind == CHECK_INPUT_ATTRIBUTE_OUT_OF_BOUNDS || kind == CHECK_INPUT_ATTRIBUTE_INDEX_BOUNDS ? bounds :
                                                 kind == CHECK_INPUT_ATTRIBUTE_INT_OR_BOOL ? "requires parameter 1 to be int or bool" :
                                                 kind == CHECK_INPUT_ATTRIBUTE_TYPES ? "cannot be applied to types" :
                                                 kind == CHECK_INPUT_FALLTHROUGH_STATEMENT ? "only applies to empty statements" :
                                                 kind == CHECK_INPUT_ATTRIBUTE_SUBJECT ? subjects :
                                                 refusal != NULL ? refusal : "cannot be applied to a label";
                            const PPOutputDiagnosticNote conflicting = {
                                .token = other < count ? parser->tokens.sources[other] : 0,
                                .message = "conflicting attribute is here",
                            };
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &conflicting, other < count ? 1u : 0u, "'%.*s%s%.*s' %s%s",
                                                        (int)scope_text.length, scope_text.pointer, scope != 0 ? "::" : "", (int)spelled.length, spelled.pointer,
                                                        other < count ? "" : "attribute ", ending);
                        }
                        else if (fp_error != NULL && (fp_error->error & CHECK_FP_ERROR_EVAL_OPTIONS) != 0)
                        {
                            for (uint8_t bit = CHECK_FP_ERROR_EVAL_APPROX_OPTION; bit <= CHECK_FP_ERROR_EVAL_RECIPROCAL_OPTION; bit <<= 1)
                            {
                                if ((fp_error->error & bit) != 0)
                                {
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "%s", check_input_fp_error_message(bit));
                                }
                            }
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_FP_EVAL_TYPE && located < count)
                        {
                            const uint32_t name = parser->tokens.names[located];
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication,
                                                  "cannot use type '%.*s' within '#pragma clang fp eval_method'; type is set according to the default eval method for the translation unit",
                                                  (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                        }
                        else if (phase == 0 && (kind == CHECK_INPUT_DIAGNOSE_IF_ERROR || kind == CHECK_INPUT_ENABLE_IF_CALL || kind == CHECK_INPUT_ENABLE_IF_ADDRESS) &&
                                 token < count)
                        {
                            // diagnose_if's and enable_if's errors (#1447, c_check_condition.c): the message, decoded and
                            // joined, with its note at the attribute ("from 'diagnose_if' attribute on 'f':") or at the
                            // function's declaration ("candidate disabled: M"); the function named by its declaration's name.
                            const CheckInputReport* report = input->reports + index;
                            const String8 message = report->detail != 0 ? check_warning_strings_value(parser, pp, report->detail - 1, scratch) : (String8){ 0 };
                            const uint32_t named = kind != CHECK_INPUT_ENABLE_IF_CALL && report->value < count ? (uint32_t)report->value : token;
                            const uint32_t function = parser->tokens.names[named];
                            const String8 function_name = { .pointer = (char*)intern_bytes(pp->table, function), .length = intern_length(pp->table, function) };
                            char noted[512];
                            if (kind == CHECK_INPUT_DIAGNOSE_IF_ERROR)
                            {
                                snprintf(noted, sizeof(noted), "from 'diagnose_if' attribute on '%.*s':", (int)function_name.length, function_name.pointer);
                            }
                            else
                            {
                                // An empty message is clang's "<no message provided>" (#2383).
                                snprintf(noted, sizeof(noted), "candidate disabled: %.*s", message.length != 0 ? (int)message.length : 21,
                                         message.length != 0 ? message.pointer : "<no message provided>");
                            }
                            const PPOutputDiagnosticNote note = {
                                .token = report->note_token < count ? parser->tokens.sources[report->note_token] : 0,
                                .message = noted,
                            };
                            const uint32_t notes = report->note_token < count && kind != CHECK_INPUT_ENABLE_IF_ADDRESS ? 1u : 0u;
                            if (kind == CHECK_INPUT_DIAGNOSE_IF_ERROR)
                            {
                                pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, notes, "%.*s", (int)message.length, message.pointer);
                            }
                            else if (kind == CHECK_INPUT_ENABLE_IF_CALL)
                            {
                                pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, notes, "no matching function for call to '%.*s'",
                                                            (int)function_name.length, function_name.pointer);
                            }
                            else
                            {
                                pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, 0,
                                                            "cannot take address of function '%.*s' because it has one or more non-tautological enable_if conditions",
                                                            (int)function_name.length, function_name.pointer);
                            }
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_UNAVAILABLE_USE && token < count)
                        {
                            // A use of what an unavailable attribute marks (#147, check_input_deprecated_uses): its name, the
                            // attribute's message, and the note at it.
                            const CheckInputReport* report = input->reports + index;
                            const String8 message = report->detail != 0 ? check_warning_string_text(parser, pp, report->detail - 1, scratch) : (String8){ 0 };
                            // The names are the tokens' (the note's its declaration's: an enumerator's enum's where the enum
                            // marks it), or at an anonymous tag's keyword its type's spelling, with its quotes.
                            const bool typed = input->declared_types != NULL;
                            const String8 used_type = parser->tokens.kinds[token] != PARSE_KIND_IDENTIFIER && typed ?
                                                      check_driver_warning_type(parser, pp, input, types, &spell, scratch, input->declared_types[token], 0) : (String8){ 0 };
                            const bool noted_token = report->note_token < count;
                            const String8 note_type = noted_token && parser->tokens.kinds[report->note_token] != PARSE_KIND_IDENTIFIER && typed ?
                                                      check_driver_warning_type(parser, pp, input, types, &spell, scratch, input->declared_types[report->note_token], 0) :
                                                      (String8){ 0 };
                            const uint32_t used = parser->tokens.names[token];
                            const String8 used_name = { .pointer = (char*)intern_bytes(pp->table, used), .length = intern_length(pp->table, used) };
                            const uint32_t declared = noted_token && parser->tokens.kinds[report->note_token] == PARSE_KIND_IDENTIFIER ? parser->tokens.names[report->note_token] : used;
                            const String8 declared_name = { .pointer = (char*)intern_bytes(pp->table, declared), .length = intern_length(pp->table, declared) };
                            const String8 used_text = used_type.length != 0 ? used_type : used_name;
                            const String8 declared_text = note_type.length != 0 ? note_type : noted_token || used_type.length == 0 ? declared_name : used_type;
                            const char* used_quote = used_type.length != 0 ? "" : "'";
                            const char* note_quote = note_type.length != 0 || (!noted_token && used_type.length != 0) ? "" : "'";
                            char noted[256];
                            snprintf(noted, sizeof(noted), "%s%.*s%s has been explicitly marked unavailable here", note_quote, (int)declared_text.length, declared_text.pointer, note_quote);
                            const PPOutputDiagnosticNote note = {
                                .token = report->note_token < count ? parser->tokens.sources[report->note_token] : 0,
                                .message = noted,
                            };
                            // Once more for each of a tag's declarators after the first (report->value).
                            for (uint64_t copy = 0; copy <= report->value; copy += 1)
                            {
                                pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, report->note_token < count ? 1u : 0u,
                                                            "%s%.*s%s is unavailable%s%.*s", used_quote, (int)used_text.length, used_text.pointer, used_quote,
                                                            message.length != 0 ? ": " : "", (int)message.length, message.pointer);
                            }
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_REGISTER_NAME)
                        {
                            // An explicit register variable's label (#1846), as written.
                            const CheckInputReport* report = input->reports + index;
                            const uint32_t length = report->detail & ~(BUSTER_CHECK_REGISTER_NAME_UNSUITABLE | BUSTER_CHECK_REGISTER_NAME_SIZE);
                            const char* text = (const char*)(uintptr_t)report->value;
                            if ((report->detail & BUSTER_CHECK_REGISTER_NAME_SIZE) != 0)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "size of register '%.*s' does not match variable size", (int)length, text);
                            }
                            else if ((report->detail & BUSTER_CHECK_REGISTER_NAME_UNSUITABLE) != 0)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "register '%.*s' unsuitable for global register variables on this target",
                                                      (int)length, text);
                            }
                            else
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "unknown register name '%.*s' in asm", (int)length, text);
                            }
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_ASM_CONSTRAINT)
                        {
                            // An asm operand's constraint clang's validation refuses (#1973), as written.
                            const CheckInputReport* report = input->reports + index;
                            const uint32_t length = report->detail & ~BUSTER_CHECK_ASM_OUTPUT;
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "invalid %s constraint '%.*s' in asm",
                                                  (report->detail & BUSTER_CHECK_ASM_OUTPUT) != 0 ? "output" : "input", (int)length,
                                                  (const char*)(uintptr_t)report->value);
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_INTERRUPT_HANDLER && (input->reports[index].detail & BUSTER_CHECK_INTERRUPT_UNLOCATED) != 0)
                        {
                            // x86's interrupt where clang finds no place for its error (#147, check_input_interrupt_handler).
                            pp_diagnose_unlocated_notes(pp, PP_SEVERITY_ERROR, publication, NULL, 0, "%s", message);
                        }
                        else if (phase == 0 && kind >= CHECK_INPUT_AVAILABILITY_PLATFORM && kind <= CHECK_INPUT_AVAILABILITY_REPEATED && token < count)
                        {
                            // __builtin_available's specs (#148): a platform's name as written, an anyAppleOS version as
                            // clang's VersionTuple prints it, and a repeated platform's canonical name.
                            const uint32_t name = parser->tokens.names[token];
                            const uint32_t platform = input->reports[index].detail;
                            if (kind == CHECK_INPUT_AVAILABILITY_PLATFORM)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "unrecognized platform name %.*s", (int)intern_length(pp->table, name),
                                                      intern_bytes(pp->table, name));
                            }
                            else if (kind == CHECK_INPUT_AVAILABILITY_REPEATED)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "version for '%s' already specified",
                                                      check_availability_platforms[platform < BUSTER_ARRAY_LENGTH(check_availability_platforms) ? platform : 0]);
                            }
                            else
                            {
                                const uint32_t at = parser->tokens.sources[token];
                                const uint64_t spelling_mark = scratch->length;
                                uint8_t* buffer = arena_push(scratch, pp->output.kinds[at] == BUSTER_PP_KIND_INTEGER ? 24 : (uint64_t)pp->output.values[at] - pp->output.locations[at] + 1, 1);
                                const uint8_t* bytes = NULL;
                                const uint32_t length = buffer != NULL ? pp_spelling(pp, pp->output.kinds[at], pp->output.values[at], pp->output.locations[at], buffer, &bytes) : 0;
                                const CheckAvailabilityVersion version = check_availability_version(bytes, length);
                                char text[48];
                                snprintf(text, sizeof(text), version.components == 1 ? "%u" : version.components == 2 ? "%u.%u" : "%u.%u.%u", version.major, version.minor,
                                         version.subminor);
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "invalid anyAppleOS version '%s' in availability check", text);
                                arena_pop_to(scratch, spelling_mark);
                            }
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_AVAILABILITY_WILDCARD && token < count)
                        {
                            // Just past the last version, where clang's fix-it inserts ", *".
                            const uint32_t at = parser->tokens.sources[token];
                            const uint64_t spelling_mark = scratch->length;
                            uint8_t* buffer = arena_push(scratch, pp->output.kinds[at] == BUSTER_PP_KIND_INTEGER ? 24 : (uint64_t)pp->output.values[at] - pp->output.locations[at] + 1, 1);
                            const uint8_t* bytes = NULL;
                            const uint32_t length = buffer != NULL ? pp_spelling(pp, pp->output.kinds[at], pp->output.values[at], pp->output.locations[at], buffer, &bytes) : 0;
                            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, length, publication, "must handle potential future platforms with '*'");
                            arena_pop_to(scratch, spelling_mark);
                        }
                        else if (phase == 0 && (kind == CHECK_INPUT_COUNTED_REF_LEAK || kind == CHECK_INPUT_COUNTED_REF_USE))
                        {
                            // __builtin_counted_by_ref's value where clang refuses it (#148).
                            const uint32_t detail = input->reports[index].detail;
                            static const char* const leaks[] = { "assigned to a variable", "passed into a function", "returned from a function" };
                            if (kind == CHECK_INPUT_COUNTED_REF_LEAK)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "value returned by '__builtin_counted_by_ref' cannot be %s",
                                                      leaks[detail < 3 ? detail : 0]);
                            }
                            else
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "value returned by '__builtin_counted_by_ref' cannot be used in %s expression",
                                                      detail == 0 ? "an array subscript" : "a binary");
                            }
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_INITIALIZER_MEMBER)
                        {
                            const uint64_t type_mark = scratch->length;
                            const CheckInitStatus status = (CheckInitStatus)input->reports[index].detail;
                            const char* text = check_driver_initializer_field_message(parser, pp, input, types, status, input->reports[index].type, 0, token, scratch);
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "%s",
                                                  text != NULL ? text : "initializer checking capacity exhausted");
                            arena_pop_to(scratch, type_mark);
                        }
                        else if (phase == 0 && kind >= CHECK_INPUT_COUNTED_UNION && kind <= CHECK_INPUT_COUNTED_PAREN)
                        {
                            // The counted_by family's errors (#148), worded as clang's, by the attribute's kind; the count's
                            // name is the report's token's, and its notes at the count's declaration or the attribute's `(`.
                            const CheckInputReport* report = input->reports + index;
                            const char* attribute = check_input_counted_names[(report->detail & 0xffu) < 4 ? (report->detail & 0xffu) + 1 : 1];
                            const bool array = (report->detail & 0x100u) != 0;
                            const uint32_t fault = report->detail >> 16;
                            const uint32_t name = token < count ? parser->tokens.names[token] : 0;
                            const int name_length = name != 0 ? (int)intern_length(pp->table, name) : 0;
                            const char* name_bytes = name != 0 ? (const char*)intern_bytes(pp->table, name) : "";
                            char noted[192];
                            snprintf(noted, sizeof(noted), kind == CHECK_INPUT_COUNTED_PAREN ? "to match this '('" : "field '%.*s' declared here", name_length, name_bytes);
                            const PPOutputDiagnosticNote note = {
                                .token = report->note_token < count ? parser->tokens.sources[report->note_token] : 0,
                                .message = noted,
                            };
                            const uint32_t notes = report->note_token < count && report->note_kind != CHECK_INPUT_NOTE_NONE ? 1u : 0u;
                            const uint64_t type_mark = scratch->length;
                            String8 type = { 0 };
                            if (kind == CHECK_INPUT_COUNTED_POINTEE)
                            {
                                CheckWarningSpelling spell = { .count = pp->table->count };
                                spell.bytes = BUSTER_ARENA_PUSH_ARRAY(scratch, const uint8_t*, spell.count);
                                spell.lengths = spell.bytes != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, spell.count) : NULL;
                                for (uint32_t s = 0; spell.lengths != NULL && s < spell.count; s += 1)
                                {
                                    spell.bytes[s] = intern_bytes(pp->table, s);
                                    spell.lengths[s] = intern_length(pp->table, s);
                                }
                                const bool spelled = spell.lengths != NULL && check_driver_spelling_build(parser, pp, input, types, &spell, scratch);
                                type = spelled ? check_driver_warning_type(parser, pp, input, types, &spell, scratch, report->type, 0) : (String8){ 0 };
                            }
                            static const char* const faults[] = { "", "an incomplete type", "a sizeless type", "a function type", "a struct type with a flexible array member" };
                            switch (kind)
                            {
                                case CHECK_INPUT_COUNTED_UNION:
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'%s' cannot be applied to a union member", attribute);
                                    break;
                                case CHECK_INPUT_COUNTED_SUBJECT:
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'%s' only applies to pointers%s%s", attribute,
                                                          (report->detail & 0xffu) == 0 ? " or C99 flexible array members" : "", array ? "; did you mean to use 'counted_by'?" : "");
                                    break;
                                case CHECK_INPUT_COUNTED_POINTEE:
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'%s' cannot be applied to %s of unknown size because %.*s is %s", attribute,
                                                          array ? "an array with element" : "a pointer with pointee", (int)type.length, type.pointer, faults[fault < 5 ? fault : 0]);
                                    break;
                                case CHECK_INPUT_COUNTED_INTEGER:
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'%s' requires a non-boolean integer type argument", attribute);
                                    break;
                                case CHECK_INPUT_COUNTED_REFERENCE:
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'%s' argument must be a simple declaration reference", attribute);
                                    break;
                                case CHECK_INPUT_COUNTED_OUTSIDE:
                                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, notes, "field '%.*s' in '%s' not inside structure", name_length,
                                                                name_bytes, attribute);
                                    break;
                                case CHECK_INPUT_COUNTED_UNION_FIELD:
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'%s' argument cannot refer to a union member", attribute);
                                    break;
                                case CHECK_INPUT_COUNTED_STRUCT:
                                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, notes, "'%s' field '%.*s' isn't within the same struct as the annotated %s",
                                                                attribute, name_length, name_bytes, array ? "flexible array" : "pointer");
                                    break;
                                case CHECK_INPUT_COUNTED_PAREN:
                                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, notes, "expected ')'");
                                    break;
                                default:
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "%s", message);
                                    break;
                            }
                            arena_pop_to(scratch, type_mark);
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_ATTRIBUTES_MISPLACED && token < count)
                        {
                            // Just after the tag's keyword, where clang's fix-it puts the attributes.
                            const uint32_t keyword = parser->tokens.names[token];
                            pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, keyword != 0 ? intern_length(pp->table, keyword) : 0, publication, "%s", message);
                        }
                        else if (phase == 0 && (kind == CHECK_INPUT_BUILTIN_CONFLICT || kind == CHECK_INPUT_BUILTIN_REDECLARE) && token < count)
                        {
                            // A builtin's declaration of another type (#1843): clang's "conflicting types", or a custom-checked
                            // one's any declaration, "cannot redeclare builtin function" (#1918's review), with its note, at
                            // the same name, of the builtin's type as clang prints it (check_driver_warning_type).
                            const CheckInputReport* report = input->reports + index;
                            const uint64_t type_mark = scratch->length;
                            CheckWarningSpelling spell = { .count = pp->table->count };
                            spell.bytes = BUSTER_ARENA_PUSH_ARRAY(scratch, const uint8_t*, spell.count);
                            spell.lengths = spell.bytes != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, spell.count) : NULL;
                            for (uint32_t s = 0; spell.lengths != NULL && s < spell.count; s += 1)
                            {
                                spell.bytes[s] = intern_bytes(pp->table, s);
                                spell.lengths[s] = intern_length(pp->table, s);
                            }
                            const bool spelled = spell.lengths != NULL && check_driver_spelling_build(parser, pp, input, types, &spell, scratch);
                            const String8 type = spelled ? check_driver_warning_type(parser, pp, input, types, &spell, scratch, report->type, 0) : (String8){ 0 };
                            const uint32_t name = parser->tokens.names[token];
                            char note_text[512];
                            snprintf(note_text, sizeof(note_text), "'%.*s' is a builtin with type %.*s", (int)intern_length(pp->table, name),
                                     (const char*)intern_bytes(pp->table, name), (int)type.length, type.pointer);
                            const PPOutputDiagnosticNote note = {
                                .token = source,
                                .message = note_text,
                            };
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, type.length != 0 ? 1u : 0u,
                                                        kind == CHECK_INPUT_BUILTIN_REDECLARE ? "cannot redeclare builtin function '%.*s'" : "conflicting types for '%.*s'",
                                                        (int)intern_length(pp->table, name), (const char*)intern_bytes(pp->table, name));
                            arena_pop_to(scratch, type_mark);
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_BUILTIN_DEFINITION && token < count)
                        {
                            const uint32_t name = parser->tokens.names[token];
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "definition of builtin function '%.*s'", (int)intern_length(pp->table, name),
                                                  (const char*)intern_bytes(pp->table, name));
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_ENUM_NOT_REPRESENTABLE)
                        {
                            // The underlying type as clang prints it, its typedef's name first (check_driver_warning_type).
                            const CheckInputReport* report = input->reports + index;
                            const uint64_t type_mark = scratch->length;
                            CheckWarningSpelling spell = { .count = pp->table->count };
                            spell.bytes = BUSTER_ARENA_PUSH_ARRAY(scratch, const uint8_t*, spell.count);
                            spell.lengths = spell.bytes != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, spell.count) : NULL;
                            for (uint32_t s = 0; spell.lengths != NULL && s < spell.count; s += 1)
                            {
                                spell.bytes[s] = intern_bytes(pp->table, s);
                                spell.lengths[s] = intern_length(pp->table, s);
                            }
                            const bool spelled = spell.lengths != NULL && check_driver_spelling_build(parser, pp, input, types, &spell, scratch);
                            const String8 type = spelled ? check_driver_warning_type(parser, pp, input, types, &spell, scratch, report->type, report->type_spelling) :
                                                           (String8){ 0 };
                            char value[32] = "";
                            if (report->detail == 1)
                            {
                                snprintf(value, sizeof(value), " %" PRIu64, report->value);
                            }
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "enumerator value%s is not representable in the underlying type %.*s", value,
                                                  (int)type.length, type.pointer);
                            arena_pop_to(scratch, type_mark);
                        }
                        else if (phase == 0 && (kind == CHECK_INPUT_VECTOR_ELEMENT || kind == CHECK_INPUT_FUNCTION_RETURN || kind == CHECK_INPUT_ARRAY_FUNCTIONS ||
                                 kind == CHECK_INPUT_ARRAY_INCOMPLETE || kind == CHECK_INPUT_ARRAY_NEGATIVE || kind == CHECK_INPUT_ARRAY_TOO_LARGE ||
                                 kind == CHECK_INPUT_ARRAY_ELEMENT_ALIGNMENT ||
                                 kind == CHECK_INPUT_ARRAY_NON_INTEGER ||
                                 ((kind == CHECK_INPUT_INCOMPLETE_OBJECT || kind == CHECK_INPUT_INCOMPLETE_TENTATIVE) && input->reports[index].type != 0)))
                        {
                            // The element type as clang prints it, its typedef's name first (check_driver_warning_type).
                            const CheckInputReport* report = input->reports + index;
                            const uint64_t type_mark = scratch->length;
                            CheckWarningSpelling spell = { .count = pp->table->count };
                            spell.bytes = BUSTER_ARENA_PUSH_ARRAY(scratch, const uint8_t*, spell.count);
                            spell.lengths = spell.bytes != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, spell.count) : NULL;
                            for (uint32_t s = 0; spell.lengths != NULL && s < spell.count; s += 1)
                            {
                                spell.bytes[s] = intern_bytes(pp->table, s);
                                spell.lengths[s] = intern_length(pp->table, s);
                            }
                            const bool spelled = spell.lengths != NULL && check_driver_spelling_build(parser, pp, input, types, &spell, scratch);
                            // An __auto_type placeholder's (#1844) as clang prints the undeduced type.
                            const bool placeholder = kind == CHECK_INPUT_VECTOR_ELEMENT && check_input_auto_type_level(types, report->type);
                            const String8 type = placeholder ? check_semantic_auto_type_spelling(types, report->type, scratch) :
                                                 spelled ? check_driver_warning_type(parser, pp, input, types, &spell, scratch, report->type, report->type_spelling) :
                                                           (String8){ 0 };
                            const bool forward = report->note_kind == CHECK_INPUT_NOTE_FORWARD_DECLARATION && report->note_token < count && spelled;
                            const String8 tag = forward ? check_driver_warning_type(parser, pp, input, types, &spell, scratch, type_unqualified(report->type), 0) :
                                                          (String8){ "", 0 };
                            char note_text[192];
                            snprintf(note_text, sizeof(note_text), "forward declaration of %.*s", (int)tag.length, tag.pointer);
                            const PPOutputDiagnosticNote note = {
                                .token = forward ? parser->tokens.sources[report->note_token] : 0,
                                .message = note_text,
                            };
                            // The declarator's errors (#1447) name its name, from the report's value, as clang's do.
                            const uint32_t named = kind == CHECK_INPUT_ARRAY_FUNCTIONS || kind == CHECK_INPUT_ARRAY_NEGATIVE ? (uint32_t)report->value : BUSTER_CHECK_NONE;
                            const uint32_t name_id = named < count ? parser->tokens.names[named] : 0;
                            const int name_length = named < count ? (int)intern_length(pp->table, name_id) : 0;
                            const char* name_bytes = named < count ? (const char*)intern_bytes(pp->table, name_id) : "";
                            const bool function_returned = kind == CHECK_INPUT_FUNCTION_RETURN &&
                                                           type_kind(types, type_strip_aligned(types, report->type)) == BUSTER_TYPE_KIND_FUNCTION;
                            if (kind == CHECK_INPUT_ARRAY_NEGATIVE && named < count)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'%.*s' declared as an array with a negative size", name_length, name_bytes);
                            }
                            else if (kind == CHECK_INPUT_ARRAY_NEGATIVE)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "array size is negative");
                            }
                            else if (kind == CHECK_INPUT_ARRAY_NON_INTEGER)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "size of array has non-integer type %.*s", (int)type.length, type.pointer);
                            }
                            else if (kind == CHECK_INPUT_ARRAY_TOO_LARGE)
                            {
                                // The count with clang's digit separators (APInt's toString with a separator).
                                char digits[32];
                                char grouped[48];
                                const int length = snprintf(digits, sizeof(digits), "%llu", (unsigned long long)report->value);
                                uint32_t at = 0;
                                for (int d = 0; d < length; d += 1)
                                {
                                    grouped[at++] = digits[d];
                                    if (d + 1 < length && (length - d - 1) % 3 == 0)
                                    {
                                        grouped[at++] = '\'';
                                    }
                                }
                                grouped[at] = 0;
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "array is too large (%s elements)", grouped);
                            }
                            else if (kind == CHECK_INPUT_ARRAY_ELEMENT_ALIGNMENT)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication,
                                                      "size of array element of type %.*s (%llu bytes) isn't a multiple of its alignment (%u bytes)", (int)type.length,
                                                      type.pointer, (unsigned long long)report->value, report->detail);
                            }
                            else if (kind == CHECK_INPUT_ARRAY_FUNCTIONS)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'%.*s' declared as array of functions of type %.*s",
                                                      named < count ? name_length : 9, named < count ? name_bytes : "type name", (int)type.length, type.pointer);
                            }
                            else
                            {
                                const char* format = kind == CHECK_INPUT_VECTOR_ELEMENT ? "invalid vector element type %.*s" :
                                                     kind == CHECK_INPUT_FUNCTION_RETURN ? (function_returned ? "function cannot return function type %.*s" :
                                                                                                                "function cannot return array type %.*s") :
                                                     kind == CHECK_INPUT_ARRAY_INCOMPLETE ? "array has incomplete element type %.*s" :
                                                     kind == CHECK_INPUT_INCOMPLETE_TENTATIVE ? "tentative definition has type %.*s that is never completed" :
                                                                                                "variable has incomplete type %.*s";
                                pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, forward ? 1u : 0u, format, (int)type.length, type.pointer);
                            }
                            arena_pop_to(scratch, type_mark);
                        }
                        else if (phase == 0 && (kind == CHECK_INPUT_JUMP_GOTO || kind == CHECK_INPUT_JUMP_SWITCH) &&
                                 input->reports[index].note_kind == CHECK_INPUT_NOTE_JUMP && input->reports[index].note_token < count)
                        {
                            // A jump into a statement expression (#1942), noted at its `(` as clang's jump scope checker notes it, or
                            // past a declaration whose scope it enters, at its name (c_check_vla_input.c's details).
                            static const char* const bypassed[4] = {
                                "jump enters a statement expression",
                                "jump bypasses initialization of variable length array",
                                "jump bypasses initialization of variable with __attribute__((cleanup))",
                                "jump bypasses initialization of VLA typedef",
                            };
                            const uint32_t detail = input->reports[index].detail;
                            const PPOutputDiagnosticNote note = {
                                .token = parser->tokens.sources[input->reports[index].note_token],
                                .message = bypassed[detail < 4 ? detail : 0],
                            };
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, 1, "%s",
                                                       kind == CHECK_INPUT_JUMP_GOTO ? "cannot jump from this goto statement to its label" :
                                                                                       "cannot jump from switch statement to this case label");
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_REDECLARATION && input->reports[index].detail == CHECK_REDECLARATION_GENERIC &&
                                 input->reports[index].note_kind == CHECK_INPUT_NOTE_PREVIOUS_DEFINITION)
                        {
                            const uint32_t previous = input->reports[index].note_token;
                            const PPOutputDiagnosticNote note = {
                                .token = previous < count ? parser->tokens.sources[previous] : 0,
                                .message = "previous definition is here",
                            };
                            const uint32_t name = parser->tokens.names[token];
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, previous < count ? 1u : 0u,
                                                       "redefinition of '%.*s'", (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_CONVENTION_CHANGE)
                        {
                            // A redeclaration's other calling convention (check_input_report_redeclaration), clang's err_cconv_change
                            // with its note at the first declaration.
                            const CheckInputReport* report = input->reports + index;
                            const bool explicit_first = (report->detail >> 16 & 1u) != 0;
                            char previous_text[64];
                            snprintf(previous_text, sizeof(previous_text), explicit_first ? "'%s'" : "without calling convention",
                                     check_input_convention_name(report->detail >> 8 & 0xffu));
                            const PPOutputDiagnosticNote note = {
                                .token = report->note_token < count ? parser->tokens.sources[report->note_token] : 0,
                                .message = "previous declaration is here",
                            };
                            if ((report->detail & BUSTER_CHECK_CONVENTION_NO_CALLER_SAVED) != 0)
                            {
                                // no_caller_saved_registers added by a redeclaration, clang's err_function_attribute_mismatch, the
                                // attribute named as clang prints it: a C23 one's with its gnu scope.
                                const char* name = (report->detail & BUSTER_CHECK_CONVENTION_STANDARD) != 0 ? "gnu::no_caller_saved_registers" : "no_caller_saved_registers";
                                pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, report->note_token < count ? 1u : 0u,
                                                            "function declared with '%s' attribute was previously declared without the '%s' attribute", name, name);
                            }
                            else
                            {
                                pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, report->note_token < count ? 1u : 0u,
                                                            "function declared '%s' here was previously declared %s", check_input_convention_name(report->detail & 0xffu),
                                                            previous_text);
                            }
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_ATTRIBUTE_CONVENTIONS)
                        {
                            // Two calling conventions on one function, by clang's names (check_input_attribute_conflict).
                            const uint32_t detail = input->reports[index].detail;
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "%s and %s attributes are not compatible", check_input_convention_name(detail & 0xffu),
                                                 check_input_convention_name(detail >> 8 & 0xffu));
                        }
                        else if (phase == 0 && kind >= CHECK_INPUT_SPECIFIER_COMBINE && kind <= CHECK_INPUT_SPECIFIER_COMPLEX)
                        {
                            // Specifiers that don't combine, named as clang's DeclSpec names them (#1127).
                            const CheckInputReport* report = input->reports + index;
                            const char* first = check_declspec_names[report->specifier < CHECK_DECLSPEC_NAME_COUNT ? report->specifier : 0];
                            const char* type = check_declspec_names[report->specifier_type < CHECK_DECLSPEC_NAME_COUNT ? report->specifier_type : 0];
                            if (kind == CHECK_INPUT_SPECIFIER_COMBINE)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "cannot combine with previous '%s' declaration specifier", first);
                            }
                            else if (kind == CHECK_INPUT_SPECIFIER_SIGN)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'%s' cannot be signed or unsigned", first);
                            }
                            else if (kind == CHECK_INPUT_SPECIFIER_WIDTH)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'%s %s' is invalid", first, type);
                            }
                            else
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'_Complex %s' is invalid", first);
                            }
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_LITERAL &&
                                 (check_driver_literal(pp, parser, located, source, publication, scratch) ||
                                  check_driver_literal_escapes(parser, pp, input, located, publication, scratch)))
                        {
                            // A numeric constant's error, or a string literal's or a character constant's escapes' (#1447),
                            // worded and placed as clang's.
                        }
                        else if (kind == CHECK_INPUT_UNDECLARED_CORRECTED && named && input->reports[index].note_token < count)
                        {
                            // A typo's correction (#1447, c_check_typo.c): clang's DiagnoseEmptyLookup's suggestion, and its
                            // note at the declaration taken.
                            const uint32_t name = parser->tokens.names[located];
                            const uint32_t meant = input->reports[index].note_token;
                            const uint32_t other = parser->tokens.names[meant];
                            char declared[512];
                            snprintf(declared, sizeof(declared), "'%.*s' declared here", (int)intern_length(pp->table, other), intern_bytes(pp->table, other));
                            const PPOutputDiagnosticNote note = { .token = parser->tokens.sources[meant], .message = declared };
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, 1, "use of undeclared identifier '%.*s'; did you mean '%.*s'?",
                                                        (int)intern_length(pp->table, name), intern_bytes(pp->table, name), (int)intern_length(pp->table, other),
                                                        intern_bytes(pp->table, other));
                        }
                        else if (kind == CHECK_INPUT_AMBIGUOUS && named && input->reports[index].note_token < count && input->reports[index].attribute < count)
                        {
                            // A name two C23 definitions of an enum declare (#1447, check_tag_enum_ambiguous): clang's candidates,
                            // the second definition's enumerator first.
                            const uint32_t name = parser->tokens.names[token];
                            // A block's names are its function's, which clang's qualified name spells before them (#1826):
                            // `g(int, char *)::B`, with `()` for a `(void)` prototype.
                            const uint64_t scope_mark = scratch->length;
                            const String8 scope = check_driver_function_scope(parser, pp, input, types, input->reports[index].note_token, scratch);
                            char candidate[512];
                            snprintf(candidate, sizeof(candidate), "candidate found by name lookup is '%.*s%.*s'", (int)scope.length, scope.pointer,
                                     (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                            arena_pop_to(scratch, scope_mark);
                            // (The definitions between, a third's, #1826: the reader's packed `value`, the later first.)
                            const uint64_t between = input->reports[index].value;
                            const uint32_t first_between = (uint32_t)between != 0 && (uint32_t)between - 1 < count ? (uint32_t)between - 1 : BUSTER_CHECK_NONE;
                            const uint32_t second_between = (uint32_t)(between >> 32) != 0 && (uint32_t)(between >> 32) - 1 < count ? (uint32_t)(between >> 32) - 1 :
                                                            BUSTER_CHECK_NONE;
                            PPOutputDiagnosticNote notes[4];
                            uint32_t note_count = 0;
                            notes[note_count++] = (PPOutputDiagnosticNote){ .token = parser->tokens.sources[input->reports[index].note_token], .message = candidate };
                            if (first_between != BUSTER_CHECK_NONE)
                            {
                                notes[note_count++] = (PPOutputDiagnosticNote){ .token = parser->tokens.sources[first_between], .message = candidate };
                            }
                            if (second_between != BUSTER_CHECK_NONE)
                            {
                                notes[note_count++] = (PPOutputDiagnosticNote){ .token = parser->tokens.sources[second_between], .message = candidate };
                            }
                            notes[note_count++] = (PPOutputDiagnosticNote){ .token = parser->tokens.sources[input->reports[index].attribute], .message = candidate };
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, notes, note_count, "reference to '%.*s' is ambiguous",
                                                        (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                        }
                        else if (kind == CHECK_INPUT_UNDECLARED && named && (input->reports[index].detail != 0 || input->reports[index].subject != 0))
                        {
                            // A typo's suggestion that takes no declaration (#1447, check_typo_suggestion): a keyword, or a type
                            // name with its note, and the name stays undeclared.
                            const CheckInputReport* report = input->reports + index;
                            const uint32_t name = parser->tokens.names[located];
                            const uint32_t meant = report->subject != 0 ? report->subject - 1 : BUSTER_CHECK_NONE;
                            const uint32_t index_of = report->detail & (CHECK_TYPO_STATEMENT - 1);
                            const uint32_t keyword = index_of != 0 && index_of <= BUSTER_ARRAY_LENGTH(check_typo_keywords) ? index_of - 1 : 0;
                            // A statement's typedef is clang's parser's "unknown type name" (check_driver_typo_edits).
                            const bool unknown_type = meant < count && (report->detail & CHECK_TYPO_STATEMENT) != 0;
                            const char* other = meant < count ? (const char*)intern_bytes(pp->table, parser->tokens.names[meant]) : check_typo_keywords[keyword];
                            const int other_length = meant < count ? (int)intern_length(pp->table, parser->tokens.names[meant]) : (int)strlen(other);
                            char declared[512];
                            snprintf(declared, sizeof(declared), "'%.*s' declared here", other_length, other);
                            const PPOutputDiagnosticNote note = { .token = meant < count ? parser->tokens.sources[meant] : 0, .message = declared };
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, meant < count ? 1u : 0u,
                                                        "%s '%.*s'; did you mean '%.*s'?", unknown_type ? "unknown type name" : "use of undeclared identifier",
                                                        (int)intern_length(pp->table, name), intern_bytes(pp->table, name), other_length, other);
                        }
                        else if ((kind == CHECK_INPUT_UNDECLARED || kind == CHECK_INPUT_UNKNOWN_BUILTIN) && named)
                        {
                            const uint32_t name = parser->tokens.names[token];
                            // clang's unknown builtin is an error of -Wimplicit-function-declaration's group, which it names.
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "use of %s '%.*s'%s", kind == CHECK_INPUT_UNDECLARED ? "undeclared identifier" :
                                                 "unknown builtin", (int)intern_length(pp->table, name), intern_bytes(pp->table, name),
                                                 kind == CHECK_INPUT_UNDECLARED ? "" : " [-Wimplicit-function-declaration]");
                        }
                        else if ((kind == CHECK_INPUT_CLEANUP_FUNCTION || kind == CHECK_INPUT_CLEANUP_PARAMETERS || kind == CHECK_INPUT_CLEANUP_TYPE) && named)
                        {
                            // clang's handleCleanupAttr names the argument; of the parameter's type it says what its other
                            // spelling of the message does, without the two types.
                            const uint32_t name = parser->tokens.names[token];
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'cleanup' %s '%.*s' %s",
                                                 kind == CHECK_INPUT_CLEANUP_FUNCTION ? "argument" : "function", (int)intern_length(pp->table, name),
                                                 intern_bytes(pp->table, name), kind == CHECK_INPUT_CLEANUP_FUNCTION ? "is not a function" :
                                                 kind == CHECK_INPUT_CLEANUP_PARAMETERS ? "must take 1 parameter" : "parameter has incompatible type");
                        }
                        else if (phase == 0 && input->reports[index].subject != 0 && input->reports[index].subject - 1 < count &&
                                 (kind == CHECK_INPUT_ALIAS_DEFINITION || kind == CHECK_INPUT_IFUNC_DEFINITION || kind == CHECK_INPUT_WEAKREF_CONTEXT ||
                                  kind == CHECK_INPUT_WEAKREF_ALIAS))
                        {
                            // GNU's alias, ifunc and weakref attributes' errors at the attribute, naming the declaration
                            // (#147), as clang's do.
                            const uint32_t name = parser->tokens.names[input->reports[index].subject - 1];
                            const int length = (int)intern_length(pp->table, name);
                            const char* spelling = (const char*)intern_bytes(pp->table, name);
                            if (kind == CHECK_INPUT_ALIAS_DEFINITION || kind == CHECK_INPUT_IFUNC_DEFINITION)
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "definition '%.*s' cannot also be an %s", length, spelling,
                                                     kind == CHECK_INPUT_IFUNC_DEFINITION ? "ifunc" : "alias");
                            }
                            else
                            {
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "weakref declaration of '%.*s' must %s", length, spelling,
                                                     kind == CHECK_INPUT_WEAKREF_CONTEXT ? "be in a global context" : "also have an alias attribute");
                            }
                        }
                        else if (phase == 0 && (kind == CHECK_INPUT_MULTIVERSION_ATTRIBUTE || kind == CHECK_INPUT_MULTIVERSION_INCOMPATIBLE ||
                                                kind == CHECK_INPUT_MULTIVERSION_DUPLICATE) &&
                                 input->reports[index].attribute != BUSTER_CHECK_NONE)
                        {
                            // GNU's function multiversioning (#839), as clang's messages spell it: the attribute it refuses
                            // at the function's name, the two incompatible attributes at the later one, or a repeated
                            // attribute, with clang's note at the earlier one or the declaration that caused
                            // multiversioning.
                            const CheckInputReport* report = input->reports + index;
                            const String8 later = check_driver_attribute_spelling(parser, pp, report->attribute, false);
                            if (kind == CHECK_INPUT_MULTIVERSION_DUPLICATE)
                            {
                                const String8 attribute = check_driver_attribute_spelling(parser, pp, report->attribute, true);
                                const PPOutputDiagnosticNote note = {
                                    .token = report->note_token < count ? parser->tokens.sources[report->note_token] : source,
                                    .message = "conflicting attribute is here",
                                };
                                pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, 1,
                                    "attribute '%.*s' cannot appear more than once on a declaration", (int)attribute.length, attribute.pointer);
                            }
                            else if (kind == CHECK_INPUT_MULTIVERSION_INCOMPATIBLE)
                            {
                                const String8 earlier = check_driver_attribute_spelling(parser, pp, report->note_token, true);
                                const PPOutputDiagnosticNote note = {
                                    .token = report->note_token < count ? parser->tokens.sources[report->note_token] : source,
                                    .message = "conflicting attribute is here",
                                };
                                pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, 1,
                                    "'%.*s' and '%.*s' attributes are not compatible", (int)later.length, later.pointer,
                                    (int)earlier.length, earlier.pointer);
                            }
                            else
                            {
                                const String8 attribute = check_driver_attribute_spelling(parser, pp, report->attribute, true);
                                const char* multiversion = check_input_multiversion_name((CheckInputMultiversionKind)report->multiversion);
                                if (report->note_kind == CHECK_INPUT_NOTE_MULTIVERSIONING_CAUSED)
                                {
                                    const PPOutputDiagnosticNote note = {
                                        .token = report->note_token < count ? parser->tokens.sources[report->note_token] : source,
                                        .message = "function multiversioning caused by this declaration",
                                    };
                                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, 1,
                                        "attribute '%s' multiversioning cannot be combined with attribute '%.*s'", multiversion,
                                        (int)attribute.length, attribute.pointer);
                                }
                                else
                                {
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication,
                                        "attribute '%s' multiversioning cannot be combined with attribute '%.*s'", multiversion,
                                        (int)attribute.length, attribute.pointer);
                                }
                            }
                        }
                        else if (phase == 0 && token < count &&
                                 (((kind == CHECK_INPUT_REDECLARATION || kind == CHECK_INPUT_CONFLICTING_TYPES) &&
                                   input->reports[index].detail > CHECK_REDECLARATION_GENERIC && input->reports[index].detail <= CHECK_REDECLARATION_AUTO_TYPE_EXTERN) ||
                                  (kind == CHECK_INPUT_ENUMERATOR_REDEFINITION && input->reports[index].note_kind == CHECK_INPUT_NOTE_PREVIOUS_DEFINITION)))
                        {
                            const CheckInputReport* conflict = input->reports + index;
                            const uint32_t previous = conflict->note_token;
                            const PPOutputDiagnosticNote note = {
                                .token = previous < count ? parser->tokens.sources[previous] : source,
                                .message = conflict->note_kind == CHECK_INPUT_NOTE_PREVIOUS_DEFINITION ?
                                           "previous definition is here" : "previous declaration is here",
                            };
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, previous < count ? 1u : 0u, "%s", message);
                        }
                        else if (phase == 0 && kind == CHECK_INPUT_TAG_INCOMPATIBLE && token < count)
                        {
                            // C23's incompatible redefinition of a record (#1447), with clang's two notes.
                            PPOutputDiagnosticNote notes[2] = { 0 };
                            const uint32_t noted = check_semantic_tag_notes(parser, pp, input, types, &spell, input->reports + index, notes, scratch);
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, notes, noted, "%s", message);
                        }
                        else if (phase == 0 && input->reports[index].message == CHECK_INPUT_CPU_DISPATCH)
                        {
                            // clang's cpu_specific and cpu_dispatch errors (#1447, c_check_cpu_dispatch.c), as its messages word
                            // them, with the note at the previous declaration or the one that caused multiversioning.
                            const CheckInputReport* report = input->reports + index;
                            const uint32_t cpu = report->attribute < count ? parser->tokens.names[report->attribute] : 0;
                            const bool noted = report->note_kind != CHECK_INPUT_NOTE_NONE && report->note_token < count;
                            const PPOutputDiagnosticNote note = {
                                .token = noted ? parser->tokens.sources[report->note_token] : 0,
                                .message = report->note_kind == CHECK_INPUT_NOTE_MULTIVERSIONING_CAUSED ? "function multiversioning caused by this declaration" :
                                                                                                         "previous declaration is here",
                            };
                            const char* different = report->detail == CHECK_CPU_DISPATCH_RETURN_TYPE ? "return type" :
                                                    report->detail == CHECK_CPU_DISPATCH_INLINE      ? "inline specification" :
                                                                                                       "linkage";
                            switch ((CheckCpuDispatchError)report->detail)
                            {
                                case CHECK_CPU_DISPATCH_INVALID_OPTION:
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "invalid option '%.*s' for %s", (int)intern_length(pp->table, cpu),
                                                          intern_bytes(pp->table, cpu), report->value != 0 ? "cpu_dispatch" : "cpu_specific");
                                    break;
                                case CHECK_CPU_DISPATCH_MAIN:
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "'main' cannot be a multiversioned function");
                                    break;
                                case CHECK_CPU_DISPATCH_PROTOTYPE:
                                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, noted ? 1u : 0u, "multiversioned function must have a prototype");
                                    break;
                                case CHECK_CPU_DISPATCH_REQUIRED:
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication,
                                                          "function declaration is missing 'cpu_specific' or 'cpu_dispatch' attribute in a multiversioned function");
                                    break;
                                case CHECK_CPU_DISPATCH_MIXED:
                                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, noted ? 1u : 0u, "multiversioning attributes cannot be combined");
                                    break;
                                case CHECK_CPU_DISPATCH_MISMATCH:
                                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, noted ? 1u : 0u, "'cpu_dispatch' function redeclared with different CPUs");
                                    break;
                                case CHECK_CPU_DISPATCH_SAME_CPU:
                                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, noted ? 1u : 0u,
                                                                "multiple 'cpu_specific' functions cannot specify the same CPU: '%.*s'", (int)intern_length(pp->table, cpu),
                                                                intern_bytes(pp->table, cpu));
                                    break;
                                case CHECK_CPU_DISPATCH_RETURN_TYPE:
                                case CHECK_CPU_DISPATCH_INLINE:
                                case CHECK_CPU_DISPATCH_LINKAGE:
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "multiversioned function declaration has a different %s", different);
                                    break;
                                case CHECK_CPU_DISPATCH_TARGET_FEATURE:
                                case CHECK_CPU_DISPATCH_TARGET_ARCHITECTURE:
                                {
                                    // The item of the target string (#1838), its bytes in the string's decoded text.
                                    const String8 text = report->attribute < count ? check_warning_strings_value(parser, pp, report->attribute, scratch) : (String8){ 0 };
                                    const uint64_t start = report->value & 0xffffffffu;
                                    const uint64_t length = report->value >> 32;
                                    const bool inside = start <= text.length && length <= text.length - start;
                                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, noted ? 1u : 0u,
                                                                "function multiversioning doesn't support %s '%.*s'",
                                                                report->detail == CHECK_CPU_DISPATCH_TARGET_FEATURE ? "feature" : "architecture", inside ? (int)length : 0,
                                                                inside ? text.pointer + start : "");
                                    break;
                                }
                                case CHECK_CPU_DISPATCH_TARGET_DUPLICATE:
                                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, noted ? 1u : 0u,
                                                                "multiversioned function redeclarations require identical target attributes");
                                    break;
                                case CHECK_CPU_DISPATCH_AFTER_USE:
                                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, noted ? 1u : 0u,
                                                                "function declaration cannot become a multiversioned function after first usage");
                                    break;
                                case CHECK_CPU_DISPATCH_TARGET_REQUIRED:
                                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, noted ? 1u : 0u,
                                                                "function declaration is missing 'target' attribute in a multiversioned function");
                                    break;
                                case CHECK_CPU_DISPATCH_CLONES_MISMATCH:
                                    pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, noted ? 1u : 0u,
                                                                "'target_clones' attribute does not match previous declaration");
                                    break;
                                case CHECK_CPU_DISPATCH_NO_MATCH:
                                {
                                    const uint32_t function = report->attribute < count ? parser->tokens.names[report->attribute] : 0;
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "no matching function for call to '%.*s'",
                                                          (int)intern_length(pp->table, function), intern_bytes(pp->table, function));
                                    break;
                                }
                            }
                        }
                        else if (phase == 0 && input->reports[index].message == CHECK_INPUT_ALIAS_TENTATIVE && token < count)
                        {
                            // A variable's alias after a tentative definition (#147), named, with its note at the last one.
                            const uint32_t name = parser->tokens.names[token];
                            const uint32_t previous = input->reports[index].note_token;
                            const PPOutputDiagnosticNote note = {
                                .token = previous < count ? parser->tokens.sources[previous] : 0,
                                .message = "previous definition is here",
                            };
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, previous < count ? 1u : 0u,
                                                        "alias definition of '%.*s' after tentative definition", (int)intern_length(pp->table, name),
                                                        intern_bytes(pp->table, name));
                        }
                        else if (phase == 0 && input->reports[index].message == CHECK_INPUT_LABEL_AMBIGUOUS && located < count &&
                                 parser->tokens.kinds[located] == PARSE_KIND_IDENTIFIER)
                        {
                            // An ambiguous label's reference (#1844), with clang's note at each candidate its lookup found.
                            const uint32_t name = parser->tokens.names[located];
                            uint32_t candidates[16];
                            const uint32_t candidate_count = check_driver_label_candidates(parser, input, index, candidates, BUSTER_ARRAY_LENGTH(candidates));
                            char candidate[512];
                            snprintf(candidate, sizeof(candidate), "candidate found by name lookup is '%.*s'", (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                            PPOutputDiagnosticNote notes[16];
                            for (uint32_t c = 0; c < candidate_count; c += 1)
                            {
                                notes[c] = (PPOutputDiagnosticNote){ .token = parser->tokens.sources[candidates[c]], .message = candidate };
                            }
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, notes, candidate_count, "reference to '%.*s' is ambiguous",
                                                        (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                        }
                        else if (phase == 0 && input->reports[index].message >= CHECK_INPUT_LABEL_UNDECLARED &&
                                 input->reports[index].message <= CHECK_INPUT_LABEL_AMBIGUOUS && located < count && parser->tokens.kinds[located] == PARSE_KIND_IDENTIFIER)
                        {
                            // A label's errors name it, as clang's do (at the report's own token, which an undeclared label's
                            // order key, its scope's closing brace, isn't); a redefinition's with its note at the first.
                            const uint32_t name = parser->tokens.names[located];
                            const CheckInputMessage label = input->reports[index].message;
                            const uint32_t previous = input->reports[index].note_kind == CHECK_INPUT_NOTE_PREVIOUS_DEFINITION ? input->reports[index].note_token : count;
                            const PPOutputDiagnosticNote note = {
                                .token = previous < count ? parser->tokens.sources[previous] : 0,
                                .message = "previous definition is here",
                            };
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, previous < count ? 1u : 0u,
                                                        label == CHECK_INPUT_LABEL_UNDECLARED ? "use of undeclared label '%.*s'" :
                                                        label == CHECK_INPUT_LABEL_REDEFINITION ? "redefinition of label '%.*s'" : "reference to '%.*s' is ambiguous",
                                                        (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                        }
                        else if (phase == 0 && input->reports[index].message == CHECK_INPUT_UNEVALUATED_ESCAPE && token < count)
                        {
                            // An unevaluated string literal's escape sequences (#147): each one's diagnostics, at its backslash.
                            check_driver_unevaluated_escapes(parser, pp, input, input->reports + index, publication, scratch);
                        }
                        else if (phase == 0 && (input->reports[index].message == CHECK_INPUT_ATTRIBUTE_ARGUMENT || input->reports[index].message == CHECK_INPUT_CONSTANT_EXPRESSION) &&
                                 input->reports[index].attribute < count)
                        {
                            // (And a declarator's constant's in the same words, #1447.)
                            // An attribute argument's error (#147) as clang's handler words it; a parser's "expected ')'" with
                            // its note at the arguments' `(`.
                            char text[768];
                            const CheckInputReport* report = input->reports + index;
                            const uint64_t type_mark = scratch->length;
                            // (The type an underaligned _Alignas names, as clang prints it: check_driver_warning_type.)
                            const bool typed = ((report->detail & 0xffu) == CHECK_ARGUMENT_ALIGNAS_UNDERALIGNED || (report->detail & 0xffu) == CHECK_ARGUMENT_PRECISE_LIFETIME ||
                                                (report->detail & 0xffu) == CHECK_ARGUMENT_NOT_BOOL || (report->detail & 0xffu) == CHECK_ARGUMENT_INCOMPLETE_TYPE ||
                                                (report->detail & 0xffu) == CHECK_ARGUMENT_EXPRESSION_MEMBER_BASE ||
                                                (report->detail & 0xffu) == CHECK_ARGUMENT_EXPRESSION_MEMBER_NAME ||
                                                (report->detail & 0xffu) == CHECK_ARGUMENT_EXPRESSION_SIZEOF ||
                                                (report->detail & 0xffu) == CHECK_ARGUMENT_EXPRESSION_BINARY ||
                                                (report->detail & 0xffu) == CHECK_ARGUMENT_EXPRESSION_ARROW_POINTER ||
                                                (report->detail & 0xffu) == CHECK_ARGUMENT_EXPRESSION_INCOMPLETE_ARITHMETIC ||
                                                (report->detail & 0xffu) == CHECK_ARGUMENT_CALLBACK_COUNT ||
                                                (report->detail & 0xffu) == CHECK_ARGUMENT_SWIFT_ASYNC_BLOCK ||
                                                (report->detail & 0xffu) == CHECK_ARGUMENT_SWIFT_PARAMETER_TYPE ||
                                                (report->detail & 0xffu) == CHECK_ARGUMENT_BUILTIN_TYPES) &&
                                               report->type != 0;
                            CheckWarningSpelling spell = { .count = pp->table->count };
                            spell.bytes = typed ? BUSTER_ARENA_PUSH_ARRAY(scratch, const uint8_t*, spell.count) : NULL;
                            spell.lengths = spell.bytes != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, spell.count) : NULL;
                            for (uint32_t s = 0; spell.lengths != NULL && s < spell.count; s += 1)
                            {
                                spell.bytes[s] = intern_bytes(pp->table, s);
                                spell.lengths[s] = intern_length(pp->table, s);
                            }
                            const bool spelled = spell.lengths != NULL && check_driver_spelling_build(parser, pp, input, types, &spell, scratch);
                            // (lifetime_capture_by's argument as written: its token's spelling.)
                            const uint32_t argument_error = report->detail & 0xffu;
                            const bool captured = (argument_error == CHECK_ARGUMENT_CAPTURE_UNKNOWN || argument_error == CHECK_ARGUMENT_ATOMIC_OPTION ||
                                                   argument_error == CHECK_ARGUMENT_CALLBACK_UNKNOWN ||
                                                   (argument_error == CHECK_ARGUMENT_ERROR_DOMAIN && (report->detail >> 8) != 0) ||
                                                   argument_error == CHECK_ARGUMENT_AVAILABILITY_REDUNDANT || argument_error == CHECK_ARGUMENT_AVAILABILITY_EQUAL ||
                                                   argument_error == CHECK_ARGUMENT_AVAILABILITY_STAGE || argument_error == CHECK_ARGUMENT_AVAILABILITY_ANY_APPLE ||
                                                   argument_error == CHECK_ARGUMENT_SOURCE_DUPLICATE || argument_error == CHECK_ARGUMENT_SOURCE_EQUAL ||
                                                   argument_error == CHECK_ARGUMENT_TYPE_TAG_FLAG) &&
                                                  report->value < count;
                            // (A TCB's string, after the other attribute's name in the value, and a duplicate aspect's.)
                            const uint32_t string_token = (report->detail & 0xffu) == CHECK_ARGUMENT_TCB_CONFLICT ? (uint32_t)(report->value >> 32) :
                                                          (report->detail & 0xffu) == CHECK_ARGUMENT_DUPLICATE_ASPECT ? (uint32_t)report->value : count;
                            const uint32_t at = captured ? parser->tokens.sources[report->value] : 0;
                            uint8_t* spelling_buffer = captured ? arena_push(scratch, pp->output.kinds[at] == BUSTER_PP_KIND_INTEGER ? 24 :
                                                                                 (uint64_t)pp->output.values[at] - pp->output.locations[at] + 1, 1) : NULL;
                            const uint8_t* spelling_bytes = NULL;
                            const uint32_t spelling_length = spelling_buffer != NULL ? pp_spelling(pp, pp->output.kinds[at], pp->output.values[at], pp->output.locations[at],
                                                                                                   spelling_buffer, &spelling_bytes) : 0;
                            String8 literal = ((report->detail & 0xffu) == CHECK_ARGUMENT_CODE_MODEL || (report->detail & 0xffu) == CHECK_ARGUMENT_WARNING_GROUP) && token < count ?
                                                    check_warning_string_text(parser, pp, token, scratch) :
                                                    string_token < count ? check_warning_strings_value(parser, pp, string_token, scratch) :
                                                    spelled ? check_driver_warning_type(parser, pp, input, types, &spell, scratch, report->type, report->type_spelling) :
                                                    captured && parser->tokens.names[report->value] != 0 ? check_driver_attribute_spelling(parser, pp, report->value, false) :
                                                    captured ? (String8){ (char*)spelling_bytes, spelling_length } : (String8){ "", 0 };
                            if (spelled && (report->detail & 0xffu) == CHECK_ARGUMENT_EXPRESSION_BINARY)
                            {
                                const String8 second = check_driver_warning_type(parser, pp, input, types, &spell, scratch, (TypeId)report->value, 0);
                                const char* both = check_warning_text(scratch, "%.*s and %.*s", (int)literal.length, literal.pointer, (int)second.length, second.pointer);
                                literal = both != NULL ? (String8){ (char*)both, strlen(both) } : (String8){ "", 0 };
                            }
                            // (An anyAppleOS version as clang's VersionTuple prints it.)
                            char version_text[48];
                            if (captured && argument_error == CHECK_ARGUMENT_AVAILABILITY_ANY_APPLE)
                            {
                                check_version_print(check_version_tuple(check_availability_version(spelling_bytes, spelling_length)), version_text, sizeof(version_text));
                                literal = (String8){ version_text, strlen(version_text) };
                            }
                            const CBuiltinLibraryNamed library_named = (report->detail & 0xffu) == CHECK_ARGUMENT_BUILTIN_TYPES ? check_driver_library_named(pp, types, input, scratch) :
                                                                                                                     (CBuiltinLibraryNamed){ 0 };
                            check_driver_attribute_argument(parser, pp, report->attribute, report->detail, report->value, literal, &library_named, text, sizeof(text));
                            // (An incomplete tag's declaration, named by its type alone: preferred_type's, #147.)
                            const bool forward = report->note_kind == CHECK_INPUT_NOTE_FORWARD_DECLARATION && spelled;
                            const String8 tag = forward ? check_driver_warning_type(parser, pp, input, types, &spell, scratch, type_unqualified(report->type), 0) :
                                                          (String8){ "", 0 };
                            char declared[192];
                            snprintf(declared, sizeof(declared), "forward declaration of %.*s", (int)tag.length, tag.pointer);
                            const bool function = report->note_kind == CHECK_INPUT_NOTE_FUNCTION_DECLARATION;
                            if (function && report->note_token < count)
                            {
                                const String8 name = check_driver_attribute_spelling(parser, pp, report->note_token, false);
                                snprintf(declared, sizeof(declared), "'%.*s' declared here", (int)name.length, name.pointer);
                            }
                            const bool constant = report->note_kind == CHECK_INPUT_NOTE_CONSTANT_SUBEXPRESSION || report->note_kind == CHECK_INPUT_NOTE_CONSTANT_DIVISION ||
                                                  report->note_kind == CHECK_INPUT_NOTE_CONSTANT_VOID || report->note_kind == CHECK_INPUT_NOTE_CONSTANT_CONVERSION;
                            const bool matched = (report->note_kind == CHECK_INPUT_NOTE_MATCH_PAREN || report->note_kind == CHECK_INPUT_NOTE_PREVIOUS_ATTRIBUTE ||
                                                  report->note_kind == CHECK_INPUT_NOTE_CONFLICTING_ATTRIBUTE || forward || function || constant) &&
                                                 report->note_token < count;
                            const PPOutputDiagnosticNote note = {
                                .token = matched ? parser->tokens.sources[report->note_token] : 0,
                                .message = forward || function ? declared : report->note_kind == CHECK_INPUT_NOTE_PREVIOUS_ATTRIBUTE ? "previous attribute is here" :
                                           report->note_kind == CHECK_INPUT_NOTE_CONFLICTING_ATTRIBUTE ? "conflicting attribute is here" :
                                           report->note_kind == CHECK_INPUT_NOTE_CONSTANT_DIVISION ? "division by zero" :
                                           report->note_kind == CHECK_INPUT_NOTE_CONSTANT_VOID ? "non-literal type 'void' cannot be used in a constant expression" :
                                           report->note_kind == CHECK_INPUT_NOTE_CONSTANT_CONVERSION ? "this conversion is not allowed in a constant expression" :
                                           constant ? "subexpression not valid in a constant expression" : "to match this '('",
                            };
                            // (A `,` or `=` clang's ExpectAndConsume expects just past the token before: past the identifier's end.)
                            const bool past = (argument_error == CHECK_ARGUMENT_AVAILABILITY_COMMA || argument_error == CHECK_ARGUMENT_SOURCE_EQUAL) && token < count &&
                                              parser->tokens.names[token] != 0;
                            if (past)
                            {
                                pp_diagnose_output_offset_at(pp, PP_SEVERITY_ERROR, source, intern_length(pp->table, parser->tokens.names[token]), publication, "%s", text);
                            }
                            else
                            {
                                pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, matched ? 1u : 0u, "%s", text);
                            }
                            // pcs's argument that is no string (#147): clang's checkStringLiteralArgumentAttr goes on with the
                            // identifier's text, and CheckCallingConvAttr then words it at the attribute's first token: aapcs a
                            // convention x86-64 warns of and ignores (on a tag, `value` 1, then one of no function), any other
                            // "invalid PCS type".
                            const uint32_t named = report->attribute;
                            const bool scoped = named + 2 < count && parser->tokens.kinds[named + 1] == PARSE_KIND_COLON;
                            const uint32_t name = !scoped ? named : parser->tokens.kinds[named + 2] == PARSE_KIND_COLON && named + 3 < count ? named + 3 : named + 2;
                            if ((report->detail & 0xffu) == CHECK_ARGUMENT_STRING && token < count &&
                                check_input_attribute_equal(check_warning_attribute_normalized(check_driver_attribute_spelling(parser, pp, name, false)), "pcs"))
                            {
                                const uint32_t at = parser->tokens.sources[named];
                                if (check_input_attribute_equal(check_driver_attribute_spelling(parser, pp, token, false), "aapcs"))
                                {
                                    const CheckWarningSeverity severity = check_common_severity(pp, parser, input, named, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, false);
                                    // "[-Werror,-Wflag]" while the promoted warning is an ordinary error, "[-Wflag]" once it is fatal.
                                    const bool error = severity == CHECK_WARNING_ERROR;
                                    const PPSeverity disposition = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL : error ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
                                    for (uint32_t warning_index = 0; warning_index < (report->value == 1 ? 2u : 1u); warning_index += 1)
                                    {
                                        check_driver_attribute_argument(parser, pp, named, warning_index == 0 ? CHECK_ARGUMENT_CONVENTION_UNSUPPORTED : CHECK_ARGUMENT_FUNCTIONS_METHODS, 0,
                                                                        (String8){ "", 0 }, NULL, text, sizeof(text));
                                        if (severity != CHECK_WARNING_IGNORE)
                                        {
                                            pp_diagnose_output_at(pp, disposition, at, publication, "%s [%s-Wignored-attributes]", text, error ? "-Werror," : "");
                                        }
                                    }
                                }
                                else
                                {
                                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, at, publication, "invalid PCS type");
                                }
                            }
                            arena_pop_to(scratch, type_mark);
                        }
                        else if (phase == 0 && input->reports[index].message == CHECK_INPUT_ATTRIBUTE_STRING_ARGUMENT && input->reports[index].attribute < count)
                        {
                            // capability's name that is no string literal (#147), at the argument: the attribute by its name
                            // without a keyword's underscores, as clang's message names it (its scope's token first where it has one).
                            const uint32_t first = input->reports[index].attribute;
                            const bool scoped = first + 2 < count && parser->tokens.kinds[first + 1] == PARSE_KIND_COLON;
                            const uint32_t at = !scoped ? first : parser->tokens.kinds[first + 2] == PARSE_KIND_COLON && first + 3 < count ? first + 3 : first + 2;
                            const String8 written = check_driver_attribute_spelling(parser, pp, at, false);
                            const String8 bare = check_warning_attribute_normalized(written);
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "expected string literal as argument of '%.*s' attribute", (int)bare.length, bare.pointer);
                        }
                        else if (phase == 0 && input->reports[index].message == CHECK_INPUT_ASM_LABEL_CONFLICT)
                        {
                            // A redeclaration's other asm label (#2262), its note at the earlier label's string.
                            const CheckInputReport* report = input->reports + index;
                            const uint32_t previous = report->note_kind != CHECK_INPUT_NOTE_NONE ? report->note_token : count;
                            const PPOutputDiagnosticNote note = {
                                .token = previous < count ? parser->tokens.sources[previous] : 0,
                                .message = "previous declaration is here",
                            };
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, previous < count ? 1u : 0u, "conflicting asm label");
                        }
                        else if (phase == 0 && (input->reports[index].message == CHECK_INPUT_LOADER_EXTERN || input->reports[index].message == CHECK_INPUT_LOADER_REDECLARATION ||
                                                input->reports[index].message == CHECK_INPUT_INTERNAL_LINKAGE_FIRST) && token < count)
                        {
                            // loader_uninitialized's and internal_linkage's errors (#147), as clang's messages word them: an extern
                            // variable named, and the others with their notes at the earlier declaration; internal_linkage named as
                            // clang prints an attribute it applied, its scope as written and its name without a keyword's underscores
                            // (the report's attribute its first token, its detail its name's plus one where it has a scope).
                            const CheckInputReport* report = input->reports + index;
                            const CheckInputMessage message = report->message;
                            const uint32_t previous = report->note_kind != CHECK_INPUT_NOTE_NONE ? report->note_token : count;
                            const PPOutputDiagnosticNote note = {
                                .token = previous < count ? parser->tokens.sources[previous] : 0,
                                .message = message == CHECK_INPUT_LOADER_REDECLARATION ? "previous definition is here" : "previous declaration is here",
                            };
                            if (message == CHECK_INPUT_LOADER_EXTERN)
                            {
                                const uint32_t name = parser->tokens.names[token];
                                pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication,
                                                      "variable '%.*s' cannot be declared both 'extern' and with the 'loader_uninitialized' attribute",
                                                      (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                            }
                            else if (message == CHECK_INPUT_LOADER_REDECLARATION)
                            {
                                pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, previous < count ? 1u : 0u,
                                                            "redeclaration cannot add 'loader_uninitialized' attribute");
                            }
                            else
                            {
                                const bool scoped = report->detail != 0 && report->detail - 1 < count && report->attribute < count;
                                const String8 scope = scoped ? check_driver_attribute_spelling(parser, pp, report->attribute, false) : (String8){ "", 0 };
                                const uint32_t name_token = scoped ? report->detail - 1 : report->attribute;
                                const String8 attribute = check_warning_attribute_normalized(check_driver_attribute_spelling(parser, pp, name_token, false));
                                pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, previous < count ? 1u : 0u,
                                                            "'%.*s%s%.*s' attribute does not appear on the first declaration", (int)scope.length, scope.pointer,
                                                            scoped ? "::" : "", (int)attribute.length, attribute.pointer);
                            }
                        }
                        else if (phase == 0 && input->reports[index].message == CHECK_INPUT_DIRECT_INITIALIZER_DEDUCED && input->reports[index].subject != 0 &&
                                 input->reports[index].subject - 1 < count)
                        {
                            const uint32_t declaration = input->reports[index].subject - 1;
                            const uint32_t name = parser->tokens.names[declaration];
                            const bool c23 = input->reports[index].detail != 0;
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication,
                                                 "initializer for variable '%.*s' with type '%s' contains multiple expressions",
                                                 (int)intern_length(pp->table, name), intern_bytes(pp->table, name), c23 ? "auto" : "__auto_type");
                        }
                        else if (phase == 0 && (input->reports[index].message == CHECK_INPUT_AUTO_TYPE_INITIALIZER || input->reports[index].message == CHECK_INPUT_AUTO_TYPE_SELF) &&
                                 token < count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER)
                        {
                            // An __auto_type variable's errors at its name, or at a use of it, name it as clang's do.
                            const uint32_t name = parser->tokens.names[token];
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication,
                                                 input->reports[index].message == CHECK_INPUT_AUTO_TYPE_INITIALIZER ?
                                                 "declaration of variable '%.*s' with deduced type '__auto_type' requires an initializer" :
                                                 "variable '%.*s' declared with deduced type '__auto_type' cannot appear in its own initializer",
                                                 (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                        }
                        else if (phase == 0 && (input->reports[index].message == CHECK_INPUT_AUTO_DEDUCED_INITIALIZER || input->reports[index].message == CHECK_INPUT_AUTO_DEDUCED_SELF) &&
                                 token < count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER)
                        {
                            // C23's auto variable's errors name it too (#793).
                            const uint32_t name = parser->tokens.names[token];
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication,
                                                 input->reports[index].message == CHECK_INPUT_AUTO_DEDUCED_INITIALIZER ?
                                                 "declaration of variable '%.*s' with deduced type 'auto' requires an initializer" :
                                                 "variable '%.*s' declared with deduced type 'auto' cannot appear in its own initializer",
                                                 (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                        }
                        else if (phase == 0 && input->reports[index].message == CHECK_INPUT_CONSTEXPR_INITIALIZER && input->reports[index].subject != 0 &&
                                 input->reports[index].subject - 1 < count)
                        {
                            // A constexpr variable's initializer error names it, as clang's does (#793).
                            const uint32_t name = parser->tokens.names[input->reports[index].subject - 1];
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication,
                                                 "constexpr variable '%.*s' must be initialized by a constant expression",
                                                 (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                        }

                        else if (phase == 0 && input->reports[index].message == CHECK_INPUT_KR_IMPLICIT_INT && token < count)
                        {
                            const uint32_t name = parser->tokens.names[token];
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication,
                                "parameter '%.*s' was not declared, defaults to 'int'; ISO C99 and later do not support implicit int [-Wimplicit-int]",
                                (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                        }
                        else if (phase == 0 && input->reports[index].message == CHECK_INPUT_PRAGMA_SEMANTICS && input->reports[index].pragma != 0 &&
                                 check_input_fp_report(input, input->reports + index) == NULL)
                        {
                            const uint32_t start = parser->tokens.pragma_sources[input->reports[index].pragma - 1];
                            const CheckInputReader names = { .pp = pp, .parser = parser };
                            const String8 first = check_driver_warning_name(pp, check_input_pragma_name(&names, start + 2));
                            const bool vendor = pp_token_is_name(pp, &pp->output, start + 2, pp->output.count, "GCC") ||
                                                pp_token_is_name(pp, &pp->output, start + 2, pp->output.count, "clang") ||
                                                pp_token_is_name(pp, &pp->output, start + 2, pp->output.count, "STDC");
                            const String8 second = vendor && start + 3 < pp->output.count ?
                                check_driver_warning_name(pp, check_input_pragma_name(&names, start + 3)) : (String8){ .pointer = "" };
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, start + 2, publication,
                                "unsupported semantic '#pragma %.*s%s%.*s' directive",
                                (int)first.length, first.pointer, vendor ? " " : "", (int)second.length, second.pointer);
                        }
                        else if (x86_message != NULL && x86_location < count && conversion_note != NULL && conversion_note_token < count)
                        {
                            const PPOutputDiagnosticNote note = { .token = parser->tokens.sources[conversion_note_token], .message = conversion_note };
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, parser->tokens.sources[x86_location], publication, &note, 1, "%s", x86_message);
                        }
                        else if (x86_message != NULL && x86_location < count)
                        {
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, parser->tokens.sources[x86_location], publication, x86_note,
                                                        check_semantic_note_count(x86_note), "%s", x86_message);
                        }
                        else if (phase == 0 && input->reports[index].note_kind == CHECK_INPUT_NOTE_ATTRIBUTE_HERE &&
                                 input->reports[index].note_token < count)
                        {
                            // A naked function's error has clang's note at its attribute.
                            const PPOutputDiagnosticNote note = {
                                .token = parser->tokens.sources[input->reports[index].note_token],
                                .message = "attribute is here",
                            };
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, 1, "%s", message);
                        }
                        else if (phase == 2 && extra[index].note != NULL && extra[index].note_token < count)
                        {
                            const PPOutputDiagnosticNote note = { .token = parser->tokens.sources[extra[index].note_token], .message = extra[index].note };
                            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, publication, &note, 1, "%s", message);
                        }
                        else
                        {
                            pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, publication, "%s", message);
                        }
                        pp_diagnostic_suborder(pp, previous, phase == 0 ? input->reports[index].suborder : 0);
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
    const CheckInput* directives = original_pragmas != NULL ? original_pragmas : input;
    // (As the pragmas' records, their warnings are the first reading's alone.)
    const bool pragma_ready = ready && (from != 0 || check_driver_warning_events_at(original_pragmas != NULL ? original : parser, pp, directives, NULL,
        &directives->pragma_warnings, NULL, publication_end, scratch));
    arena_pop_to(scratch, mark);
    check_common_contexts = (CheckCommonContexts){ 0 };
    return pragma_ready && shifts_ready;
}

static bool check_driver_reports(const Parser* parser, const Parser* original, Preprocessor* pp, uint32_t publication_end,
                                  const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                  const CheckRepair* repair, const CheckInput* original_pragmas, const CheckDriverDiagnostic* extra, uint32_t extra_count,
                                  const uint32_t* excess_tokens, uint32_t excess_count, bool quiet,
                                  CheckCommonShiftWarnings* shifts, Arena* scratch)
{
    return check_driver_reports_from(parser, original, pp, publication_end, input, stream, types, repair, original_pragmas, extra, extra_count, excess_tokens,
                                     excess_count, quiet, shifts, 0, scratch);
}

// The fate of each original token in a rejected unit's private view (#1447, check_driver_rejected_view). A left out run
// starts with its replacement, if it has one, and holds the tokens after it that are DROP: a statement's (BLOCK) or a
// condition's (VALUE); a run without a replacement (an initializer, a for clause, a declarator) is DROP alone and
// always follows a kept token.
enum
{
    CHECK_DRIVER_EDIT_KEEP = 0,
    CHECK_DRIVER_EDIT_DROP = 1,
    // A statement's first token: `{` `}` take its place.
    CHECK_DRIVER_EDIT_BLOCK = 2,
    // A condition's first token: `-*__func__` takes its place, an int that reads no object of the unit's and is no
    // constant, so the statements after it stay as reachable as they were (a constant would change which of their
    // warnings the flushed body publishes in its walk, check_common_flush).
    CHECK_DRIVER_EDIT_VALUE = 3,
    // A file-scope declaration's or definition's first token (check_driver_rejected_declaration): an empty declaration `;`
    // takes its place, so that the token after the body before it is still at the declaration's place, where clang
    // publishes the body's deferred warnings (check_driver_publication_position), before the declaration's errors.
    CHECK_DRIVER_EDIT_EMPTY = 4,
};

// A kept token's casts in a rejected unit's private view (#1969, check_driver_rejected_view's `casts`): a conversion clang
// warns of with an error by default and makes (check_driver_report_default_error) has its source cast, so that the reading
// reads the same expression and its uses, with no error and no warning of the cast's own: to a pointer target through
// `long` and `void *` (`(void *)(long)(`), which converts to any pointer type quietly, or to an integer target through
// `long` (`(long)(`), each before the source's first token, and `)` after its last.
//
// A left-out expression statement's complete operands (#2120, check_driver_rejected_operands) are kept in its `{` `}`, each
// as its own statement `(void)(A);`, so that the warnings clang gives as it builds them before it finds the statement's
// error are given: OPERAND marks the operand's tokens, FIRST its first, which `(void)(` precedes, and LAST its last,
// which `);` follows. The tokens stay DROP in `edits`, so that their events count as the left-out part's.
enum
{
    CHECK_DRIVER_CAST_POINTER = 1,
    CHECK_DRIVER_CAST_INTEGER = 2,
    CHECK_DRIVER_CAST_CLOSE = 4,
    CHECK_DRIVER_CAST_OPERAND = 8,
    CHECK_DRIVER_CAST_OPERAND_FIRST = 16,
    CHECK_DRIVER_CAST_OPERAND_LAST = 32,
};

// A rejected unit's private view (#1447, check_driver_rejected_common): its tokens with what holds an error left out,
// reparsed, as a repair's view is (c_check_repair.c). `edits` gives each original token's fate (CHECK_DRIVER_EDIT_*).
// The view's tokens keep their sources, so its diagnostics are the original places; a brace's source is its statement's
// last token's, a condition's `-*__func__` its first token's, and a declaration's `;` its first token's. `to_view` maps
// each original token, and the count, to its view token, or a dropped one's to the next kept; `to_original` maps each
// view token to its original one, a replacement's to BUSTER_CHECK_NONE. True where the view parses without an error.
typedef struct CheckDriverRejectedView CheckDriverRejectedView;
struct CheckDriverRejectedView
{
    Parser parser;
    uint32_t* to_view;
    uint32_t* to_original;
};

static bool check_driver_rejected_view(CheckDriverRejectedView* result, const Parser* original, const uint8_t* edits, const uint8_t* casts, Arena* output,
                                       Arena* scratch)
{
    const uint64_t scratch_mark = scratch->length;
    const uint32_t count = original->tokens.count;
    // A kept token's casts' tokens (CHECK_DRIVER_CAST_*): 8 before it, 4, and 1 after it.
    const uint8_t cast_tokens[8] = { 0, 8, 4, 0, 1, 9, 5, 0 };
    uint64_t total64 = 0;
    for (uint32_t token = 0; token < count; token += 1)
    {
        const uint8_t cast = casts != NULL && edits[token] == CHECK_DRIVER_EDIT_KEEP ? casts[token] & 7u : 0u;
        total64 += edits[token] == CHECK_DRIVER_EDIT_KEEP || edits[token] == CHECK_DRIVER_EDIT_EMPTY ? 1u : edits[token] == CHECK_DRIVER_EDIT_DROP ? 0u :
                   edits[token] == CHECK_DRIVER_EDIT_BLOCK ? 2u : 3u;
        total64 += cast_tokens[cast];
        // A left-out statement's operand's token, and its `(void)(` and `);` (CHECK_DRIVER_CAST_OPERAND).
        const uint8_t operand = casts != NULL && (edits[token] == CHECK_DRIVER_EDIT_DROP || edits[token] == CHECK_DRIVER_EDIT_BLOCK) ? casts[token] : 0u;
        total64 += (operand & CHECK_DRIVER_CAST_OPERAND) != 0 ? 1u + ((operand & CHECK_DRIVER_CAST_OPERAND_FIRST) != 0 ? 4u : 0u) +
                                                              ((operand & CHECK_DRIVER_CAST_OPERAND_LAST) != 0 ? 2u : 0u) : 0u;
    }
    bool valid = total64 != 0 && total64 <= BUSTER_PARSE_MAX_TOKENS;
    const uint32_t total = valid ? (uint32_t)total64 : 0;
    Parser* view = &result->parser;
    *view = (Parser){ .options = original->options, .language = original->language, .preprocessor = original->preprocessor };
    memcpy(view->predeclared, original->predeclared, sizeof(view->predeclared));
    if (valid)
    {
        view->tokens.count = total;
        view->tokens.capacity = total;
        view->tokens.kinds = check_prefix_column(output, total, 1);
        view->tokens.names = check_prefix_column(output, total, sizeof(uint32_t));
        view->tokens.sources = check_prefix_column(output, total, sizeof(uint32_t));
        view->tokens.flags = check_prefix_column(output, total, 1);
        view->tree.roles = check_prefix_column(output, total, 1);
        view->tree.parents = check_prefix_column(output, total, sizeof(uint32_t));
        view->tree.depths = check_prefix_column(output, total, sizeof(uint32_t));
        view->postorder = check_prefix_column(output, total, sizeof(uint32_t));
        view->sizes = check_prefix_column(output, total, sizeof(uint32_t));
        view->diagnostic_capacity = total + 16;
        view->diagnostics = BUSTER_ARENA_PUSH_ARRAY(output, ParseDiagnostic, view->diagnostic_capacity);
        view->meanings.capacity = original->meanings.capacity;
        view->meanings.meanings = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, view->meanings.capacity);
        result->to_view = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1);
        result->to_original = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, total);
        uint32_t* positions = original->tokens.pragma_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, original->tokens.pragma_count) : NULL;
        valid = view->tokens.kinds != NULL && view->tokens.names != NULL && view->tokens.sources != NULL && view->tokens.flags != NULL &&
                view->tree.roles != NULL && view->tree.parents != NULL && view->tree.depths != NULL && view->postorder != NULL && view->sizes != NULL &&
                view->diagnostics != NULL && view->meanings.meanings != NULL && result->to_view != NULL && result->to_original != NULL &&
                (original->tokens.pragma_count == 0 || positions != NULL);
        uint32_t written = 0;
        // The left-out statement whose `}` is still to come, after its kept operands (CHECK_DRIVER_CAST_OPERAND): its last
        // token, whose source the braces take.
        uint32_t block_last = BUSTER_CHECK_NONE;
        for (uint32_t token = 0; valid && token < count; token += 1)
        {
            result->to_view[token] = written;
            const uint8_t operand = casts != NULL && (edits[token] == CHECK_DRIVER_EDIT_DROP || edits[token] == CHECK_DRIVER_EDIT_BLOCK) ? casts[token] : 0u;
            if (edits[token] == CHECK_DRIVER_EDIT_BLOCK || edits[token] == CHECK_DRIVER_EDIT_VALUE)
            {
                const bool block = edits[token] == CHECK_DRIVER_EDIT_BLOCK;
                // A statement's `;` is its last token, whose source the braces take.
                uint32_t last = token;
                while (block && last + 1 < count && edits[last + 1] == CHECK_DRIVER_EDIT_DROP)
                {
                    last += 1;
                }
                const uint8_t kinds[2][3] = {
                    { PARSE_KIND_L_BRACE, PARSE_KIND_R_BRACE },
                    { PARSE_KIND_MINUS, PARSE_KIND_STAR, PARSE_KIND_NUMBER },
                };
                const uint32_t names[2][3] = {
                    { 0, 0 },
                    { 0, 0, C_NAME_FUNC },
                };
                // A block's `}` comes after its operands, at its last token.
                for (uint32_t b = 0; b < (block ? 1u : 3u); b += 1)
                {
                    view->tokens.kinds[written] = kinds[!block][b];
                    view->tokens.names[written] = names[!block][b];
                    view->tokens.sources[written] = original->tokens.sources[last];
                    view->tokens.flags[written] = 0;
                    result->to_original[written] = BUSTER_CHECK_NONE;
                    written += 1;
                }
                block_last = block ? last : block_last;
            }
            else if (edits[token] == CHECK_DRIVER_EDIT_EMPTY)
            {
                view->tokens.kinds[written] = PARSE_KIND_SEMI;
                view->tokens.names[written] = 0;
                view->tokens.sources[written] = original->tokens.sources[token];
                view->tokens.flags[written] = 0;
                result->to_original[written] = BUSTER_CHECK_NONE;
                written += 1;
            }
            else if (edits[token] == CHECK_DRIVER_EDIT_KEEP)
            {
                // A conversion's source's casts (CHECK_DRIVER_CAST_*), at its first token's source, and its `)` at its last's.
                static const uint8_t prefix[8] = {
                    PARSE_KIND_L_PAREN, PARSE_KIND_TYPE, PARSE_KIND_STAR, PARSE_KIND_R_PAREN, PARSE_KIND_L_PAREN, PARSE_KIND_TYPE, PARSE_KIND_R_PAREN, PARSE_KIND_L_PAREN,
                };
                static const uint32_t prefix_names[8] = { 0, C_NAME_VOID, 0, 0, 0, C_NAME_LONG, 0, 0 };
                const uint8_t cast = casts != NULL ? casts[token] : 0;
                const uint32_t skip = (cast & CHECK_DRIVER_CAST_POINTER) != 0 ? 0u : (cast & CHECK_DRIVER_CAST_INTEGER) != 0 ? 4u : 8u;
                for (uint32_t b = skip; b < 8; b += 1)
                {
                    view->tokens.kinds[written] = prefix[b];
                    view->tokens.names[written] = prefix_names[b];
                    view->tokens.sources[written] = original->tokens.sources[token];
                    view->tokens.flags[written] = 0;
                    result->to_original[written] = BUSTER_CHECK_NONE;
                    written += 1;
                }
                result->to_view[token] = written;
                view->tokens.kinds[written] = original->tokens.kinds[token];
                view->tokens.names[written] = original->tokens.names[token];
                view->tokens.sources[written] = original->tokens.sources[token];
                view->tokens.flags[written] = original->tokens.flags[token];
                result->to_original[written] = token;
                written += 1;
                if ((cast & CHECK_DRIVER_CAST_CLOSE) != 0)
                {
                    view->tokens.kinds[written] = PARSE_KIND_R_PAREN;
                    view->tokens.names[written] = 0;
                    view->tokens.sources[written] = original->tokens.sources[token];
                    view->tokens.flags[written] = 0;
                    result->to_original[written] = BUSTER_CHECK_NONE;
                    written += 1;
                }
            }
            if ((operand & CHECK_DRIVER_CAST_OPERAND) != 0)
            {
                // A left-out statement's operand (#2120): `(void)(` before its first token, at its source, and `);` after
                // its last, at its.
                static const uint8_t open_kinds[4] = { PARSE_KIND_L_PAREN, PARSE_KIND_TYPE, PARSE_KIND_R_PAREN, PARSE_KIND_L_PAREN };
                static const uint32_t open_names[4] = { 0, C_NAME_VOID, 0, 0 };
                static const uint8_t close_kinds[2] = { PARSE_KIND_R_PAREN, PARSE_KIND_SEMI };
                for (uint32_t b = 0; (operand & CHECK_DRIVER_CAST_OPERAND_FIRST) != 0 && b < 4; b += 1)
                {
                    view->tokens.kinds[written] = open_kinds[b];
                    view->tokens.names[written] = open_names[b];
                    view->tokens.sources[written] = original->tokens.sources[token];
                    view->tokens.flags[written] = 0;
                    result->to_original[written] = BUSTER_CHECK_NONE;
                    written += 1;
                }
                result->to_view[token] = written;
                view->tokens.kinds[written] = original->tokens.kinds[token];
                view->tokens.names[written] = original->tokens.names[token];
                view->tokens.sources[written] = original->tokens.sources[token];
                view->tokens.flags[written] = original->tokens.flags[token];
                result->to_original[written] = token;
                written += 1;
                for (uint32_t b = 0; (operand & CHECK_DRIVER_CAST_OPERAND_LAST) != 0 && b < 2; b += 1)
                {
                    view->tokens.kinds[written] = close_kinds[b];
                    view->tokens.names[written] = 0;
                    view->tokens.sources[written] = original->tokens.sources[token];
                    view->tokens.flags[written] = 0;
                    result->to_original[written] = BUSTER_CHECK_NONE;
                    written += 1;
                }
            }
            if (token == block_last)
            {
                view->tokens.kinds[written] = PARSE_KIND_R_BRACE;
                view->tokens.names[written] = 0;
                view->tokens.sources[written] = original->tokens.sources[token];
                view->tokens.flags[written] = 0;
                result->to_original[written] = BUSTER_CHECK_NONE;
                written += 1;
                block_last = BUSTER_CHECK_NONE;
            }
        }
        if (valid)
        {
            result->to_view[count] = written;
            for (uint32_t i = 0; i < original->tokens.pragma_count; i += 1)
            {
                const uint32_t position = original->tokens.pragma_positions[i];
                positions[i] = position <= count ? result->to_view[position] : total;
            }
            view->tokens.pragma_count = original->tokens.pragma_count;
            view->tokens.pragma_positions = positions;
            view->tokens.pragma_sources = original->tokens.pragma_sources;
            memset(view->meanings.meanings, PARSE_MEANING_ORDINARY, view->meanings.capacity);
            for (uint32_t i = 0; i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
            {
                view->meanings.meanings[view->predeclared[i]] = PARSE_MEANING_TYPEDEF;
            }
            const ParseScalarResult parsed = parse_scalar(view->tokens.kinds, view->tokens.names, view->tokens.flags, total, view->language, &view->meanings,
                                                          &view->tree, scratch, view->diagnostics, view->diagnostic_capacity, &view->diagnostic_count);
            valid = !parsed.out_of_memory && parsed.errors == 0 && view->diagnostic_count == 0 && check_prefix_order(view, scratch) == CHECK_PREFIX_READY;
        }
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

// Whether `token` is in the parse subtree of `root` (its parents lead there).
static bool check_driver_descends(const Parser* parser, uint32_t token, uint32_t root)
{
    uint32_t at = token;
    for (uint32_t steps = 0; at != root && at < parser->tokens.count && steps < parser->tokens.count; steps += 1)
    {
        at = parser->tree.parents[at];
    }
    return at == root;
}

// The first token of `root`'s subtree, which is contiguous and ends at or after `root`, at or after `low`.
static uint32_t check_driver_subtree_start(const Parser* parser, uint32_t low, uint32_t root)
{
    uint32_t first = low;
    uint32_t high = root;
    while (first < high)
    {
        const uint32_t middle = first + (high - first) / 2;
        const bool inside = check_driver_descends(parser, middle, root);
        first = inside ? first : middle + 1;
        high = inside ? middle : high;
    }
    return first;
}

// The child of `parent` with `role`, the nearest before `before` and after `floor` (an opener before its closer), or
// BUSTER_CHECK_NONE.
static uint32_t check_driver_child_before(const Parser* parser, uint32_t parent, uint8_t role, uint32_t floor, uint32_t before)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t at = before; result == BUSTER_CHECK_NONE && at-- > floor;)
    {
        result = parser->tree.parents[at] == parent && parser->tree.roles[at] == role ? at : BUSTER_CHECK_NONE;
    }
    return result;
}

// What check_driver_rejected_edit reads and writes. `owners` maps an initializer's `=` to its declaration, else
// BUSTER_CHECK_NONE. `work` holds the tokens whose parts are still to be left out (an error's, and its declarations'
// uses); `log` the edits made for the current error, as (token, previous fate) pairs, to undo where it fails.
typedef struct CheckDriverEditor CheckDriverEditor;
struct CheckDriverEditor
{
    const Parser* parser;
    const CheckInput* input;
    const TypeTable* types;
    const uint32_t* owners;
    uint8_t* edits;
    uint32_t* work;
    uint32_t work_count;
    uint32_t work_capacity;
    uint32_t* log;
    uint32_t log_count;
    uint32_t log_capacity;
    // The first reading's non-constant static initializers' errors (#1447, CHECK_INIT_NON_CONSTANT), by token, or NULL; and
    // the declarations whose initializers the view keeps for them, which the reading's initializer run skips.
    const uint8_t* nonconstant;
    uint8_t* skipped;
};

// Whether the tag definition whose `{` is `open`, inside [begin, last], declares no name used outside that region: its
// tag's, and an enumeration's enumerators', which the reading after leaving it out would read as another declaration's
// (an outer one's) without an error. Any identifier of the same spelling outside counts, in any name space.
static bool check_driver_tag_local(const Parser* parser, uint32_t open, uint32_t begin, uint32_t last)
{
    const uint32_t count = parser->tokens.count;
    const bool enumeration = parser->tree.roles[open] == PARSE_ROLE_OPEN_ENUM;
    const uint8_t close_role = enumeration ? PARSE_ROLE_CLOSE_ENUM : PARSE_ROLE_CLOSE_STRUCT;
    // The definition's `}`: the first closer of its kind at its depth.
    uint32_t close = open + 1;
    for (uint32_t depth = 0; close <= last && !(depth == 0 && parser->tree.roles[close] == close_role); close += 1)
    {
        depth += parser->tree.roles[close] == PARSE_ROLE_OPEN_STRUCT || parser->tree.roles[close] == PARSE_ROLE_OPEN_ENUM;
        depth -= (parser->tree.roles[close] == PARSE_ROLE_CLOSE_STRUCT || parser->tree.roles[close] == PARSE_ROLE_CLOSE_ENUM) && depth != 0;
    }
    // Its tag is the identifier right after its keyword (`struct` stands for `union` too), or it has none; anything else between them (a C23 enumeration's
    // `: type`, an attribute) makes it not local, as its tag isn't found.
    const uint8_t keyword = open > 0 ? parser->tokens.kinds[open - 1] : PARSE_KIND_COUNT;
    const uint8_t before_tag = open > 1 ? parser->tokens.kinds[open - 2] : PARSE_KIND_COUNT;
    const bool anonymous = keyword == PARSE_KIND_STRUCT || keyword == PARSE_KIND_ENUM;
    const bool tagged = keyword == PARSE_KIND_IDENTIFIER && (before_tag == PARSE_KIND_STRUCT || before_tag == PARSE_KIND_ENUM);
    bool local = close <= last && (anonymous || tagged);
    // Its names: the tag before the `{`, and an enumeration's enumerators, each after its `{` or a `,` (an argument after
    // a value's `,` too, which only makes it less often local).
    for (uint32_t at = open > 0 ? open - 1 : open; local && at < close; at += 1)
    {
        const uint8_t before = at > 0 ? parser->tokens.kinds[at - 1] : PARSE_KIND_COUNT;
        const bool tag = at + 1 == open && parser->tokens.kinds[at] == PARSE_KIND_IDENTIFIER;
        const bool enumerator = enumeration && at > open && parser->tokens.kinds[at] == PARSE_KIND_IDENTIFIER &&
                                (before == PARSE_KIND_L_BRACE || before == PARSE_KIND_COMMA);
        const uint32_t name = parser->tokens.names[at];
        for (uint32_t use = 0; (tag || enumerator) && local && use < count; use += 1)
        {
            local = (use >= begin && use <= last) || parser->tokens.kinds[use] != PARSE_KIND_IDENTIFIER || parser->tokens.names[use] != name;
        }
    }
    return local;
}

// Leave out the region [begin, last], `first` its first token's fate (DROP, or its run's replacement). A region already
// left out whole is left so. A run with a replacement is left out whole or not at all: the region may hold it, or be
// held by it, but not cross it; one without a replacement may also be crossed by one without. The region may hold no
// case label, which its switch's checks need, nor a tag definition that declares a name used outside it
// (check_driver_tag_local), nor a goto inside a loop, whose jump `-Wfor-loop-analysis` looks for. It may hold a label,
// another goto, a label address or a local label's declaration (#1447): a label left out that a kept goto names leaves
// the reading an error, whose view isn't taken, and a goto left out of no loop changes no warning a flushed body gives
// (while buster gives no -Wunused-label: one would then warn of a kept label the goto named). False where it can't be
// left out.
// Whether `token` is inside a loop's statement in its body: a `for`'s, a `do`'s or a `while`'s.
static bool check_driver_in_loop(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    bool loop = false;
    uint32_t at = token < count ? parser->tree.parents[token] : BUSTER_CHECK_NONE;
    for (uint32_t steps = 0; !loop && at < count && parser->tree.roles[at] != PARSE_ROLE_CLOSE_BODY && steps < count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[at];
        const uint32_t open = role == PARSE_ROLE_CLOSE_CONDITION ? check_driver_child_before(parser, at, PARSE_ROLE_OPEN_CONDITION, 0, at) : BUSTER_CHECK_NONE;
        loop = role == PARSE_ROLE_CLOSE_FOR || role == PARSE_ROLE_DO || (open != BUSTER_CHECK_NONE && open > 0 && parser->tokens.kinds[open - 1] == PARSE_KIND_WHILE);
        at = parser->tree.parents[at];
    }
    return loop;
}

static bool check_driver_edit_apply(CheckDriverEditor* editor, uint32_t begin, uint32_t last, uint8_t first)
{
    const Parser* parser = editor->parser;
    uint8_t* edits = editor->edits;
    const uint32_t count = parser->tokens.count;
    bool valid = begin <= last && last < count;
    bool covered = valid;
    for (uint32_t at = begin; valid && at <= last; at += 1)
    {
        covered &= edits[at] != CHECK_DRIVER_EDIT_KEEP;
        const uint8_t role = parser->tree.roles[at];
        const uint8_t kind = parser->tokens.kinds[at];
        const bool tag = role == PARSE_ROLE_OPEN_STRUCT || role == PARSE_ROLE_OPEN_ENUM;
        valid = role != PARSE_ROLE_OPEN_CASE && role != PARSE_ROLE_CLOSE_CASE && kind != PARSE_KIND_CASE && kind != PARSE_KIND_DEFAULT &&
                (!tag || check_driver_tag_local(parser, at, begin, last)) && (kind != PARSE_KIND_GOTO || !check_driver_in_loop(parser, at));
    }
    if (valid && !covered)
    {
        // The run its first token is in, where that run begins before it.
        uint32_t start = begin;
        while (edits[start] == CHECK_DRIVER_EDIT_DROP && start > 0 && edits[start - 1] != CHECK_DRIVER_EDIT_KEEP)
        {
            start -= 1;
        }
        const bool plain = first == CHECK_DRIVER_EDIT_DROP;
        valid = start == begin || (plain && edits[start] == CHECK_DRIVER_EDIT_DROP);
        // The run past its last token, where it begins at or before it.
        if (valid && edits[last + 1] == CHECK_DRIVER_EDIT_DROP)
        {
            uint32_t after = last;
            while (edits[after] == CHECK_DRIVER_EDIT_DROP && after > 0 && edits[after - 1] != CHECK_DRIVER_EDIT_KEEP)
            {
                after -= 1;
            }
            valid = plain && edits[after] == CHECK_DRIVER_EDIT_DROP && edits[after] != CHECK_DRIVER_EDIT_KEEP;
        }
        for (uint32_t at = begin; valid && at <= last; at += 1)
        {
            const uint8_t fate = at == begin ? first : CHECK_DRIVER_EDIT_DROP;
            if (edits[at] != fate)
            {
                valid = editor->log_count + 2 <= editor->log_capacity;
                if (valid)
                {
                    editor->log[editor->log_count++] = at;
                    editor->log[editor->log_count++] = edits[at];
                    edits[at] = fate;
                }
            }
        }
    }
    return valid;
}

// Queue `token`'s part to be left out.
static bool check_driver_edit_queue(CheckDriverEditor* editor, uint32_t token)
{
    const bool valid = editor->work_count < editor->work_capacity;
    if (valid)
    {
        editor->work[editor->work_count++] = token;
    }
    return valid;
}

// Whether `role` is a declaration's specifier's, whose children after it are the declaration's.
static bool check_driver_specifier_role(uint8_t role)
{
    return role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_TAG || role == PARSE_ROLE_CLOSE_SPECIFIER || role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER ||
           role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM;
}

// The root of the declarators of the declaration that ends at `terminator`: below its specifiers, the
// child after the innermost one; BUSTER_CHECK_NONE where it declares nothing. `head` is the terminator's child.
static uint32_t check_driver_declarators(const Parser* parser, uint32_t head, uint32_t terminator)
{
    uint32_t node = head;
    uint32_t result = BUSTER_CHECK_NONE;
    bool searching = node < terminator && check_driver_specifier_role(parser->tree.roles[node]);
    while (searching)
    {
        uint32_t child = BUSTER_CHECK_NONE;
        for (uint32_t at = node + 1; child == BUSTER_CHECK_NONE && at < terminator; at += 1)
        {
            child = parser->tree.parents[at] == node ? at : BUSTER_CHECK_NONE;
        }
        // A child after it that is a specifier too is the next level; `struct S`'s tag, `int`'s `const`.
        searching = child != BUSTER_CHECK_NONE && check_driver_specifier_role(parser->tree.roles[child]);
        node = searching ? child : node;
        result = searching ? BUSTER_CHECK_NONE : child;
    }
    return result;
}

// Queue the uses of the name `declarator`'s segment [begin, last] declares, its first identifier, from after `after` to
// its scope's end, the closing brace of the block or body it is in: every identifier of that name, as an ordinary name
// (not a member, a tag or a label), what reads the declaration or an outer one alike, which the reading without it
// mustn't read in its place.
static bool check_driver_edit_uses(CheckDriverEditor* editor, uint32_t begin, uint32_t last, uint32_t after, uint32_t scope_end)
{
    const Parser* parser = editor->parser;
    uint32_t name_token = BUSTER_CHECK_NONE;
    for (uint32_t at = begin; name_token == BUSTER_CHECK_NONE && at <= last; at += 1)
    {
        name_token = parser->tokens.kinds[at] == PARSE_KIND_IDENTIFIER && parser->tree.roles[at] == PARSE_ROLE_OPERAND ? at : BUSTER_CHECK_NONE;
    }
    bool valid = true;
    const uint32_t name = name_token != BUSTER_CHECK_NONE ? parser->tokens.names[name_token] : 0;
    for (uint32_t at = after + 1; valid && name_token != BUSTER_CHECK_NONE && at < scope_end; at += 1)
    {
        const uint8_t previous = parser->tokens.kinds[at - 1];
        const bool ordinary = parser->tokens.kinds[at] == PARSE_KIND_IDENTIFIER && parser->tokens.names[at] == name && parser->tree.roles[at] != PARSE_ROLE_TAG &&
                              parser->tree.roles[at] != PARSE_ROLE_MEMBER && previous != PARSE_KIND_PERIOD && previous != PARSE_KIND_ARROW &&
                              previous != PARSE_KIND_GOTO && parser->tree.roles[parser->tree.parents[at] < parser->tokens.count ? parser->tree.parents[at] : at] != PARSE_ROLE_LABEL;
        valid = !ordinary || editor->edits[at] != CHECK_DRIVER_EDIT_KEEP || check_driver_edit_queue(editor, at);
    }
    return valid;
}

// Whether `at` is one of the declarator list's commas: its root `declarators`, or the left child of one, in turn.
static bool check_driver_list_comma(const Parser* parser, uint32_t at, uint32_t declarators)
{
    const uint32_t count = parser->tokens.count;
    uint32_t node = at;
    bool chain = node < count && parser->tree.roles[node] == PARSE_ROLE_COMMA;
    while (chain && node != declarators)
    {
        const uint32_t parent = parser->tree.parents[node];
        chain = parent < count && parent > node && parser->tree.roles[parent] == PARSE_ROLE_COMMA;
        node = parent;
    }
    return chain && node == declarators;
}

// Leave out the part of the declaration [low, terminator] that holds `token`: its declarator, with the comma after it
// where a later declarator is kept, else with the comma after the last kept one before it; or the whole declaration,
// where the token is in its specifiers or no other declarator is kept (a for clause's keeps its `;`). The uses of what
// is left out are queued. Not a typedef's, whose name the parse reads as a type.
static bool check_driver_edit_declaration(CheckDriverEditor* editor, uint32_t low, uint32_t head, uint32_t terminator, uint32_t token, bool clause)
{
    const Parser* parser = editor->parser;
    const uint8_t* edits = editor->edits;
    bool valid = true;
    for (uint32_t at = low; valid && at < terminator; at += 1)
    {
        valid = parser->tokens.kinds[at] != PARSE_KIND_TYPEDEF;
    }
    // The scope's end: the brace that closes the block or body the declaration is in.
    uint32_t scope_end = parser->tree.parents[terminator];
    for (uint32_t steps = 0; scope_end < parser->tokens.count && parser->tree.roles[scope_end] != PARSE_ROLE_CLOSE_BLOCK &&
         parser->tree.roles[scope_end] != PARSE_ROLE_CLOSE_BODY && steps < parser->tokens.count; steps += 1)
    {
        scope_end = parser->tree.parents[scope_end];
    }
    valid = valid && scope_end < parser->tokens.count;
    const uint32_t declarators = valid ? check_driver_declarators(parser, head, terminator) : BUSTER_CHECK_NONE;
    const uint32_t first = declarators != BUSTER_CHECK_NONE ? check_driver_subtree_start(parser, low, declarators) : terminator;
    // The token's declarator, between the list's commas around it; the comma after the last kept declarator before it;
    // and whether one after it is kept.
    uint32_t before = BUSTER_CHECK_NONE;
    uint32_t after = terminator;
    uint32_t kept_comma = BUSTER_CHECK_NONE;
    bool kept_later = false;
    uint32_t segment = first;
    for (uint32_t at = first; valid && declarators != BUSTER_CHECK_NONE && at <= terminator; at += 1)
    {
        if (at == terminator || check_driver_list_comma(parser, at, declarators))
        {
            const bool kept = edits[segment] == CHECK_DRIVER_EDIT_KEEP;
            before = at < token ? at : before;
            after = at > token && after == terminator ? at : after;
            kept_comma = kept && at < token && at != terminator ? at : kept_comma;
            kept_later |= kept && segment > token;
            segment = at + 1;
        }
    }
    const uint32_t segment_begin = before != BUSTER_CHECK_NONE ? before + 1 : first;
    const uint32_t segment_last = after - 1;
    const bool whole = declarators == BUSTER_CHECK_NONE || token < first || (!kept_later && kept_comma == BUSTER_CHECK_NONE);
    if (valid && whole)
    {
        valid = clause ? low < terminator && check_driver_edit_apply(editor, low, terminator - 1, CHECK_DRIVER_EDIT_DROP) :
                         check_driver_edit_apply(editor, low, terminator, CHECK_DRIVER_EDIT_BLOCK);
        // Every declarator's uses, by each one's first identifier.
        segment = first;
        for (uint32_t at = first; valid && declarators != BUSTER_CHECK_NONE && at <= terminator; at += 1)
        {
            if (at == terminator || check_driver_list_comma(parser, at, declarators))
            {
                valid = check_driver_edit_uses(editor, segment, at - 1, terminator, scope_end);
                segment = at + 1;
            }
        }
    }
    else if (valid)
    {
        const uint32_t drop_begin = kept_later ? segment_begin : kept_comma;
        const uint32_t drop_last = kept_later ? after : segment_last;
        valid = check_driver_edit_apply(editor, drop_begin, drop_last, CHECK_DRIVER_EDIT_DROP) &&
                check_driver_edit_uses(editor, segment_begin, segment_last, terminator, scope_end);
    }
    return valid;
}

// Leave out the part of the statement ending at `terminator` that holds `token`: a for clause; a statement expression's
// last statement's value, which is its expression's (`token` is then the expression's `(`, queued); a declaration's
// declarator (check_driver_edit_declaration); else the statement, which `{` `}` replace. Only a body's statements.
static bool check_driver_edit_statement(CheckDriverEditor* editor, uint32_t terminator, uint32_t token)
{
    const Parser* parser = editor->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t parent = parser->tree.parents[terminator];
    const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_COUNT;
    const bool contained = parent_role == PARSE_ROLE_CLOSE_BODY || parent_role == PARSE_ROLE_CLOSE_BLOCK || parent_role == PARSE_ROLE_CLOSE_CONDITION ||
                           parent_role == PARSE_ROLE_CLOSE_FOR || parent_role == PARSE_ROLE_LABEL || parent_role == PARSE_ROLE_ELSE ||
                           parent_role == PARSE_ROLE_DO || parent_role == PARSE_ROLE_CLOSE_CASE;
    const bool clause = parent_role == PARSE_ROLE_CLOSE_FOR && terminator < parent;
    // A for statement's condition, after its init clause's `;`, is replaced, as an empty one would make it endless.
    const uint32_t open = clause ? check_driver_child_before(parser, parent, PARSE_ROLE_OPEN_FOR, 0, terminator) : BUSTER_CHECK_NONE;
    const bool condition = open != BUSTER_CHECK_NONE && check_driver_child_before(parser, parent, PARSE_ROLE_TERMINATOR, open, terminator) != BUSTER_CHECK_NONE;
    const uint32_t low = contained ? check_driver_subtree_start(parser, 0, terminator) : terminator;
    uint32_t head = BUSTER_CHECK_NONE;
    for (uint32_t at = low; contained && head == BUSTER_CHECK_NONE && at < terminator; at += 1)
    {
        head = parser->tree.parents[at] == terminator ? at : BUSTER_CHECK_NONE;
    }
    const uint8_t head_role = head != BUSTER_CHECK_NONE ? parser->tree.roles[head] : PARSE_ROLE_COUNT;
    bool declares = check_driver_specifier_role(head_role) || (head_role == PARSE_ROLE_CLOSE_KEYWORD && parser->tokens.kinds[low] == PARSE_KIND_STATIC_ASSERT);
    for (uint32_t at = low; contained && !declares && at <= terminator; at += 1)
    {
        declares = editor->input->declarations != NULL && editor->input->declarations[at] == at;
    }
    // A statement expression's last statement, its labels' too: `(` `{` … `;` `}` `)`.
    uint32_t block = parent;
    for (uint32_t steps = 0; block < count && (parser->tree.roles[block] == PARSE_ROLE_LABEL || parser->tree.roles[block] == PARSE_ROLE_CLOSE_CASE) &&
         steps < count; steps += 1)
    {
        block = parser->tree.parents[block];
    }
    const uint32_t group = block < count && parser->tree.roles[block] == PARSE_ROLE_CLOSE_BLOCK && terminator + 1 == block ? parser->tree.parents[block] :
                           BUSTER_CHECK_NONE;
    const uint32_t brace = group < count ? check_driver_child_before(parser, block, PARSE_ROLE_OPEN_BLOCK, 0, terminator) : BUSTER_CHECK_NONE;
    const bool value = group < count && parser->tree.roles[group] == PARSE_ROLE_CLOSE_GROUP && group == block + 1 && brace != BUSTER_CHECK_NONE && brace > 0 &&
                       parser->tree.roles[brace - 1] == PARSE_ROLE_OPEN_GROUP && parser->tree.parents[brace - 1] == group && !declares &&
                       head_role != PARSE_ROLE_RETURN;
    bool valid = contained;
    if (valid && value)
    {
        valid = check_driver_edit_queue(editor, brace - 1);
    }
    else if (valid && declares)
    {
        valid = check_driver_edit_declaration(editor, low, head, terminator, token, clause);
    }
    else if (valid && clause)
    {
        valid = low < terminator && check_driver_edit_apply(editor, low, terminator - 1, condition ? CHECK_DRIVER_EDIT_VALUE : CHECK_DRIVER_EDIT_DROP);
    }
    else if (valid)
    {
        valid = check_driver_edit_apply(editor, low, terminator, CHECK_DRIVER_EDIT_BLOCK);
    }
    return valid;
}

// Leave out the nearest part around `token` that the view can leave out alone: its initializer, from its `=`, where its
// declaration is complete without it (not an array of unknown size, not const-qualified but at file scope); its statement, or the
// part of it check_driver_edit_statement finds; its condition, which `-true` replaces; or its for statement's
// increment. A token already left out is. False where none is.
static bool check_driver_edit_site(CheckDriverEditor* editor, uint32_t token)
{
    const Parser* parser = editor->parser;
    const CheckInput* input = editor->input;
    const TypeTable* types = editor->types;
    const uint32_t count = parser->tokens.count;
    bool valid = token < count;
    bool done = !valid || editor->edits[token] != CHECK_DRIVER_EDIT_KEEP;
    uint32_t at = token;
    for (uint32_t steps = 0; !done && steps <= count; steps += 1)
    {
        const uint8_t role = at < count ? parser->tree.roles[at] : PARSE_ROLE_COUNT;
        const uint32_t declaration = at < count && parser->tokens.kinds[at] == PARSE_KIND_EQUAL ? editor->owners[at] : BUSTER_CHECK_NONE;
        if (declaration != BUSTER_CHECK_NONE && token > at)
        {
            const uint32_t end = check_sparse_get(input->initializer_ends, declaration);
            const TypeId type = input->declared_types != NULL ? input->declared_types[declaration] : 0;
            const TypeId bare = type != 0 ? type_strip_aligned(types, type) : 0;
            const bool unsized = bare != 0 && type_kind(types, bare) == BUSTER_TYPE_KIND_ARRAY && !type_is_complete(types, bare);
            const bool constant = (type_qualifiers(type) & BUSTER_TYPE_CONST) != 0;
            // `__func__` is a function's alone: at file scope the value reads undeclared and the view's reading fails,
            // so a file-scope const object's initializer is left to check_driver_rejected_declaration instead.
            const bool function = check_sparse_get(input->declaration_bodies, declaration) != BUSTER_CHECK_NONE;
            // A const object of an arithmetic or enumeration type keeps it, as clang's stays valid, with `-*__func__`'s
            // unknown value in its place; another initializer the declaration can't do without leaves out its declarator.
            const bool replaced = function && constant && bare != 0 && (type_is_arithmetic(types, bare) || type_kind(types, bare) == BUSTER_TYPE_KIND_ENUM);
            // At file scope, a const object's initializer is left out alone (#1447): clang keeps such a declaration valid,
            // its uses warning as before, its value unknown (`const int c;`). (An array of unknown size's isn't: the first
            // reading leaves out the events of the bodies that use it, which a later reading doesn't give again.)
            const bool alone = !function && constant && !unsized;
            // A static initializer that is only no constant (#1447, `const int d = 1 / 0;`) is kept: clang builds it and
            // gives its warnings, then its error, which the reading's initializer run doesn't give again (`skipped`).
            const bool kept = editor->nonconstant != NULL && editor->nonconstant[token] != 0 && token < end && end <= count;
            if (kept)
            {
                editor->skipped[declaration] = 1;
            }
            done = kept || (token < end && end <= count && type != 0 && ((!unsized && (!constant || replaced)) || alone) && at + 1 < end);
            valid = kept || !done || check_driver_edit_apply(editor, replaced ? at + 1 : at, end - 1, replaced ? CHECK_DRIVER_EDIT_VALUE : CHECK_DRIVER_EDIT_DROP);
        }
        else if (role == PARSE_ROLE_TERMINATOR)
        {
            valid = check_driver_edit_statement(editor, at, token);
            done = true;
        }
        else if (role == PARSE_ROLE_CLOSE_CONDITION)
        {
            // `if`, `while`, `switch` or `do`'s `while`, `(` … `)`: the token is in the parentheses, or is the keyword.
            const uint32_t open = check_driver_child_before(parser, at, PARSE_ROLE_OPEN_CONDITION, 0, at);
            valid = open != BUSTER_CHECK_NONE && open + 1 < at && token + 1 >= open && token <= at &&
                    check_driver_edit_apply(editor, open + 1, at - 1, CHECK_DRIVER_EDIT_VALUE);
            done = true;
        }
        else if (role == PARSE_ROLE_CLOSE_FOR)
        {
            // The increment: after the header's second `;`, the init and condition clauses' own terminators.
            const uint32_t open = check_driver_child_before(parser, at, PARSE_ROLE_OPEN_FOR, 0, at);
            const uint32_t clause = open != BUSTER_CHECK_NONE ? check_driver_child_before(parser, at, PARSE_ROLE_TERMINATOR, open, at) : BUSTER_CHECK_NONE;
            valid = clause != BUSTER_CHECK_NONE && open != BUSTER_CHECK_NONE && open < clause && clause < token && token < at && clause + 1 < at &&
                    check_driver_edit_apply(editor, clause + 1, at - 1, CHECK_DRIVER_EDIT_DROP);
            done = true;
        }
        else if (at >= count || role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_CLOSE_CASE ||
                 role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM)
        {
            valid = false;
            done = true;
        }
        at = at < count ? parser->tree.parents[at] : at;
    }
    return valid && done;
}

// What of a rejected unit's error at `token` its view leaves out (check_driver_rejected_common): the nearest part around
// it check_driver_edit_site finds, and the parts around the uses of what that leaves out of a declaration, in turn. All
// or none: where one can't be left out, what the others changed is undone, and its body is left out whole.
static bool check_driver_rejected_edit(CheckDriverEditor* editor, uint32_t token)
{
    editor->work_count = 0;
    editor->log_count = 0;
    bool valid = check_driver_edit_queue(editor, token);
    while (valid && editor->work_count != 0)
    {
        editor->work_count -= 1;
        valid = check_driver_edit_site(editor, editor->work[editor->work_count]);
    }
    while (!valid && editor->log_count != 0)
    {
        editor->log_count -= 2;
        editor->edits[editor->log[editor->log_count]] = (uint8_t)editor->log[editor->log_count + 1];
    }
    return valid;
}

// Whether a node of `role` (and token `kind`) is one an operand's error can sit under while its sibling operands are
// built and kept (check_driver_rejected_operands): an evaluated operator's, a call's or its argument list's separator's,
// a subscript's, a group's, a cast's, a member access's, a conditional's, or a return's. Not sizeof's or alignof's, whose operands clang doesn't evaluate,
// nor a statement expression's.
static bool check_driver_operand_path_role(uint8_t role, uint8_t kind)
{
    const bool unevaluated = kind == PARSE_KIND_SIZEOF;
    return !unevaluated &&
           (role == PARSE_ROLE_OPERAND || role == PARSE_ROLE_MEMBER || role == PARSE_ROLE_PREFIX || role == PARSE_ROLE_POSTFIX ||
            role == PARSE_ROLE_MEMBER_OPERATOR || (role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_ASSIGN) || role == PARSE_ROLE_COMMA ||
            role == PARSE_ROLE_SEPARATOR || role == PARSE_ROLE_RETURN || role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_CAST ||
            role == PARSE_ROLE_CLOSE_SUBSCRIPT || role == PARSE_ROLE_CLOSE_TERNARY || role == PARSE_ROLE_OPEN_TERNARY);
}

// Whether a subtree whose root has `role` (and token `kind`) is an expression the view can keep as `(void)(A);`.
static bool check_driver_operand_root_role(uint8_t role, uint8_t kind)
{
    const bool unevaluated = kind == PARSE_KIND_SIZEOF;
    return !unevaluated &&
           (role == PARSE_ROLE_OPERAND || role == PARSE_ROLE_PREFIX || role == PARSE_ROLE_POSTFIX || role == PARSE_ROLE_MEMBER_OPERATOR ||
            (role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_ASSIGN) || role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_CALL ||
            role == PARSE_ROLE_CLOSE_CAST || role == PARSE_ROLE_CLOSE_SUBSCRIPT || role == PARSE_ROLE_CLOSE_COMPOUND || role == PARSE_ROLE_CLOSE_TERNARY);
}

// A rejected unit's left-out expression statements' complete operands (#2120, CHECK_DRIVER_CAST_OPERAND). Clang builds
// each operand of an expression before it finds the expression's error, and gives its warnings (a call's -Wformat, a
// comma's -Wunused-value, -Wself-assign), on either side of the error; the statement, left out, gave none. For each known
// error `known[e]` in an expression statement the view leaves out whole (`{` `}`, from its first token to its `;`): the
// path from the error's token to the statement's root, through evaluated operators alone (a call's argument list's
// separators are the call's); every subtree off it whose parent is on it, an expression holding no known error (`errors`)
// and no part left out otherwise, is kept as its own `(void)(A);`. A `(void)` cast gives no warning of its own, and the
// body holds an error, so that its reading runs no flow analysis, as clang's doesn't. Whether any is kept.
static bool check_driver_rejected_operands(const Parser* parser, const uint8_t* edits, const uint8_t* errors, const uint32_t* known, uint32_t known_count,
                                           uint8_t* casts)
{
    const uint32_t count = parser->tokens.count;
    bool kept = false;
    // The path, at most this deep: a deeper one keeps nothing.
    enum { CHECK_DRIVER_OPERAND_DEPTH = 64 };
    uint32_t path[CHECK_DRIVER_OPERAND_DEPTH];
    for (uint32_t e = 0; e < known_count; e += 1)
    {
        const uint32_t token = known[e];
        uint32_t depth = 0;
        uint32_t node = token;
        bool valid = token < count && edits[token] != CHECK_DRIVER_EDIT_KEEP;
        while (valid && parser->tree.roles[node] != PARSE_ROLE_TERMINATOR)
        {
            valid = depth < CHECK_DRIVER_OPERAND_DEPTH && (node == token || check_driver_operand_path_role(parser->tree.roles[node], parser->tokens.kinds[node]));
            path[depth < CHECK_DRIVER_OPERAND_DEPTH ? depth : 0] = node;
            depth += valid;
            node = parser->tree.parents[node];
            valid = valid && node < count;
        }
        // The statement: its `;`, the block's last token, whose first token is the block's, left out from there to here.
        const uint32_t terminator = node;
        const uint32_t low = valid ? check_driver_subtree_start(parser, 0, terminator) : terminator;
        valid = valid && depth != 0 && edits[low] == CHECK_DRIVER_EDIT_BLOCK && (terminator + 1 >= count || edits[terminator + 1] != CHECK_DRIVER_EDIT_DROP);
        for (uint32_t at = low + 1; valid && at <= terminator; at += 1)
        {
            valid = edits[at] == CHECK_DRIVER_EDIT_DROP;
        }
        // A call's argument list's separators, from the last (each the parent of the one before it): on the path.
        for (uint32_t at = terminator; valid && at-- > low;)
        {
            const uint32_t parent = parser->tree.parents[at];
            bool listed = false;
            for (uint32_t p = 0; parser->tree.roles[at] == PARSE_ROLE_SEPARATOR && !listed && p < depth; p += 1)
            {
                listed = path[p] == parent && (parser->tree.roles[parent] == PARSE_ROLE_CLOSE_CALL || parser->tree.roles[parent] == PARSE_ROLE_SEPARATOR);
            }
            bool present = false;
            for (uint32_t p = 0; listed && !present && p < depth; p += 1)
            {
                present = path[p] == at;
            }
            if (listed && !present)
            {
                valid = depth < CHECK_DRIVER_OPERAND_DEPTH;
                path[depth < CHECK_DRIVER_OPERAND_DEPTH ? depth : 0] = at;
                depth += valid;
            }
        }
        // The children of the path's nodes that are off it, in source order: each a subtree [first, child].
        for (uint32_t child = low; valid && child < terminator; child += 1)
        {
            const uint32_t parent = parser->tree.parents[child];
            bool under = false;
            bool on = false;
            for (uint32_t p = 0; p < depth; p += 1)
            {
                under |= path[p] == parent;
                on |= path[p] == child;
            }
            if (under && !on && check_driver_operand_root_role(parser->tree.roles[child], parser->tokens.kinds[child]))
            {
                const uint32_t first = check_driver_subtree_start(parser, low, child);
                uint32_t last = child;
                while (last + 1 < terminator && check_driver_descends(parser, last + 1, child))
                {
                    last += 1;
                }
                bool clean = (casts[first] & CHECK_DRIVER_CAST_OPERAND) == 0;
                for (uint32_t at = first; clean && at <= last; at += 1)
                {
                    clean = errors[at] == 0 && (casts[at] & CHECK_DRIVER_CAST_OPERAND) == 0;
                }
                for (uint32_t at = first; clean && at <= last; at += 1)
                {
                    casts[at] |= CHECK_DRIVER_CAST_OPERAND;
                }
                if (clean)
                {
                    casts[first] |= CHECK_DRIVER_CAST_OPERAND_FIRST;
                    casts[last] |= CHECK_DRIVER_CAST_OPERAND_LAST;
                    kept = true;
                }
            }
        }
    }
    return kept;
}

// A rejected unit's error in a file-scope declaration that check_driver_rejected_edit leaves (#1447): its specifiers',
// its declarator's, an invalid type's, an array of unknown size's or a const object's initializer; or in a function
// definition outside its body (its declarator's, its parameters'). Clang marks such a declaration invalid and keeps it
// bound, its uses quiet; the view leaves the whole declaration out instead, from its first token to its `;` or its body's
// `}`, and the mask leaves out every body that names one of its names (its file-scope ones, not its parameters'), which
// the reading would otherwise find undeclared. False, leaving `edits` and `mask` as they were, where the error isn't in
// such a declaration (a body's) or a file-scope declaration outside it names one of them: its round isn't ready, as
// before. A use `quiet` marks, an invalid declaration's that the reader kept bound (CHECK_INPUT_INVALID_USE), is a known
// error itself, its part left out, and counts as no naming here. Only a rejected unit's rounds call it.
static bool check_driver_rejected_declaration(const Parser* parser, const CheckInput* input, const CBodyRange* ranges, uint32_t body_count, uint32_t token,
                                              const uint8_t* quiet, uint8_t* edits, uint8_t* mask)
{
    const uint32_t count = parser->tokens.count;
    // The declaration: the error's outermost parent, a `;` at file scope.
    uint32_t top = token;
    for (uint32_t steps = 0; top < count && parser->tree.parents[top] < count && steps < count; steps += 1)
    {
        top = parser->tree.parents[top];
    }
    // A function definition's, whose root is its body's `}`, is left out whole, its body too.
    const bool definition = top < count && parser->tree.roles[top] == PARSE_ROLE_CLOSE_BODY;
    bool valid = top < count && (parser->tree.roles[top] == PARSE_ROLE_TERMINATOR || definition) && input->declarations != NULL;
    uint32_t low = 0;
    uint32_t high = valid ? top : 0;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool inside = check_driver_descends(parser, middle, top);
        low = inside ? low : middle + 1;
        high = inside ? middle : high;
    }
    // A declaration holds no body (a statement expression's), and no part of it is left out otherwise; a definition's
    // parts already left out are its body's, which it leaves out with it.
    uint32_t open = top;
    for (uint32_t at = low; valid && at <= top; at += 1)
    {
        const bool body = parser->tree.roles[at] == PARSE_ROLE_OPEN_BODY;
        open = body && open == top ? at : open;
        valid = definition || (edits[at] != CHECK_DRIVER_EDIT_BLOCK && !body);
    }
    valid = valid && (!definition || open < top);
    // Its names, outside its parameter lists (the parse reads a definition's as a call's) and its attributes' lists (#2137:
    // an attribute's name, `noinline`, or an argument it names declares nothing): a declaration's declared identifiers; a
    // definition's ordinary identifiers before its body, its name among them, which the reader doesn't record (and a K&R
    // list's parameters, which mask a body needlessly).
    const uint32_t names_last = definition ? open : top;
    uint32_t depth = 0;
    uint32_t attributes = 0;
    // The bodies that name one of its names, outside it; a file-scope use declines. A C23 enum redefinition's enumerator
    // of a name the earlier definition declares too (#1447, CheckInput.ambiguous_index) is still declared where it's left
    // out, by that one.
    uint32_t users = 0;
    for (uint32_t at = low; valid && at <= names_last; at += 1)
    {
        const uint8_t role = parser->tree.roles[at];
        depth -= (role == PARSE_ROLE_CLOSE_FUNCTION || role == PARSE_ROLE_CLOSE_CALL) && depth != 0;
        attributes -= role == PARSE_ROLE_CLOSE_ATTRIBUTE_LIST && attributes != 0;
        const uint32_t name = parser->tokens.names[at];
        const bool declared = parser->tokens.kinds[at] == PARSE_KIND_IDENTIFIER && name != 0 && depth == 0 && attributes == 0 &&
                              (definition ? role == PARSE_ROLE_OPERAND : input->declarations[at] == at) &&
                              (input->ambiguous_index == NULL || input->ambiguous_index[at] == 0);
        depth += role == PARSE_ROLE_OPEN_FUNCTION || role == PARSE_ROLE_OPEN_CALL;
        attributes += role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST;
        for (uint32_t use = 0; declared && valid && use < count; use += 1)
        {
            // (Another declaration of the name, an earlier C23 definition's enumerator among them, names nothing.)
            const bool named = (use < low || use > top) && parser->tokens.kinds[use] == PARSE_KIND_IDENTIFIER && parser->tokens.names[use] == name &&
                               (quiet == NULL || (quiet[use] & 3) == 0) && input->declarations[use] != use;
            uint32_t first = 0;
            uint32_t last = body_count;
            while (named && first < last)
            {
                const uint32_t middle = first + (last - first) / 2;
                const bool before = ranges[middle].begin <= use;
                first = before ? middle + 1 : first;
                last = before ? last : middle;
            }
            const bool bodied = named && first != 0 && use < ranges[first - 1].end;
            valid = !named || bodied;
            users += bodied;
        }
    }
    if (valid)
    {
        depth = 0;
        attributes = 0;
        for (uint32_t at = low; users != 0 && at <= names_last; at += 1)
        {
            const uint8_t role = parser->tree.roles[at];
            depth -= (role == PARSE_ROLE_CLOSE_FUNCTION || role == PARSE_ROLE_CLOSE_CALL) && depth != 0;
            attributes -= role == PARSE_ROLE_CLOSE_ATTRIBUTE_LIST && attributes != 0;
            const uint32_t name = parser->tokens.names[at];
            const bool declared = parser->tokens.kinds[at] == PARSE_KIND_IDENTIFIER && name != 0 && depth == 0 && attributes == 0 &&
                                  (definition ? role == PARSE_ROLE_OPERAND : input->declarations[at] == at) &&
                                  (input->ambiguous_index == NULL || input->ambiguous_index[at] == 0);
            depth += role == PARSE_ROLE_OPEN_FUNCTION || role == PARSE_ROLE_OPEN_CALL;
            attributes += role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST;
            for (uint32_t b = 0; declared && b < body_count; b += 1)
            {
                bool names = false;
                for (uint32_t use = ranges[b].begin; !names && use < ranges[b].end; use += 1)
                {
                    // (A local of the name declared there, `void f(void) { int Z; }`, names nothing of this one's.)
                    names = parser->tokens.kinds[use] == PARSE_KIND_IDENTIFIER && parser->tokens.names[use] == name && (quiet == NULL || (quiet[use] & 3) == 0) &&
                            input->declarations[use] != use;
                }
                if (names)
                {
                    memset(mask + ranges[b].begin, 1, ranges[b].end - ranges[b].begin);
                }
            }
        }
        memset(edits + low, CHECK_DRIVER_EDIT_DROP, top - low + 1);
        edits[low] = CHECK_DRIVER_EDIT_EMPTY;
    }
    return valid;
}

// A rejected unit's common warnings (#1447): clang's for the bodies its errors don't reach. The bodies that hold one of
// `errors` (tokens) are left out of a second reading of the unit (check_input_selected, as -flazy-system-headers'
// bodies are), whose program, where it is valid, gives the -Wreturn-type and common warnings of the others: what clang
// gives of them, the bodies after the unit's first error flushed (check_driver_flushed_bodies). Its other warnings are
// the first reading's, which the caller published, but those the first reading lacks: its events' (by kind and token),
// and past `stop`, where a reader's error ended its walk, its reports' and warnings'. Nothing where an error lies
// outside every body and every part the view can leave out, or the reading isn't valid. Only a rejected unit pays for
// it; the type table, input and program are its own, and the arenas' pushes stay to the unit's end.
// What a rejected unit's reading came to (check_driver_rejected_read).
typedef enum CheckDriverRound
{
    // Its reading was valid, and its warnings are published.
    CHECK_DRIVER_ROUND_DONE,
    // Its reading found errors the unit's earlier readings hadn't: they are published, and `found` has their tokens.
    CHECK_DRIVER_ROUND_ERRORS,
    // Its reading couldn't be made or isn't valid: nothing is published.
    CHECK_DRIVER_ROUND_FAILED,
} CheckDriverRound;

// Whether a known error is in the top-level declaration that holds `token` (#1447): an edited view's reading may find an
// error the first reading found at another token of it (a macro's use and its declaration's name), so its own isn't fresh.
static bool check_driver_rejected_top_known(const Parser* parser, const uint32_t* errors, uint32_t error_count, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t top = token;
    for (uint32_t steps = 0; top < count && parser->tree.parents[top] < count && steps < count; steps += 1)
    {
        top = parser->tree.parents[top];
    }
    bool known = false;
    for (uint32_t e = 0; !known && top < count && e < error_count; e += 1)
    {
        uint32_t other = errors[e];
        for (uint32_t steps = 0; other < count && parser->tree.parents[other] < count && steps < count; steps += 1)
        {
            other = parser->tree.parents[other];
        }
        known = other == top;
    }
    return known;
}

// Whether the first reading's errors at `token` all leave their declaration valid (check_driver_report_keeps_valid,
// #1835): no other error is there. A typo's correction (#1447, CHECK_INPUT_UNDECLARED_CORRECTED) does in a body too
// (`bodied`): the reading corrects it again, its use bound as clang's, whose warnings (an array index's) stay. Only a
// rejected unit's rounds call it.
static bool check_driver_rejected_recovered(const CheckInput* input, uint32_t token, bool bodied)
{
    bool recovered = false;
    bool stopped = false;
    for (uint32_t i = 0; i < input->report_count; i += 1)
    {
        const bool at = input->reports[i].token == token;
        const bool corrected = input->reports[i].message == CHECK_INPUT_UNDECLARED_CORRECTED && input->reports[i].recovered;
        const bool keeps = corrected || (!bodied && check_driver_report_keeps_valid(input->reports + i));
        recovered |= at && keeps;
        stopped |= at && !keeps;
    }
    return recovered && !stopped;
}

// Whether the first reading's errors at `token` count in the scopes around it (check_driver_error_scoped): one does, or
// none is a report's (a graph's or a control's, which count).
static bool check_driver_rejected_scoped(const Parser* parser, const CheckInput* input, uint32_t token)
{
    bool reported = false;
    bool scoped = false;
    for (uint32_t i = 0; i < input->report_count; i += 1)
    {
        const bool at = input->reports[i].token == token;
        reported |= at;
        scoped |= at && check_driver_error_scoped(parser, input, input->reports + i);
    }
    return scoped || !reported;
}

// The scope of the block-scope declaration whose declarator `token` names, else BUSTER_CHECK_NONE: the `}` of the block
// or body it is in, or its for statement's `)` for a clause's. A declarator's name is the first ordinary identifier of
// its segment of the declaration's list (check_driver_edit_uses'), so a prototype's parameters and a record's members
// declare none here.
static uint32_t check_driver_declared_scope(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t terminator = token < count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && parser->tree.roles[token] == PARSE_ROLE_OPERAND ? token :
                                                                                                                                           BUSTER_CHECK_NONE;
    for (uint32_t steps = 0; terminator < count && parser->tree.roles[terminator] != PARSE_ROLE_TERMINATOR && steps < count; steps += 1)
    {
        terminator = parser->tree.parents[terminator];
    }
    // The block it is in, past its labels.
    uint32_t scope = terminator < count ? parser->tree.parents[terminator] : BUSTER_CHECK_NONE;
    for (uint32_t steps = 0; scope < count && (parser->tree.roles[scope] == PARSE_ROLE_LABEL || parser->tree.roles[scope] == PARSE_ROLE_CLOSE_CASE) &&
         steps < count; steps += 1)
    {
        scope = parser->tree.parents[scope];
    }
    const uint8_t scope_role = scope < count ? parser->tree.roles[scope] : PARSE_ROLE_COUNT;
    const bool block = scope_role == PARSE_ROLE_CLOSE_BLOCK || scope_role == PARSE_ROLE_CLOSE_BODY ||
                       (scope_role == PARSE_ROLE_CLOSE_FOR && terminator < scope);
    const uint32_t low = block ? check_driver_subtree_start(parser, 0, terminator) : terminator;
    uint32_t head = BUSTER_CHECK_NONE;
    for (uint32_t at = low; block && head == BUSTER_CHECK_NONE && at < terminator; at += 1)
    {
        head = parser->tree.parents[at] == terminator ? at : BUSTER_CHECK_NONE;
    }
    // A local label's declaration (`__label__ p;`) declares a label, no object.
    bool declares = head != BUSTER_CHECK_NONE && check_driver_specifier_role(parser->tree.roles[head]);
    for (uint32_t at = low; declares && at < terminator; at += 1)
    {
        declares = parser->tokens.kinds[at] != PARSE_KIND_LABEL;
    }
    const uint32_t declarators = declares ? check_driver_declarators(parser, head, terminator) : BUSTER_CHECK_NONE;
    const uint32_t first = declarators != BUSTER_CHECK_NONE ? check_driver_subtree_start(parser, low, declarators) : terminator;
    // The first ordinary identifier of the segment that holds the token.
    uint32_t name = BUSTER_CHECK_NONE;
    for (uint32_t at = first; declarators != BUSTER_CHECK_NONE && at <= token; at += 1)
    {
        const bool ordinary = parser->tokens.kinds[at] == PARSE_KIND_IDENTIFIER && parser->tree.roles[at] == PARSE_ROLE_OPERAND;
        name = check_driver_list_comma(parser, at, declarators) ? BUSTER_CHECK_NONE : name == BUSTER_CHECK_NONE && ordinary ? at : name;
    }
    return name == token ? scope : BUSTER_CHECK_NONE;
}

// The parameter list whose prototype's parameter `token` names (`int (*)(int p)`), its closer, else BUSTER_CHECK_NONE: an
// identifier after its specifiers, its parent, in a parameter list (a call's, where a type name reads one so), which
// declares it in the list's own scope.
static uint32_t check_driver_prototype_scope(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t parent = token < count ? parser->tree.parents[token] : BUSTER_CHECK_NONE;
    const bool named = parent < token && check_driver_specifier_role(parser->tree.roles[parent]) && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER;
    uint32_t at = named ? parent : BUSTER_CHECK_NONE;
    uint32_t list = BUSTER_CHECK_NONE;
    for (uint32_t steps = 0; list == BUSTER_CHECK_NONE && at < count && parser->tree.roles[at] != PARSE_ROLE_TERMINATOR &&
         parser->tree.roles[at] != PARSE_ROLE_CLOSE_BLOCK && parser->tree.roles[at] != PARSE_ROLE_CLOSE_BODY && steps < count; steps += 1)
    {
        list = parser->tree.roles[at] == PARSE_ROLE_CLOSE_CALL || parser->tree.roles[at] == PARSE_ROLE_CLOSE_FUNCTION ? at : BUSTER_CHECK_NONE;
        at = parser->tree.parents[at];
    }
    return list;
}

// Whether an error's token is among the unit's known ones.
static bool check_driver_rejected_known(const uint32_t* errors, uint32_t error_count, uint32_t token)
{
    bool known = false;
    for (uint32_t e = 0; !known && e < error_count; e += 1)
    {
        known = errors[e] == token;
    }
    return known;
}

// A rejected unit's second reading (check_driver_rejected_common): with `edits`, through the view that leaves out what they
// mark, the mask's bodies left out besides; without, the unit itself with the mask's bodies left out. Where its reading is
// valid, its warnings are published. Where it finds errors of its own, which the first reading didn't reach (past a reader's
// error that ended its walk, or a control the reader's scan then skipped), a view's reading publishes nothing, as its errors
// may be the view's own (a statement expression's value left void); the unit's, with bodies alone left out, publishes those
// not among `errors`, in clang's words, and puts their tokens in `found` (room for `found_capacity`), for the next round
// to leave out too.
static CheckDriverRound check_driver_rejected_read(const Parser* unit, const Parser* original, Preprocessor* pp, const CheckInput* first_input,
                                                   const uint32_t* errors, uint32_t error_count,
                                                   const CBodyRange* ranges, uint32_t body_count, const uint8_t* bodies, uint8_t* mask, const uint8_t* edits,
                                                   const uint8_t* casts, const uint8_t* skipped, bool whole, bool scalar, bool return_warning, CheckWarningSeverity return_baseline, CheckCommonShiftWarnings* shifts,
                                                   const CheckWarningEvents* first, const CheckWarningEvents* first_graph, uint32_t stop,
                                                   uint32_t publication_end, uint32_t* found, uint32_t found_capacity, uint32_t* found_count,
                                                   const uint8_t* quiet, Arena* arena, Arena* scratch)
{
    CheckDriverRound round = CHECK_DRIVER_ROUND_FAILED;
    *found_count = 0;
    const Parser* parser = unit;
    const uint32_t count = unit->tokens.count;
    // The first reading's stop in the unit's tokens, past which the driver gives each literal's own errors (#1447,
    // check_driver_literal_escapes): a reading's error at such a literal is the unit's already.
    const uint32_t unit_stop = stop;
    const bool edited = edits != NULL;
    // The view, where an error's part is left out; its mask is the bodies', each brace with the token before it.
    CheckDriverRejectedView view = { 0 };
    const bool viewed = edited && check_driver_rejected_view(&view, unit, edits, casts, arena, scratch);
    uint8_t* view_mask = viewed ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)view.parser.tokens.count + 1) : NULL;
    bool valid = !edited || view_mask != NULL;
    // What the reading leaves out, in the unit's tokens: the bodies it masks and the parts its view drops.
    const uint8_t* left_bodies = mask;
    const uint8_t* left_parts = edits;
    if (valid && edited)
    {
        for (uint32_t token = 0; token < view.parser.tokens.count; token += 1)
        {
            const uint32_t from = view.to_original[token];
            // (A replacement takes the mask of the token before it, but a left-out declaration's `;`, which is at file
            // scope: after a masked body, that would mask a token outside every body.)
            const bool empty = from == BUSTER_CHECK_NONE && view.parser.tokens.kinds[token] == PARSE_KIND_SEMI;
            view_mask[token] = from != BUSTER_CHECK_NONE ? mask[from] : token != 0 && !empty ? view_mask[token - 1] : 0;
        }
        view_mask[view.parser.tokens.count] = 0;
        parser = &view.parser;
        original = &view.parser;
        mask = view_mask;
        stop = view.to_view[stop < count ? stop : count];
        valid = check_lazy_mask_valid(parser, mask);
    }
    const uint32_t view_count = parser->tokens.count;
    // The errors' prefix count in the reading's tokens (check_driver_error_counts'), which leaves out its unused variables
    // in a scope that holds an error, as the first reading's are: a dropped part's error is at the token after it.
    uint32_t* error_counts = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)view_count + 2) : NULL;
    valid = valid && error_counts != NULL;
    if (valid)
    {
        memset(error_counts, 0, ((uint64_t)view_count + 2) * sizeof(uint32_t));
        for (uint32_t e = 0; e < error_count; e += 1)
        {
            const uint32_t at = errors[e] < count ? (edited ? view.to_view[errors[e]] : errors[e]) : view_count;
            // A dropped part's error counts just before the token after it (#1447): in its own scope, a dropped
            // declaration's at file scope, and not in the definition that follows it.
            const bool dropped = edited && errors[e] < count && edits[errors[e]] != 0;
            // (An invalid declaration's quiet use, 1 in `quiet`, is no error of its scope's, #1447, check_driver_error_counts, nor
            // a warning's that defaults to an error, 4, #1969.)
            // A recovered error clang gives before a body's scope opens counts in none (check_driver_error_scoped, #1835).
            const bool scoped = errors[e] >= count || check_driver_rejected_scoped(unit, first_input, errors[e]);
            error_counts[(at < view_count ? at : view_count) + (dropped ? 0u : 1u)] += scoped && (quiet == NULL || errors[e] >= count || (quiet[errors[e]] & 3) != 1) && (quiet == NULL || errors[e] >= count || (quiet[errors[e]] & 4) == 0) ? 1u : 0u;
        }
        for (uint32_t t = 1; t < view_count + 2; t += 1)
        {
            error_counts[t] += error_counts[t - 1];
        }
    }
    const TypeCapacity capacity = {
        .types = view_count <= (1u << 27) ? view_count * 4 + 64 : 0,
        .parameters = view_count,
        .tags = view_count,
        .members = view_count,
    };
    const uint64_t type_bytes = type_capacity_bytes(capacity);
    const uint64_t index_bytes = type_index_capacity_bytes(capacity);
    Arena type_arena = valid ? arena_reserve_small(type_bytes, type_bytes) : (Arena){ 0 };
    Arena index_arena = valid ? arena_reserve_small(index_bytes, BUSTER_TYPE_INDEX_SMALL_BYTES) : (Arena){ 0 };
    TypeTable types = { 0 };
    valid = valid && capacity.types != 0 && type_initialize(&types, &type_arena, &index_arena, capacity);
    CheckInput input = { 0 };
    // (A reading whose reader reports an error isn't selected's valid one, but its reports are the round's. One whose only
    // reports are recovered ones, a recovered error the round left in (#1835), reads on as a valid one, as the unit's first
    // reading does: CheckInput.recovered_valid.)
    const bool selected = valid && check_input_selected(&input, parser, pp, &types, arena, scratch, mask);
    // A recovered reading (#1447, an invalid declaration's error, CheckInput.recovered_valid) that meets an error the unit's
    // earlier readings don't hold has errors of its own too: an edited view reaches a definition's head error past the
    // left-out one before it, which the next round leaves out with its definition.
    // Only an error that made its declaration invalid (CheckInput.invalid_declarations in its top-level declaration): one that
    // leaves its declaration valid (an attribute's) clang may not give past the round's earlier errors.
    bool fresh_recovered = false;
    for (uint32_t i = 0; valid && !input.resource && input.recovered_valid && input.invalid_declarations != NULL && !fresh_recovered && i < input.report_count;
         i += 1)
    {
        const uint32_t token = input.reports[i].token;
        const uint32_t at = token < view_count && edited ? view.to_original[token] : token;
        const bool fresh = input.reports[i].message != CHECK_INPUT_INVALID_USE && at < count && !check_driver_rejected_known(errors, error_count, at) &&
                           (!edited || !check_driver_rejected_top_known(unit, errors, error_count, at));
        uint32_t top = fresh && token < view_count ? token : BUSTER_CHECK_NONE;
        for (uint32_t steps = 0; top < view_count && parser->tree.parents[top] < view_count && steps < view_count; steps += 1)
        {
            top = parser->tree.parents[top];
        }
        const uint32_t first = top < view_count ? check_driver_subtree_start(parser, 0, top) : top;
        for (uint32_t t = first; !fresh_recovered && top < view_count && t <= top; t += 1)
        {
            fresh_recovered = input.invalid_declarations[t] != 0;
        }
    }
    valid = valid && (selected || input.report_count != 0) && !input.resource;
    const CheckOptions check_options = { .scalar = scalar };
    CheckStream stream = { 0 };
    // The reading's own errors: its reader's (a recovered one's unit is valid), whose reports stand without the graph, which
    // may not run after them, and its graph's.
    const bool reader_errors = valid && input.report_count != 0 && (!input.recovered_valid || fresh_recovered);
    const bool graphed = valid && check_graph_run(&types, &input.graph, arena, scratch, &check_options, &stream);
    if (!graphed)
    {
        stream = (CheckStream){ 0 };
    }
    valid = valid && (graphed || reader_errors);
    bool errored_reading = valid && (reader_errors || stream.report_count != 0);
    valid = valid && (errored_reading || (check_input_packed_finalize(&input, parser, &types, &stream, scratch) &&
                                          check_input_warning_graph(&input, parser, &types, &stream, arena, scratch)));
    // The initializers the first reading found no constant, which the view keeps for their warnings (#1447): their errors
    // are the unit's already, so this run reads their declarations as uninitialized.
    for (uint32_t t = 0; valid && !errored_reading && skipped != NULL && t < view_count; t += 1)
    {
        const uint32_t at = edited ? view.to_original[t] : t;
        if (at < count && skipped[at] != 0 && check_sparse_get(input.initializer_begins, t) != BUSTER_CHECK_NONE)
        {
            check_sparse_set(input.sparse_pool, input.initializer_begins, t, BUSTER_CHECK_NONE);
        }
    }
    CheckAsms asms = { 0 };
    CheckInitializers initializers = { 0 };
    valid = valid && (errored_reading || (check_asms_run(&asms, &input, &stream, &types, arena, scratch) &&
                                          check_initializers_run_events(&initializers, &input, &stream, parser, &types, arena, scratch, NULL, NULL)));
    const CheckProgramOptions program_options = check_request_program_options(pp, (CheckProgramOptions){
        .initializers = &initializers,
        .asms = &asms,
        .scalar = scalar,
        .return_warnings = return_warning,
        .noreturn_warnings = true,
    });
    CheckProgram program = { 0 };
    const bool checked = valid && !errored_reading && check_program(&program, parser, &input, &stream, &types, program_options, arena, scratch);
    errored_reading = valid && (errored_reading || (!checked && program.status == CHECK_PROGRAM_INVALID));
    valid = valid && (errored_reading || checked);
    // (The errors of a view that leaves out only whole declarations, `whole`, are the unit's; another's may be its own.)
    if (errored_reading && edited && !whole)
    {
        valid = false;
    }
    else if (errored_reading)
    {
        // The reading's errors the unit's earlier ones don't hold, published as the first reading's are: its reader's and
        // its graph's reports, and its controls' constraints, each in clang's words, in source order.
        CheckProgramDiagnostic control = { 0 };
        const bool listed = graphed && check_program_diagnose_all(&control, &input, &stream, parser, &types, BUSTER_CHECK_NONE, arena, scratch);
        const uint32_t constraint_count = listed ? control.constraint_count : 0;
        CheckInputReport* reports = input.report_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, CheckInputReport, input.report_count) : NULL;
        CheckReport* graph_reports = stream.report_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, CheckReport, stream.report_count) : NULL;
        CheckDriverDiagnostic* extra = constraint_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, CheckDriverDiagnostic, constraint_count) : NULL;
        valid = (input.report_count == 0 || reports != NULL) && (stream.report_count == 0 || graph_reports != NULL) && (constraint_count == 0 || extra != NULL);
        CheckInput published = input;
        CheckStream published_stream = stream;
        published.reports = reports;
        published.report_count = 0;
        published_stream.reports = graph_reports;
        published_stream.report_count = 0;
        uint32_t extra_count = 0;
        // Each error's token in the unit's, where the view put one there.
        for (uint32_t i = 0; valid && i < input.report_count + stream.report_count + constraint_count; i += 1)
        {
            const uint32_t phase = i < input.report_count ? 0 : i < input.report_count + stream.report_count ? 1 : 2;
            const uint32_t index = phase == 0 ? i : phase == 1 ? i - input.report_count : i - input.report_count - stream.report_count;
            const uint32_t token = phase == 0 ? input.reports[index].token : phase == 1 ? stream.reports[index].token : control.constraints[index].token;
            const uint32_t at = token < view_count && edited ? view.to_original[token] : token;
            const bool literal = phase == 0 && at < count && at >= unit_stop &&
                                 (input.reports[index].message == CHECK_INPUT_LITERAL || input.reports[index].message == CHECK_INPUT_WIDE_LITERAL);
            const bool fresh = at < count && !literal && !check_driver_rejected_known(errors, error_count, at) &&
                               !check_driver_rejected_known(found, *found_count, at) && (!edited || !check_driver_rejected_top_known(unit, errors, error_count, at));
            valid = !fresh || *found_count < found_capacity;
            if (fresh && valid)
            {
                found[(*found_count)++] = at;
                if (phase == 0)
                {
                    reports[published.report_count++] = input.reports[index];
                }
                else if (phase == 1)
                {
                    graph_reports[published_stream.report_count++] = stream.reports[index];
                }
                else
                {
                    const CheckProgramConstraint* constraint = control.constraints + index;
                    extra[extra_count++] = (CheckDriverDiagnostic){
                        .token = constraint->token,
                        .message = check_semantic_constraint(parser, pp, &input, &types, constraint, arena),
                        .position = constraint->position,
                        .completion = constraint->position < view_count ? constraint->position + 1u : view_count,
                        .repeated = true,
                        .note_token = constraint->previous,
                        .note = constraint->previous != BUSTER_CHECK_NONE ? "previous case defined here" : NULL,
                    };
                }
            }
        }
        // Errors alone: the reading's warnings are the next round's, and its unit-wide records (pragmas, implicit
        // declarations, K&R's) the first reading's.
        published.implicit_function_count = 0;
        published.kr_diagnostic_count = 0;
        published.fp_pragma_count = 0;
        published.visibility_pragmas = NULL;
        published.packed_member_count = 0;
        published.pragma_warnings.count = 0;
        if (valid && *found_count != 0)
        {
            check_driver_reports_from(parser, original, pp, publication_end, &published, &published_stream, &types, NULL, NULL, extra, extra_count, NULL, 0, true,
                                      NULL, 0, scratch);
        }
        round = valid && *found_count != 0 ? CHECK_DRIVER_ROUND_ERRORS : CHECK_DRIVER_ROUND_FAILED;
        valid = false;
    }
    // The reader's and the graph's warning events the first reading lacks (#1447): a reader's error that ends its
    // reading leaves the bodies after it without theirs, and every unit without its unused declarations'. The first
    // reading's own events, published or left out, are its; the others are the rejected unit's (the unit-end ones left out).
    const uint64_t events_mark = scratch->length;
    const uint32_t known_count = first->count + first_graph->count;
    uint64_t* known = valid && known_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, known_count) : NULL;
    uint64_t* sorting = known != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, known_count) : NULL;
    const uint64_t* sorted = NULL;
    if (sorting != NULL)
    {
        for (uint32_t i = 0; i < known_count; i += 1)
        {
            const CheckWarningEvent* event = i < first->count ? first->events + i : first_graph->events + (i - first->count);
            known[i] = (uint64_t)event->token << 32 | event->kind;
        }
        sorted = c_bindings_sort(known, sorting, known_count, !scalar, UINT32_MAX);
    }
    const CheckWarningEvents* readings[] = { &input.warning_events, &stream.warning_events };
    for (uint32_t r = 0; valid && (known_count == 0 || sorted != NULL) && r < BUSTER_ARRAY_LENGTH(readings); r += 1)
    {
        const CheckWarningEvents* events = readings[r];
        CheckWarningEvent* missing = events->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckWarningEvent, events->count) : NULL;
        CheckWarningEvents selected = { .events = missing, .notes = events->notes, .note_count = events->note_count };
        for (uint32_t i = 0; missing != NULL && i < events->count; i += 1)
        {
            const uint32_t event_token = events->events[i].token;
            const uint32_t at = edited && event_token < view_count ? view.to_original[event_token] : event_token;
            const uint64_t key = (uint64_t)at << 32 | events->events[i].kind;
            uint32_t low = 0;
            uint32_t high = known_count;
            // (The sort orders the tokens alone: a token's few events are scanned for the kind.)
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2;
                const bool below = sorted[middle] >> 32 < key >> 32;
                low = below ? middle + 1 : low;
                high = below ? high : middle;
            }
            bool known_event = false;
            for (uint32_t at = low; !known_event && at < known_count && sorted[at] >> 32 == key >> 32; at += 1)
            {
                known_event = sorted[at] == key;
            }
            // A parameter's unused warning, where its function holds an error, as clang's (ActOnFinishFunctionBody gives
            // them whatever the body's errors): unless what the reading left out of its body names it, which clang's
            // reading of that part takes for a use, as it does an erroneous expression's operands; a member's name doesn't.
            bool errored = false;
            if (events->events[i].kind == CHECK_WARNING_EVENT_UNUSED_PARAMETER && at < count)
            {
                uint32_t first_body = 0;
                uint32_t last_body = body_count;
                while (first_body < last_body)
                {
                    const uint32_t middle = first_body + (last_body - first_body) / 2;
                    const bool before = ranges[middle].begin <= at;
                    first_body = before ? middle + 1 : first_body;
                    last_body = before ? last_body : middle;
                }
                // (A body left out whole without an error of its own, one that names a left-out declaration, too.)
                const bool holds = first_body < body_count && (bodies[ranges[first_body].begin] != 0 || left_bodies[ranges[first_body].begin] != 0);
                const uint32_t name = unit->tokens.names[at];
                const uint32_t begin = holds ? ranges[first_body].begin : 0;
                const uint32_t end = holds ? ranges[first_body].end : 0;
                // The scopes of the body's declarations of the name so far, which a use inside one reads instead (#1447):
                // a local's declarator and its uses aren't the parameter's.
                const uint64_t scopes_mark = scratch->length;
                uint32_t* scopes = holds ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)(end - begin) + 1) : NULL;
                uint32_t scope_count = 0;
                for (uint32_t t = begin; scopes != NULL && !errored && t < end; t += 1)
                {
                    const uint8_t before = unit->tokens.kinds[t - 1];
                    const uint32_t parent = unit->tree.parents[t];
                    const bool ordinary = unit->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && unit->tokens.names[t] == name && before != PARSE_KIND_PERIOD &&
                                          before != PARSE_KIND_ARROW && before != PARSE_KIND_GOTO && unit->tree.roles[t] != PARSE_ROLE_TAG &&
                                          unit->tree.roles[t] != PARSE_ROLE_MEMBER && !(parent < count && unit->tree.roles[parent] == PARSE_ROLE_LABEL);
                    const uint32_t declared = ordinary ? check_driver_declared_scope(unit, t) : BUSTER_CHECK_NONE;
                    const uint32_t scope = ordinary && declared == BUSTER_CHECK_NONE ? check_driver_prototype_scope(unit, t) : declared;
                    if (scope != BUSTER_CHECK_NONE)
                    {
                        scopes[scope_count++] = scope;
                    }
                    bool shadowed = false;
                    for (uint32_t s = 0; ordinary && scope == BUSTER_CHECK_NONE && !shadowed && s < scope_count; s += 1)
                    {
                        shadowed = check_driver_descends(unit, t, scopes[s]);
                    }
                    const bool left = left_bodies[t] != 0 || (left_parts != NULL && left_parts[t] != 0);
                    errored = left && ordinary && scope == BUSTER_CHECK_NONE && !shadowed;
                }
                // (Without room for the scopes, any use of the name is the parameter's, as before.)
                for (uint32_t t = scopes == NULL ? begin : end; !errored && t < end; t += 1)
                {
                    const bool left = left_bodies[t] != 0 || (left_parts != NULL && left_parts[t] != 0);
                    const uint8_t before = unit->tokens.kinds[t - 1];
                    errored = left && unit->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && unit->tokens.names[t] == name && before != PARSE_KIND_PERIOD &&
                              before != PARSE_KIND_ARROW;
                }
                arena_pop_to(scratch, scopes_mark);
            }
            // (Past `stop` the first reading published none of its events.)
            if (!errored && (!known_event || events->events[i].token >= stop))
            {
                missing[selected.count++] = events->events[i];
            }
        }
        CheckWarningEvents events_view;
        if (selected.count != 0 && check_driver_rejected_warnings(parser, &selected, true, error_counts, scratch, &events_view))
        {
            check_driver_warning_events_at(parser, pp, &input, &types, &events_view, NULL, publication_end, scratch);
        }
    }
    arena_pop_to(scratch, events_mark);
    // Where a reader's error stopped the first reading's walk (`stop`), the second's reports and warnings from there on,
    // which are its warnings alone: it has no error.
    if (valid && stop < view_count)
    {
        check_driver_reports_from(parser, original, pp, publication_end, &input, &stream, &types, NULL, NULL, NULL, 0, NULL, 0, false, NULL, stop, scratch);
    }
    if (valid)
    {
        // The folded shifts' tokens in the reading's.
        for (uint32_t i = 0; edited && shifts != NULL && i < shifts->count; i += 1)
        {
            shifts->records[i].token = shifts->records[i].token < count ? view.to_view[shifts->records[i].token] : shifts->records[i].token;
        }
        check_driver_return_reports(parser, original, pp, &program, return_baseline, false);
        const uint64_t flushed_mark = scratch->length;
        const uint8_t* flushed = check_driver_flushed_bodies(parser, original, pp, &program, scratch);
        // The bodies that hold an error, whose reads the reading may lack: 2 (CheckCommonFacts.flushed).
        uint8_t* errored = flushed != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, view_count) : NULL;
        for (uint32_t token = 0; errored != NULL && token < view_count; token += 1)
        {
            const uint32_t at = edited ? view.to_original[token] : token;
            const bool held = at < count ? bodies[at] != 0 : token != 0 && errored[token - 1] == 2;
            errored[token] = flushed[token] != 0 && held ? 2 : flushed[token];
        }
        check_driver_common_warnings(parser, pp, &input, &stream, &types, &program, errored != NULL ? errored : flushed, shifts, scratch);
        arena_pop_to(scratch, flushed_mark);
        round = CHECK_DRIVER_ROUND_DONE;
    }
    arena_release(&type_arena);
    arena_release(&index_arena);
    return round;
}

// The most readings of a rejected unit (check_driver_rejected_common) past those that reach the error limit's cut: the first's
// errors' parts left out, then each one's new errors' too. Each reading before the cut adds an error, so they are at most
// the limit's; past the cut, the bodies after it are left out, and these readings find what a body that holds the cut, or
// file scope after it, still hides. Without a limit (-ferror-limit=0) the rounds go on while each finds an error.
#define BUSTER_CHECK_DRIVER_REJECTED_ROUNDS 4u

static void check_driver_rejected_common(const Parser* unit, const Parser* original, Preprocessor* pp, const CheckInput* first_input, const TypeTable* first_types,
                                         const uint32_t* errors, uint32_t error_count, const CheckDriverConversionSite* sites, uint32_t site_count,
                                         const uint8_t* nonconstant, bool scalar, bool return_warning,
                                         CheckWarningSeverity return_baseline,
                                         CheckCommonShiftWarnings* shifts, const CheckWarningEvents* first, const CheckWarningEvents* first_graph, uint32_t stop,
                                         uint32_t publication_end, Arena* arena, Arena* scratch)
{
    const Parser* parser = unit;
    const uint32_t count = parser->tokens.count;
    uint32_t body_count = 0;
    for (uint32_t token = 0; token < count; token += 1)
    {
        body_count += parser->tree.roles[token] == PARSE_ROLE_OPEN_BODY;
    }
    // (A unit of no body reads its file-scope initializers' warnings only for an initializer that is no constant, #1447.)
    CBodyRange* ranges = body_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, CBodyRange, body_count) : NULL;
    uint8_t* mask = ranges != NULL || (body_count == 0 && nonconstant != NULL) ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : NULL;
    uint32_t found = 0;
    bool valid = mask != NULL && error_count != 0 &&
                 (body_count == 0 || (c_environment_bodies(&parser->tree, count, ranges, body_count, &found) && found == body_count));
    // What the view leaves out of the bodies (check_driver_rejected_edit): the part around each error it can, else its
    // whole body, which the mask leaves out. Its editor's columns: each initializer's `=`'s declaration, the work list
    // and the undo log, each bounded by the tokens (a token is queued once a declaration, and logged at most twice).
    uint8_t* edits = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : NULL;
    uint32_t* owners = edits != NULL ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1) : NULL;
    uint32_t* work = owners != NULL ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1) : NULL;
    const uint64_t log_capacity = 4 * ((uint64_t)count + 1);
    uint32_t* log = work != NULL && log_capacity <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, log_capacity) : NULL;
    valid = log != NULL;
    if (valid)
    {
        memset(mask, 0, (uint64_t)count + 1);
        memset(edits, 0, (uint64_t)count + 1);
        memset(owners, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t begin = check_sparse_get(first_input->initializer_begins, token);
            if (begin != BUSTER_CHECK_NONE && begin != 0 && begin <= count && parser->tokens.kinds[begin - 1] == PARSE_KIND_EQUAL)
            {
                owners[begin - 1] = token;
            }
        }
    }
    // The unit's invalid declarations (#1447, check_input_invalid_declare) and their uses (CHECK_INPUT_INVALID_USE): known
    // errors, which count as no error of their scope's (clang warns of its unused variables), whose parts the readings leave
    // out, as each other's. A body that names such a declaration there isn't left out whole for it
    // (check_driver_rejected_declaration), and a redeclaration the first reading bound quietly after an invalid one, which
    // would conflict where the view has left that out, is left out too. 1 at a use, 2 at a declaration's name; NULL in a
    // unit without one.
    // A conversion's that defaults to an error, whose view casts its source (#1969, `sites`, whose tokens are `errors`' last),
    // has 4 too, where no other error is at its token: its scope doesn't count it.
    const uint32_t scoped_count = error_count - site_count;
    uint8_t* quiet = NULL;
    const uint32_t invalid_count = first_input->invalid_declarations != NULL ? first_input->invalid_count : 0;
    for (uint32_t i = 0; valid && i < first_input->report_count + (invalid_count != 0 ? count : 0); i += 1)
    {
        const bool reported = i < first_input->report_count;
        const uint32_t token = reported ? first_input->reports[i].token : i - first_input->report_count;
        if (reported ? first_input->reports[i].message == CHECK_INPUT_INVALID_USE && token < count : first_input->invalid_declarations[token] != 0)
        {
            if (quiet == NULL)
            {
                quiet = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, count);
                valid = quiet != NULL;
                if (valid)
                {
                    memset(quiet, 0, count);
                }
            }
            if (quiet != NULL)
            {
                quiet[token] = reported ? 1 : 2;
            }
        }
    }
    // So has a non-void function's `return;` (-Wreturn-mismatch, which defaults to an error), whose statement the view leaves
    // out, where no other report is at its token.
    for (uint32_t i = 0; valid && i < first_input->report_count; i += 1)
    {
        const uint32_t token = first_input->reports[i].token;
        bool alone = first_input->reports[i].message == CHECK_INPUT_RETURN && token < count;
        for (uint32_t j = 0; alone && j < first_input->report_count; j += 1)
        {
            alone = j == i || first_input->reports[j].token != token;
        }
        if (alone && quiet == NULL)
        {
            quiet = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, count);
            valid = quiet != NULL;
            if (valid)
            {
                memset(quiet, 0, count);
            }
        }
        if (alone && quiet != NULL)
        {
            quiet[token] |= 4u;
        }
    }
    for (uint32_t e = scoped_count; valid && e < error_count; e += 1)
    {
        const uint32_t token = errors[e];
        if (quiet == NULL && token < count)
        {
            quiet = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, count);
            valid = quiet != NULL;
            if (valid)
            {
                memset(quiet, 0, count);
            }
        }
        if (quiet != NULL && token < count && !check_driver_rejected_known(errors, scoped_count, token))
        {
            quiet[token] |= 4u;
        }
    }
    CheckDriverEditor editor = {
        .parser = parser,
        .input = first_input,
        .types = first_types,
        .owners = owners,
        .edits = edits,
        .work = work,
        .work_capacity = count + 1,
        .log = log,
        .log_capacity = (uint32_t)(valid ? log_capacity : 0),
        .nonconstant = nonconstant,
    };
    // The declarations whose non-constant initializers the view keeps (CheckDriverEditor.skipped), each round's.
    uint8_t* skipped = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : NULL;
    valid = skipped != NULL;
    editor.skipped = skipped;
    // The bodies of every error, the mask where the view can't be made; and whether each error has one.
    uint8_t* bodies = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : NULL;
    // The fallback's view and mask, where the view's reading isn't valid: each error's body left out whole, and each
    // error outside every body its whole declaration (check_driver_rejected_declaration), whose names' users are left
    // out too, so that what that reading reads the first did too, but for whole declarations: its errors are the unit's.
    uint8_t* fallback_edits = bodies != NULL ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : NULL;
    uint8_t* fallback_mask = fallback_edits != NULL ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : NULL;
    // The view's casts of the conversions' sources and its left-out statements' kept operands (CHECK_DRIVER_CAST_*), and
    // each round's known errors' tokens, which no kept operand holds.
    uint8_t* casts = fallback_mask != NULL ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : NULL;
    uint8_t* error_marks = casts != NULL ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : NULL;
    valid = error_marks != NULL;
    // The unit's known errors: the first reading's, its invalid declarations, then each round's new ones, at most one per
    // token.
    const uint64_t known_capacity = (uint64_t)error_count + invalid_count + count + 1;
    uint32_t* known = valid && bodies != NULL && known_capacity <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, known_capacity) : NULL;
    uint32_t known_count = error_count;
    if (known != NULL)
    {
        memcpy(known, errors, (uint64_t)error_count * sizeof(uint32_t));
        for (uint32_t token = 0; invalid_count != 0 && token < count; token += 1)
        {
            if (first_input->invalid_declarations[token] != 0 && known_count < error_count + invalid_count)
            {
                known[known_count++] = token;
            }
        }
    }
    // Rounds (#1447): a reading that finds errors of its own publishes them, and the next leaves their parts out too, so
    // that each round adds parts left out, until the error limit's cut and BUSTER_CHECK_DRIVER_REJECTED_ROUNDS readings
    // past it; past them, or where a reading fails, the unit's warnings aren't given.
    const uint32_t limit = pp->options.fatal_errors ? 1u : pp->options.error_limit;
    const uint32_t rounds = limit != 0 && limit <= UINT32_MAX - BUSTER_CHECK_DRIVER_REJECTED_ROUNDS ? limit + BUSTER_CHECK_DRIVER_REJECTED_ROUNDS : UINT32_MAX;
    CheckDriverRound round = known != NULL ? CHECK_DRIVER_ROUND_ERRORS : CHECK_DRIVER_ROUND_FAILED;
    for (uint32_t r = 0; round == CHECK_DRIVER_ROUND_ERRORS && r < rounds; r += 1)
    {
        memset(mask, 0, (uint64_t)count + 1);
        memset(edits, 0, (uint64_t)count + 1);
        memset(bodies, 0, (uint64_t)count + 1);
        memset(fallback_edits, 0, (uint64_t)count + 1);
        memset(fallback_mask, 0, (uint64_t)count + 1);
        memset(skipped, 0, (uint64_t)count + 1);
        memset(casts, 0, (uint64_t)count + 1);
        bool edited = false;
        bool fallback_ready = true;
        bool fallback_edited = false;
        bool ready = true;
        for (uint32_t e = 0; ready && e < known_count; e += 1)
        {
            // The body whose tokens hold the error: the last one to begin at or before it.
            const uint32_t token = known[e];
            uint32_t low = 0;
            uint32_t high = body_count;
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2;
                const bool before = ranges[middle].begin <= token;
                low = before ? middle + 1 : low;
                high = before ? high : middle;
            }
            const bool bodied = low != 0 && token < ranges[low - 1].end;
            // An error in a function definition outside its body (its declarator's, its parameters') leaves the definition
            // out whole: the editor's smaller parts there (a parameter's declarator) would leave its body's uses undeclared.
            uint32_t top = token;
            for (uint32_t steps = 0; !bodied && top < count && parser->tree.parents[top] < count && steps < count; steps += 1)
            {
                top = parser->tree.parents[top];
            }
            const bool head = !bodied && top < count && parser->tree.roles[top] == PARSE_ROLE_CLOSE_BODY;
            // A recovered error outside every body (#1447, CheckInputReport.recovered: a definition's parameter's, #1835)
            // needs no part left out, nor does a typo's correction anywhere: the reading recovers it again, its declaration
            // valid, as clang keeps it. It still counts in its scope (error_counts). (A prototype's dropped declaration would
            // leave out the bodies that name its parameters' names too.)
            const bool recovered = token < count && check_driver_rejected_recovered(first_input, token, bodied);
            // A conversion that defaults to an error (#1969) is kept, its source cast, where its tokens are all kept; where
            // the parts left out before it hold them all, it is left out with them. (Its sites come after the others' errors.)
            const CheckDriverConversionSite* site = NULL;
            for (uint32_t i = 0; e >= scoped_count && e < error_count && site == NULL && i < site_count; i += 1)
            {
                site = sites[i].token == token ? sites + i : NULL;
            }
            uint32_t site_kept = 0;
            for (uint32_t at = site != NULL ? site->first : 0; site != NULL && at <= site->last; at += 1)
            {
                site_kept += edits[at] == CHECK_DRIVER_EDIT_KEEP;
            }
            const bool cast = site != NULL && site_kept == site->last - site->first + 1;
            const bool covered = site != NULL && site_kept == 0;
            if (cast)
            {
                casts[site->first] |= site->pointer ? CHECK_DRIVER_CAST_POINTER : CHECK_DRIVER_CAST_INTEGER;
                casts[site->last] |= CHECK_DRIVER_CAST_CLOSE;
            }
            else if (site != NULL && !covered && quiet != NULL)
            {
                // Left out instead: its scope counts it.
                quiet[token] &= (uint8_t)~4u;
            }
            // (A body's error the editor refuses leaves its body out alone, below, not its whole definition.)
            const bool kept = cast || covered || (!recovered && token < count &&
                              ((!head && check_driver_rejected_edit(&editor, token)) ||
                               (!bodied && check_driver_rejected_declaration(parser, first_input, ranges, body_count, token, quiet, edits, mask))));
            edited |= kept;
            ready = kept || bodied || recovered;
            const bool dropped = !recovered && !bodied && token < count &&
                                 check_driver_rejected_declaration(parser, first_input, ranges, body_count, token, quiet, fallback_edits, fallback_mask);
            fallback_ready &= bodied || dropped || recovered;
            fallback_edited |= dropped;
            if (bodied)
            {
                memset(bodies + ranges[low - 1].begin, 1, ranges[low - 1].end - ranges[low - 1].begin);
                memset(fallback_mask + ranges[low - 1].begin, 1, ranges[low - 1].end - ranges[low - 1].begin);
            }
            if (!kept && !recovered && bodied)
            {
                memset(mask + ranges[low - 1].begin, 1, ranges[low - 1].end - ranges[low - 1].begin);
            }
        }
        // Past the error limit (#1447): clang's output stops at its cut, the error after the limit's (the first one, with
        // -Wfatal-errors). The known errors' tokens may be several for one error (its position, its program's token), so
        // the cut is counted in the bodies that hold one, each at least an error: the bodies after the one that brings
        // them past the limit are past clang's cut, left out whole, and what those hold no longer asks for rounds.
        const uint32_t cut_index = pp->options.fatal_errors ? 0 : pp->options.error_limit;
        const bool limited = pp->options.fatal_errors || pp->options.error_limit != 0;
        uint32_t errored_bodies = 0;
        for (uint32_t b = 0; limited && b < body_count; b += 1)
        {
            if (errored_bodies > cut_index)
            {
                memset(mask + ranges[b].begin, 1, ranges[b].end - ranges[b].begin);
                memset(bodies + ranges[b].begin, 1, ranges[b].end - ranges[b].begin);
                memset(fallback_mask + ranges[b].begin, 1, ranges[b].end - ranges[b].begin);
            }
            else
            {
                errored_bodies += bodies[ranges[b].begin] != 0;
            }
        }
        ready = ready && check_lazy_mask_valid(parser, mask);
        // The view, where an error's part is left out. Where its reading isn't valid (a statement expression's last
        // statement left out makes its value void) or the view can't be made, every error's body is left out instead, as
        // where none is edited: what that reading reads, the first did too.
        uint32_t added = 0;
        uint32_t* fresh = known + known_count;
        const uint32_t fresh_capacity = (uint32_t)(known_capacity - known_count);
        // The left-out statements' complete operands (#2120); where the reading with them isn't valid, it is read again
        // without them.
        memset(error_marks, 0, (uint64_t)count + 1);
        for (uint32_t e = 0; e < known_count; e += 1)
        {
            error_marks[known[e] < count ? known[e] : count] = 1;
        }
        const bool operands = ready && edited && check_driver_rejected_operands(parser, edits, error_marks, known, known_count, casts);
        round = ready && edited ? check_driver_rejected_read(unit, original, pp, first_input, known, known_count, ranges, body_count, bodies, mask, edits, casts, skipped, false,
                                                             scalar,
                                                             return_warning, return_baseline, shifts, first, first_graph, stop, publication_end, fresh,
                                                             fresh_capacity, &added, quiet, arena, scratch) :
                                  CHECK_DRIVER_ROUND_FAILED;
        if (operands && round == CHECK_DRIVER_ROUND_FAILED)
        {
            for (uint32_t token = 0; token <= count; token += 1)
            {
                casts[token] &= (uint8_t)~(CHECK_DRIVER_CAST_OPERAND | CHECK_DRIVER_CAST_OPERAND_FIRST | CHECK_DRIVER_CAST_OPERAND_LAST);
            }
            added = 0;
            round = check_driver_rejected_read(unit, original, pp, first_input, known, known_count, ranges, body_count, bodies, mask, edits, casts, skipped, false, scalar,
                                               return_warning, return_baseline, shifts, first, first_graph, stop, publication_end, fresh, fresh_capacity, &added,
                                               quiet, arena, scratch);
        }
        uint8_t* fallback = !edited ? mask : fallback_ready ? fallback_mask : NULL;
        const uint8_t* fallback_view = edited && fallback_edited ? fallback_edits : NULL;
        // Its errors are the unit's only where no left-out declaration defines a tag outside every body (`struct T { … }
        // g(...)`), whose later uses would read an incomplete type clang doesn't: else an error it finds isn't taken.
        bool whole = true;
        uint32_t next_body = 0;
        for (uint32_t token = 0; whole && fallback_view != NULL && token < count; token += 1)
        {
            next_body += next_body < body_count && ranges[next_body].end <= token;
            const bool in_body = next_body < body_count && ranges[next_body].begin <= token && token < ranges[next_body].end;
            const uint8_t role = parser->tree.roles[token];
            whole = fallback_view[token] == CHECK_DRIVER_EDIT_KEEP || in_body || (role != PARSE_ROLE_OPEN_STRUCT && role != PARSE_ROLE_OPEN_ENUM);
        }
        // Nor where a left-out declaration declares a name a kept one declares too (a function's other declaration): the
        // kept one's checks against it are gone, and an error it finds alone (multiversioning's attributes) isn't clang's.
        for (uint32_t token = 0; whole && fallback_view != NULL && token < count; token += 1)
        {
            const bool declared = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && first_input->declarations[token] == token;
            const uint32_t name = parser->tokens.names[token];
            for (uint32_t other = 0; whole && declared && fallback_view[token] != CHECK_DRIVER_EDIT_KEEP && other < count; other += 1)
            {
                whole = !(fallback_view[other] == CHECK_DRIVER_EDIT_KEEP && parser->tokens.kinds[other] == PARSE_KIND_IDENTIFIER &&
                          parser->tokens.names[other] == name && first_input->declarations[other] == other);
            }
        }
        if (ready && round == CHECK_DRIVER_ROUND_FAILED && fallback != NULL && check_lazy_mask_valid(unit, fallback))
        {
            round = check_driver_rejected_read(unit, original, pp, first_input, known, known_count, ranges, body_count, bodies, fallback, fallback_view, NULL, NULL, whole,
                                               scalar,
                                               return_warning, return_baseline, shifts, first, first_graph, stop, publication_end, fresh, fresh_capacity,
                                               &added, quiet, arena, scratch);
        }
        known_count += added;
    }
}

static void check_driver_dump(const CheckGraph* graph, const CheckStream* stream, TypeTable* types, Arena* scratch)
{
    const TypeNames names = { 0 };
    uint32_t effect = 0;
    uint32_t varargs = 0;
    for (uint32_t node = 0; node < stream->count; node += 1)
    {
        const uint64_t mark = scratch->length;
        const CheckValue checked_value = check_stream_value(stream, node);
        const CheckValue* value = &checked_value;
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

// A function object's name, from one of its declarations: its spelling and length.
static const uint8_t* check_driver_function_name(const CheckProgram* program, const Parser* parser, const InternTable* identifiers, uint32_t object,
                                                 uint32_t* length)
{
    const uint32_t declaration = object < program->object_count ? program->object_declarations[object] : BUSTER_CHECK_NONE;
    const uint32_t name = declaration < parser->tokens.count ? parser->tokens.names[declaration] : 0;
    *length = intern_length(identifiers, name);
    return intern_bytes(identifiers, name);
}

// Code generation's check of target features (#775, check_program_target_features): clang's errors, at -c and when it
// links but not at -fsyntax-only, for the calls of always_inline functions, and a flatten function's calls, whose callees
// need features their callers lack, and the x86 builtins' calls whose callers lack their features (#148), in clang's
// words. Clang reports them as it emits their function: after its closing
// brace and the lookahead past it (as the x86 builtins' deferred range errors, from `original`'s tree), or at the
// unit's end for a function it defers. Returns false after an error.
static bool check_driver_target_features(CheckProgram* program, const Parser* parser, const Parser* original, Preprocessor* pp, const PredefinesOptions* predefines,
                                         bool scalar, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    CheckTargetFeatureError* errors = NULL;
    uint32_t count = 0;
    const bool valid = check_program_target_features(program, &pp->options, predefines, scalar, output, scratch, &errors, &count);
    if (!valid)
    {
        check_driver_diagnose(parser, pp, UINT32_MAX, "checker program capacity exhausted");
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const CheckTargetFeatureError* error = errors + i;
        uint32_t caller_length = 0;
        uint32_t callee_length = 0;
        const uint8_t* caller = check_driver_function_name(program, parser, pp->table, error->caller, &caller_length);
        const uint8_t* callee = check_driver_function_name(program, parser, pp->table, error->callee, &callee_length);
        const char* feature = predefines_features[error->feature].name;
        char message[1024];
        if (error->builtin != NULL)
        {
            // An SSE compare's predicate past 7 needs AVX (check_target_feature_predicate), else the builtin's features.
            const char* features = error->predicate ? "avx" : error->builtin->features;
            const uint32_t features_length = error->predicate ? 3u : error->builtin->features_length;
            snprintf(message, sizeof(message), "'%.*s' needs target feature %.*s", (int)error->builtin->name_length, error->builtin->name,
                     (int)features_length, features);
        }
        else if (error->flatten)
        {
            snprintf(message, sizeof(message), "flatten function '%.*s' calls '%.*s' which requires target feature '%s', but the caller is compiled without support for '%s'",
                     (int)caller_length, (const char*)caller, (int)callee_length, (const char*)callee, feature, feature);
        }
        else
        {
            snprintf(message, sizeof(message),
                     "always_inline function '%.*s' requires target feature '%s', but would be inlined into function '%.*s' that is compiled without support for '%s'",
                     (int)callee_length, (const char*)callee, feature, (int)caller_length, (const char*)caller, feature);
        }
        uint32_t publication = pp->output.count + 1u;
        for (uint32_t at = error->token, steps = 0; !error->deferred && at < original->tokens.count && steps < original->tokens.count; steps += 1)
        {
            if (original->tree.roles[at] == PARSE_ROLE_CLOSE_BODY && original->tokens.kinds[at] == PARSE_KIND_R_BRACE)
            {
                publication = check_driver_publication_position(original, pp, at + 1u, pp->output.count);
                break;
            }
            at = original->tree.parents[at];
        }
        if (error->token < parser->tokens.count)
        {
            pp_diagnose_output_phase(pp, PP_SEVERITY_ERROR, parser->tokens.sources[error->token], publication, PP_DIAGNOSTIC_DEFERRED, "%s", message);
        }
        else
        {
            check_driver_diagnose(parser, pp, error->token, message);
        }
    }
    arena_pop_to(output, mark);
    return valid && count == 0;
}

// The name clang gives an overloadable function's symbol where no asm label renames it, its mangled one
// (check_mangle_name), in `output`: an empty one for an object that isn't an overloadable function's, or whose signature
// the mangling refuses.
static String8 check_driver_overload_name(const CheckProgram* program, const Parser* parser, const InternTable* identifiers, uint32_t object, Arena* output)
{
    const CheckGraph* graph = &program->input->graph;
    String8 spelling = { 0 };
    for (uint32_t i = 0; spelling.length == 0 && i < graph->overload_candidate_count; i += 1)
    {
        const CheckOverloadCandidate* candidate = graph->overload_candidates + i;
        if ((candidate->flags & BUSTER_CHECK_OVERLOADABLE) != 0 && candidate->object == object)
        {
            CheckMangleSource mangling = {
                .input = program->input,
                .parser = parser,
                .identifiers = identifiers,
                .types = program->types,
                .output = output,
                .scalar = parser->options.scalar,
                .internal = check_input_linkage(program->input->declaration_bits, candidate->declaration) == 1,
                .object_size_compat = candidate->object_sizes != NULL,
            };
            Scratch work = scratch_begin(output);
            check_mangle_name(&mangling, candidate->type, parser->tokens.names[candidate->declaration], candidate->declaration, work.arena, &spelling);
            scratch_end(work);
        }
    }
    return spelling;
}

// One of check_driver_alias_warnings' warnings, as the warning options and the source's pragmas map it at `token`, after the
// errors and warnings published before it: whether it is an error now (`errors` counts it).
static void check_driver_alias_warning(const CheckProgram* program, const Parser* parser, Preprocessor* pp, uint32_t token, uint32_t id, const char* message,
                                       const CheckCommonNote* notes, uint32_t note_count, Arena* scratch, uint32_t* errors)
{
    const CheckWarningSeverity severity = token < parser->tokens.count && !pp->fatal ? check_common_severity(pp, parser, program->input, token, id, false) : CHECK_WARNING_IGNORE;
    if (severity != CHECK_WARNING_IGNORE)
    {
        check_common_publish(parser, pp, token, pp->output.count + 1u, PP_DIAGNOSTIC_SEMANTIC, id, severity, message, notes, note_count, scratch);
    }
    *errors += severity >= CHECK_WARNING_ERROR ? 1 : 0;
}

// The warnings of clang's code generation at an alias or ifunc it accepts (#1849, CodeGenModule::checkAliasedGlobal and
// checkAliases), in its order: an alias of a function whose type isn't compatible with its aliasee's definition's
// (-Wattribute-alias, at the alias attribute, with the definition noted); a section attribute (its first) whose name
// isn't the aliasee's or the resolver's (the section attribute's, or none), at it; and an immediate target that is a
// weak alias, which the symbol doesn't follow, at the attribute. Returns false without room.
static bool check_driver_alias_warnings(const CheckProgram* program, const Parser* parser, Preprocessor* pp, const CheckProgramAlias* alias, Arena* scratch,
                                        uint32_t* errors)
{
    const CheckInput* input = program->input;
    const bool ifunc = alias->kind == CHECK_PROGRAM_IFUNC;
    const uint32_t at = alias->attribute < parser->tokens.count ? alias->attribute : 0;
    const uint32_t definition = alias->definition;
    bool valid = true;
    const bool functions = !ifunc && definition < input->token_count && input->declaration_kinds[alias->token] == C_BINDING_FUNCTION &&
                           input->declaration_kinds[definition] == C_BINDING_FUNCTION;
    const TypeId own = functions ? input->declared_types[alias->token] : 0;
    const TypeId aliased = functions ? input->declared_types[definition] : 0;
    if (functions && own != 0 && aliased != 0 && type_compatible(program->types, own, aliased) == TYPE_ANSWER_NO)
    {
        CheckWarningSpelling spell = { 0 };
        const String8 left = check_driver_warning_type(parser, pp, input, program->types, &spell, scratch, own, 0);
        const String8 right = check_driver_warning_type(parser, pp, input, program->types, &spell, scratch, aliased, 0);
        const char* message = left.length != 0 && right.length != 0 ?
                              check_warning_text(scratch, "alias and aliasee have different types %.*s and %.*s", (int)left.length, left.pointer, (int)right.length,
                                                 right.pointer) : NULL;
        valid = message != NULL;
        const CheckCommonNote note = { .token = definition, .message = "aliasee is declared here" };
        if (valid)
        {
            check_driver_alias_warning(program, parser, pp, at, CHECK_WARNING_ID_ATTRIBUTE_ALIAS, message, &note, 1, scratch, errors);
        }
    }
    const CheckInputAttributes* attributes = check_input_attributes_at(input, alias->token);
    const CheckInputAttribute* section = NULL;
    for (uint32_t a = 0; attributes != NULL && a < attributes->count; a += 1)
    {
        section = section == NULL && !attributes->records[a].ignored && attributes->records[a].kind == CHECK_ATTRIBUTE_SECTION ? attributes->records + a : section;
    }
    const CheckInputAttributes* base_attributes = section != NULL && definition < input->token_count ? check_input_attributes_at(input, definition) : NULL;
    String8 placed = { 0 };
    for (uint32_t a = 0; base_attributes != NULL && placed.pointer == NULL && a < base_attributes->count; a += 1)
    {
        placed = !base_attributes->records[a].ignored && base_attributes->records[a].kind == CHECK_ATTRIBUTE_SECTION ? base_attributes->records[a].text : placed;
    }
    if (valid && section != NULL && (placed.length != section->text.length || (placed.length != 0 && memcmp(placed.pointer, section->text.pointer, placed.length) != 0)))
    {
        const char* message = check_warning_text(scratch, ifunc ? "ifunc will not be in section '%.*s' but in the same section as the resolver" :
                                                                  "alias will not be in section '%.*s' but in the same section as the aliasee",
                                                  (int)section->text.length, section->text.pointer);
        valid = message != NULL;
        if (valid)
        {
            check_driver_alias_warning(program, parser, pp, section->name_token, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, message, NULL, 0, scratch, errors);
        }
    }
    // The immediate target: an alias of the unit's that a weak attribute (or `#pragma weak`) makes weak.
    const uint32_t entry = alias->target_object != 0 ? check_program_alias_of(program->aliases, program->alias_count, alias->target_object) : 0;
    const CheckProgramAlias* target = entry != 0 && program->aliases[entry - 1].kind == CHECK_PROGRAM_ALIAS ? program->aliases + entry - 1 : NULL;
    bool weak = false;
    for (uint32_t d = 0; target != NULL && d < 2; d += 1)
    {
        const uint32_t declaration = d == 0 ? target->token : program->object_declarations[target->object];
        const CheckInputAttributes* records = declaration < input->token_count ? check_input_attributes_at(input, declaration) : NULL;
        for (uint32_t a = 0; records != NULL && a < records->count; a += 1)
        {
            weak |= !records->records[a].ignored && records->records[a].kind == CHECK_ATTRIBUTE_WEAK;
        }
    }
    if (valid && weak)
    {
        const char* message = check_warning_text(scratch, "%s will always resolve to %.*s even if weak definition of %.*s is overridden", ifunc ? "ifunc" : "alias",
                                                  (int)alias->aliasee_name.length, alias->aliasee_name.pointer, (int)alias->target.length, alias->target.pointer);
        valid = message != NULL;
        if (valid)
        {
            check_driver_alias_warning(program, parser, pp, at, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, message, NULL, 0, scratch, errors);
        }
    }
    return valid;
}

// Code generation's check of GNU's aliases and ifuncs (#147, CheckProgram.aliases): clang's errors, at -c and when it
// links but not at -fsyntax-only, as CodeGenModule::checkAliases reports them at the unit's end, at each one's attribute,
// in the order clang emits them (by their first declarations with the attribute), with clang's notes. A shape the
// emission refuses by name (CHECK_PROGRAM_ALIAS_THREAD_LOCAL) is the emission's to report.
// But an alias or ifunc that names itself stops clang's code generation at its first declaration with the attribute
// (EmitAliasDefinition and emitIFuncDefinition report its cycle at once, after which no later declaration is emitted
// and checkAliases doesn't run): the first such one's error is then the unit's only one. Returns false after an error.
static bool check_driver_aliases(const CheckProgram* program, const Parser* parser, Preprocessor* pp, bool scalar, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    // Two functions' definitions of one symbol name through an asm label (#1979, CheckProgram.label_conflict): clang's
    // error at the later one as code generation emits it, before its check of the aliases.
    const uint32_t conflict = program->label_conflict != 0 && program->label_conflict - 1 < parser->tokens.count ? program->label_conflict - 1 : BUSTER_CHECK_NONE;
    if (conflict != BUSTER_CHECK_NONE)
    {
        const String8 name = program->label_conflict_name;
        const PPOutputDiagnosticNote note = {
            .token = parser->tokens.sources[program->label_previous < parser->tokens.count ? program->label_previous : conflict],
            .message = "previous definition is here",
        };
        pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, parser->tokens.sources[conflict], pp->output.count + 1u, &note, 1,
                                    "definition with same mangled name '%.*s' as another definition", (int)name.length, name.pointer);
    }
    uint64_t* keys = program->alias_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)program->alias_count * 2) : 0;
    bool valid = program->alias_count == 0 || keys != 0;
    // The first alias or ifunc that names itself, by its first declaration with the attribute, or none.
    uint32_t itself = BUSTER_CHECK_NONE;
    uint32_t itself_token = BUSTER_CHECK_NONE;
    for (uint32_t k = 0; valid && k < program->alias_count; k += 1)
    {
        const CheckProgramAlias* alias = program->aliases + k;
        const bool named = alias->kind != CHECK_PROGRAM_WEAKREF && alias->target_object != 0 && alias->target_object == alias->object;
        itself = named && alias->token < itself_token ? k : itself;
        itself_token = named && alias->token < itself_token ? alias->token : itself_token;
    }
    // Each alias and ifunc that clang emits, with its error, or else its warnings (#1849): unless one names itself, which
    // stops clang's code generation before its check.
    uint32_t count = 0;
    uint32_t errors = 0;
    for (uint32_t k = 0; valid && k < program->alias_count; k += 1)
    {
        const CheckProgramAlias* alias = program->aliases + k;
        const bool unemitted = (program->alias_states[k] & BUSTER_CHECK_PROGRAM_ALIAS_UNEMITTED) != 0;
        const bool refused = alias->error == CHECK_PROGRAM_ALIAS_THREAD_LOCAL;
        const bool reported = itself != BUSTER_CHECK_NONE ? k == itself :
                                                            alias->kind != CHECK_PROGRAM_WEAKREF && ((alias->error != CHECK_PROGRAM_ALIAS_OK && !refused) || unemitted);
        const bool checked = itself == BUSTER_CHECK_NONE && alias->kind != CHECK_PROGRAM_WEAKREF && !refused;
        if (reported || checked)
        {
            keys[count++] = (uint64_t)alias->token << 32 | k << 1 | (reported ? 1u : 0u);
        }
    }
    const uint64_t* sorted = count > 1 ? c_bindings_sort(keys, keys + program->alias_count, count, !BUSTER_SCALAR_PATH(scalar), UINT32_MAX) : keys;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const CheckProgramAlias* alias = program->aliases + ((uint32_t)sorted[i] >> 1);
        if ((sorted[i] & 1) == 0)
        {
            valid = check_driver_alias_warnings(program, parser, pp, alias, scratch, &errors);
            continue;
        }
        errors += 1;
        const bool ifunc = alias->kind == CHECK_PROGRAM_IFUNC;
        const CheckProgramAliasError error = itself != BUSTER_CHECK_NONE ? CHECK_PROGRAM_ALIAS_CYCLE :
                                             alias->error != CHECK_PROGRAM_ALIAS_OK ? (CheckProgramAliasError)alias->error : CHECK_PROGRAM_ALIAS_UNDEFINED;
        const char* message = error == CHECK_PROGRAM_ALIAS_CYCLE ? (ifunc ? "ifunc definition is part of a cycle" : "alias definition is part of a cycle") :
                              error == CHECK_PROGRAM_ALIAS_TO_VARIABLE ? "cannot alias a variable with a function" :
                              error == CHECK_PROGRAM_ALIAS_TO_FUNCTION ? "cannot alias a function with a variable" :
                              error == CHECK_PROGRAM_ALIAS_RESOLVER ? "ifunc resolver function must return a pointer" :
                              ifunc ? "ifunc must point to a defined function" : "alias must point to a defined variable or function";
        const uint32_t at = alias->attribute < parser->tokens.count ? alias->attribute : 0;
        const uint32_t aliasee = alias->definition < parser->tokens.count ? alias->definition : 0;
        // An undefined one's notes: that the name must be the mangled one, and for each object whose identifier its
        // aliasee's name spells and that clang gives a name (MangledDeclNames' alternatives: the program's symbols, the
        // definitions and referenced declarations, and the aliasee where the selection leaves out its definition, an
        // inline one's), that name: another where an asm label renames it, or the same. Their messages are in `scratch`.
        const bool undefined = error == CHECK_PROGRAM_ALIAS_UNDEFINED;
        const String8 named = alias->aliasee_name;
        const IdentId spelled = undefined && named.length != 0 && named.length < UINT32_MAX ? intern_find(pp->table, (const uint8_t*)named.pointer, (uint32_t)named.length) : 0;
        const bool unselected = spelled != 0 && alias->aliasee != 0 && program->object_symbols[alias->aliasee] == 0 && alias->definition != BUSTER_CHECK_NONE;
        uint32_t alternatives = unselected ? 1 : 0;
        for (uint32_t s = 0; spelled != 0 && s < program->symbol_count; s += 1)
        {
            const uint32_t declaration = program->object_declarations[program->symbol_objects[s]];
            alternatives += declaration < parser->tokens.count && parser->tokens.names[declaration] == spelled;
        }
        PPOutputDiagnosticNote* notes = BUSTER_ARENA_PUSH_ARRAY(scratch, PPOutputDiagnosticNote, (uint64_t)alternatives + 1);
        valid = notes != 0;
        uint32_t note_count = 0;
        if (valid && (undefined || error == CHECK_PROGRAM_ALIAS_TO_VARIABLE || error == CHECK_PROGRAM_ALIAS_TO_FUNCTION))
        {
            notes[note_count++] = (PPOutputDiagnosticNote){
                .token = parser->tokens.sources[undefined ? at : aliasee],
                .message = undefined ? (ifunc ? "the function specified in an ifunc must refer to its mangled name" :
                                                "the function or variable specified in an alias must refer to its mangled name") :
                                       "aliasee is declared here",
            };
        }
        for (uint32_t s = 0; valid && alternatives != 0 && s < program->symbol_count; s += 1)
        {
            const uint32_t declaration = program->object_declarations[program->symbol_objects[s]];
            if (declaration < parser->tokens.count && parser->tokens.names[declaration] == spelled)
            {
                const String8 label = check_program_alias_label(program->input, declaration);
                const String8 mangled = label.length == 0 ? check_driver_overload_name(program, parser, pp->table, program->symbol_objects[s], scratch) : (String8){ 0 };
                const String8 name = label.length != 0   ? label :
                                     mangled.length != 0 ? mangled :
                                                           (String8){ .pointer = (char*)intern_bytes(pp->table, spelled), .length = intern_length(pp->table, spelled) };
                char* text = BUSTER_ARENA_PUSH_ARRAY(scratch, char, name.length + 64);
                valid = text != 0;
                if (valid)
                {
                    snprintf(text, name.length + 64, "function by that name is mangled as \"%.*s\"", (int)name.length, name.pointer);
                    notes[note_count++] = (PPOutputDiagnosticNote){ .token = parser->tokens.sources[at], .message = text };
                }
            }
        }
        char* unselected_text = valid && unselected ? BUSTER_ARENA_PUSH_ARRAY(scratch, char, named.length + 64) : 0;
        valid = valid && (!unselected || unselected_text != 0);
        if (valid && unselected)
        {
            snprintf(unselected_text, named.length + 64, "function by that name is mangled as \"%.*s\"", (int)named.length, named.pointer);
            notes[note_count++] = (PPOutputDiagnosticNote){ .token = parser->tokens.sources[at], .message = unselected_text };
        }
        if (valid)
        {
            pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, parser->tokens.sources[at], pp->output.count + 1u, notes, note_count, "%s", message);
        }
    }
    if (!valid)
    {
        check_driver_diagnose(parser, pp, UINT32_MAX, "checker program capacity exhausted");
    }
    arena_pop_to(scratch, mark);
    return valid && errors == 0 && conflict == BUSTER_CHECK_NONE;
}

// Why check_program_emit failed, at its token: an unsupported construct names the feature it needs (#145), and the
// others are the compiler's own failures, with what the backend reported.
static void check_driver_emission_failed(const Parser* parser, Preprocessor* pp, const CheckProgramCode* code)
{
    char message[256];
    if (code->redefined.lines != 0)
    {
        // A file-scope label of a C definition's name (#1848, CheckProgramRedefined): clang's assembler errors, without a
        // source location, as many as clang prints.
        for (uint32_t line = 0; line < code->redefined.lines; line += 1)
        {
            fprintf(stderr, "error: symbol '%.*s' is already defined\n", (int)code->redefined.length, code->redefined.name);
        }
    }
    else if (code->tier1_failure.category != CHECK_TIER1_UNATTEMPTED)
    {
        check_tier1_diagnostic(message, sizeof(message), &code->tier1_failure);
    }
    else if (code->status == CHECK_PROGRAM_RESOURCE)
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
    if (code->redefined.lines == 0)
    {
        check_driver_diagnose(parser, pp, code->token, message);
    }
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
        const uint64_t templates = code->lowered.template_nodes;
        // #1048's node templates: the nodes and entries their closed subtrees gave, and the shares of the evaluated nodes
        // and of the lowering's entries.
        fprintf(stderr,
                "lowering-nodes\tevaluated=%" PRIu64 "\tbatched=%" PRIu64 "\tshare=%.2f%%\tclassified=%" PRIu64 "\ttemplates=%" PRIu64
                "\ttemplate-share=%.2f%%\ttemplate-entries=%" PRIu64 "\tentry-share=%.2f%%\n",
                nodes, batch, nodes != 0 ? 100.0 * (double)batch / (double)nodes : 0.0, code->lowered.classified_nodes, templates,
                nodes != 0 ? 100.0 * (double)templates / (double)nodes : 0.0, code->lowered.template_entries,
                code->lowered.entries != 0 ? 100.0 * (double)code->lowered.template_entries / (double)code->lowered.entries : 0.0);
    }
}

// A unit's link kept for the link of several units (c_checker_units.c), which check_driver_object fills.
typedef struct CheckUnitsKept CheckUnitsKept;
static bool check_units_keep_object(CheckUnitsKept* kept, const Object* object);
static bool check_units_keep(CheckUnitsKept* kept, const Link* link, const CheckProgram* program, const Parser* parser, Preprocessor* pp,
                             const ObjectDebug* debug);
static uint32_t check_units_group_unkept(const Link* link);

// Whether `link`'s symbol `s` is a local `.L` label defined in code, which -c's object leaves out as an assembler's
// temporary (#1845). The helpers' own functions (`.Lbuster_asm_N`, `.Lbuster_builtin_*`), which clang's code has no
// counterpart of, keep their symbols: the tools that read -c's objects find the helpers' calls by them.
static bool check_driver_code_temporary(const Link* link, uint32_t s)
{
    const NameId name = link->symbols.name[s];
    const AtomId atom = link->symbols.atom[s];
    return (link->symbols.flags[s] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL &&
           (link->symbols.flags[s] & BUSTER_LINK_SYMBOL_TYPE) != BUSTER_LINK_SYMBOL_FUNCTION && atom != 0 && atom < link->atoms.count &&
           link->atoms.class[atom] == BUSTER_LINK_CLASS_TEXT && name != 0 && link->names.length[name] >= 2 && link->names.bytes[name][0] == '.' &&
           link->names.bytes[name][1] == 'L';
}

// The object of a unit's emitted code (`code`, its static data published and its sections placed: `sections`, one per
// atom, or null), as -c writes it: its link checked, its temporaries (the jump tables' symbols and their labels', the
// unit's own) left out, as clang's .L labels are, with `debug`'s DWARF (or none), named after `source_path`. Its error
// is OBJECT_ERROR_FULL where the link isn't valid or memory ran out before the build. check_driver_object writes it;
// #1057's parity tests build their preparation's so, to compare it with -c's file byte for byte.
static Object check_driver_object_build(CheckProgramCode* code, ObjectAtomSection* sections, const ObjectDebug* debug, const char* source_path, Arena* scratch)
{
    ObjectUnits units = { 0 };
    ObjectSymbols symbols = { 0 };
    bool valid = link_check(&code->link) == LINK_ERROR_NONE && object_prepare(&units, &code->link, 1, scratch) && object_map_symbols(&symbols, &code->link, scratch);
    // The jump tables' symbols and their labels' (CheckProgramLowered.case_symbol_base) are the lowering's
    // temporaries, as clang's .LJTI and .LBB labels are the assembler's: the object leaves them out, and its
    // relocations name their sections' symbols instead (ObjectOptions.temporary_symbols). So are the patchable function
    // entries' labels (#1447, CheckProgramCode.patchable_symbol_base), as clang's .Ltmp ones.
    // And every local `.L` label in code (#1845): the labels of asm templates assembled by their own statements and of
    // naked functions and the address-taken labels' `.Lbuster_label_N` (clang's `.Ltmp`), all places in a function's
    // code, which an assembler never keeps.
    const uint32_t switch_symbols = code->lowered.table_count + code->lowered.case_symbol_count;
    const Link* link = &code->link;
    bool coded = false;
    for (uint32_t s = 1; valid && !coded && s < link->symbols.count; s += 1)
    {
        coded = check_driver_code_temporary(link, s);
    }
    const bool temporaries = switch_symbols != 0 || code->unit_temporary_count != 0 || code->patchable_count != 0 || coded;
    uint8_t* temporary = valid && temporaries ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, code->link.symbols.count) : 0;
    valid = valid && (!temporaries || temporary != 0);
    if (temporary != 0)
    {
        memset(temporary, 0, code->link.symbols.count);
        memset(temporary + code->lowered.case_symbol_base, 1, switch_symbols);
        memset(temporary + code->patchable_symbol_base, 1, code->patchable_count);
        for (uint32_t s = 0; s < code->unit_temporary_count; s += 1)
        {
            temporary[code->unit_temporary[s]] = 1;
        }
        for (uint32_t s = 1; coded && s < link->symbols.count; s += 1)
        {
            temporary[s] |= check_driver_code_temporary(link, s);
        }
    }
    Object result = { .error = OBJECT_ERROR_FULL };
    if (valid)
    {
        const String8 source = { .pointer = (char*)source_path, .length = strlen(source_path) };
        const ObjectOptions options = {
            .atom_sections = sections,
            .atom_section_count = sections != 0 ? code->link.atoms.count : 0,
            .temporary_symbols = temporary,
            .temporary_symbol_count = temporary != 0 ? code->link.symbols.count : 0,
            .debug = debug,
            .debug_count = debug != NULL ? 1 : 0,
        };
        result = object_build(&code->link, &units, &symbols, 0, options, source, scratch);
    }
    return result;
}

// The checked program's code and data, as -c writes them: its object at `output_path`, or with `kept` (one of several
// units that link into one executable, c_checker_units.c), its link, kept for theirs. Its functions compile on `lanes`
// (check_program_emit_lanes). With `debug` (-g), the object carries DWARF: the unit the executable's would
// (c_check_debug.c), relocated against the functions' sections (ObjectOptions.debug); and with `unwind`, its functions'
// unwind tables (check_program_emit's). With `stage_clock` (--time-stages), its passes' times. The driver's own errors go
// to `errors`.
static bool check_driver_object(const CheckProgram* program, const CheckDataPrepared* data, const Parser* parser,
                                 Preprocessor* pp, TypeTable* types, const char* source_path, const char* output_path,
                                 bool scalar, bool verify, bool dump_static, bool debug, bool unwind, CheckProgramLanes* lanes, Arena* scratch,
                                 CheckClock* stage_clock, FILE* errors, CheckUnitsKept* kept, bool foreign, bool* fatal)
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
    if (valid && debug)
    {
        valid = check_debug_build(&dwarf, program, &rows, parser, pp, BUSTER_SCALAR_PATH(scalar), &output, scratch);
        if (!valid)
        {
            fprintf(errors, "error: can't make the debug information: no memory, or a source name or file name DWARF can't hold\n");
        }
    }
    check_clock_mark(stage_clock, CHECK_STAGE_DEBUG);
    // A section group of the file-scope assembly's (#1848) whose signature is no global symbol it defines, which -c's
    // object has as clang's, has no name the units' selection keys (check_units_group_unkept): refused by name, at the
    // first file-scope statement, in a link of several C units.
    if (valid && kept != NULL && !foreign && check_units_group_unkept(&code.link) != 0)
    {
        code.status = CHECK_PROGRAM_UNSUPPORTED;
        code.reason = "file-scope assembly's section groups whose signature isn't a global symbol defined in the group, in a link of several units";
        code.token = code.lowered.asms.file_token;
        check_driver_emission_failed(parser, pp, &code);
        valid = false;
    }
    if (valid && kept != NULL)
    {
        const LinkError checked = link_check(&code.link);
        if (foreign)
        {
            const Object object = check_driver_object_build(&code, sections, debug ? &dwarf.object : NULL, source_path, scratch);
            valid = checked == LINK_ERROR_NONE && check_units_keep_object(kept, &object);
        }
        else
        {
            valid = checked == LINK_ERROR_NONE && check_units_keep(kept, &code.link, program, parser, pp, debug ? &dwarf.object : NULL);
        }
        if (checked != LINK_ERROR_NONE)
        {
            fprintf(errors, "internal compiler error: the unit's link isn't valid (link error %u)\n", (uint32_t)checked);
        }
        else if (!valid)
        {
            fprintf(errors, "fatal error: can't keep the unit's link: no memory\n");
            *fatal = true;
        }
    }
    else if (valid)
    {
        const Object object = check_driver_object_build(&code, sections, debug ? &dwarf.object : NULL, source_path, scratch);
        // The optional dump consumes the same resolved Link as the object writer. Validate and render it
        // before publishing the file, so a later diagnostic cannot leave a newly emitted rejected object.
        valid = object.error == OBJECT_ERROR_NONE && (!dump_static || check_driver_dump_link(&code.link, &emission));
        if (object.error == OBJECT_ERROR_NONE)
        {
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
    // A units' lane keeps lane 0's writer's columns for its next unit (check_program_lanes_settle, #442); a command of one
    // file is done with them.
    if (lanes != NULL && lanes->units_lane != NULL)
    {
        check_program_lanes_settle(lanes);
    }
    else
    {
        check_program_lanes_release(lanes);
    }
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

// A command of several units' flow (#905, c_checker_units.c), a LaneFlow of its session's lanes: lane 0 runs the units'
// driver alone (check_driver_units), and each of its wide parts is a round of a CheckUnitsRound kind: the units' run, the
// link's merge, the link's check and the executable's write. Lane 0 calls its round's operation itself, between
// lane_flow_round_begin and lane_flow_round_end; the others follow the rounds (check_units_follow), calling each by its
// kind, until lane 0 ends the flow (check_units_flow_end). A unit's compile reaches the driver's link through the
// one-unit driver, so lane 0 calling the operations by kind would close a cycle that the call graph check refuses. A
// round's lanes are those the worker budget gives of those it wants, taken before its setup and given back after it,
// before any serial work (check_units_slots, check_units_slots_back).
typedef struct CheckUnitsFlow CheckUnitsFlow;
typedef enum CheckUnitsRound
{
    CHECK_UNITS_ROUND_END,
    CHECK_UNITS_ROUND_UNITS,
    CHECK_UNITS_ROUND_MERGE,
    CHECK_UNITS_ROUND_CHECK,
    CHECK_UNITS_ROUND_WRITE,
    CHECK_UNITS_ROUND_FOREIGN,
} CheckUnitsRound;
struct CheckUnitsFlow
{
    LaneFlow rounds;
    // Whether a round's slots failed to go back to the worker budget, which fails the command.
    bool slots_lost;
};
static void check_units_flow_begin(CheckUnitsFlow* flow);
static void check_units_flow_end(CheckUnitsFlow* flow);
static void check_units_follow(CheckUnitsFlow* flow);
// A round's lanes: `wanted` of them as `budget` gives (all of them without one), in `lease`, which
// check_units_slots_back gives back, a failure to which `flow` (if any) records.
static uint32_t check_units_slots(WorkerBudget* budget, uint32_t wanted, WorkerBudgetLease* lease);
static void check_units_slots_back(CheckUnitsFlow* flow, WorkerBudgetLease* lease);

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

// The bound link's link_check: on `wanted` lanes, a round of several units' flow (#905) on those the worker budget gives
// (link_check_lanes, the same answer), or on the calling thread when there is one.
static LinkError check_driver_link_check(Link* link, CheckProgramLanes* lanes, uint32_t wanted)
{
    CheckDriverCheck check = { .link = link, .result = LINK_ERROR_NONE };
    WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
    const uint32_t granted = wanted > 1 && lanes->units_flow != NULL ? check_units_slots(lanes->budget, wanted, &lease) : 1;
    if (granted > 1)
    {
        lane_flow_round_begin(&lanes->units_flow->rounds, CHECK_UNITS_ROUND_CHECK, &check, granted);
        check_driver_check_main(&check);
        lane_flow_round_end(&lanes->units_flow->rounds);
    }
    else
    {
        check.result = link_check(link);
    }
    check_units_slots_back(lanes->units_flow, &lease);
    return check.result;
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

// Names an atom from a symbol attached to it, if it has one. This scan happens only after a failed link.
static const char* check_driver_atom_name(const Link* link, AtomId atom, char* buffer, uint64_t capacity)
{
    NameId name = 0;
    for (SymbolId symbol = 1; name == 0 && symbol < link->symbols.count; symbol += 1)
    {
        name = link->symbols.atom[symbol] == atom ? link->symbols.name[symbol] : 0;
    }
    return check_driver_link_name(link, name, buffer, capacity);
}

// The name of a writer phase, for a failure whose cause is the phase.
static const char* check_driver_executable_phase(ExecutablePhase phase)
{
    static const char* const names[EXECUTABLE_PHASES] = {
        "counting the link's atoms, symbols and fixups",
        "laying out the image",
        "filling the symbols and the GOT",
        "building the dynamic sections and unwind tables",
        "opening the output",
        "patching and writing the image",
        "finishing the output",
    };
    return phase < EXECUTABLE_PHASES ? names[phase] : "writing the executable";
}

// The reason a dynamic PIE can't relocate a fixup, from the writer's cause.
static const char* check_driver_executable_relocation(ExecutableCause cause)
{
    switch (cause)
    {
        case EXECUTABLE_CAUSE_RELOCATION_TEXT: return "a pointer in a section the loader doesn't relocate is a text relocation";
        case EXECUTABLE_CAUSE_RELOCATION_ABSOLUTE32: return "a 32-bit absolute value needs a load address";
        case EXECUTABLE_CAUSE_RELOCATION_DYNAMIC: return "the fixup can't reach a dynamic symbol";
        case EXECUTABLE_CAUSE_RELOCATION_ZERO: return "a PC-relative value to address 0 can't be relocated";
        case EXECUTABLE_CAUSE_RELOCATION_ABSOLUTE: return "a PC-relative or call value to an absolute symbol can't be relocated";
        case EXECUTABLE_CAUSE_RELOCATION_SHARED: return "a shared library can't relocate the fixup";
        default: return "the fixup can't be relocated";
    }
}

// Describes the writer's refusal, with the cause and the input it names: an atom, symbol or fixup, and the limit the
// writer compared with.
static void check_driver_executable_failure(const Link* link, const Runtime* runtime, uint32_t unit_base, const char* path, const ExecutableResult* result, FILE* out)
{
    char name[256];
    char label[320];
    const bool atom = result->atom != 0 && result->atom < link->atoms.count;
    const bool symbol = result->symbol != 0 && result->symbol < link->symbols.count;
    const char* atom_name = atom ? check_driver_atom_name(link, result->atom, name, sizeof(name)) : "";
    if (atom_name[0])
    {
        snprintf(label, sizeof(label), " ('%s')", atom_name);
    }
    else
    {
        label[0] = 0;
    }
    if (result->error == EXECUTABLE_ERROR_UNDEFINED && symbol)
    {
        fprintf(out, "error: undefined reference to '%s'\n", check_driver_link_name(link, link->symbols.name[result->symbol], name, sizeof(name)));
    }
    else if (result->error == EXECUTABLE_ERROR_FILE)
    {
        fprintf(out, "error: can't write %s: %s: %s\n", path, result->call != NULL ? result->call : "write", strerror(result->system_error));
    }
    else if ((result->error == EXECUTABLE_ERROR_RELOCATION || result->error == EXECUTABLE_ERROR_FIXUP) && result->fixup != 0 && result->fixup < link->fixups.count)
    {
        char origin[BUSTER_RUNTIME_PATH_BYTES + 512];
        fprintf(out, "error: can't link %s: %s: %s\n", path, check_driver_fixup_origin(link, runtime, unit_base, result->fixup, origin, sizeof(origin)),
                result->error == EXECUTABLE_ERROR_FIXUP ? "the relocation's value does not fit its field" : check_driver_executable_relocation(result->cause));
    }
    else if (result->error == EXECUTABLE_ERROR_LAYOUT && atom)
    {
        const uint64_t size = link->atoms.size[result->atom];
        const uint8_t atom_class = result->cause == EXECUTABLE_CAUSE_ATOM_CLASS && result->value < BUSTER_LINK_CLASS_COUNT ? (uint8_t)result->value
                                                                                                                          : link->atoms.class[result->atom];
        switch (result->cause)
        {
            case EXECUTABLE_CAUSE_ATOM_TOO_LARGE:
                fprintf(out, "error: can't link %s: atom %u%s is %" PRIu64 " bytes; the maximum atom size is %" PRIu64 " bytes\n", path, result->atom, label, size,
                        result->limit != 0 ? result->limit : (uint64_t)BUSTER_EXECUTABLE_MAX_ATOM_BYTES);
                break;
            case EXECUTABLE_CAUSE_ATOM_ALIGNMENT:
                fprintf(out, "error: can't link %s: atom %u%s has %" PRIu64 "-byte alignment; the maximum atom alignment is %" PRIu64 " bytes\n", path, result->atom, label,
                        result->value, result->limit != 0 ? result->limit : (uint64_t)BUSTER_EXECUTABLE_PAGE_BYTES);
                break;
            case EXECUTABLE_CAUSE_ATOM_ARRAY_ENTRY:
                fprintf(out, "error: can't link %s: atom %u%s of an init or fini array has %" PRIu64 " bytes; each atom must be whole 8-byte entries aligned to at most 8 bytes\n",
                        path, result->atom, label, size);
                break;
            case EXECUTABLE_CAUSE_ATOM_CLASS:
                if (atom_class == BUSTER_LINK_CLASS_INIT || atom_class == BUSTER_LINK_CLASS_FINI)
                {
                    fprintf(out, "error: can't link %s: atom %u%s is a .%s atom, which an executable without the C runtime has no section for\n", path, result->atom, label,
                            atom_class == BUSTER_LINK_CLASS_INIT ? "init" : "fini");
                }
                else
                {
                    fprintf(out, "error: can't link %s: atom %u%s is of section class %u, which this executable has no section for\n", path, result->atom, label, atom_class);
                }
                break;
            case EXECUTABLE_CAUSE_ATOM_ORDER:
                fprintf(out, "error: can't link %s: atom %u%s is at image offset 0x%" PRIx64 ", not aligned to its ordered section's %" PRIu64 "-byte alignment\n", path,
                        result->atom, label, result->value, result->limit);
                break;
            case EXECUTABLE_CAUSE_FAR_GOT:
                fprintf(out, "error: can't link %s: atom %u%s is far, and fixup %u to '%s' has no GOT slot\n", path, result->atom, label, result->fixup,
                        symbol ? check_driver_link_name(link, link->symbols.name[result->symbol], name, sizeof(name)) : "");
                break;
            case EXECUTABLE_CAUSE_ATOM_FAR:
                if (result->fixup != 0)
                {
                    fprintf(out, "error: can't link %s: atom %u%s is at image offset 0x%" PRIx64 ", which fixup %u to '%s' can't reach (the maximum is 0x%" PRIx64 ")\n",
                            path, result->atom, label, result->value, result->fixup,
                            symbol ? check_driver_link_name(link, link->symbols.name[result->symbol], name, sizeof(name)) : "", result->limit);
                }
                else
                {
                    fprintf(out, "error: can't link %s: atom %u%s is at image offset 0x%" PRIx64 ", which the writer's 32-bit addresses can't hold (the maximum is 0x%" PRIx64 ")\n",
                            path, result->atom, label, result->value, result->limit);
                }
                break;
            case EXECUTABLE_CAUSE_ATOM_TOTAL_TOO_LARGE:
                fprintf(out, "error: can't link %s: atom %u%s's bucket exceeds the image's address space\n", path, result->atom, label);
                break;
            default:
                fprintf(out, "error: can't link %s: atom %u%s (size %" PRIu64 " bytes) can't be placed in the image\n", path, result->atom, label, size);
                break;
        }
    }
    else if (result->error == EXECUTABLE_ERROR_LAYOUT)
    {
        switch (result->cause)
        {
            case EXECUTABLE_CAUSE_IMAGE_TOO_LARGE:
                fprintf(out, "error: can't link %s: the image would end at address 0x%" PRIx64 "; the maximum is 0x%" PRIx64 "\n", path, result->value, result->limit);
                break;
            case EXECUTABLE_CAUSE_TLS_TOO_LARGE:
                fprintf(out, "error: can't link %s: the TLS block is %" PRIu64 " bytes; the maximum is %" PRIu64 " bytes\n", path, result->value, result->limit);
                break;
            case EXECUTABLE_CAUSE_SECTION_NAMES_TOO_LARGE:
                fprintf(out, "error: can't link %s: .shstrtab is %" PRIu64 " bytes; the maximum is %" PRIu64 " bytes\n", path, result->value, result->limit);
                break;
            case EXECUTABLE_CAUSE_SYMBOL_FAR:
                fprintf(out, "error: can't link %s: symbol '%s' is at image offset 0x%" PRIx64 ", beyond the 32-bit references' reach (the maximum is 0x%" PRIx64
                             "); fixup %u refers to it\n",
                        path, symbol ? check_driver_link_name(link, link->symbols.name[result->symbol], name, sizeof(name)) : "", result->value, result->limit, result->fixup);
                break;
            case EXECUTABLE_CAUSE_FAR_GOT:
                fprintf(out, "error: can't link %s: symbol '%s' is far, and fixup %u has no GOT slot\n", path,
                        symbol ? check_driver_link_name(link, link->symbols.name[result->symbol], name, sizeof(name)) : "", result->fixup);
                break;
            case EXECUTABLE_CAUSE_ATOM_TOTAL_TOO_LARGE:
                fprintf(out, "error: can't link %s: the atoms' combined sizes overflow the image's address space\n", path);
                break;
            default:
                fprintf(out, "error: can't link %s: image, TLS or section-name layout exceeds executable limits (image end at most 0x%" PRIx64
                             ", TLS block at most %" PRIu64 " bytes, .shstrtab at most %" PRIu64 " bytes)\n",
                        path, (uint64_t)BUSTER_EXECUTABLE_MAX_IMAGE_ADDRESS, (uint64_t)BUSTER_EXECUTABLE_MAX_TLS_BYTES, (uint64_t)BUSTER_EXECUTABLE_SHSTRTAB_BYTES);
                break;
        }
    }
    else if (result->error == EXECUTABLE_ERROR_UNWIND && atom)
    {
        const uint64_t size = link->atoms.size[result->atom];
        switch (result->cause)
        {
            case EXECUTABLE_CAUSE_UNWIND_ATOM_TOO_LARGE:
                fprintf(out, "error: can't link %s: unwind atom %u%s is %" PRIu64 " bytes; the maximum atom size is %" PRIu64 " bytes\n", path, result->atom, label, size,
                        result->limit != 0 ? result->limit : (uint64_t)BUSTER_EXECUTABLE_MAX_ATOM_BYTES);
                break;
            case EXECUTABLE_CAUSE_UNWIND_RANGE:
                fprintf(out, "error: can't link %s: unwind atom %u%s (size %" PRIu64
                             " bytes) has an FDE whose initial location is farther than the table's 2147483647-byte range from .eh_frame_hdr\n",
                        path, result->atom, label, size);
                break;
            default:
                fprintf(out, "error: can't link %s: unwind atom %u%s (size %" PRIu64 " bytes) is not a sequence of CIE and FDE records .eh_frame_hdr can index\n", path,
                        result->atom, label, size);
                break;
        }
    }
    else if (result->error == EXECUTABLE_ERROR_UNWIND)
    {
        fprintf(out, "error: can't link %s: the unwind table can't be indexed while %s\n", path, check_driver_executable_phase(result->phase));
    }
    else if (result->error == EXECUTABLE_ERROR_ENTRY)
    {
        const char* entry = symbol ? check_driver_link_name(link, link->symbols.name[result->symbol], name, sizeof(name)) : "";
        if (entry[0] && result->cause == EXECUTABLE_CAUSE_ENTRY_NOT_TEXT)
        {
            const AtomId entry_atom = link->symbols.atom[result->symbol];
            fprintf(out, "error: can't link %s: entry point '%s' is defined in atom %u, which is not a code atom\n", path, entry, entry_atom);
        }
        else if (entry[0])
        {
            fprintf(out, "error: can't link %s: entry point '%s' is not defined\n", path, entry);
        }
        else
        {
            fprintf(out, "error: can't link %s: the entry point is not defined in code\n", path);
        }
    }
    else if (result->error == EXECUTABLE_ERROR_UNSUPPORTED && symbol)
    {
        const char* unsupported = check_driver_link_name(link, link->symbols.name[result->symbol], name, sizeof(name));
        switch (result->cause)
        {
            case EXECUTABLE_CAUSE_UNSUPPORTED_DEBUG_SYMBOL: fprintf(out, "error: can't link %s: debug information refers to unsupported symbol '%s'\n", path, unsupported); break;
            case EXECUTABLE_CAUSE_UNSUPPORTED_TLS_CALL:
                fprintf(out, "error: can't link %s: %s is called outside a general- or local-dynamic TLS access\n", path, unsupported);
                break;
            case EXECUTABLE_CAUSE_UNSUPPORTED_VERSION:
                fprintf(out, "error: can't link %s: symbol '%s' has unavailable version %" PRIu64 "\n", path, unsupported, result->value);
                break;
            case EXECUTABLE_CAUSE_UNSUPPORTED_BOUND:
                fprintf(out, "error: can't link %s: '%s' bounds an input section whose atoms one output section can't hold\n", path, unsupported);
                break;
            default: fprintf(out, "error: can't link %s: unsupported symbol '%s'\n", path, unsupported); break;
        }
    }
    else if (result->error == EXECUTABLE_ERROR_UNSUPPORTED)
    {
        switch (result->cause)
        {
            case EXECUTABLE_CAUSE_UNSUPPORTED_DEBUG: fprintf(out, "error: can't link %s: the debug information is not valid for this executable\n", path); break;
            case EXECUTABLE_CAUSE_UNSUPPORTED_SYMBOLS: fprintf(out, "error: can't link %s: the requested symbol table is not valid for this executable\n", path); break;
            case EXECUTABLE_CAUSE_UNSUPPORTED_TLS_SECTION:
                fprintf(out, "error: can't link %s: a thread-local symbol needs an empty %s section, which the link has none of\n", path,
                        result->value == BUSTER_LINK_CLASS_TDATA ? ".tdata" : ".tbss");
                break;
            case EXECUTABLE_CAUSE_UNSUPPORTED_STRING_TABLE:
                fprintf(out, "error: can't link %s: the symbol names are %" PRIu64 " bytes; the maximum is %" PRIu64 " bytes\n", path, result->value, result->limit);
                break;
            default:
                fprintf(out, "error: can't link %s: unsupported executable configuration while %s\n", path, check_driver_executable_phase(result->phase));
                break;
        }
    }
    else if (result->error == EXECUTABLE_ERROR_MEMORY)
    {
        const char* where = result->cause == EXECUTABLE_CAUSE_MEMORY_SHARED ? "allocating the linker's shared state" : check_driver_executable_phase(result->phase);
        fprintf(out, "error: can't link %s: linker scratch memory exhausted while %s\n", path, where);
    }
    else
    {
        fprintf(out, "error: can't link %s: the executable writer failed while %s\n", path, check_driver_executable_phase(result->phase));
    }
}

// The libraries the command line names, -lNAME (or -l NAME), and the directories it adds to their search, -LDIR (or
// -L DIR), each in its order, for the direct path to link (runtime_libraries_import).
typedef struct CheckDriverLibraries CheckDriverLibraries;
struct CheckDriverLibraries
{
    LinkerOptions linker;
    bool* required;
    String8* names;
    uint32_t count;
    String8* directories;
    uint32_t directory_count;
    // The positional files and -l requests in their original order, for mixed-input links (#1120).
    ForeignArgument* arguments;
    uint32_t argument_count;
};

#include "c_driver_named_libraries.c"

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
        arena_report_failure(stderr, "the libraries", UINT64_C(1) << 36);
    }
}

// The units of a command that links several (c_checker_units.c): their kept links, which check_units_merge joins into
// one, in the units' order, binding them as an emission's (check_program_emit) binds its link.
typedef struct CheckUnitsUnit CheckUnitsUnit;
static bool check_units_merge(Link* link, const CheckUnitsUnit* units, uint32_t count, CheckProgramBinding* binding, ExecutableDebug* debug, Arena* output,
                              Arena* bytes, Arena* scratch);
static bool check_units_merge_lanes(Link* link, const CheckUnitsUnit* units, uint32_t count, CheckProgramBinding* binding, ExecutableDebug* debug, Arena* output,
                                    Arena* bytes, Arena* scratch, uint32_t lanes, CheckUnitsFlow* flow, WorkerBudgetLease* lease);
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
                                     const CheckUnitsUnit* units, uint32_t unit_count, RuntimeKind runtime_kind)
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
        const SnapshotResult opened = runtime_open_cache(&runtime, cached ? cache : NULL, runtime_kind);
        // The libraries -l names, imported for this runtime, whose version indices theirs follow.
        static RuntimeLibraries named;
        const bool naming = libraries->count != 0 && opened.error == RUNTIME_ERROR_NONE;
        const RuntimeError imported = naming ? runtime_libraries_import_options(&named, &runtime, libraries->names, libraries->count, libraries->directories,
                                                                                libraries->directory_count, libraries->required) : RUNTIME_ERROR_NONE;
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
            arena_report_failure(stderr, "linking", UINT64_C(1) << 36);
        }
        check_clock_mark(stage_clock, CHECK_STAGE_RUNTIME);
        // The units' links merged report their own failures (check_units_merge), but binding's: on the units' lanes by the
        // vector path (#778), a round of their flow on those the worker budget gives (#905), and by its twin on one lane
        // or the scalar path.
        WorkerBudgetLease merge_lease = { .control = -1, .reply = -1, .child = -1 };
        const uint32_t merge_lanes = valid && units != NULL && lanes->units_flow != NULL && lanes->lanes > 1 && !BUSTER_SCALAR_PATH(scalar) ?
                                         check_units_slots(lanes->budget, lanes->lanes, &merge_lease) : 1;
        const bool emitted = !valid || (merge_lanes > 1   ? check_units_merge_lanes(&code.link, units, unit_count, &binding, debug ? &dwarf.attach : NULL, &output, &bytes,
                                                                                    scratch, merge_lanes, lanes->units_flow, &merge_lease) :
                                        units != NULL     ? check_units_merge(&code.link, units, unit_count, &binding, debug ? &dwarf.attach : NULL, &output, &bytes, scratch) :
                                                            check_program_emit_timed(&code, program, parser, pp->table, data->capacity, BUSTER_SCALAR_PATH(scalar), &output,
                                                                                     &bytes, scratch, &binding, debug ? &rows : NULL, unwind, verify, lanes, stage_clock));
        check_units_slots_back(lanes->units_flow, &merge_lease);
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
        if (valid)
        {
            // One C unit has no competing groups; a multi-unit merge has already selected them. Group metadata is
            // for relocatable objects and unit selection, and must not retain the atom count before runtime binding.
            code.link.groups = (LinkGroups){ 0 };
            // Binding takes the libraries between bind_begin, which check_program_emit ran, and bind_end.
            bind.libraries = naming ? &named : 0;
            bind.libraries_needed |= naming ? named.required : 0;
            valid = bind_end(&bind, &code.link);
            again = !valid && bind.error == BIND_ERROR_SNAPSHOT;
            if (!valid && !again)
            {
                check_driver_link_name(&code.link, bind.name, name, sizeof(name));
                // A unit's declaration has its place in the output, which the backend gave back (pp_release_output); a link
                // of several units asks their kept parts, and has no preprocessor here.
                if (units == NULL)
                {
                    pp_output_ensure(pp);
                }
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
        ExecutableRuntime static_inputs = { 0 };
        if (valid)
        {
            // Several units' bound link is checked on their lanes (#778).
            const LinkError checked = check_driver_link_check(&code.link, lanes, units != NULL && !BUSTER_SCALAR_PATH(scalar) ? lanes->lanes : 1);
            valid = checked == LINK_ERROR_NONE;
            if (valid && runtime_kind == RUNTIME_KIND_STATIC)
            {
                dynamic_static_inputs(&static_inputs, &runtime, &bind);
            }
            else if (valid && runtime_kind == RUNTIME_KIND_STATIC_PIE)
            {
                dynamic_static_pie_inputs(&dynamic, &static_inputs, &runtime, &bind);
            }
            else if (valid)
            {
                valid = dynamic_inputs(&dynamic, &runtime, &bind, &arena);
            }
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
        dynamic.runpath = libraries->linker.runpath;
        dynamic.old_dtags = libraries->linker.old_dtags;
        SymbolId entry = valid ? dynamic_global(&runtime, &bind, "_start") : 0;
        if (valid && libraries->linker.entry.pointer != NULL)
        {
            const String8 wanted = libraries->linker.entry;
            entry = 0;
            const uint32_t symbol_count = code.link.symbols.count;
            for (SymbolId s = 1; s < symbol_count; s += 1)
            {
                const NameId n = code.link.symbols.name[s];
                const AtomId atom = code.link.symbols.atom[s];
                const uint8_t binding_kind = code.link.symbols.flags[s] & BUSTER_LINK_SYMBOL_BINDING;
                if (n != 0 && atom != 0 && binding_kind != BUSTER_LINK_SYMBOL_LOCAL && code.link.names.length[n] == wanted.length &&
                    memcmp(code.link.names.bytes[n], wanted.pointer, wanted.length) == 0 && code.link.atoms.class[atom] == BUSTER_LINK_CLASS_TEXT)
                {
                    entry = s;
                }
            }
            valid = entry != 0;
            if (!valid)
            {
                fprintf(stderr, "error: linker entry '%.*s' is not a defined code symbol\n", (int)wanted.length, wanted.pointer);
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
                    .entry = entry,
                    .dynamic = runtime_kind != RUNTIME_KIND_STATIC ? &dynamic : NULL,
                    .runtime = runtime_kind == RUNTIME_KIND_STATIC || runtime_kind == RUNTIME_KIND_STATIC_PIE ? &static_inputs : NULL,
                    .debug = debug ? &dwarf.attach : NULL,
                    .symbols = selected != NULL ? &symbols : NULL,
                    .exact = BUSTER_SCALAR_PATH(scalar),
                },
            };
            // Several units' executable is written on their lanes (`lanes`' count), a round of their flow on those the
            // worker budget gives (#905); one unit's by its calling thread.
            WorkerBudgetLease write_lease = { .control = -1, .reply = -1, .child = -1 };
            const uint32_t write_lanes = units != NULL && lanes->lanes > 1 && lanes->units_flow != NULL ? check_units_slots(lanes->budget, lanes->lanes, &write_lease) : 1;
            bool wrote = true;
            if (write_lanes > 1)
            {
                lane_flow_round_begin(&lanes->units_flow->rounds, CHECK_UNITS_ROUND_WRITE, &write, write_lanes);
                check_driver_write_main(&write);
                lane_flow_round_end(&lanes->units_flow->rounds);
            }
            else
            {
                wrote = lane_run(1, check_driver_write_main, &write, 0);
            }
            check_units_slots_back(lanes->units_flow, &write_lease);
            valid = wrote && write.succeeded;
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
            if (!valid)
            {
                check_driver_executable_failure(&code.link, &runtime, units != NULL ? unit_count : 1, output_path, &write.result, stderr);
            }
        }
        check_clock_mark(stage_clock, CHECK_STAGE_WRITE);
        if (again && cached)
        {
            snapshot_discard(cache, runtime_kind);
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

// How many CPUs the process may run on (sched_getaffinity's), at most the lanes a command takes: the functions' lanes'
// default, which the command's session bounds by those CPUs' cores (BUSTER_LANE_SESSION_PER_CORE), and of which the
// bodies' tokens then decide how many pay (check_program_emit_lanes).
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
        pp_run_input(pp, path);
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

// Fault injection for test-check-units' reusable-driver child, set before any workers start and immutable until its
// command returns. Normal commands leave it null. Fail the named unit's type-table reservation, not its preprocessing.
static const char* check_driver_test_failed_types;

// What every translation unit of a command reads, and none writes: run_check_command_mode's options.
typedef struct CheckDriverCommand CheckDriverCommand;
struct CheckDriverCommand
{
    // The preprocessor's, from -E's options on the command line, for every unit.
    PPOptions options;
    // --scalar, --verify, --dump-typed, --dump-static, -w, -flazy-system-headers and --lower-all.
    bool scalar;
    bool verify;
    // --check-transitions: the checker's vector path by its memoized transitions (CheckOptions.transitions, #1050).
    bool check_transitions;
    bool dump;
    bool dump_static;
    bool quiet;
    bool lazy_system_headers;
    bool lower_all;
    // --deferred-bodies: a unit's function bodies read apart, on the command's lanes (#151, check_input_deferred_compile);
    // --no-deferred-bodies reads them in source order.
    bool deferred_bodies;
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
    // they took and how many lanes left their memory to the exit, after the units' diagnostics (c_checker_units.c).
    bool shared_parts;
    // --units-memory=BYTES, #1109's test option: the memory the units' lanes may hold at once, by their estimates, in
    // place of what the process may take, and the command reports how many units waited for it, after the units'
    // diagnostics (c_checker_units.c). 0 without it.
    uint64_t units_memory;
    // -c, and the default C command's link, unless -c, -fsyntax-only or a dump says otherwise.
    bool emit_object;
    bool link;
    RuntimeKind runtime_kind;
    // -g's source rows, with -c or a link, and the functions' unwind tables.
    bool source_rows;
    bool unwind_tables;
    // -o's file, or null.
    const char* output;
    // The direct path's -l libraries and -L directories.
    const CheckDriverLibraries* libraries;
    const CheckDriverInput* inputs;
    uint32_t source_count;
    bool mixed_inputs;
    bool assembly_intel;
    // --tier1 (c_check_tier1.c), and the report its units' emissions sum into, atomically: units compile on lanes; and
    // --tier1-refusals' stream, or null; and --tier1-no-rematerialize; and --tier1-gvn, which each unit's lanes take
    // too (check_units_compile), so that a command of several files runs the solver its option asked for; and
    // --tier1-no-lifetimes; and --tier1-no-inline (#1282).
    bool tier1;
    bool tier1_strict;
    bool tier1_no_rematerialize;
    bool tier1_gvn;
    bool tier1_no_lifetimes;
    bool tier1_no_inline;
    // --tier0-no-inline keeps Tier 0's calls of always_inline functions (#1398).
    bool tier0_no_inline;
    // --tier1-schedule=post|pre (#172): BUSTER_CHECK_TIER1_SCHEDULE_*.
    uint8_t tier1_schedule;
    CheckTier1Report* tier1_report;
    FILE* tier1_refusals;
    // The report its units' emissions sum their helper plans into, atomically (#1059's --helper-report), null without the
    // option, when no unit makes its report; and --helper-forms' families, whose operations take their helpers
    // (CheckProgramOptions.helper_forms).
    CheckHelperReport* helper_report;
    uint32_t helper_forms;
};

typedef struct CheckUnitsKept CheckUnitsKept;
static int check_driver_units(const CheckDriverCommand* command, char** inputs, uint32_t count, CheckProgramLanes* lanes, CheckClock* stage_clock, bool release);
static void check_units_admit(struct CheckUnitsLane* lane, uint32_t tokens);
static bool check_units_discard_huge(struct CheckUnitsLane* lane);
static bool check_units_unit_left(const struct CheckUnitsLane* lane);

// A unit's deferred bodies' lanes in the command's flow (#151, CheckDeferredFlow): the emission's lanes (`lanes`), whose
// worker budget's slots the bodies take under a lease of their own, and whether giving them back failed.
typedef struct CheckDriverDeferred CheckDriverDeferred;
struct CheckDriverDeferred
{
    CheckProgramLanes* lanes;
    WorkerBudgetLease lease;
    bool return_failed;
};

// A strict, ordered list of distinct CPUs already in the caller's affinity mask. A CPU list is an opt-in control
// for a reproducible campaign, so malformed or unavailable entries must not silently select a different placement.
static uint32_t check_driver_cpu_list(const char* text, uint32_t* cpus, uint32_t capacity)
{
    uint64_t allowed[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    const long bytes = syscall(SYS_sched_getaffinity, 0, sizeof(allowed), allowed);
    uint32_t count = 0;
    bool valid = text != NULL && bytes > 0;
    const char* p = text;
    while (valid && *p != 0)
    {
        uint32_t cpu = 0;
        bool digits = false;
        while (*p >= '0' && *p <= '9')
        {
            digits = true;
            cpu = cpu * 10u + (uint32_t)(*p++ - '0');
            if (cpu >= BUSTER_LANE_CPU_MASK_WORDS * 64u)
            {
                valid = false;
                break;
            }
        }
        valid = valid && digits && count < capacity && (allowed[cpu / 64] & (UINT64_C(1) << (cpu % 64))) != 0;
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            valid = cpus[i] != cpu;
        }
        if (valid)
        {
            cpus[count++] = cpu;
            if (*p == ',')
            {
                p += 1;
                valid = *p != 0;
            }
            else
            {
                valid = *p == 0;
            }
        }
    }
    return valid ? count : 0;
}

// The lanes a unit's reading may give its deferred bodies: the command's (its CPUs, --lanes N and --worker-budget N), no
// more than the reader has. Of those, its grant (check_driver_deferred_grant) takes what the session and the worker budget
// give, once the reader has counted the bodies' tokens: a small unit's never waits for the session's lanes.
static uint32_t check_driver_deferred_lanes(const CheckProgramLanes* lanes)
{
    const uint32_t result = lanes->lanes < BUSTER_CHECK_DEFERRED_MAX_LANES ? lanes->lanes : BUSTER_CHECK_DEFERRED_MAX_LANES;
    return result != 0 ? result : 1;
}

// The unit's tokens from which a round of one source file's flow may pay for a lane past the first: the emission's rule
// (check_program_emit_lanes) pays one per BUSTER_CHECK_EMIT_LANE_TOKENS of the bodies' tokens past twice the largest
// body's, and the deferred reader's (--deferred-bodies) takes the bodies apart only on BUSTER_CHECK_DEFERRED_COMPILE_LANES
// lanes, each past the first for BUSTER_CHECK_DEFERRED_LANE_TOKENS of them at the least; a unit has more tokens than its
// bodies.
#define BUSTER_CHECK_DRIVER_LANE_TOKENS BUSTER_CHECK_EMIT_LANE_TOKENS
_Static_assert(BUSTER_CHECK_DRIVER_LANE_TOKENS <= (BUSTER_CHECK_DEFERRED_COMPILE_LANES - 1) * BUSTER_CHECK_DEFERRED_LANE_TOKENS,
               "a unit whose bodies the deferred reader takes apart starts its lanes");

// One source file's lanes (#1444), which its session starts on demand (BUSTER_LANE_SESSION_ON_DEMAND): once the unit is
// preprocessed, if its `tokens` may pay for a second lane (BUSTER_CHECK_DRIVER_LANE_TOKENS), so that lane 1 counts the
// cores and creates the others while lane 0 parses and checks it. A smaller unit's -c creates no thread. The rules that
// ask for lanes start them too, before they ask (lane_session_start, which only its first call does): the emission's and
// the deferred reader's grant.
static void check_driver_start_lanes(const CheckProgramLanes* lanes, uint64_t tokens)
{
    if (lanes != NULL && lanes->flow != NULL && tokens >= BUSTER_CHECK_DRIVER_LANE_TOKENS)
    {
        lane_session_start();
    }
}

// CheckDeferredFlow.grant: of the `wanted` lanes, no more than the session asks for, by default one per core
// (lane_session_asked), those it started (lane_flow_lanes), and those the worker budget gives, whose slots it takes without
// waiting, as the emission takes its own (worker_budget_acquire). An on-demand session's lanes start here if they haven't.
static uint32_t check_driver_deferred_grant(void* context, uint32_t wanted)
{
    CheckDriverDeferred* deferred = context;
    CheckProgramLanes* lanes = deferred->lanes;
    const uint32_t most = check_driver_deferred_lanes(lanes);
    if (wanted > 1 && most > 1)
    {
        lane_session_start();
    }
    const uint32_t asked = lane_session_asked();
    uint32_t result = wanted < most ? wanted : most;
    result = asked < result ? asked : result;
    const uint32_t started = result > 1 ? lane_flow_lanes(lanes->flow) : 1;
    result = started < result ? started : result;
    deferred->lease = (WorkerBudgetLease){ .control = -1, .reply = -1, .child = -1 };
    if (result > 1 && lanes->budget != NULL)
    {
        result = worker_budget_acquire(lanes->budget, result, &deferred->lease);
    }
    return result;
}

// CheckDeferredFlow.give_back: the slots the grant took, once the final reader round's lanes have met.
static void check_driver_deferred_give_back(void* context)
{
    CheckDriverDeferred* deferred = context;
    deferred->return_failed |= !worker_budget_release(&deferred->lease);
}

// A typo correction that clang's parser takes at a statement's start (#1447, c_check_typo.c's CHECK_TYPO_STATEMENT):
// ClassifyName corrects the name to a keyword or a type name and the parser reads the statement again with it
// (`retrn x;` is a `return`, `myin x;` a declaration of a `myint`). The checker's view can't parse again, so the driver
// does: each pass's reports say which names the next view reads as what (CheckPrefixEdit), and the next view is the
// original's tokens with them, recovered and checked again. An expression statement whose missing `;` follows its first
// name (ParseDiagnostic.probe) and whose name stays undeclared is clang's invalid expression, skipped to its `;`: the
// next view leaves out its rest, and its error with it. At most this many passes: each finds what the one before read.
#define BUSTER_CHECK_DRIVER_TYPO_PASSES 4
#define BUSTER_CHECK_DRIVER_TYPO_EDITS (2 * BUSTER_CHECK_TYPO_LIMIT)

typedef struct CheckDriverTypoEdits CheckDriverTypoEdits;
struct CheckDriverTypoEdits
{
    CheckPrefixEdit edits[BUSTER_CHECK_DRIVER_TYPO_EDITS];
    // Beside each replacement, the keyword it takes (its index in check_typo_keywords + 1) or the typedef's declaration
    // token (the original's), for its error; BUSTER_CHECK_NONE for a left-out tail.
    uint32_t keywords[BUSTER_CHECK_DRIVER_TYPO_EDITS];
    uint32_t types[BUSTER_CHECK_DRIVER_TYPO_EDITS];
    // Beside a left-out block item after GNU attributes, the name its errors are at, and the value's declaration token
    // its correction took in `types` (check_driver_typo_tail); BUSTER_CHECK_NONE for any other edit.
    uint32_t names[BUSTER_CHECK_DRIVER_TYPO_EDITS];
    // The names whose tails (check_driver_typo_tail) a reading didn't confirm, at the original's tokens, which no later
    // view leaves out again.
    uint32_t rejected[BUSTER_CHECK_DRIVER_TYPO_EDITS];
    const InternTable* table;
    uint32_t count;
    uint32_t rejected_count;
};

// The original token whose source a view's `token` has (each original token's is its own), or BUSTER_CHECK_NONE.
static uint32_t check_driver_original_token(const Parser* original, const Parser* view, uint32_t token)
{
    const uint32_t count = original->tokens.count;
    const uint32_t source = token < view->tokens.count ? view->tokens.sources[token] : UINT32_MAX;
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = original->tokens.sources[middle] < source;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low < count && original->tokens.sources[low] == source ? low : BUSTER_CHECK_NONE;
}

// The view's token whose source the original's `token` has (the first, past a `;` a view put before it), or
// BUSTER_CHECK_NONE.
static uint32_t check_driver_view_token(const Parser* view, const Parser* original, uint32_t token)
{
    const uint32_t count = view->tokens.count;
    const uint32_t source = token < original->tokens.count ? original->tokens.sources[token] : UINT32_MAX;
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = view->tokens.sources[middle] < source;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low < count && view->tokens.sources[low] == source ? low : BUSTER_CHECK_NONE;
}

// Inserts an edit in token order, unless one is at its token or overlaps it. Whether it was added.
static bool check_driver_typo_add(CheckDriverTypoEdits* edits, CheckPrefixEdit edit, uint32_t keyword, uint32_t type, uint32_t name)
{
    uint32_t at = 0;
    while (at < edits->count && edits->edits[at].token < edit.token)
    {
        at += 1;
    }
    const uint32_t last = edit.end != 0 ? edit.end : edit.token + 1;
    const bool before_free = at == 0 || (edits->edits[at - 1].end != 0 ? edits->edits[at - 1].end : edits->edits[at - 1].token + 1) <= edit.token;
    const bool after_free = at == edits->count || edits->edits[at].token >= last;
    const bool added = edits->count < BUSTER_CHECK_DRIVER_TYPO_EDITS && before_free && after_free;
    for (uint32_t k = edits->count; added && k > at; k -= 1)
    {
        edits->edits[k] = edits->edits[k - 1];
        edits->keywords[k] = edits->keywords[k - 1];
        edits->types[k] = edits->types[k - 1];
        edits->names[k] = edits->names[k - 1];
    }
    if (added)
    {
        edits->edits[at] = edit;
        edits->keywords[at] = keyword;
        edits->types[at] = type;
        edits->names[at] = name;
        edits->count += 1;
    }
    return added;
}

static uint32_t check_driver_skip_end(const Parser* parser, uint32_t token);

// A parse error just after a name within an expression, before an operand's first token, where the name may be a typo of
// an expression's keyword or a statement's (`sizeo a`, `retrn 5`): clang's correction there takes the keyword
// (DiagnoseEmptyLookup's keyword replacement), and the expression is invalid, its rest skipped to the `,`, `;` or closer
// that ends it, with no error of the parser's. A recovery view leaves out that tail of each such name near one of those
// keywords as clang's search measures it (CheckPrefixTails), tentatively: the reading confirms it where the name's search
// suggests a keyword, and otherwise the next pass's view keeps it, with its error (check_driver_typo_edits). The token
// that ends the tail, or 0.
//
// The other tail is a block item's GNU attributes before a name no binding of the parse names, which the parser reads as
// an implicit int's declarator (parse_scalar_attribute_name): clang's statement reader asks Sema what the name is first,
// with its typo correction (ClassifyName), and a name it corrects to a value is "expected identifier or '('" there. The
// view leaves the attributes out, so that the reading searches the name at the statement's start; a correction it takes
// makes the next pass's view leave the item out (check_driver_typo_edits), with both errors.
static uint32_t check_driver_typo_tail(const Parser* view, const uint32_t* origin, const ParseDiagnostic* diagnostic, uint32_t* begin, uint32_t* name_of,
                                       const void* context)
{
    const CheckDriverTypoEdits* edits = context;
    const InternTable* table = edits->table;
    const uint32_t count = view->tokens.count;
    const uint32_t at = diagnostic->token;
    // The attributes before an implicit int's name, from their first `__attribute__`, after a statement's start.
    const bool implicit = (diagnostic->message == PARSE_MESSAGE_TYPE_SPECIFIER_MISSING || diagnostic->message == PARSE_MESSAGE_TYPE_SPECIFIER_REQUIRED) &&
                          at < count && at >= 2 && view->tokens.kinds[at] == PARSE_KIND_IDENTIFIER && view->tokens.kinds[at - 1] == PARSE_KIND_R_PAREN;
    uint32_t first = at;
    bool going = implicit;
    while (going)
    {
        uint32_t depth = 0;
        uint32_t k = first - 1;
        bool open = true;
        while (open)
        {
            const uint8_t kind = view->tokens.kinds[k];
            depth += kind == PARSE_KIND_R_PAREN ? 1u : 0u;
            depth -= kind == PARSE_KIND_L_PAREN && depth != 0 ? 1u : 0u;
            open = depth != 0 && k != 0;
            k -= open ? 1u : 0u;
        }
        const bool attribute = depth == 0 && k != 0 && view->tokens.kinds[k - 1] == PARSE_KIND_ATTRIBUTE;
        first = attribute ? k - 1 : first;
        going = attribute && first >= 2 && view->tokens.kinds[first - 1] == PARSE_KIND_R_PAREN;
    }
    const uint8_t before_first = first < at && first != 0 ? view->tokens.kinds[first - 1] : PARSE_KIND_END;
    const bool attributed = first < at && (before_first == PARSE_KIND_SEMI || before_first == PARSE_KIND_L_BRACE || before_first == PARSE_KIND_R_BRACE);
    bool fresh = attributed;
    for (uint32_t r = 0; fresh && r < edits->rejected_count; r += 1)
    {
        fresh = edits->rejected[r] != (origin != NULL ? origin[at] : at);
    }
    *begin = fresh ? first : at;
    *name_of = fresh ? at : at - 1;
    uint32_t result = fresh ? at : 0;
    const uint8_t kind = at < count ? view->tokens.kinds[at] : PARSE_KIND_END;
    const bool operand = kind == PARSE_KIND_IDENTIFIER || kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER || kind == PARSE_KIND_STRING;
    const uint32_t name = result == 0 && !diagnostic->probe && operand && at >= 2 && view->tokens.kinds[at - 1] == PARSE_KIND_IDENTIFIER ? at - 1 :
                          BUSTER_CHECK_NONE;
    const uint8_t before = name < count ? view->tokens.kinds[name - 1] : PARSE_KIND_END;
    bool near = name < count && before != PARSE_KIND_PERIOD && before != PARSE_KIND_ARROW;
    const uint32_t named = near && origin != NULL ? origin[name] : name;
    for (uint32_t r = 0; near && r < edits->rejected_count; r += 1)
    {
        near = edits->rejected[r] != named;
    }
    const uint32_t length = near ? intern_length(table, view->tokens.names[name]) : 0;
    const uint8_t* spelling = near ? (const uint8_t*)intern_bytes(table, view->tokens.names[name]) : NULL;
    bool keyword = false;
    uint32_t row[65];
    for (uint32_t w = 0; near && length >= 3 && length <= 64 && w < BUSTER_ARRAY_LENGTH(check_typo_keywords); w += 1)
    {
        const char* other = check_typo_keywords[w];
        const uint32_t other_length = (uint32_t)strlen(other);
        const bool listed = (check_typo_keyword_kinds[w] & (CHECK_TYPO_KEYWORD_EXPRESSION | CHECK_TYPO_KEYWORD_REMAINING)) != 0 &&
                            check_typo_keyword_offered(other, view->language.standard, view->language.gnu);
        const uint32_t difference = other_length > length ? other_length - length : length - other_length;
        const uint32_t distance = listed && 3 * difference <= length ? check_typo_distance_table((const uint8_t*)other, other_length, spelling, length, row) : 0;
        keyword |= distance != 0 && distance <= (length + 2) / 3 && length / distance >= 3;
    }
    // (Any other name too: one that stays undeclared is clang's "use of undeclared identifier", its expression invalid
    // and its rest skipped as a keyword's typo's is, `u8'a'` before C23 among them; the reading confirms it.)
    keyword |= near;
    const uint32_t end = keyword ? check_driver_skip_end(view, at) : BUSTER_CHECK_NONE;
    // (The tail ends at a `,` outside its groups too, an initializer's or an argument's end.)
    uint32_t stop = end;
    uint32_t depth = 0;
    for (uint32_t t = at; keyword && end < count && t < end; t += 1)
    {
        const uint8_t k = view->tokens.kinds[t];
        depth += k == PARSE_KIND_L_PAREN || k == PARSE_KIND_L_SQUARE || k == PARSE_KIND_L_BRACE ? 1u : 0u;
        depth -= depth != 0 && (k == PARSE_KIND_R_PAREN || k == PARSE_KIND_R_SQUARE || k == PARSE_KIND_R_BRACE) ? 1u : 0u;
        stop = k == PARSE_KIND_COMMA && depth == 0 && t < stop ? t : stop;
    }
    result = keyword && stop < count && stop > at ? stop : result;
    return result;
}

// The token where clang's SkipUntil(r_brace, StopAtSemi | StopBeforeMatch) stops from `token` on: a `;` or a `}` outside
// the groups it opens, which it skips whole; or BUSTER_CHECK_NONE where the unit ends first.
static uint32_t check_driver_skip_end(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t depth = 0;
    uint32_t at = token;
    bool going = at < count;
    while (going)
    {
        const uint8_t kind = parser->tokens.kinds[at];
        const bool stop = depth == 0 && (kind == PARSE_KIND_SEMI || kind == PARSE_KIND_R_BRACE || kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE);
        depth += kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE ? 1u : 0u;
        depth -= !stop && depth != 0 && (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE) ? 1u : 0u;
        at += stop ? 0u : 1u;
        going = !stop && at < count;
    }
    return at < count ? at : BUSTER_CHECK_NONE;
}

// The edits a pass's reports ask for (CheckDriverTypoEdits): `checking` the view checked, `input` its reading, and
// `diagnostics` its parse's, at the original's tokens. Whether any edit is new.
static bool check_driver_typo_edits(CheckDriverTypoEdits* edits, const Parser* original, const Parser* checking, const CheckInput* input,
                                    const ParseDiagnostic* diagnostics, uint32_t diagnostic_count, const InternTable* table)
{
    static const ParseKeywordKind kinds[] =
    {
#define BUSTER_CHECK_DRIVER_KEYWORD_KIND(name, kind) { C_NAME_##name, PARSE_KIND_##kind },
        BUSTER_PARSE_KEYWORD_KINDS(BUSTER_CHECK_DRIVER_KEYWORD_KIND)
#undef BUSTER_CHECK_DRIVER_KEYWORD_KIND
    };
    const uint32_t count = original->tokens.count;
    bool added = false;
    // The tails left out (ParseDiagnostic.tail, check_driver_typo_tail): an expression's stands where the name stays
    // undeclared (a keyword suggested, a tie, or nothing near), as clang's expression is invalid and its rest skipped, and
    // the next view keeps it, with its error, where the name was declared or a correction took a value. A block item's
    // attributes stand where a correction took a value: the next view leaves the whole item out, to its `;`, with its
    // errors; otherwise the next view keeps them.
    for (uint32_t d = 0; d < diagnostic_count; d += 1)
    {
        const ParseDiagnostic* diagnostic = diagnostics + d;
        const uint32_t name = diagnostic->tail ? diagnostic->argument : BUSTER_CHECK_NONE;
        const bool attributes = diagnostic->message == PARSE_MESSAGE_TYPE_SPECIFIER_MISSING || diagnostic->message == PARSE_MESSAGE_TYPE_SPECIFIER_REQUIRED;
        bool confirmed = false;
        uint32_t taken = BUSTER_CHECK_NONE;
        for (uint32_t r = 0; name < count && r < input->report_count; r += 1)
        {
            const CheckInputReport* report = input->reports + r;
            const bool at_name = check_driver_original_token(original, checking, report->token) == name;
            const bool value = attributes && report->message == CHECK_INPUT_UNDECLARED_CORRECTED;
            confirmed |= at_name && (value || (!attributes && report->message == CHECK_INPUT_UNDECLARED && (report->detail & CHECK_TYPO_STATEMENT) == 0));
            taken = at_name && value ? check_driver_original_token(original, checking, report->note_token) : taken;
        }
        const uint32_t end = confirmed && attributes ? check_driver_skip_end(original, name) : BUSTER_CHECK_NONE;
        if (end < count && original->tokens.kinds[end] == PARSE_KIND_SEMI && taken < count)
        {
            // The attributes and the rest left out, and the name an empty statement's `;` at its place.
            const CheckPrefixEdit attributes_out = { .token = diagnostic->token, .end = name, .name = 0, .kind = 0 };
            const CheckPrefixEdit empty = { .token = name, .end = 0, .name = 0, .kind = PARSE_KIND_SEMI };
            const CheckPrefixEdit rest_out = { .token = name + 1, .end = end + 1, .name = 0, .kind = 0 };
            added |= check_driver_typo_add(edits, attributes_out, 0, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            added |= check_driver_typo_add(edits, empty, 0, taken, name);
            added |= check_driver_typo_add(edits, rest_out, 0, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        }
        confirmed &= !attributes || (end < count && original->tokens.kinds[end] == PARSE_KIND_SEMI && taken < count);
        if (name < count && !confirmed && edits->rejected_count < BUSTER_CHECK_DRIVER_TYPO_EDITS)
        {
            edits->rejected[edits->rejected_count] = name;
            edits->rejected_count += 1;
            added = true;
        }
    }
    for (uint32_t r = 0; r < input->report_count; r += 1)
    {
        const CheckInputReport* report = input->reports + r;
        const bool statement = report->message == CHECK_INPUT_UNDECLARED && (report->detail & CHECK_TYPO_STATEMENT) != 0;
        const uint32_t token = statement ? check_driver_original_token(original, checking, report->token) : BUSTER_CHECK_NONE;
        const uint32_t keyword = report->detail & (CHECK_TYPO_STATEMENT - 1);
        const uint32_t type = statement && report->subject != 0 ? check_driver_original_token(original, checking, report->subject - 1) : BUSTER_CHECK_NONE;
        uint32_t name = 0;
        uint8_t kind = PARSE_KIND_IDENTIFIER;
        if (keyword != 0 && keyword <= BUSTER_ARRAY_LENGTH(check_typo_keywords))
        {
            const char* spelling = check_typo_keywords[keyword - 1];
            name = intern_find(table, (const uint8_t*)spelling, (uint32_t)strlen(spelling));
            kind = PARSE_KIND_UNKNOWN;
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(kinds); k += 1)
            {
                kind = kinds[k].name == name ? kinds[k].kind : kind;
            }
        }
        else
        {
            name = type < count ? original->tokens.names[type] : 0;
        }
        if (token < count && name != 0 && kind != PARSE_KIND_UNKNOWN && original->tokens.kinds[token] == PARSE_KIND_IDENTIFIER)
        {
            const CheckPrefixEdit edit = { .token = token, .end = 0, .name = name, .kind = kind };
            added |= check_driver_typo_add(edits, edit, keyword, keyword != 0 ? BUSTER_CHECK_NONE : type, BUSTER_CHECK_NONE);
        }
    }
    // A probe's statement whose expression has an undeclared name that no correction took: its rest is left out.
    for (uint32_t d = 0; d < diagnostic_count; d += 1)
    {
        const ParseDiagnostic* diagnostic = diagnostics + d;
        const uint32_t at = diagnostic->token;
        uint32_t first = diagnostic->probe && at != 0 && at < count ? at - 1 : BUSTER_CHECK_NONE;
        // (The name's call's `(`: the name before the group, matched backwards.)
        uint32_t depth = 0;
        while (first < count && first != 0 && (depth != 0 || original->tokens.kinds[first] == PARSE_KIND_R_PAREN))
        {
            const uint8_t kind = original->tokens.kinds[first];
            depth += kind == PARSE_KIND_R_PAREN ? 1u : 0u;
            depth -= kind == PARSE_KIND_L_PAREN && depth != 0 ? 1u : 0u;
            first -= 1;
        }
        // (Not where the name is corrected: the statement is read again.)
        for (uint32_t e = 0; first < count && e < edits->count; e += 1)
        {
            first = edits->edits[e].token == first && edits->edits[e].end == 0 ? BUSTER_CHECK_NONE : first;
        }
        bool undeclared = false;
        for (uint32_t r = 0; first < count && r < input->report_count; r += 1)
        {
            const CheckInputReport* report = input->reports + r;
            const bool corrected = (report->detail & CHECK_TYPO_STATEMENT) != 0;
            const uint32_t token = report->message == CHECK_INPUT_UNDECLARED && !corrected ? check_driver_original_token(original, checking, report->token) :
                                   BUSTER_CHECK_NONE;
            undeclared |= token >= first && token < at;
        }
        const uint32_t end = undeclared ? check_driver_skip_end(original, at) : BUSTER_CHECK_NONE;
        if (end < count && original->tokens.kinds[end] == PARSE_KIND_SEMI && end > at)
        {
            const CheckPrefixEdit edit = { .token = at, .end = end, .name = 0, .kind = 0 };
            added |= check_driver_typo_add(edits, edit, 0, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        }
    }
    return added;
}

// Each statement start's correction's error (CheckDriverTypoEdits), at its name in the view `checking`: clang's
// diagnoseTypo of ClassifyName's, "use of undeclared identifier" with a keyword, or "unknown type name" with a typedef and
// its note, as the driver's own diagnostics (CheckDriverDiagnostic), appended to `additional` from `count`, whose room
// has two for each edit; their texts in `arena`. Returns the new count.
// One whose name the view left out (a body a later parse error ended) is printed at once, before the parse's errors.
static uint32_t check_driver_typo_errors(const CheckDriverTypoEdits* edits, const Parser* original, const Parser* checking, Preprocessor* pp,
                                         CheckDriverDiagnostic* additional, uint32_t count, Arena* arena)
{
    uint32_t result = count;
    for (uint32_t e = 0; e < edits->count; e += 1)
    {
        const CheckPrefixEdit* edit = edits->edits + e;
        const uint32_t keyword = edits->keywords[e];
        const uint32_t type = edits->types[e];
        // (A block item's name after GNU attributes that a correction took as a value, check_driver_typo_tail, is a `;` in
        // the view, its errors there: the correction's, then the parser's.)
        const uint32_t named = edits->names[e];
        const bool value = named < original->tokens.count && edit->end == 0;
        const uint32_t token = edit->end == 0 ? check_driver_view_token(checking, original, edit->token) : BUSTER_CHECK_NONE;
        const bool typedef_name = !value && keyword == 0 && type < original->tokens.count;
        const bool noted = typedef_name || (value && type < original->tokens.count);
        const uint32_t declared = noted ? check_driver_view_token(checking, original, type) : BUSTER_CHECK_NONE;
        const bool viewed = token < checking->tokens.count && (!noted || declared < checking->tokens.count);
        char* message = edit->end == 0 && (keyword != 0 || noted) ? arena_push_array(arena, 512, 1, 1) : NULL;
        char* note = message != NULL && noted ? arena_push_array(arena, 512, 1, 1) : NULL;
        if (message != NULL && (!noted || note != NULL))
        {
            const uint32_t name = original->tokens.names[edit->token];
            const char* other = noted ? (const char*)intern_bytes(pp->table, original->tokens.names[type]) : check_typo_keywords[keyword - 1];
            const int other_length = noted ? (int)intern_length(pp->table, original->tokens.names[type]) : (int)strlen(other);
            snprintf(message, 512, "%s '%.*s'; did you mean '%.*s'?", typedef_name ? "unknown type name" : "use of undeclared identifier",
                     (int)intern_length(pp->table, name), intern_bytes(pp->table, name), other_length, other);
            if (note != NULL)
            {
                snprintf(note, 512, "'%.*s' declared here", other_length, other);
            }
            static const char expected[] = "expected identifier or '('";
            if (viewed)
            {
                additional[result] = (CheckDriverDiagnostic){ .token = token, .message = message, .position = token, .note_token = declared, .note = note };
                result += 1;
                additional[result] = (CheckDriverDiagnostic){ .token = token, .message = expected, .position = token, .repeated = true };
                result += value ? 1u : 0u;
            }
            else
            {
                const uint32_t source = original->tokens.sources[edit->token];
                const PPOutputDiagnosticNote printed = { .token = noted ? original->tokens.sources[type] : 0, .message = note };
                pp_history_ensure(pp);
                pp_diagnose_output_notes_at(pp, PP_SEVERITY_ERROR, source, source, &printed, note != NULL ? 1u : 0u, "%s", message);
                if (value)
                {
                    pp_diagnose_output_at(pp, PP_SEVERITY_ERROR, source, source, "%s", expected);
                }
            }
        }
    }
    return result;
}

// One translation unit, `path`, with `options` (the command's, with the unit's file cache and diagnostics' stream):
// preprocessed, parsed and checked, its diagnostics printed through the preprocessor's stream and the driver's own to
// `errors`; then, as the command says, its object written, its executable linked or, with `kept` (#152: one of several
// units that link into one executable), its link kept for theirs. `identifiers` is the unit's intern table, which its
// caller initialized (null if it couldn't), `pp` and `parser` its state, and `arena`, `scratch` and `output_arena` its
// memory; its functions compile on `lanes`. The preprocessor's phases' ticks go to `pp_ticks`, and with `stage_clock`
// (--time-stages), each stage's time. With `release`, it releases the parser's, the preprocessor's and the type table's
// memory before it returns. With `deferred` (--deferred-bodies, one source file's), the unit's reading reads its bodies
// apart on the command's lanes, with that state, which the caller keeps until the unit is done (check_deferred_release).
// If `fatal` is nonnull, retain the publication's fatal state before releasing the preprocessor: a multi-unit caller
// uses that state to publish the canonical prefix, independently of which lane finishes first.
// Returns the command's exit status: 0 for a unit without errors whose outputs were written.
// The bytes a token of the scratch that check-expressions, check-declarations, static-data and check_program push
// before check_program gives its pages back (#628): about 64 MB of sqlite3.c's 716,157 tokens. The discard after the
// reader keeps that many past the scratch's length, which they then find resident rather than fault in again, cleared.
#define BUSTER_CHECK_DRIVER_SCRATCH_REUSE 96

// Gives the kernel back the scratch's pages past `kept` bytes from its start (arena_discard_past, #628): those of a pass
// whose pushes are over, which the passes after it, pushing fewer, would otherwise hold through the compile's peak. A
// lane of several units with another to compile (check_units_unit_left) gives them back in whole huge pages, as
// check_program does (CheckProgramOptions.discard_huge, #777): the huge page a discard frees part of is split, and the
// next unit would fault it in a base page at a time.
static void check_driver_discard_scratch(Arena* scratch, CheckProgramLanes* lanes, uint64_t kept)
{
    arena_discard_past(scratch, kept, lanes != NULL && lanes->units_lane != NULL && check_units_unit_left(lanes->units_lane));
}

static int check_driver_unit(const CheckDriverCommand* command, const PPOptions* options, const char* path, InternTable* identifiers, Preprocessor* pp,
                             Parser* parser, Arena* arena, Arena* scratch, Arena* output_arena, CheckProgramLanes* lanes, CheckClock* stage_clock,
                             uint64_t* pp_ticks, FILE* errors, CheckUnitsKept* kept, bool release, DependencyOutput* dependency, Arena* dependency_arena,
                             CheckDeferred* deferred, bool* fatal)
{
    int result = 1;
    bool driver_fatal = false;
    Arena type_arena = { 0 };
    Arena index_arena = { 0 };
    // The thread's last arena failure as the unit starts, against which its end tells a commit the unit was refused.
    const ArenaFailure failure_before = arena_failure;
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
    // Whether the unit reported its memory failure itself (arena_report_failure), which the notes at its end then don't.
    bool memory_reported = false;
    if (!ready || scratch->base == 0 || output_arena->base == 0)
    {
        arena_report_failure(errors, "checking", UINT64_C(1) << 36);
        driver_fatal = true;
        memory_reported = true;
    }
    else
    {
        CheckPrefix prefix = { 0 };
        CheckRepair repair = { 0 };
        CheckResumed resumed = { 0 };
        const Parser* checking = parser;
        // A failed parse as clang's recovery reads it (check_prefix_resume, #147), or the parse itself: what the views
        // after a failed parse are made of, and the original whose places their diagnostics publish at. Its parse's
        // diagnostics are the original's.
        const Parser* recovered = parser;
        Preprocessor* checking_pp = pp;
        bool prefix_only = false;
        bool repairing = false;
        bool parse_failed = false;
        CheckInput original_pragmas = { 0 };
        bool original_pragmas_ready = false;
        bool original_pragmas_printed = false;
        // A statement start's typo corrections (check_driver_typo_edits), and whether the parse's warnings were printed
        // before a correction failed the unit.
        CheckDriverTypoEdits typo_edits = { .count = 0, .table = pp->table };
        const CheckPrefixTails typo_tails = { .end = check_driver_typo_tail, .context = &typo_edits };
        bool parse_printed = false;
        check_clock_mark(stage_clock, CHECK_STAGE_SETUP);
        pp->options.collect_diagnostics = true;
        pp_driver_verbose(options, path);
        ready = pp_run_input(pp, path);
        if (!ready && pp->errors != 0 && !pp->diagnostic_collection_failed && !pp->history_failed)
        {
            // Only the produced source prefix is parsed after a real preprocessing fatal. Nonfatal errors
            // leave the whole stream. Either remains a rejected unit, independent of publication's cutoff.
            pp->fatal = false;
            ready = true;
        }
        pp->diagnostic_source = true;
        check_clock_mark(stage_clock, CHECK_STAGE_PREPROCESS);
        check_clock_tokens(stage_clock, pp->output.count);
        check_driver_start_lanes(lanes, pp->output.count);
        // The phases of pp_run alone: a diagnostic's later replay of the unit (pp_history_ensure) adds to them.
        memcpy(pp_ticks, pp->ticks, sizeof(pp->ticks));
        // The files' token columns have no reader past the run (pp_release_file_columns, #628); --poison-arenas makes
        // them unreadable instead, for a test to find one.
        pp_release_file_columns(pp, command->poison_arenas);
        // One of several units (#1109): the unit's estimated peak, from its tokens, out of the memory the units' lanes
        // share, before the parse and the checker take it; it may wait for another lane's units to give theirs back.
        if (lanes != NULL && lanes->units_lane != NULL)
        {
            check_units_admit(lanes->units_lane, pp->output.count);
        }
        if (ready)
        {
            ready = parse_translation_unit(parser, pp);
            parse_failed = !ready;
            if (ready)
            {
                parse_print_diagnostics(parser, pp);
                parse_printed = true;
            }
            else
            {
                // (With the tails an expression's keyword typo would skip, tentatively: check_driver_typo_tail.)
                recovered = check_prefix_resume(&resumed, parser, NULL, 0, &typo_tails, arena, scratch) == CHECK_PREFIX_READY ? &resumed.parser : parser;
                if (recovered != parser && recovered->errors == 0)
                {
                    // Every missing `;` read past as clang's recovery reads it: the whole view, checked as a prefix is.
                    checking = recovered;
                    resumed.parser.whole_view = true;
                    prefix_only = true;
                    ready = true;
                }
                else if (check_repair_make(&repair, recovered, pp, arena, scratch) == CHECK_REPAIR_READY)
                {
                    // Only completed original expressions can contribute diagnostics. The synthetic closure
                    // belongs to a private checking view and can never produce a successful source result.
                    checking = &repair.parser;
                    checking_pp = repair.preprocessor;
                    prefix_only = true;
                    repairing = true;
                    ready = true;
                }
                else if (check_prefix_make(&prefix, recovered, arena, scratch) == CHECK_PREFIX_READY)
                {
                    // Only complete earlier declarations survive this view. Its checking suppresses obligations
                    // that belong to the full translation-unit end; the original parser rejection stays final.
                    checking = &prefix.parser;
                    prefix_only = true;
                    ready = true;
                }
            }
        }
        if (parse_failed)
        {
            original_pragmas_ready = check_diagnostic_original_pragmas(&original_pragmas, recovered, pp,
                                                                       check_diagnostic_parse_end(recovered), arena, scratch);
            if (!original_pragmas_ready)
            {
                fprintf(errors, "fatal error: original pragma diagnostic capacity exhausted\n");
                driver_fatal = true;
                ready = false;
            }
        }
        check_clock_mark(stage_clock, CHECK_STAGE_PARSE);
        uint32_t tokens = checking->tokens.count;
        // A complete declaration prefix ends before the original parser's lookahead, not at the whole PP EOF.
        // Repairs retain their synthetic/end boundary; completed original bodies are handled independently.
        uint32_t publication_end = !repairing && tokens < recovered->tokens.count ? recovered->tokens.sources[tokens] : pp->output.count;
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
            // column's first touch cleared a 2 MB page: 39 MB of sqlite3.c's peak RSS (#528). The index's first
            // BUSTER_TYPE_INDEX_SMALL_BYTES too, which hold a unit's buckets but the largest's: sqlite3.c's take 128 KB,
            // where the index's first huge page took 2 MB (#672).
            const uint64_t type_bytes = type_capacity_bytes(capacity);
            const uint64_t index_bytes = type_index_capacity_bytes(capacity);
            const bool fail_types = check_driver_test_failed_types != NULL && strcmp(path, check_driver_test_failed_types) == 0;
            type_arena = !fail_types ? arena_reserve_small(type_bytes, type_bytes) : (Arena){ 0 };
            index_arena = !fail_types ? arena_reserve_small(index_bytes, BUSTER_TYPE_INDEX_SMALL_BYTES) : (Arena){ 0 };
            ready = !fail_types && capacity.types != 0 && type_initialize(&types, &type_arena, &index_arena, capacity);
            if (!ready)
            {
                fprintf(errors, "fatal error: can't reserve type table\n");
                driver_fatal = true;
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
                driver_fatal = true;
            }
        }
        if (ready)
        {
            CheckInput input;
            // A repaired fragment (check_input_fragment), a prefix (check_input_prefix), a unit with its system headers'
            // bodies deferred (check_input_selected) or the whole unit (check_input). A whole unit's bodies are read apart
            // on the command's lanes (#151) where it asks for that and has more than one lane.
            const uint8_t* selected = lazy_system_headers && !prefix_only && lazy.deferred_count != 0 ? lazy.deferred_tokens : 0;
            const bool deferring = deferred != NULL && lanes != NULL && lanes->flow != NULL && !prefix_only && selected == NULL;
            const uint32_t deferred_lanes = deferring ? check_driver_deferred_lanes(lanes) : 1;
            CheckDriverDeferred grants = { .lanes = lanes };
            bool input_valid = false;
            if (deferred_lanes > 1)
            {
                const CheckDeferredFlow flow = {
                    .flow = lanes->flow,
                    .grant = check_driver_deferred_grant,
                    .give_back = check_driver_deferred_give_back,
                    .context = &grants,
                };
                deferred->flow = &flow;
                deferred->every_lane = lanes->every_lane;
                input_valid = check_input_deferred_compile(deferred, &input, checking, checking_pp, &types, arena, scratch, deferred_lanes);
                deferred->flow = NULL;
            }
            else
            {
                input_valid = check_input_compile(&input, checking, checking_pp, &types, arena, scratch, !prefix_only,
                                                  repairing ? repair.original_end : checking->tokens.count, selected);
            }
            // A statement start's typo correction that clang's parser reads the statement again with (#1447,
            // check_driver_typo_edits): the original's tokens with the corrections, recovered and read again.
            for (uint32_t pass = 1; pass < BUSTER_CHECK_DRIVER_TYPO_PASSES && ready && !input.resource && (input.report_count != 0 || parse_failed) &&
                                    check_driver_typo_edits(&typo_edits, parser, checking, &input, recovered != parser ? resumed.diagnostics : parser->diagnostics,
                                                            recovered != parser ? resumed.diagnostic_count : parser->diagnostic_count, pp->table);
                 pass += 1)
            {
                CheckResumed edited = { 0 };
                const bool viewed = check_prefix_resume(&edited, parser, typo_edits.edits, typo_edits.count, &typo_tails, arena, scratch) == CHECK_PREFIX_READY;
                // (No view, where the last tail was given back and nothing else is edited: the failed parse's own.)
                const bool original = !viewed && parser->errors != 0;
                if (viewed || original)
                {
                    resumed = viewed ? edited : resumed;
                    recovered = viewed ? &resumed.parser : parser;
                    parse_failed = true;
                    prefix_only = true;
                    repairing = false;
                    checking_pp = pp;
                    if (recovered->errors == 0)
                    {
                        resumed.parser.whole_view = true;
                        checking = recovered;
                    }
                    else if (check_repair_make(&repair, recovered, pp, arena, scratch) == CHECK_REPAIR_READY)
                    {
                        checking = &repair.parser;
                        checking_pp = repair.preprocessor;
                        repairing = true;
                    }
                    else if (check_prefix_make(&prefix, recovered, arena, scratch) == CHECK_PREFIX_READY)
                    {
                        checking = &prefix.parser;
                    }
                    else
                    {
                        ready = false;
                    }
                }
                if ((viewed || original) && ready)
                {
                    original_pragmas_ready = check_diagnostic_original_pragmas(&original_pragmas, recovered, pp, check_diagnostic_parse_end(recovered), arena,
                                                                               scratch);
                    tokens = checking->tokens.count;
                    publication_end = !repairing && tokens < recovered->tokens.count ? recovered->tokens.sources[tokens] : pp->output.count;
                    const TypeCapacity again = {
                        .types = tokens <= (1u << 27) ? tokens * 4 + 64 : 0,
                        .parameters = tokens,
                        .tags = tokens,
                        .members = tokens,
                    };
                    arena_release(&type_arena);
                    arena_release(&index_arena);
                    const uint64_t type_bytes = type_capacity_bytes(again);
                    const uint64_t index_bytes = type_index_capacity_bytes(again);
                    type_arena = arena_reserve_small(type_bytes, type_bytes);
                    index_arena = arena_reserve_small(index_bytes, BUSTER_TYPE_INDEX_SMALL_BYTES);
                    types = (TypeTable){ 0 };
                    ready = original_pragmas_ready && again.types != 0 && type_initialize(&types, &type_arena, &index_arena, again);
                    if (!ready)
                    {
                        fprintf(errors, "fatal error: can't reserve type table\n");
                        driver_fatal = true;
                    }
                    else
                    {
                        input_valid = check_input_compile(&input, checking, checking_pp, &types, arena, scratch, false,
                                                          repairing ? repair.original_end : checking->tokens.count, NULL);
                    }
                }
            }
            check_clock_mark(stage_clock, CHECK_STAGE_CHECK_INPUT);
            // The parse's and the reader's pages of the scratch, 82 MB of sqlite3.c's, which check-expressions and
            // check-declarations would otherwise hold to static-data's end (check_driver_discard_scratch, #628), but for
            // what they and check_program push into it again (BUSTER_CHECK_DRIVER_SCRATCH_REUSE).
            check_driver_discard_scratch(scratch, lanes, scratch->length + BUSTER_CHECK_DRIVER_SCRATCH_REUSE * (uint64_t)checking->tokens.count);
            if (stage_clock != NULL && deferring)
            {
                stage_clock->deferred = true;
                stage_clock->deferred_lanes = deferred_lanes > 1 && !deferred->unsupported ? deferred->active : 0;
                stage_clock->deferred_reason = deferred_lanes <= 1 ? "one lane" : deferred->unsupported ? deferred->reason : NULL;
                stage_clock->deferred_expression_parts = deferred->expression_parts;
                stage_clock->deferred_expression_parallel_tokens = deferred->expression_parallel_tokens;
                stage_clock->deferred_expression_serial_tokens = deferred->expression_serial_tokens;
                stage_clock->deferred_expression_worker_mask = deferred->expression_worker_mask;
                stage_clock->deferred_expression_retry = deferred->expression_retry;
                memcpy(stage_clock->deferred_expression_ticks, deferred->expression_ticks, sizeof(stage_clock->deferred_expression_ticks));
            }
            if (grants.return_failed)
            {
                fprintf(errors, "fatal error: could not return GNU make worker slots\n");
                driver_fatal = true;
            }
            if (stage_clock != NULL)
            {
                _Static_assert(BUSTER_CHECK_CLOCK_WALK_PARTS == BUSTER_CHECK_EXPRESSION_PARTS, "the clock reports each of the walk's parts");
                stage_clock->expression_ticks += input.expression_ticks;
                for (uint32_t p = 0; p < BUSTER_CHECK_CLOCK_WALK_PARTS; p += 1)
                {
                    stage_clock->expression_part_ticks[p] += input.expression_part_ticks[p];
                }
                stage_clock->expression_faults += input.expression_faults;
                stage_clock->expression_system_us += input.expression_system_us;
                stage_clock->expression_usage_failed = stage_clock->expression_usage_failed || input.expression_usage_failed;
            }
            if (input.resource)
            {
                fprintf(errors, "fatal error: checker input capacity exhausted\n");
                driver_fatal = true;
            }
            // A rejected reader may retain complete earlier expressions solely for diagnostics. Their typed
            // reports share source order with declaration reports, but this fragment can never be emitted.
            if (grants.return_failed)
            {
                result = 1;
            }
            else if (input_valid || input.diagnostic_graph)
            {
                const CheckOptions check_options = {
                    .scalar = scalar,
                    .verify = verify,
                    .transitions = command->check_transitions,
                    .verify_transitions = verify,
                    // The run is the output arena's first user in the unit, through no copy of it: past its length it is
                    // as the kernel mapped it unless an earlier unit's pushes reached there (arena_untouched).
                    .fresh_output = true,
                };
                CheckStream stream;
                ready = check_graph_run(&types, &input.graph, output_arena, scratch, &check_options, &stream);
                if (stage_clock != NULL)
                {
                    stage_clock->transition_hits += stream.statistics.transition_hits;
                    stage_clock->transition_misses += stream.statistics.transition_misses;
                    stage_clock->transition_folds += stream.statistics.transition_folds;
                    stage_clock->transition_entries += stream.statistics.transition_entries;
                }
                ready = ready && check_input_packed_finalize(&input, checking, &types, &stream, scratch);
                ready = ready && check_input_warning_graph(&input, checking, &types, &stream, output_arena, scratch);
                check_clock_mark(stage_clock, CHECK_STAGE_CHECK_EXPRESSIONS);
                if (ready && input.recovered_published != 0)
                {
                    // An array's recovered "array initializer must be an initializer list" whose initializer is no valid
                    // value, which clang reports alone (#1447).
                    check_init_listless_drop(&input, checking, &stream, &types, scratch);
                }
                if (ready)
                {
                    // The musttail returns' errors (c_check_musttail.c), each clang's, with the three below.
                    CheckMusttails musttails = { 0 };
                    // The reader's reports after checking are unavailable members' uses (check_input_member_uses, #147).
                    // A unit whose only reports are recovered ones (#1447, CheckInput.recovered_valid) was checked as a valid
                    // one is, and its diagnostics are found so; its result still fails, and nothing is emitted.
                    const bool checked_valid = input_valid || input.recovered_valid;
                    const bool expression_valid = checked_valid && stream.report_count == 0 && (input.report_count == 0 || input.recovered_valid);
                    const bool musttails_checked = !expression_valid || repairing || check_musttail_run(&musttails, &input, checking, &stream, &types, output_arena, scratch);
                    CheckDriverDiagnostic fallback[5] = { 0 };
                    CheckDriverDiagnostic* pushed = BUSTER_ARENA_PUSH_ARRAY(output_arena, CheckDriverDiagnostic, 5 + (uint64_t)musttails.count + 2 * typo_edits.count);
                    CheckDriverDiagnostic* additional = pushed != NULL ? pushed : fallback;
                    uint32_t additional_capacity = pushed != NULL ? 5 + musttails.count + 2 * typo_edits.count : 5;
                    uint32_t additional_count = 0;
                    if (pushed == NULL || !musttails_checked)
                    {
                        additional[additional_count++] = (CheckDriverDiagnostic){ 0, "musttail checking capacity exhausted", 0 };
                    }
                    for (uint32_t m = 0; pushed != NULL && m < musttails.count; m += 1)
                    {
                        const CheckMusttailDiagnostic* musttail = musttails.diagnostics + m;
                        const char* message = check_driver_musttail_message(checking, pp, &input, musttail, output_arena);
                        additional[additional_count++] = (CheckDriverDiagnostic){ musttail->token, message != NULL ? message : "musttail checking capacity exhausted",
                                                                                 musttail->position, musttail->completion, musttail->error == CHECK_MUSTTAIL_SCOPE };
                    }
                    // A statement start's typo corrections' errors (check_driver_typo_errors).
                    additional_count = pushed != NULL ? check_driver_typo_errors(&typo_edits, parser, checking, pp, additional, additional_count, output_arena) :
                                                        additional_count;
                    const uint32_t* excess_tokens = NULL;
                    uint32_t excess_count = 0;
                    CheckAsms asms = { 0 };
                    CheckInitializers initializers = { 0 };
                    // The initializer walk's output and scratch mustn't own these facts: its plan is rolled back on a
                    // failure, and the warning buffer outlives it for rendering.
                    CheckWarningBuffer initializer_warnings = { .arena = arena };
                    CheckWarningSet initializer_warning_keys = { .arena = arena };
                    bool initializer_warning_storage = true;
                    for (uint32_t i = 0; initializer_warning_storage && i < input.warning_events.count; i += 1)
                    {
                        const CheckWarningEvent* event = input.warning_events.events + i;
                        const bool selected = event->kind == CHECK_WARNING_EVENT_MESSAGE && event->id == CHECK_WARNING_ID_UNDEFINED_INTERNAL && event->payload.first_type != 0;
                        if (event->kind == CHECK_WARNING_EVENT_NON_LITERAL_NULL || event->kind == CHECK_WARNING_EVENT_NODEREF_CONVERSION || selected)
                        {
                            const uint64_t key = (uint64_t)(selected ? CHECK_WARNING_EVENT_KIND_COUNT : event->kind) << 32 | ((uint64_t)event->token + 1);
                            initializer_warning_storage = check_warning_set_add(&initializer_warning_keys, key);
                        }
                    }
                    if (!initializer_warning_storage)
                    {
                        initializers.status = CHECK_INIT_RESOURCE;
                        initializers.token = 0;
                    }
                    if (expression_valid && !repairing && initializer_warning_storage)
                    {
                        if (!check_asms_run(&asms, &input, &stream, &types, output_arena, scratch) &&
                            (asms.status == CHECK_ASM_RESOURCE ||
                             !check_driver_asm_errors(checking, pp, &input, &stream, &types, BUSTER_CHECK_NONE, &additional, &additional_capacity, &additional_count,
                                                      output_arena, scratch)))
                        {
                            additional[additional_count++] = (CheckDriverDiagnostic){ asms.token,
                                asms.status == CHECK_ASM_RESOURCE ? "assembly operand checking capacity exhausted" : "invalid assembly operand or constraint", asms.token };
                        }
                        if (check_initializers_run_events(&initializers, &input, &stream, checking, &types, output_arena, scratch,
                                                          &initializer_warnings, &initializer_warning_keys))
                        {
                            excess_tokens = initializers.warning_tokens;
                            excess_count = initializers.warning_count;
                        }
                    }
                    if (!expression_valid || repairing || initializers.status != CHECK_INIT_OK)
                    {
                        CheckInitDiagnostic earlier = { 0 };
                        const uint32_t before = repairing ? repair.record_begin : BUSTER_CHECK_NONE;
                        // Under -w too: its unavailable members' errors are its events' (#147), which publication keeps alone.
                        CheckInitWarningSink warnings = {
                            .capacity = tokens,
                            .events = &initializer_warnings,
                            .event_keys = &initializer_warning_keys,
                        };
                        warnings.tokens = warnings.capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output_arena, uint32_t, warnings.capacity) : NULL;
                        const bool storage = warnings.capacity == 0 || warnings.tokens != NULL;
                        const bool diagnosed = storage && initializers.status != CHECK_INIT_RESOURCE &&
                                               check_initializers_diagnose_warnings(&earlier, &input, &stream, checking, &types, before,
                                                                                     output_arena, scratch, &warnings);
                        if (!storage || initializers.status == CHECK_INIT_RESOURCE)
                        {
                            earlier.status = CHECK_INIT_RESOURCE;
                            earlier.token = initializers.token < tokens ? initializers.token : 0;
                        }
                        else if (!diagnosed && initializers.status != CHECK_INIT_OK)
                        {
                            earlier.status = initializers.status;
                            earlier.failed_type = initializers.failed_type;
                            earlier.failed_source = initializers.failed_source;
                            earlier.failed_offset = initializers.failed_offset;
                            earlier.failed_declaration = initializers.failed_declaration;
                            earlier.token = initializers.token;
                            earlier.completion = initializers.completion;
                        }
                        // The other declarations' failed initializers (#1447, phase 2), which clang reports too: room for
                        // two diagnostics each beside the first's.
                        const uint32_t others = diagnosed && earlier.status != CHECK_INIT_OK && earlier.status != CHECK_INIT_RESOURCE ? earlier.failure_count : 0;
                        CheckDriverDiagnostic* widened = others > 1 ?
                            BUSTER_ARENA_PUSH_ARRAY(output_arena, CheckDriverDiagnostic, 5 + (uint64_t)musttails.count + 2 * (uint64_t)others) : NULL;
                        if (widened != NULL)
                        {
                            memcpy(widened, additional, (uint64_t)additional_count * sizeof(CheckDriverDiagnostic));
                            additional = widened;
                        }
                        const uint32_t failure_total = widened != NULL ? others + 1 : 1;
                        for (uint32_t f = 0; f < failure_total; f += 1)
                        {
                            const CheckInitFailure* other = f != 0 ? earlier.failures + (f - 1) : NULL;
                            // The first is `earlier`'s own, which the list holds too.
                            const bool repeated = other != NULL && other->token == earlier.token && other->failed_declaration == earlier.failed_declaration &&
                                                  other->status == earlier.status;
                            const CheckInitFailure failure = other != NULL ? *other : (CheckInitFailure){
                                .status = earlier.status,
                                .failed_type = earlier.failed_type,
                                .failed_source = earlier.failed_source,
                                .failed_offset = earlier.failed_offset,
                                .failed_declaration = earlier.failed_declaration,
                                .token = earlier.token,
                                .completion = earlier.completion,
                            };
                            const bool given = other != NULL ? !repeated :
                                               (diagnosed && earlier.status != CHECK_INIT_OK) || earlier.status == CHECK_INIT_RESOURCE ||
                                               (!diagnosed && initializers.status != CHECK_INIT_OK);
                            if (given)
                            {
                                // A subobject's failed conversion is clang's words of it (#147), at its place.
                                uint32_t place = failure.token;
                                const char* conversion = failure.status == CHECK_INIT_INVALID ?
                                    check_driver_initializer_conversion(checking, checking_pp, &input, &stream, &types, failure.failed_type, failure.failed_source - 1,
                                                                        failure.failed_declaration, failure.failed_offset, failure.token, &place, output_arena) : NULL;
                                const bool designator = failure.status == CHECK_INIT_MEMBER || failure.status == CHECK_INIT_ARRAY_DESIGNATOR ||
                                                        failure.status == CHECK_INIT_FIELD_DESIGNATOR || failure.status == CHECK_INIT_NON_AGGREGATE_DESIGNATOR;
                                // A designated list of a non-aggregate is clang's at the declarator where it is the whole initializer (#2202),
                                // its type as the declarator spells it.
                                const bool outermost = failure.status == CHECK_INIT_NON_AGGREGATE_DESIGNATOR && failure.failed_declaration < input.token_count &&
                                                       check_sparse_get(input.initializer_begins, failure.failed_declaration) == failure.token;
                                uint32_t spelling = 0;
                                const TypeId designated = outermost ? check_input_object_spelling(&types, &input, checking, failure.failed_declaration, 0, failure.failed_type,
                                                                                                  &spelling) : failure.failed_type;
                                const char* message = conversion != NULL ? conversion : designator ?
                                    check_driver_initializer_field_message(checking, checking_pp, &input, &types, failure.status, designated, spelling, failure.token,
                                                                           output_arena) :
                                    check_driver_initializer_message(failure.status);
                                place = conversion != NULL ? place : outermost ? failure.failed_declaration : failure.token;
                                const TypeId converted = conversion != NULL && failure.failed_source != 0 && failure.failed_source - 1 < stream.count ?
                                    check_driver_conversion_source(&types, &stream, failure.failed_source - 1) : 0;
                                const bool default_error = converted != 0 && !type_kind_is_array(type_kind(&types, type_strip_aligned(&types, failure.failed_type))) &&
                                    check_driver_conversion_kind(&types, failure.failed_type, converted) != CHECK_DRIVER_CONVERSION_INCOMPATIBLE;
                                // (Its node plus one, CheckDriverDiagnostic.default_source.)
                                additional[additional_count++] = (CheckDriverDiagnostic){
                                    .token = place,
                                    .message = message != NULL ? message : "initializer checking capacity exhausted",
                                    .position = place,
                                    .completion = failure.completion,
                                    .default_source = default_error ? failure.failed_source : 0,
                                    .default_target = default_error ? failure.failed_type : 0,
                                };
                                // A static object's braced subobject whose conversion clang warns of and makes, where its value is
                                // then no constant: clang's second error at it (check_init_value_nonconstant, #147).
                                // Once at its place (#1812): a compound literal's own declaration and the array it initializes
                                // check the same body, each failure at the same subobject, which clang reports once.
                                const char* nonconstant = check_driver_initializer_message(CHECK_INIT_NON_CONSTANT);
                                bool given_before = false;
                                for (uint32_t d = 0; conversion != NULL && d < additional_count; d += 1)
                                {
                                    given_before |= additional[d].repeated && additional[d].token == place && additional[d].message == nonconstant;
                                }
                                if (conversion != NULL && !given_before && failure.failed_source != 0 && failure.failed_declaration < input.token_count &&
                                    check_input_bit(input.declaration_bits, failure.failed_declaration, BUSTER_CHECK_BITS_STATIC) != 0 &&
                                    check_init_value_nonconstant(&stream, &types, failure.failed_type, failure.failed_source - 1))
                                {
                                    // (Repeated: at the conversion's own place, which the reports' walk would keep alone.)
                                    additional[additional_count++] = (CheckDriverDiagnostic){ place, nonconstant, place, failure.completion, true };
                                }
                            }
                        }
                        // A file-scope array's recovered mismatch with the compound literal it is initialized by (#1812, "cannot
                        // initialize array of type ... with array of type ..."), whose body holds a value that is no constant:
                        // clang's literal is invalid then, and its initialization gives no error of its own.
                        uint32_t kept_reports = 0;
                        for (uint32_t r = 0; r < input.report_count; r += 1)
                        {
                            const CheckInputReport* report = input.reports + r;
                            const bool candidate = report->recovered && report->message == CHECK_INPUT_ARRAY_LITERAL_MISMATCH && report->token < input.token_count &&
                                                   check_input_bit(input.declaration_bits, report->token, BUSTER_CHECK_BITS_STATIC) != 0 &&
                                                   check_sparse_get(input.declaration_bodies, report->token) >= input.token_count;
                            bool nonconstant_body = false;
                            for (uint32_t d = 0; candidate && d < additional_count; d += 1)
                            {
                                nonconstant_body |= additional[d].token > report->token && additional[d].token < report->completion &&
                                                    additional[d].message == check_driver_initializer_message(CHECK_INIT_NON_CONSTANT);
                            }
                            for (uint32_t u = 0; candidate && u < input.report_count; u += 1)
                            {
                                nonconstant_body |= input.reports[u].message == CHECK_INPUT_CONSTANT_INITIALIZER && input.reports[u].token > report->token &&
                                                    input.reports[u].token < report->completion;
                            }
                            input.reports[kept_reports] = *report;
                            kept_reports += !nonconstant_body;
                            input.recovered_published -= nonconstant_body;
                        }
                        input.report_count = kept_reports;
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
                        const bool listed = check_driver_asm_errors(checking, pp, &input, &stream, &types, before, &additional, &additional_capacity, &additional_count,
                                                                    output_arena, scratch);
                        const bool asm_diagnosed = !listed && check_asms_diagnose(&assembly, &input, &stream, &types, before, scratch);
                        if (!listed && ((asm_diagnosed && assembly.status != CHECK_ASM_OK) || assembly.status == CHECK_ASM_RESOURCE))
                        {
                            additional[additional_count++] = (CheckDriverDiagnostic){ assembly.token,
                                assembly.status == CHECK_ASM_RESOURCE ? "assembly operand checking capacity exhausted" : "invalid assembly operand or constraint", assembly.token };
                        }
                    }
                    const bool initializer_context = check_initializers_warning_context(&initializer_warnings, &input, checking, &types, scratch);
                    if (!initializer_context)
                    {
                        fprintf(errors, "fatal error: initializer diagnostic capacity exhausted\n");
                        driver_fatal = true;
                        ready = false;
                    }
                    const CheckWarningEvents initializer_warning_view = initializer_context ? check_warning_view(&initializer_warnings) : (CheckWarningEvents){ 0 };
                    uint32_t promoted_token = BUSTER_CHECK_NONE;
                    bool promoted = !quiet && check_diagnostic_first_error(&input, &stream, &promoted_token);
                    promoted |= check_driver_warning_promoted(pp, &input, &input.warning_events, repairing ? &repair : NULL);
                    promoted |= check_driver_warning_promoted(pp, &input, &initializer_warning_view, repairing ? &repair : NULL);
                    promoted |= check_driver_warning_promoted(pp, &input, &stream.warning_events, repairing ? &repair : NULL);
                    uint32_t promoted_source = BUSTER_CHECK_NONE;
                    const CheckInput* pragma_input = original_pragmas_ready ? &original_pragmas : &input;
                    const uint32_t pragma_end = original_pragmas_ready || !repairing ? BUSTER_CHECK_NONE : check_repair_pragma_end(&repair);
                    promoted |= check_driver_warning_promoted(pp, pragma_input, &pragma_input->pragma_warnings, NULL);
                    promoted |= check_diagnostic_first_pragma_error_mode(pragma_input, pragma_end, &promoted_token, &promoted_source, quiet);
                    bool visibility_diagnostics = false;
                    bool visibility_errors = false;
                    const CheckInputVisibilityPragmas* visibility_history = pragma_input->visibility_pragmas;
                    for (uint32_t i = 0; visibility_history != NULL && i < visibility_history->count; i += 1)
                    {
                        const CheckInputVisibilityPragma* record = visibility_history->records + i;
                        visibility_diagnostics |= record->error || record->warning != 0;
                        visibility_errors |= record->error;
                    }
                    for (uint32_t i = 0; !quiet && !promoted && i < excess_count; i += 1)
                    {
                        promoted = check_diagnostic_severity(&input, excess_tokens[i], CHECK_WARNING_EXCESS_INITIALIZERS) >= CHECK_WARNING_ERROR;
                    }
                    if (!expression_valid || repairing || additional_count != 0 || promoted || visibility_diagnostics || pp->errors != 0)
                    {
                        CheckProgramDiagnostic control = { 0 };
                        const uint32_t control_before = repairing ? repair.record_begin : BUSTER_CHECK_NONE;
                        const bool control_diagnosed = check_program_diagnose_all(&control, &input, &stream, checking, &types, control_before, output_arena, scratch);
                        // Every constraint error, in clang's words and order, with its previous case's note (#1447).
                        const bool listed = control_diagnosed && control.constraint_count != 0 &&
                                            check_driver_additional_room(&additional, &additional_capacity, additional_count, control.constraint_count, output_arena);
                        for (uint32_t c = 0; listed && c < control.constraint_count; c += 1)
                        {
                            const CheckProgramConstraint* constraint = control.constraints + c;
                            additional[additional_count++] = (CheckDriverDiagnostic){
                                .token = constraint->token,
                                .message = check_semantic_constraint(checking, pp, &input, &types, constraint, output_arena),
                                .position = constraint->position,
                                .completion = constraint->position < tokens ? constraint->position + 1u : tokens,
                                .repeated = true,
                                .note_token = constraint->previous,
                                .note = constraint->previous != BUSTER_CHECK_NONE ? "previous case defined here" : NULL,
                            };
                        }
                        if (!listed && ((control_diagnosed && control.status != CHECK_PROGRAM_OK) || control.status == CHECK_PROGRAM_RESOURCE))
                        {
                            additional[additional_count++] = (CheckDriverDiagnostic){ control.token,
                                check_semantic_control(pp, &types, control.status, &control.failure, scratch), control.position,
                                control.position < tokens ? control.position + 1u : tokens };
                        }
                    }
                    // The errors the reports' publication below makes: warnings promoted to errors and visibility pragmas'
                    // errors, which reject the unit as those already published do.
                    const bool publication_errors = promoted || visibility_errors;
                    result = !driver_fatal && ready && expression_valid && !input.recovered_valid && additional_count == 0 && !repairing && pp->errors == 0 &&
                             !publication_errors && !pp->diagnostic_collection_failed && !pp->history_failed ? 0 : 1;
                    CheckProgram program;
                    CheckDataPrepared data = { 0 };
                    check_clock_mark(stage_clock, CHECK_STAGE_CHECK_DECLARATIONS);
                    if (result == 0 && (emit_object || link) && !prefix_only && !check_data_prepare(&data, &input, &stream, &initializers, &types, scalar, output_arena, scratch))
                    {
                        // A named refusal of the static data (an NSString of no literal's object, #1785), as the code's are.
                        char refusal[256];
                        snprintf(refusal, sizeof(refusal), "unsupported: %s", data.reason != NULL ? data.reason : "");
                        check_driver_diagnose(checking, pp, data.token, data.status == CHECK_DATA_INPUT_UNSUPPORTED && data.reason != NULL ? refusal :
                                                                        "static-data preparation failed");
                        result = 1;
                    }
                    check_clock_mark(stage_clock, CHECK_STAGE_STATIC_DATA);
                    // The identifiers, the target and the predefines are the unit's preprocessor's, as every client's
                    // program takes them (check_request_program_options, #1057).
                    const CheckProgramOptions program_options = check_request_program_options(pp, (CheckProgramOptions){
                        .initializers = &initializers,
                        .asms = &asms,
                        .prepared_data = result == 0 && (emit_object || link) && !prefix_only ? &data : NULL,
                        .initializer_references = data.references,
                        .initializer_reference_count = data.reference_count,
                        .scalar = scalar,
                        .return_warnings = !quiet && return_warning,
                        .noreturn_warnings = !quiet,
                        // Tier 1's frame shares its objects' slots by their blocks' lifetimes (#177).
                        .lifetimes = lanes != NULL && lanes->tier1 && !lanes->tier1_no_lifetimes,
                        // Tier 1's IR takes asm bodies (#1839), not a statement's row.
                        .asm_bodies = lanes != NULL && lanes->tier1,
                        .helper_forms = lanes != NULL ? lanes->helper_forms : 0,
                        // A units lane with a unit left to take gives its scratch's pages back in whole huge pages
                        // (#777).
                        .discard_huge = lanes != NULL && lanes->units_lane != NULL && check_units_discard_huge(lanes->units_lane),
                    });
                    // A resume that read the whole unit (#147) checks its view as the semantic unit it is, its `prefix_only`
                    // keeping only the emission and the translation unit's end obligations back (`checking` the recovered
                    // parser, where a prefix's or a repair's is a view of its own).
                    const bool program_diagnostics = result == 0 ||
                        (ready && expression_valid && additional_count == 0 && !repairing && (!prefix_only || checking == recovered) &&
                         (pp->errors != 0 || publication_errors || input.recovered_valid) && !pp->diagnostic_collection_failed && !pp->history_failed);
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
                    // A control error's constraints, where check_program finds one: every one, as the diagnosis of a rejected unit
                    // gives them (#1447), among the unit's errors, which the reports below publish, so that the unit's warnings
                    // leave out what clang's do after them (check_driver_rejected_warnings).
                    CheckProgramDiagnostic control = { 0 };
                    if (program_diagnostics && !program_checked)
                    {
                        const bool listed = program.status == CHECK_PROGRAM_INVALID &&
                                            check_program_diagnose_all(&control, program_input, &stream, checking, &types, BUSTER_CHECK_NONE, output_arena, scratch) &&
                                            control.constraint_count != 0 &&
                                            check_driver_additional_room(&additional, &additional_capacity, additional_count, control.constraint_count, output_arena);
                        for (uint32_t c = 0; listed && c < control.constraint_count; c += 1)
                        {
                            const CheckProgramConstraint* constraint = control.constraints + c;
                            additional[additional_count++] = (CheckDriverDiagnostic){
                                .token = constraint->token,
                                .message = check_semantic_constraint(checking, pp, program_input, &types, constraint, output_arena),
                                .position = constraint->position,
                                .completion = constraint->position < tokens ? constraint->position + 1u : tokens,
                                .repeated = true,
                                .note_token = constraint->previous,
                                .note = constraint->previous != BUSTER_CHECK_NONE ? "previous case defined here" : NULL,
                            };
                        }
                        if (!listed)
                        {
                            additional[additional_count++] = (CheckDriverDiagnostic){ program.token, check_semantic_control(pp, &types, program.status, &program.failure, output_arena),
                                                                                     program.token };
                        }
                        result = 1;
                    }
                    CheckCommonShiftWarnings shift_warnings = { 0 };
                    // The unit's errors, whose warnings clang leaves out (check_driver_rejected_warnings): a preprocessing or parse
                    // error counts at the unit's start, for the unit-end warnings alone.
                    const bool unit_errors = pp->errors != 0 || parse_failed || input.report_count != 0 || stream.report_count != 0 || additional_count != 0;
                    const bool located = input.report_count != 0 || stream.report_count != 0 || additional_count != 0;
                    const uint32_t* error_counts = located ? check_driver_error_counts(checking, &input, &stream, &types, additional, additional_count, scratch) : NULL;
                    CheckWarningEvents input_warnings;
                    // The unit-end warnings publish after every other diagnostic (check_driver_unit_end_split).
                    CheckWarningEvents unit_end_warnings[2] = { 0 };
                    const bool unit_end_ready = (!located || error_counts != NULL) &&
                        check_driver_rejected_warnings(checking, &input.warning_events, unit_errors, error_counts, scratch, &input_warnings) &&
                        check_driver_unit_end_split(&input_warnings, output_arena, &input_warnings, unit_end_warnings);
                    if (!unit_end_ready ||
                        !check_driver_initializer_warning_events(checking, pp, &input, &types, &initializer_warning_view, repairing ? &repair : NULL, quiet, scratch) ||
                        !check_driver_reports(checking, recovered, pp, publication_end, &input, &stream, &types, repairing ? &repair : NULL,
                                              original_pragmas_ready ? &original_pragmas : NULL, additional, additional_count,
                                              NULL, 0, quiet, &shift_warnings, scratch) ||
                        !check_driver_warning_events_at(checking, pp, &input, &types, &input_warnings, repairing ? &repair : NULL, publication_end, scratch) ||
                        !check_driver_warning_events_at(checking, pp, &input, &types, &stream.warning_events, repairing ? &repair : NULL, publication_end, scratch))
                    {
                        fprintf(errors, "fatal error: checker diagnostic capacity exhausted\n");
                        driver_fatal = true;
                        ready = false;
                        result = 1;
                    }
                    original_pragmas_printed = true;
                    if (ready && !quiet && !parse_failed && !repairing && !prefix_only && checking == recovered && input.report_count != 0 &&
                        input.walk_end != 0 && input.walk_end < tokens && !pp->fatal)
                    {
                        // The literals past where the reader's error stopped it (#1447): clang's literal parsers diagnose them as
                        // they do the unchecked rest of a failed parse.
                        check_driver_unread_literals(checking, pp, &input, scratch);
                    }
                    if (input_valid && dump && !prefix_only)
                    {
                        check_driver_dump(&input.graph, &stream, &types, scratch);
                    }
                    // A warning the publication promoted to an error rejects the unit.
                    result = pp->errors != 0 ? 1 : result;
                    check_driver_x86_reports(checking, recovered, pp, &input, &stream, &types, quiet);
                    if (program_checked)
                    {
                        const CheckWarningSeverity baseline = !return_warning ? CHECK_WARNING_IGNORE :
                            return_error > 0 || (return_error == 0 && warning_errors) ? CHECK_WARNING_ERROR : CHECK_WARNING_WARNING;
                        check_driver_return_reports(checking, recovered, pp, &program, baseline, quiet);
                        if (!quiet)
                        {
                            // (The scratch's flushed marks go with the common warnings' own, which give it back past them.)
                            const uint64_t flushed_mark = scratch->length;
                            const uint8_t* flushed = check_driver_flushed_bodies(checking, recovered, pp, &program, scratch);
                            check_driver_common_warnings(checking, pp, &input, &stream, &types, &program, flushed, &shift_warnings, scratch);
                            arena_pop_to(scratch, flushed_mark);
                        }
                        else
                        {
                            // -w spares the common checks that are errors by default (#2264): a chained comparison's.
                            check_common_chained_comparisons(checking, pp, &input, scratch);
                        }
                    }
                    else if (!driver_fatal && ready && (!prefix_only || checking == recovered) && !repairing && !pp->fatal &&
                             !pp->diagnostic_collection_failed && !pp->history_failed && (located || (program_diagnostics && program.token < tokens)))
                    {
                        // A rejected unit's common warnings, of the bodies its errors don't reach (#1447): its reader's, its
                        // graph's and its other errors' tokens, and its program's. A parse a resume read whole (#147) is
                        // one too: its checking is the recovered parser, where a prefix's or a repair's is a view of its own.
                        // Under -w too (#2162): its rounds' readings find the errors past a reader's error that ended its walk,
                        // which clang gives, where -w's policy leaves out every warning they publish.
                        const uint64_t error_room = (uint64_t)input.report_count + stream.report_count + 2 * (uint64_t)additional_count + control.constraint_count + 1;
                        uint32_t* error_tokens = error_room <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(output_arena, uint32_t, error_room) : NULL;
                        uint32_t error_count = 0;
                        for (uint32_t i = 0; error_tokens != NULL && i < input.report_count; i += 1)
                        {
                            error_tokens[error_count++] = input.reports[i].token;
                        }
                        // The conversions that default to errors whose sources the view casts (#1969): their tokens come last.
                        const uint64_t site_room = (uint64_t)stream.report_count + additional_count;
                        CheckDriverConversionSite* sites = error_tokens != NULL && site_room != 0 ?
                            BUSTER_ARENA_PUSH_ARRAY(output_arena, CheckDriverConversionSite, site_room) : NULL;
                        uint32_t site_count = 0;
                        uint8_t* nonconstant = NULL;
                        bool nonconstant_fresh = true;
                        for (uint32_t i = 0; error_tokens != NULL && i < stream.report_count; i += 1)
                        {
                            uint32_t source = BUSTER_CHECK_NONE;
                            TypeId target = 0;
                            const bool default_error = check_driver_report_default_error(checking, &stream, &types, stream.reports + i, &source, &target);
                            if (default_error && sites != NULL &&
                                check_driver_conversion_site(checking, &stream, &types, stream.reports[i].token, source, target, sites + site_count))
                            {
                                site_count += 1;
                            }
                            else
                            {
                                error_tokens[error_count++] = stream.reports[i].token;
                            }
                        }
                        for (uint32_t i = 0; error_tokens != NULL && i < additional_count; i += 1)
                        {
                            const uint32_t source = additional[i].default_source != 0 ? additional[i].default_source - 1 : BUSTER_CHECK_NONE;
                            if (additional[i].default_source != 0 && additional[i].token == additional[i].position && sites != NULL &&
                                check_driver_conversion_site(checking, &stream, &types, additional[i].token, source, additional[i].default_target, sites + site_count))
                            {
                                site_count += 1;
                            }
                            else
                            {
                                error_tokens[error_count++] = additional[i].token;
                                error_tokens[error_count++] = additional[i].position;
                            }
                            // A static initializer's non-constant error (#1447), whose initializer the view keeps.
                            const char* nonconstant_message = check_driver_initializer_message(CHECK_INIT_NON_CONSTANT);
                            if (additional[i].message != NULL && strcmp(additional[i].message, nonconstant_message) == 0 && additional[i].token < tokens)
                            {
                                nonconstant = nonconstant != NULL ? nonconstant : BUSTER_ARENA_PUSH_ARRAY(output_arena, uint8_t, tokens);
                                if (nonconstant != NULL && nonconstant_fresh)
                                {
                                    memset(nonconstant, 0, tokens);
                                    nonconstant_fresh = false;
                                }
                                if (nonconstant != NULL)
                                {
                                    nonconstant[additional[i].token] = 1;
                                }
                            }
                        }
                        for (uint32_t i = 0; error_tokens != NULL && i < control.constraint_count; i += 1)
                        {
                            error_tokens[error_count++] = control.constraints[i].token;
                        }
                        if (error_tokens != NULL && program_diagnostics)
                        {
                            error_tokens[error_count++] = program.token;
                        }
                        for (uint32_t i = 0; i < site_count; i += 1)
                        {
                            error_tokens[error_count++] = sites[i].token;
                        }
                        const CheckWarningSeverity baseline = !return_warning ? CHECK_WARNING_IGNORE :
                            return_error > 0 || (return_error == 0 && warning_errors) ? CHECK_WARNING_ERROR : CHECK_WARNING_WARNING;
                        if (error_tokens != NULL)
                        {
                            check_driver_rejected_common(checking, recovered, pp, &input, &types, error_tokens, error_count, sites, site_count, nonconstant, scalar,
                                                         return_warning, baseline,
                                                         &shift_warnings,
                                                         &input.warning_events, &stream.warning_events,
                                                         input.walk_end != 0 && input.diagnostic_limits == NULL ? input.walk_end : tokens, publication_end, arena,
                                                         scratch);
                        }
                    }
                    // The folded shifts' stashed runtime warnings no common walk reported or dropped (a unit with errors runs
                    // none).
                    check_common_shift_finish(pp, &shift_warnings);
                    // The unit's end, in clang's ActOnEndOfTranslationUnit's order, after every other diagnostic (#2238):
                    // the internal functions used but never defined, unless an error has occurred (clang's
                    // hasErrorOccurred, which counts a warning -Werror promotes); the tentative arrays' warnings; then, again
                    // unless an error has occurred, a promoted one among those included, the unused file-scope declarations'.
                    const bool final_warnings = pp->errors == 0 && !pp->fatal && program_checked && !prefix_only && !repairing && !parse_failed &&
                                                input.deferred_tokens == NULL && !pp->diagnostic_collection_failed && !pp->history_failed;
                    if (final_warnings)
                    {
                        CheckInput undefined_input;
                        CheckWarningBuffer undefined_warnings = { .arena = output_arena };
                        const bool completed = check_initializers_warning_uses(&undefined_input, &input, &initializer_warning_view, output_arena) &&
                                               check_warning_undefined_internal(&undefined_warnings, &undefined_input, checking, &types, &stream, scratch);
                        const CheckWarningEvents undefined_view = check_warning_view(&undefined_warnings);
                        if (!completed || !check_driver_warning_events_at(checking, pp, &input, &types, &undefined_view, NULL, pp->output.count, scratch))
                        {
                            fprintf(errors, "fatal error: final diagnostic capacity exhausted\n");
                            driver_fatal = true;
                            result = 1;
                        }
                        result = pp->errors != 0 ? 1 : result;
                    }
                    if (!pp->fatal && unit_end_warnings[0].count != 0 &&
                        !check_driver_warning_events_at(checking, pp, &input, &types, unit_end_warnings, repairing ? &repair : NULL, publication_end, scratch))
                    {
                        fprintf(errors, "fatal error: checker diagnostic capacity exhausted\n");
                        driver_fatal = true;
                    }
                    if (!pp->fatal && pp->errors == 0 && unit_end_warnings[1].count != 0 &&
                        !check_driver_warning_events_at(checking, pp, &input, &types, unit_end_warnings + 1, repairing ? &repair : NULL, publication_end, scratch))
                    {
                        fprintf(errors, "fatal error: checker diagnostic capacity exhausted\n");
                        driver_fatal = true;
                    }
                    result = pp->errors != 0 ? 1 : result;
                    check_clock_mark(stage_clock, CHECK_STAGE_CHECK_PROGRAM);
                    // The backend reads the program, not the preprocessor's output: only its diagnostics do, which ask for it
                    // first (pp_output_ensure), and -g's line table, whose units keep their history and so their output. So a
                    // unit without errors that goes on to its object or executable gives the output back; one that links with
                    // others keeps it for the link, and one with errors for the diagnostics after these, which read it as they
                    // are. --poison-arenas makes it unreadable instead, for a test to find a reader that doesn't ask
                    // (pp_release_output, #628).
                    if (result == 0 && (emit_object || link) && kept == NULL && !prefix_only && !dump_static && !parse_failed && !repairing)
                    {
                        pp_release_output(pp, command->poison_arenas);
                    }
                    // -c's object, or a kept link (one of several units that link), are written alike: the unit's code and
                    // data.
                    if (result == 0 && (emit_object || kept != NULL) && !prefix_only)
                    {
                        CheckProgramSelection selected;
                        const bool selection = check_program_select_mode(&selected, &program, &data, lower_all ? C_LOWERING_ALL : C_LOWERING_REACHABLE, scalar,
                                                                         kept != NULL, output_arena, scratch);
                        check_clock_mark(stage_clock, CHECK_STAGE_SELECT);
                        if (selection)
                        {
                            result = check_driver_target_features(&selected.program, checking, recovered, pp, &options->predefines, scalar, output_arena, scratch) ? result : 1;
                            result = check_driver_aliases(&selected.program, checking, pp, scalar, scratch) ? result : 1;
                        }
                        if (selection && result == 0)
                        {
                            const char* object_path = output != 0 ? output : check_driver_object_name(arena, path);
                            result = object_path != 0 && check_driver_object(&selected.program, &selected.data, checking, pp, &types, path,
                                                                           object_path, scalar, verify, dump_static, source_rows, unwind_tables, lanes,
                                                                           scratch, stage_clock, errors, kept, command->mixed_inputs, &driver_fatal) ? 0 : 1;
                        }
                        else if (!selection)
                        {
                            check_driver_diagnose(checking, pp, selected.token, "checked definition selection failed");
                            result = 1;
                        }
                    }
                    if (result == 0 && link && kept == NULL && !prefix_only)
                    {
                        CheckProgramSelection selected;
                        const bool selection = check_program_select_mode(&selected, &program, &data, lower_all ? C_LOWERING_ALL : C_LOWERING_REACHABLE, scalar,
                                                                         false, output_arena, scratch);
                        check_clock_mark(stage_clock, CHECK_STAGE_SELECT);
                        if (selection)
                        {
                            result = check_driver_target_features(&selected.program, checking, recovered, pp, &options->predefines, scalar, output_arena, scratch) ? result : 1;
                            result = check_driver_aliases(&selected.program, checking, pp, scalar, scratch) ? result : 1;
                        }
                        if (selection && result == 0)
                        {
                            result = check_driver_executable(&selected.program, &selected.data, checking, pp, &types, output != 0 ? output : "a.out", scalar, verify,
                                                             source_rows, unwind_tables, command->libraries, lanes, scratch, stage_clock, NULL, 0, command->runtime_kind) ? 0 : 1;
                        }
                        else if (!selection)
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
                    driver_fatal = true;
                }
            }
            else
            {
                // (With a statement start's typo corrections' errors, check_driver_typo_errors.)
                CheckDriverDiagnostic* typo_errors = typo_edits.count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output_arena, CheckDriverDiagnostic, 2 * typo_edits.count) : NULL;
                const uint32_t typo_count = typo_errors != NULL ? check_driver_typo_errors(&typo_edits, parser, checking, pp, typo_errors, 0, output_arena) : 0;
                if (!check_driver_reports(checking, recovered, pp, publication_end, &input, NULL, &types, repairing ? &repair : NULL,
                                          original_pragmas_ready ? &original_pragmas : NULL, typo_errors, typo_count, NULL, 0, quiet, NULL, scratch) ||
                    !check_driver_warning_events_at(checking, pp, &input, &types, &input.warning_events, repairing ? &repair : NULL, publication_end, scratch))
                {
                    fprintf(errors, "fatal error: checker diagnostic capacity exhausted\n");
                    driver_fatal = true;
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
                check_driver_remaining_pragmas(recovered, pp, &original_pragmas, &fp, &visibility, quiet);
                if (!check_driver_warning_events_at(recovered, pp, &original_pragmas, NULL, &original_pragmas.pragma_warnings, NULL, publication_end, scratch))
                {
                    fprintf(errors, "fatal error: original pragma warning capacity exhausted\n");
                    driver_fatal = true;
                }
            }
            if (!pp->fatal)
            {
                // The checked view's reports covered its tokens: the original ones of a repair, a prefix's whole.
                const uint32_t checked_end = !ready || !prefix_only ? 0 : repairing ? repair.original_end : checking->tokens.count;
                check_driver_unparsed_literals(recovered, pp, checked_end, scratch);
                check_driver_unparsed_strings(recovered, pp, checked_end, scratch);
                // The parse's errors as clang's recovery reads on past a missing `;` (check_prefix_resume): the original's
                // places, with what its own recovery skipped.
                Parser printed = *parser;
                printed.diagnostics = recovered != parser ? resumed.diagnostics : parser->diagnostics;
                printed.diagnostic_count = recovered != parser ? resumed.diagnostic_count : parser->diagnostic_count;
                // (A unit that only a typo's correction failed printed its parse's warnings already.)
                uint32_t kept_count = 0;
                // (Nor does a typo's tail left out, check_driver_typo_tail, give its error.)
                for (uint32_t d = 0; recovered != parser && d < printed.diagnostic_count; d += 1)
                {
                    resumed.diagnostics[kept_count] = resumed.diagnostics[d];
                    kept_count += (resumed.diagnostics[d].error || !parse_printed) && !resumed.diagnostics[d].tail ? 1u : 0u;
                }
                printed.diagnostic_count = recovered != parser ? kept_count : printed.diagnostic_count;
                // (An error that names a corrected token spells the correction: "expected '(' after 'while'".)
                uint32_t* respelled = typo_edits.count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)parser->tokens.count + 1) : NULL;
                for (uint32_t e = 0; respelled != NULL && e < typo_edits.count; e += 1)
                {
                    if (e == 0)
                    {
                        memset(respelled, 0, ((uint64_t)parser->tokens.count + 1) * sizeof(uint32_t));
                    }
                    respelled[typo_edits.edits[e].token] = typo_edits.edits[e].end == 0 ? typo_edits.edits[e].name : 0;
                }
                printed.respelled = respelled;
                parse_print_diagnostics(&printed, pp);
            }
            result = 1;
        }
    }
    // What follows the last stage (a dump, diagnostics after a failed parse) is charged to the release.
    check_clock_mark(stage_clock, CHECK_STAGE_RELEASE);
    pp_diagnostic_publish(pp);
    pp_diagnostic_summary(pp);
    result = pp->errors != 0 || driver_fatal ? 1 : result;
    // A failure that memory may explain (#1521), which the stage that failed may have reported as its capacity: on a
    // thread switched to compact reservations, each arena it reserved since then holds at most
    // BUSTER_ARENA_COMPACT_BYTES; and a commit refused since the unit started (strict overcommit, a finite RLIMIT_DATA)
    // left a push short of memory.
    const ArenaFailure failure = arena_failure;
    const bool refused = failure.commit && (failure.bytes != failure_before.bytes || failure.error != failure_before.error || !failure_before.commit);
    if (result != 0 && arena_compact && !memory_reported)
    {
        fprintf(errors, "note: memory reservations were limited to %llu MiB each after the address space refused a larger one; a capacity error above may be that limit\n",
                (unsigned long long)(BUSTER_ARENA_COMPACT_BYTES >> 20));
    }
    if (result != 0 && refused && !memory_reported)
    {
        fprintf(errors, "note: the system refused to commit %llu bytes of memory (%s); a capacity error above may be that refusal\n",
                (unsigned long long)failure.bytes, strerror(failure.error != 0 ? failure.error : ENOMEM));
    }
    if (fatal != NULL)
    {
        *fatal = pp->fatal || driver_fatal;
    }
    if (result == 0 && options->dependencies.enabled)
    {
        result = pp_dependencies(pp, path, output, dependency_arena, dependency, errors) ? 0 : 1;
    }
    if (release)
    {
        parse_release(parser);
        pp_release(pp);
        arena_release(&index_arena);
        arena_release(&type_arena);
    }
    return result;
}

// A command of the semantic driver as its flow runs it (#905): what check_driver_command made of its options, the flow's
// rounds (a unit's emission's, and several units' wide parts', LaneFlows), and the command's exit status, lane 0's.
typedef struct CheckDriverRun CheckDriverRun;
struct CheckDriverRun
{
    const CheckDriverCommand* command;
    char** inputs;
    uint32_t input_count;
    const char* path;
    CheckProgramLanes* lanes;
    const uint32_t* pinned_cpus;
    CheckClock* stage_clock;
    uint64_t* pp_ticks;
    Arena* arena;
    Arena* scratch;
    Arena* output_arena;
    bool release;
    LaneFlow flow;
    CheckUnitsFlow units_flow;
    // The lanes the command asks for (check_driver_run), which --time-stages reports with those its flow ran on.
    uint32_t capacity;
    int result;
};

// One source file's command, on lane 0: the unit's intern table, preprocessor and parser, which the process's commands
// take in turn, initialized for it (and with `release`, released after it).
static int check_driver_one_unit(const CheckDriverRun* run)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    // The deferred bodies' state (#151), which the process's commands take in turn too: its lanes' memory stays in place
    // from one command's unit to the next, and the unit's input points into it until the unit is done.
    static CheckDeferred deferred;
    const bool interned = intern_initialize(&identifiers);
    DependencyOutput dependency = { 0 };
    PPOptions options = run->command->options;
    options.preprocessed = run->command->inputs != NULL && run->command->inputs[0].preprocessed;
    int result = check_driver_unit(run->command, &options, run->path, interned ? &identifiers : NULL, &pp, &parser, run->arena, run->scratch,
                                  run->output_arena, run->lanes, run->stage_clock, run->pp_ticks, stderr, NULL, run->release, &dependency, run->arena,
                                  run->command->deferred_bodies ? &deferred : NULL, NULL);
    if (result == 0)
    {
        result = dependency_publish(&dependency, stderr) ? 0 : 1;
    }
    if (run->release)
    {
        intern_release(&identifiers);
        check_deferred_release(&deferred);
    }
    return result;
}

// The command's flow, which every lane of its session runs (#905). One source file: lane 0 runs the unit's stages alone,
// and every lane runs each round of its emission (check_program_emit_round), the bodies' parts on as many lanes as the
// bodies' tokens and the worker budget pay for. Several (#152): lane 0 runs their driver alone, and every lane runs each
// of its rounds (CheckUnitsFlow): the units' run, the merge, the link's check and the write. The command's lanes are the
// session's at most: one after a failed start.
static void check_driver_flow(void* argument)
{
    CheckDriverRun* run = argument;
    if (run->pinned_cpus != NULL && run->stage_clock != NULL)
    {
        unsigned cpu = UINT_MAX;
        const long observed = syscall(SYS_getcpu, &cpu, NULL, NULL);
        fprintf(stderr, "lane-pin\tindex=%u\texpected=%u\tobserved=%u\tstatus=%ld\n", lane_index(), run->pinned_cpus[lane_index()], cpu, observed);
    }
    if (lane_index() == 0)
    {
        check_clock_mark(run->stage_clock, CHECK_STAGE_LANES_START);
        run->lanes->lanes = run->lanes->lanes < lane_count() ? run->lanes->lanes : lane_count();
    }
    if (run->input_count > 1 || run->command->mixed_inputs)
    {
        // Lane 0 runs the units' driver alone, its wide parts rounds of the flow (CheckUnitsFlow), the lanes' start having
        // gone on until the first.
        if (lane_index() == 0)
        {
            run->lanes->units_flow = &run->units_flow;
            check_units_flow_begin(&run->units_flow);
            run->result = check_driver_units(run->command, run->inputs, run->input_count, run->lanes, run->stage_clock, run->release);
            check_units_flow_end(&run->units_flow);
            run->lanes->units_flow = NULL;
        }
        else
        {
            check_units_follow(&run->units_flow);
        }
    }
    else if (lane_index() == 0)
    {
        run->lanes->flow = &run->flow;
        lane_flow_begin(&run->flow);
        run->result = check_driver_one_unit(run);
        lane_flow_end(&run->flow);
        run->lanes->flow = NULL;
    }
    else
    {
        check_program_emit_follow(&run->flow);
    }
    // The lanes the session asked for, by default one per core of the capacity's CPUs, and those the flow ran on, whose
    // group lane 0 is a lane of again at its end: one after a failed start.
    if (lane_index() == 0)
    {
        const uint32_t asked = lane_session_current() != NULL ? lane_session_asked() : run->capacity;
        check_clock_lanes(run->stage_clock, asked < run->capacity ? asked : run->capacity, lane_count());
    }
}

// Runs the command's flow (#905). With no session, the command's own starts here: as many lanes as the command may run
// (at most its allowed CPUs, --lanes N and --worker-budget N), by default at most one per core of those CPUs, which lane
// 1 counts while lane 0 starts the command (BUSTER_LANE_SESSION_PER_CORE), but one for one source file without code to
// emit (-fsyntax-only, check without -c, a dump), whose flow has no wide part, and for one with code, by default, none
// until its unit is preprocessed and large enough to pay for them (check_driver_start_lanes); a terminal command
// (without `release`) leaves its lanes parked for the process's exit. In a session, the flow is a collective call's,
// which every lane makes (run_check_command_mode), whose other lanes lane 0 tells of the flow (`*announced`), or a lane's
// alone. Returns the command's exit status.
static int check_driver_run(CheckDriverRun* run, bool* announced)
{
    const CheckDriverCommand* command = run->command;
    const uint32_t cap = run->lanes->budget != NULL && run->lanes->budget->maximum != 0 ? run->lanes->budget->maximum : UINT32_MAX;
    uint32_t capacity = run->lanes->lanes < cap ? run->lanes->lanes : cap;
    const uint32_t cpus = check_driver_cpus();
    capacity = capacity < cpus ? capacity : cpus;
    capacity = capacity == 0 || (run->input_count <= 1 && !command->emit_object && !command->link) ? 1 : capacity;
    run->capacity = capacity;
    if (run->pinned_cpus != NULL && (capacity != run->lanes->lanes || lane_session_current() != NULL))
    {
        fprintf(stderr, "error: requested pinned lanes cannot start in this command\n");
        return 1;
    }
    // The options are the setup's; the session's start, or the flow's dispatch, is lanes-start's (check_driver_flow).
    check_clock_mark(run->stage_clock, CHECK_STAGE_SETUP);
    if (lane_session_current() == NULL)
    {
        // The session in a mapping of its own, in base pages: past the command's arena's first pages, which the options
        // take some of, or in .bss, its first touch could clear a huge page. A terminal command leaves it to the
        // process's exit, its lanes parked on it, and a reusable one releases it once the lanes have joined.
        Arena memory = arena_reserve(sizeof(LaneSession) + _Alignof(LaneSession));
        LaneSession* session = BUSTER_ARENA_PUSH_ARRAY(&memory, LaneSession, 1);
        // Lane 1 creates the other lanes while lane 0 starts the command (BUSTER_LANE_SESSION_LAZY), by default no more
        // than the cores, which it counts first: two lanes on one core's SMT siblings compiled sqlite3.c slower, in 18%
        // more user time and 44 MB more peak memory, on the 9700X. By default one source file's lanes start once it is
        // preprocessed, and only if it is large enough for its rounds to pay for a second lane
        // (BUSTER_LANE_SESSION_ON_DEMAND, check_driver_start_lanes): a small unit's -c creates no thread (#1444).
        const bool one_unit = run->input_count <= 1 && !command->mixed_inputs;
        const uint32_t per_core = run->lanes->every_lane ? 0 : BUSTER_LANE_SESSION_PER_CORE;
        const uint32_t on_demand = one_unit && !run->lanes->every_lane ? BUSTER_LANE_SESSION_ON_DEMAND : 0;
        const uint32_t flags = BUSTER_LANE_SESSION_LAZY | per_core | on_demand | (run->release ? 0 : BUSTER_LANE_SESSION_PARK_AT_END) |
                               (run->pinned_cpus != NULL ? BUSTER_LANE_SESSION_REQUIRE_START : 0);
        if (session == NULL || lane_session_run(session, capacity, run->pinned_cpus, check_driver_flow, run, flags) == 0)
        {
            if (run->pinned_cpus != NULL)
            {
                fprintf(stderr, "error: could not start the requested pinned lanes\n");
                if (run->release)
                {
                    arena_release(&memory);
                }
                return 1;
            }
            // A thread already in a team's or a cold group's operation, which a session can't nest in: alone.
            const Lane group = lane_alone();
            check_driver_flow(run);
            lane_restore(group);
        }
        if (run->release)
        {
            arena_release(&memory);
        }
    }
    else if (lane_count() > 1)
    {
        lane_park_broadcast((uint64_t)(uintptr_t)run, 0);
        *announced = true;
        check_driver_flow(run);
    }
    else
    {
        check_driver_flow(run);
    }
    return run->result;
}

// The driver's lanes (check_driver_command): 98 KB, almost all of it the lanes' arenas (CheckProgramLanes.arenas), in .bss
// as the unit's preprocessor and parser are (check_driver_one_unit), so that a compile touches only the pages it uses: a
// zeroed local touched them all, 24 pages of stack, each a page fault, even for an empty file's -fsyntax-only (#652). A
// command after the process's first zeroes them again; only the tests' reusable routes run more than one, and only lane
// 0 runs the command (run_check_command_mode).
static CheckProgramLanes check_driver_lanes;
static bool check_driver_lanes_taken;

// The semantic driver: `compiler check` (`executable` false) checks, and with -c writes an object; the default C command
// (`executable` true) also links an executable at -o (a.out by default) unless -c or -fsyntax-only says otherwise. With
// several source files (#152), it compiles each of them (check_driver_units): -c writes each one's object, and the link
// one executable of them all.
// Only main's terminal routes pass release false; every reusable caller releases its mappings before returning.
static int check_driver_command(int count, char** arguments, bool executable, bool release, bool* announced)
{
    arena_failure = (ArenaFailure){ 0 };
    WorkerBudget worker_budget;
    // --time-stages's clock (c_check_clock.c), started first so that its span covers the whole command, and only with the
    // option: a compile without it reads neither the clocks nor its resource use. A value that spells the option (`-o
    // --time-stages`) starts the clock too, which the arguments' loop below doesn't then hand to the stages.
    bool time_stages = false;
    for (int i = 0; i < count; i += 1)
    {
        time_stages |= c_option_is(arguments[i], "--time-stages");
    }
    CheckClock clock_storage = { 0 };
    if (time_stages)
    {
        check_clock_start(&clock_storage);
        clock_storage.pages_busy = arena_pages_busy;
    }
    time_stages = false;
    worker_budget_initialize(&worker_budget, getenv("MAKEFLAGS"), BUSTER_CHECK_EMIT_MAX_LANES);
    int result = 1;
    const uint64_t reservation_bytes = UINT64_C(1) << 36;
    Arena arena = arena_reserve(reservation_bytes);
    Arena scratch = arena_reserve(reservation_bytes);
    Arena output_arena = arena_reserve(reservation_bytes);
    // The command fails at once if one of them can't be reserved, though a command of several units uses only the first.
    const bool reserved = arena.base != NULL && scratch.base != NULL && output_arena.base != NULL;
    char** filtered = arena_push_array(&arena, (uint64_t)count + 1, sizeof(char*), _Alignof(char*));
    uint32_t filtered_count = 0;
    // The source files: every argument that is neither an option nor an option's value (#152). pp_parse_options reads the
    // first, as it reads -E's one file, and the others are each a unit too.
    char** inputs = arena_push_array(&arena, (uint64_t)count + 1, sizeof(char*), _Alignof(char*));
    CheckDriverInput* input_kinds = BUSTER_ARENA_PUSH_ARRAY(&arena, CheckDriverInput, (uint64_t)count + 1);
    CheckDriverInputKind language = CHECK_DRIVER_INPUT_AUTO;
    const char* invalid_language = NULL;
    bool invalid_stdin = false;
    bool mixed_inputs = false;
    bool assembly_intel = false;
    uint32_t source_count = 0;
    uint32_t input_count = 0;
    uint32_t stdin_count = 0;
    bool end_options = false;
    bool scalar = false;
    // --verify checks every function's records with x86_validate (the c-testsuite harness passes it).
    bool verify = false;
    // --check-transitions checks the expressions by their memoized transitions (#1050), which --verify verifies.
    bool check_transitions = false;
    bool dump = false;
    bool dump_static = false;
    bool emit_object = false;
    RuntimeKind runtime_kind = RUNTIME_KIND_DYNAMIC;
    bool syntax_only = false;
    bool quiet = false;
    bool return_warning = true;
    bool warning_errors = false;
    int32_t return_error = 0;
    bool lazy_system_headers = false;
    // --deferred-bodies reads a unit's function bodies apart, on the command's lanes (#151); --no-deferred-bodies, the
    // default, reads them in source order.
    bool deferred_bodies = false;
    // --lower-all lowers every definition that may be emitted, referenced or not: #129's test option, to measure what
    // lowering only the referenced ones saves.
    bool lower_all = false;
    // --id-order N hands the unit's identifiers their ids in another order (check_driver_id_order): #153's test option.
    bool id_ordered = false;
    // --poison-arenas, --shared-parts and --units-memory=BYTES: #152's, #777's and #1109's test options
    // (CheckDriverCommand's).
    bool poison_arenas = false;
    bool shared_parts = false;
    uint64_t units_memory = 0;
    bool invalid_units_memory = false;
    bool invalid_id_order = false;
    uint64_t id_order = 0;
    // --time-stages reports each stage's time, the preprocessor's phases and the process's resource use on standard
    // error after the command (#518).
    // Flatten only the linker stream, preserving the order of forwarded options and native -l/-L arguments.
    // Counting all commas may overestimate when an argument is another option's value, but never underallocates.
    uint64_t linker_capacity = (uint64_t)count + 1;
    for (int i = 0; i < count; i += 1)
    {
        for (const char* c = arguments[i]; *c != 0; c += 1)
        {
            linker_capacity += *c == ',';
        }
    }
    char** linker_arguments = linker_capacity <= INT_MAX ? arena_push_array(&arena, linker_capacity, sizeof(char*), _Alignof(char*)) : NULL;
    uint32_t* linker_inputs = linker_capacity <= INT_MAX ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, linker_capacity) : NULL;
    if (linker_inputs != NULL)
    {
        memset(linker_inputs, 0, linker_capacity * sizeof(*linker_inputs));
    }
    int linker_count = 0;
    CheckDriverLibraries libraries = {
        .names = arena_push_array(&arena, linker_capacity, sizeof(String8), _Alignof(String8)),
        .directories = arena_push_array(&arena, linker_capacity, sizeof(String8), _Alignof(String8)),
        .required = arena_push_array(&arena, linker_capacity, sizeof(bool), _Alignof(bool)),
        .arguments = BUSTER_ARENA_PUSH_ARRAY(&arena, ForeignArgument, linker_capacity),
    };
    const bool linker_ready = linker_arguments != NULL && linker_inputs != NULL && input_kinds != NULL && libraries.arguments != NULL && libraries.required != NULL &&
                              linker_options_initialize(&libraries.linker, (uint32_t)linker_capacity, &arena);
    bool missing_value = false;
    // The code generation and ABI layout options the driver refuses, read as every client of the request reads them
    // (c_check_request.c, #1057).
    CheckRequestScan request = { 0 };
    // The lanes the functions compile on (#151, check_program_emit_lanes): by default up to one per core of the CPUs the
    // process may run on (#905), as many as the bodies' tokens pay for; --lanes N runs N, at most one per CPU, as long as
    // there are bodies for them. With several source files, the lanes their units compile on, one unit at a time each
    // (check_driver_units).
    // The command's lanes, in .bss (check_driver_lanes): a command after the process's first zeroes them again.
    CheckProgramLanes* const lanes = &check_driver_lanes;
    if (check_driver_lanes_taken)
    {
        memset(lanes, 0, sizeof(*lanes));
    }
    check_driver_lanes_taken = true;
    lanes->lanes = check_driver_cpus();
    // The caller is one worker. A build system can cap this process explicitly; GNU make supplies shared ownership
    // of additional workers at emission time. No external budget changes the body's profitability decision itself.
    lanes->budget = &worker_budget;
    // A test's hook, in every build (#673): the jobserver tokens a growing team's lane 0 supplies itself after its first
    // part (CheckProgramLanes.grow_supply), read only when it is set.
    const char* grow_supply = getenv("BUSTER_TEST_WORKER_SUPPLY");
    lanes->grow_supply = grow_supply != NULL ? (uint32_t)strtoul(grow_supply, NULL, 10) : 0;
    // And another (#673): "LANE,MICROSECONDS", a lane (its index plus one) that sleeps that long at each part it takes,
    // before each part's lowering in a growing team (CheckProgramLanes.slow_lane), read only when it is set.
    const char* slow_lane = getenv("BUSTER_TEST_SLOW_LANE");
    char* slow_end = NULL;
    lanes->slow_lane = slow_lane != NULL ? (uint32_t)strtoul(slow_lane, &slow_end, 10) : 0;
    lanes->slow_microseconds = slow_end != NULL && *slow_end == ',' ? (uint32_t)strtoul(slow_end + 1, NULL, 10) : 0;
    bool invalid_lanes = false;
    uint32_t pinned_cpus[BUSTER_CHECK_EMIT_MAX_LANES] = { 0 };
    uint32_t pinned_cpu_count = 0;
    bool pinned_cpus_seen = false;
    bool invalid_helper_forms = false;
    // --tier1 compiles each function Tier 1's pipeline takes by Tier 1 (c_check_tier1.c, #138's piece for #140),
    // --tier1-report prints how many each tier compiled and why Tier 1 refused the others, and --tier1-refusals each
    // function Tier 0 compiled with the step that refused it, on standard error; --tier1-no-rematerialize compiles Tier
    // 1's functions spilling the values it would recompute (#178's comparison: the report's spills and frames).
    bool tier1_report = false;
    bool tier1_gvn = false;
    bool tier1_schedule_seen = false;
    bool invalid_tier1_schedule = false;
    // --helper-report prints, on standard error, how many uses of each helper family the lowering took directly and how
    // many call their helper, the helpers planned and the units refused (#1059, CheckHelperReport).
    bool helper_report = false;
    for (int i = 0; linker_ready && !libraries.linker.resource && filtered != 0 && inputs != 0 && libraries.names != 0 && libraries.directories != 0 && i < count; i += 1)
    {
        // The argument's option IDs (c_option_table.c), which the tests below compare; none past --.
        const COptionMatch match = end_options ? (COptionMatch){ .exact = C_OPTION_NONE, .joined = C_OPTION_NONE } : c_option_find(arguments[i]);
        if (!end_options && match.exact == C_OPTION_END_OF_OPTIONS)
        {
            end_options = true;
            filtered[filtered_count++] = arguments[i];
            continue;
        }
        if (end_options)
        {
            if (input_count == 0)
            {
                filtered[filtered_count++] = arguments[i];
            }
            const bool standard_input = arguments[i][0] == '-' && arguments[i][1] == 0;
            stdin_count += standard_input;
            invalid_stdin |= standard_input && language == CHECK_DRIVER_INPUT_AUTO;
            input_kinds[input_count] = check_driver_classify(arguments[i], language);
            mixed_inputs |= input_kinds[input_count].kind != CHECK_DRIVER_INPUT_C;
            source_count += input_kinds[input_count].kind != CHECK_DRIVER_INPUT_LINK;
            linker_inputs[linker_count] = input_count + 1;
            linker_arguments[linker_count++] = arguments[i];
            inputs[input_count++] = arguments[i];
            continue;
        }
        if (arguments[i][0] == '-' && arguments[i][1] == 'x')
        {
            const char* value = arguments[i][2] != 0 ? arguments[i] + 2 : i + 1 < count ? arguments[++i] : "";
            language = check_driver_language(value);
            invalid_language = language == CHECK_DRIVER_INPUT_INVALID && invalid_language == NULL ? value : invalid_language;
            continue;
        }
        if (match.exact == C_OPTION_INTEL)
        {
            assembly_intel = true;
            continue;
        }
        scalar |= match.exact == C_OPTION_SCALAR;
        quiet |= match.exact == C_OPTION_W;
        // An option that takes the next argument as its value (pp_option_takes_value: pp_parse_options' and
        // c_predefines.c's) passes on with it, unread: `-o -lresult` writes -lresult, as clang's driver takes it.
        // `-Wl` without its comma and `-Wl=...` are not linker transports: rejecting them here keeps them from
        // becoming warning options, the silent-ignore class this issue fixes.
        const bool malformed_forwarding = match.exact == C_OPTION_WL || match.joined == C_OPTION_WL_EQ;
        if (malformed_forwarding)
        {
            libraries.linker.error = arguments[i];
            libraries.linker.missing = match.exact == C_OPTION_WL;
        }
        // -rpath DIRECTORY, clang's and GCC's linker option without a transport (#1854), passes on as -Xlinker -rpath
        // -Xlinker DIRECTORY does.
        const bool forwarding = !malformed_forwarding && (match.exact == C_OPTION_XLINKER || match.exact == C_OPTION_RPATH || c_option_starts(arguments[i], "-Wl,"));
        const bool valued = !forwarding && !malformed_forwarding && i + 1 < count && pp_option_id_takes_value(match.exact);
        if (!valued)
        {
            if (match.exact == C_OPTION_WRETURN_TYPE || match.exact == C_OPTION_WERROR_EQ_RETURN_TYPE)
            {
                return_warning = true;
            }
            else if (match.exact == C_OPTION_WNO_RETURN_TYPE)
            {
                return_warning = false;
            }
            if (match.exact == C_OPTION_WERROR || match.exact == C_OPTION_WNO_ERROR)
            {
                warning_errors = match.exact == C_OPTION_WERROR;
            }
            else if (match.exact == C_OPTION_WERROR_EQ_RETURN_TYPE || match.exact == C_OPTION_WNO_ERROR_EQ_RETURN_TYPE)
            {
                return_error = match.exact == C_OPTION_WERROR_EQ_RETURN_TYPE ? 1 : -1;
            }
        }
        const bool library = !valued && arguments[i][0] == '-' && arguments[i][1] == 'l';
        const bool directory = !valued && arguments[i][0] == '-' && arguments[i][1] == 'L';
        if (forwarding)
        {
            if (match.exact == C_OPTION_XLINKER || match.exact == C_OPTION_RPATH)
            {
                if (i + 1 < count)
                {
                    if (match.exact == C_OPTION_RPATH)
                    {
                        linker_arguments[linker_count++] = arguments[i];
                    }
                    linker_arguments[linker_count++] = arguments[++i];
                }
                else
                {
                    libraries.linker.error = arguments[i];
                    libraries.linker.missing = true;
                }
            }
            else
            {
                const char* value = arguments[i] + 4;
                const uint64_t length = strlen(value);
                char* copy = arena_push(&arena, length + 1, 1);
                libraries.linker.resource |= copy == NULL;
                if (copy != NULL)
                {
                    memcpy(copy, value, length + 1);
                    linker_arguments[linker_count++] = copy;
                    for (uint64_t c = 0; c < length; c += 1)
                    {
                        if (copy[c] == ',')
                        {
                            copy[c] = 0;
                            linker_arguments[linker_count++] = copy + c + 1;
                        }
                    }
                }
            }
        }
        else if (valued)
        {
            // The option and its value, however the value is spelled, for pp_parse_options.
            filtered[filtered_count++] = arguments[i];
            filtered[filtered_count++] = arguments[i + 1];
            i += 1;
        }
        else if (malformed_forwarding)
        {
            // Diagnosed through LinkerOptions; a malformed transport is never a source or warning option.
        }
        else if (library || directory)
        {
            // A native -l or -L with no value is a driver usage error, as it has always been, not a linker-option
            // error: the first pass records it in missing_value and the second pass never sees it.
            char* option = arguments[i];
            const bool separate = option[2] == 0;
            char* value = separate && i + 1 < count ? arguments[++i] : NULL;
            const bool absent = separate && (value == NULL || value[0] == 0);
            missing_value |= absent;
            if (!absent)
            {
                linker_arguments[linker_count++] = option;
                if (separate)
                {
                    linker_arguments[linker_count++] = value;
                }
            }
        }
        else if (match.exact == C_OPTION_STATIC || match.exact == C_OPTION_STATIC_PIE || match.exact == C_OPTION_PIE)
        {
            // Clang gives -static-pie priority over -static, and either static request priority over -pie,
            // irrespective of their argv order. Never downgrade a static request to shared-library resolution.
            if (match.exact == C_OPTION_STATIC_PIE)
            {
                runtime_kind = RUNTIME_KIND_STATIC_PIE;
            }
            else if (match.exact == C_OPTION_STATIC && runtime_kind != RUNTIME_KIND_STATIC_PIE)
            {
                runtime_kind = RUNTIME_KIND_STATIC;
            }
        }
        else if (!check_request_scan_match(&request, arguments[i], match))
        {
            // An option that asks for code generation the driver doesn't implement, which the preprocessor doesn't model:
            // refused below (check_request_refusal). The stack protector's, -fcf-protection's and the layout options go
            // on to pp_parse_options as the other options do, for their predefined macros. Values of options above are
            // already consumed, so an output name like -pg is never treated as a flag.
        }
        else if (match.exact == C_OPTION_VERIFY)
        {
            verify = true;
        }
        else if (match.exact == C_OPTION_CHECK_TRANSITIONS)
        {
            check_transitions = true;
        }
        else if (match.exact == C_OPTION_TIER1)
        {
            lanes->tier1 = true;
        }
        else if (match.exact == C_OPTION_TIER1_STRICT)
        {
            lanes->tier1_strict = true;
        }
        else if (match.exact == C_OPTION_TIER1_REPORT)
        {
            tier1_report = true;
        }
        else if (match.exact == C_OPTION_TIER1_REFUSALS)
        {
            lanes->tier1_refusals = stderr;
        }
        else if (match.exact == C_OPTION_HELPER_REPORT)
        {
            helper_report = true;
            lanes->helper_reporting = true;
        }
        else if (match.joined == C_OPTION_HELPER_FORMS_EQ)
        {
            // #1059's paired measurements: the families, by --helper-report's names (or `all`), whose operations take
            // their helpers where they have a direct form.
            const uint32_t forms = check_helper_forms_parse(arguments[i] + 15);
            if (forms == 0)
            {
                fprintf(stderr, "error: --helper-forms= takes a comma-separated list of fence, atomic-access, atomic-rmw and asm, or all\n");
            }
            invalid_helper_forms |= forms == 0;
            lanes->helper_forms = forms;
        }
        else if (match.exact == C_OPTION_TIER1_NO_REMATERIALIZE)
        {
            lanes->tier1_no_rematerialize = true;
        }
        else if (match.exact == C_OPTION_TIER1_NO_INLINE)
        {
            lanes->tier1_no_inline = true;
        }
        else if (match.exact == C_OPTION_TIER0_NO_INLINE)
        {
            lanes->tier0_no_inline = true;
        }
        else if (match.exact == C_OPTION_TIER1_GVN)
        {
            lanes->tier1_gvn = true;
            tier1_gvn = true;
        }
        else if (match.exact == C_OPTION_TIER1_NO_LIFETIMES)
        {
            lanes->tier1_no_lifetimes = true;
        }
        else if (match.joined == C_OPTION_TIER1_SCHEDULE_EQ)
        {
            // #172's measurement: Tier 1's code scheduled block by block after allocation (post) or before selection
            // (pre); none, the default, as without the option.
            const char* variant = arguments[i] + 17;
            const bool known = strcmp(variant, "none") == 0 || strcmp(variant, "post") == 0 || strcmp(variant, "pre") == 0;
            lanes->tier1_schedule = strcmp(variant, "post") == 0 ? BUSTER_CHECK_TIER1_SCHEDULE_POST :
                                    strcmp(variant, "pre") == 0  ? BUSTER_CHECK_TIER1_SCHEDULE_PRE :
                                                                   BUSTER_CHECK_TIER1_SCHEDULE_NONE;
            tier1_schedule_seen = true;
            if (!known)
            {
                fprintf(stderr, "error: --tier1-schedule= takes none, post or pre\n");
            }
            invalid_tier1_schedule |= !known;
        }
        else if (match.exact == C_OPTION_LANE_CPUS || match.joined == C_OPTION_LANE_CPUS_EQ)
        {
            const char* value = arguments[i][11] == '=' ? arguments[i] + 12 : i + 1 < count ? arguments[++i] : NULL;
            invalid_lanes |= pinned_cpus_seen;
            pinned_cpus_seen = true;
            pinned_cpu_count = check_driver_cpu_list(value, pinned_cpus, BUSTER_CHECK_EMIT_MAX_LANES);
            invalid_lanes |= pinned_cpu_count == 0;
        }
        else if (match.exact == C_OPTION_LANES || match.joined == C_OPTION_LANES_EQ)
        {
            const char* value = arguments[i][7] == '=' ? arguments[i] + 8 : i + 1 < count ? arguments[++i] : 0;
            char* end = 0;
            const unsigned long lane_count = value != 0 ? strtoul(value, &end, 10) : 0;
            invalid_lanes |= value == 0 || end == value || *end != 0 || lane_count == 0 || lane_count > BUSTER_CHECK_EMIT_MAX_LANES;
            lanes->lanes = (uint32_t)lane_count;
            lanes->every_lane = true;
        }
        else if (match.exact == C_OPTION_WORKER_BUDGET || match.joined == C_OPTION_WORKER_BUDGET_EQ)
        {
            const char* value = arguments[i][15] == '=' ? arguments[i] + 16 : i + 1 < count ? arguments[++i] : NULL;
            char* end = NULL;
            const unsigned long workers = value != NULL ? strtoul(value, &end, 10) : 0;
            invalid_lanes |= value == NULL || end == value || *end != 0 || workers == 0 || workers > BUSTER_CHECK_EMIT_MAX_LANES;
            worker_budget.maximum = (uint32_t)workers;
        }
        else if (match.exact == C_OPTION_FLAZY_SYSTEM_HEADERS)
        {
            lazy_system_headers = true;
        }
        else if (match.exact == C_OPTION_DEFERRED_BODIES || match.exact == C_OPTION_NO_DEFERRED_BODIES)
        {
            deferred_bodies = arguments[i][2] == 'd';
        }
        else if (match.exact == C_OPTION_LOWER_ALL)
        {
            lower_all = true;
        }
        else if (match.exact == C_OPTION_POISON_ARENAS)
        {
            poison_arenas = true;
        }
        else if (match.exact == C_OPTION_SHARED_PARTS)
        {
            shared_parts = true;
        }
        else if (match.joined == C_OPTION_UNITS_MEMORY_EQ)
        {
            char* end = NULL;
            units_memory = strtoull(arguments[i] + 15, &end, 10);
            invalid_units_memory |= end == arguments[i] + 15 || *end != 0 || units_memory == 0 || arguments[i][15] == '-';
        }
        else if (match.exact == C_OPTION_ID_ORDER || match.joined == C_OPTION_ID_ORDER_EQ)
        {
            const char* value = arguments[i][10] == '=' ? arguments[i] + 11 : i + 1 < count ? arguments[++i] : NULL;
            char* end = NULL;
            id_order = value != NULL ? strtoull(value, &end, 10) : 0;
            invalid_id_order |= value == NULL || end == value || *end != 0;
            id_ordered = true;
        }
        else if (match.exact == C_OPTION_TIME_STAGES)
        {
            time_stages = true;
        }
        else if (match.exact == C_OPTION_FNO_LAZY_SYSTEM_HEADERS)
        {
            lazy_system_headers = false;
        }
        else if (match.exact == C_OPTION_C)
        {
            emit_object = true;
        }
        else if (match.exact == C_OPTION_FSYNTAX_ONLY)
        {
            syntax_only = true;
        }
        else if (match.exact == C_OPTION_DUMP_TYPED)
        {
            dump = true;
        }
        else if (match.exact == C_OPTION_DUMP_STATIC)
        {
            dump_static = true;
        }
        else
        {
            // A source file: the first goes to pp_parse_options in its place, and each is a unit.
            const bool source = arguments[i][0] != '-' || match.exact == C_OPTION_STANDARD_INPUT;
            if (!source || input_count == 0)
            {
                filtered[filtered_count++] = arguments[i];
            }
            if (source)
            {
                const bool standard_input = match.exact == C_OPTION_STANDARD_INPUT;
                stdin_count += standard_input;
                invalid_stdin |= standard_input && language == CHECK_DRIVER_INPUT_AUTO;
                input_kinds[input_count] = check_driver_classify(arguments[i], language);
                mixed_inputs |= input_kinds[input_count].kind != CHECK_DRIVER_INPUT_C;
                source_count += input_kinds[input_count].kind != CHECK_DRIVER_INPUT_LINK;
                linker_inputs[linker_count] = input_count + 1;
                linker_arguments[linker_count++] = arguments[i];
                inputs[input_count++] = arguments[i];
            }
        }
    }
    // Syntax-only takes precedence over -c, whichever came first.
    emit_object = emit_object && !syntax_only;
    // The default C command links unless -c, -fsyntax-only or a dump says otherwise.
    const bool link = executable && !emit_object && !syntax_only && !dump && !dump_static;
    // Non-linking actions consume the transport but do not invoke a linker, as -c and -E do in a system driver.
    // A missing -Xlinker operand is still a malformed driver command, not an unused linker option.
    bool as_needed = true;
    for (int i = 0; link && linker_ready && libraries.linker.error == NULL && !libraries.linker.resource && i < linker_count; i += 1)
    {
        const int option_index = i;
        const char* argument = linker_arguments[i];
        if (linker_inputs[i] != 0)
        {
            const uint32_t input = linker_inputs[i] - 1;
            input_kinds[input].argument = libraries.argument_count;
            libraries.arguments[libraries.argument_count++] = (ForeignArgument){
                .name = foreign_string(argument),
                .flags = as_needed ? BUSTER_FOREIGN_INPUT_AS_NEEDED : 0,
            };
        }
        else if (linker_options_parse(&libraries.linker, linker_count, linker_arguments, &i))
        {
            // Shared output options.
        }
        else if (strcmp(argument, "--as-needed") == 0 || strcmp(argument, "--no-as-needed") == 0)
        {
            as_needed = strcmp(argument, "--as-needed") == 0;
        }
        else if (argument[0] == '-' && (argument[1] == 'l' || argument[1] == 'L'))
        {
            const bool library = argument[1] == 'l';
            const char* value = argument[2] != 0 ? argument + 2 : i + 1 < linker_count ? linker_arguments[++i] : NULL;
            if (value == NULL || value[0] == 0)
            {
                libraries.linker.error = argument;
                libraries.linker.missing = true;
            }
            else
            {
                const String8 text = {
                    .pointer = (char*)value,
                    .length = strlen(value),
                };
                if (library)
                {
                    libraries.required[libraries.count] = !as_needed;
                    libraries.names[libraries.count++] = text;
                    libraries.arguments[libraries.argument_count++] = (ForeignArgument){
                        .name = text,
                        .flags = BUSTER_FOREIGN_INPUT_LIBRARY | (as_needed ? BUSTER_FOREIGN_INPUT_AS_NEEDED : 0) |
                                 (runtime_kind_is_static(runtime_kind) ? BUSTER_FOREIGN_INPUT_STATIC : 0),
                    };
                }
                else
                {
                    libraries.directories[libraries.directory_count++] = text;
                }
            }
        }
        else
        {
            libraries.linker.error = argument;
        }
        if (i != option_index && linker_inputs[i] != 0)
        {
            libraries.linker.error = linker_arguments[option_index];
            libraries.linker.missing = true;
        }
    }
    const bool linker_valid = linker_ready && linker_options_finish(&libraries.linker, &arena);
    const bool runtime_path_refused = link && linker_valid && libraries.linker.runpath.pointer != NULL && runtime_kind != RUNTIME_KIND_DYNAMIC;
    // Code generation's options, which the driver reports before a usage error: the first one it doesn't implement, else
    // the last stack-protector and CET option, which decides whether those unsupported behaviors were requested, else,
    // where it emits code, the object and linkage options its producer doesn't implement (#1124: a command line's
    // default visibility, per-definition sections), before any input or output is opened.
    const CheckRequestRefusal codegen = check_request_refusal(&request, NULL, emit_object || link);
    // Zero until pp_parse_options fills them, which a failed allocation or an option without its value skips.
    PPOptions options = { 0 };
    const char* path = 0;
    const char* output = 0;
    bool markers = false;
    const bool arguments_ready = filtered != NULL && inputs != NULL && libraries.names != NULL && libraries.directories != NULL && linker_ready;
    invalid_lanes |= pinned_cpus_seen && (!lanes->every_lane || pinned_cpu_count != lanes->lanes);
    bool parsed = reserved && arguments_ready && linker_valid && !runtime_path_refused && codegen.error == CHECK_REQUEST_ACCEPTED && !missing_value &&
                  !invalid_lanes && !invalid_id_order && !invalid_units_memory && !invalid_helper_forms && !invalid_tier1_schedule && pp_parse_options((int)filtered_count, filtered, &options, &arena, &path, &output, &markers);
    if (parsed && (stdin_count > 1 || invalid_stdin))
    {
        fprintf(stderr, "error: standard input may appear once and requires -x c, assembler or assembler-with-cpp when compiling\n");
        parsed = false;
    }
    if (invalid_language != NULL)
    {
        fprintf(stderr, "error: unsupported or missing input language '%s'\n", invalid_language);
        parsed = false;
    }
    for (uint32_t i = 0; parsed && i < input_count; i += 1)
    {
        if (input_kinds[i].kind == CHECK_DRIVER_INPUT_INVALID)
        {
            fprintf(stderr, "error: unsupported input language for '%s'\n", inputs[i]);
            parsed = false;
        }
    }
    parsed = parsed && dependency_check_destinations(&options.dependencies, inputs, input_count, output, emit_object, link, &arena);
    // Resolve the remaining request refusals before the archive probe or any source stream is opened: PIC/PIE,
    // instrumentation, ABI layouts and the language/target. The Tier 1 combination check has no source dependency.
    const CheckRequestRefusal refusal = parsed ? check_request_refusal(&request, &options, emit_object || link) : codegen;
    const bool invalid_tier1 = !lanes->tier1 && (lanes->tier1_strict || tier1_report || lanes->tier1_refusals != NULL || lanes->tier1_no_rematerialize ||
                                               tier1_gvn || lanes->tier1_no_lifetimes || lanes->tier1_no_inline || tier1_schedule_seen);
    const bool libraries_ready = !parsed || refusal.error != CHECK_REQUEST_ACCEPTED || !link || mixed_inputs || libraries.count == 0 ||
                                 worker_budget.dry_run || invalid_tier1 || check_driver_named_archives(&libraries, runtime_kind, &arena, &mixed_inputs);
    if (parsed && libraries_ready && stdin_count != 0)
    {
        parsed = driver_read_stdin(&arena, &options.stdin_source);
        options.stdin_ready = parsed;
    }
    if (parsed && libraries_ready)
    {
        char** sources = BUSTER_ARENA_PUSH_ARRAY(&arena, char*, (uint64_t)input_count + 1);
        uint32_t source_inputs = 0;
        parsed = sources != NULL && (input_count == 0 || input_kinds != NULL);
        for (uint32_t i = 0; parsed && i < input_count; i += 1)
        {
            const CheckDriverInputKind kind = input_kinds[i].kind;
            if (kind == CHECK_DRIVER_INPUT_C || kind == CHECK_DRIVER_INPUT_ASSEMBLY_CPP)
            {
                sources[source_inputs] = inputs[i];
                source_inputs += 1;
            }
        }
        parsed = parsed && driver_cache_streams(&arena, sources, source_inputs, &options);
    }
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
        .check_transitions = check_transitions,
        .dump = dump,
        .dump_static = dump_static,
        .quiet = quiet,
        .lazy_system_headers = lazy_system_headers,
        .lower_all = lower_all,
        // The scalar path keeps source order's reader, the twin the deferred one is checked against.
        .deferred_bodies = deferred_bodies && !scalar,
        .return_warning = return_warning,
        .warning_errors = warning_errors,
        .return_error = return_error,
        .id_ordered = id_ordered,
        .id_order = id_order,
        .poison_arenas = poison_arenas,
        .shared_parts = shared_parts,
        .units_memory = units_memory,
        .emit_object = emit_object,
        .link = link,
        .runtime_kind = runtime_kind,
        .source_rows = source_rows,
        .unwind_tables = unwind_tables,
        .output = output,
        .libraries = &libraries,
        .inputs = input_kinds,
        .source_count = source_count,
        .mixed_inputs = mixed_inputs,
        .assembly_intel = assembly_intel,
        .tier1 = lanes->tier1,
        .tier1_strict = lanes->tier1_strict,
        .tier1_no_rematerialize = lanes->tier1_no_rematerialize,
        .tier1_gvn = lanes->tier1_gvn,
        .tier1_no_lifetimes = lanes->tier1_no_lifetimes,
        .tier1_no_inline = lanes->tier1_no_inline,
        .tier0_no_inline = lanes->tier0_no_inline,
        .tier1_schedule = lanes->tier1_schedule,
        .tier1_report = &lanes->tier1_report,
        .tier1_refusals = lanes->tier1_refusals,
        .helper_report = helper_report ? &lanes->helper_report : NULL,
        .helper_forms = lanes->helper_forms,
    };
    if (worker_budget.dry_run)
    {
        result = 0;
    }
    else if (!reserved)
    {
        fprintf(stderr, "fatal error: can't reserve checker memory (%" PRIu64 " bytes requested per arena)\n", reservation_bytes);
    }
    else if (!arguments_ready || (!parsed && arena_failure.bytes != 0))
    {
        arena_report_failure(stderr, "checker arguments", (uint64_t)(count + 1) * sizeof(char*));
    }
    else if (!linker_valid)
    {
        linker_options_diagnose(&libraries.linker, "");
    }
    else if (runtime_path_refused)
    {
        fprintf(stderr, "error: runtime search paths require dynamic output\n");
    }
    else if (codegen.error != CHECK_REQUEST_ACCEPTED)
    {
        fprintf(stderr, "error: ");
        check_request_describe(stderr, &codegen);
        fprintf(stderr, "\n");
    }
    else if (!parsed)
    {
        fprintf(stderr, "usage: compiler [check] [--scalar] [--verify] [--check-transitions] [--dump-typed] [--dump-static] [-flazy-system-headers] [--deferred-bodies | --no-deferred-bodies] [--lower-all] [--poison-arenas] [--shared-parts] [--units-memory=BYTES] [--lanes N] [--lane-cpus LIST] "
                        "[--worker-budget N] [--time-stages] [--id-order N] [--tier1 [--tier1-strict] [--tier1-report] [--tier1-refusals] [--tier1-no-rematerialize] [--tier1-gvn] [--tier1-no-lifetimes] [--tier1-schedule=none|post|pre]] [--helper-report] [--helper-forms=LIST] [-fsyntax-only | -c] [-o file] [-lNAME] [-LDIR] [preprocessor options] file.c...\n");
        fprintf(stderr, "note: -O flags set preprocessor optimization macros but do not select Tier 1; Tier 0 is the default, and --tier1 opts into Tier 1 (with per-function Tier 0 fallback)\n");
    }
    // The rest of the request's refusals (check_request_refusal), before any source, worker session or output is
    // opened: the instrumentation's, decided from the resolved predefines rather than from an enabling option, which a
    // later -fno-sanitize= or -fno-openmp may have cancelled, then position-independent code, the layouts and the
    // language and target.
    else if (refusal.error != CHECK_REQUEST_ACCEPTED)
    {
        fprintf(stderr, "error: ");
        check_request_describe(stderr, &refusal);
        fprintf(stderr, "\n");
    }
    else if (!libraries_ready)
    {
        if (arena_failure.bytes != 0)
        {
            arena_report_failure(stderr, "named-library arguments", (uint64_t)(count + 1) * sizeof(String8));
        }
    }
    else if (invalid_tier1)
    {
        fprintf(stderr, "error: %s needs --tier1\n", lanes->tier1_strict ? "--tier1-strict" : tier1_report ? "--tier1-report" : lanes->tier1_refusals != NULL ? "--tier1-refusals" :
                                                    lanes->tier1_no_rematerialize ? "--tier1-no-rematerialize" : tier1_gvn ? "--tier1-gvn" :
                                                    lanes->tier1_no_lifetimes ? "--tier1-no-lifetimes" : lanes->tier1_no_inline ? "--tier1-no-inline" :
                                                    "--tier1-schedule");
    }
    else
    {
        for (uint32_t i = 0; i < input_count; i += 1)
        {
            if (input_kinds[i].kind == CHECK_DRIVER_INPUT_LINK && !link)
            {
                fprintf(stderr, "warning: %s: linker input unused because linking was not requested\n", inputs[i]);
            }
            else if (input_kinds[i].unknown)
            {
                fprintf(stderr, "warning: %s: unrecognized suffix; treating as linker input\n", inputs[i]);
            }
        }
        // --tier1's tables, before any lane may compile a unit (x86_zen5_initialize's contract).
        if (lanes->tier1 && !check_tier1_ready)
        {
            check_tier1_ready = x86_zen5_initialize() && tier1_select_initialize();
        }
        // The command runs on its session's lanes (#905), which start here, after its options and before any source is
        // read (check_driver_run).
        CheckDriverRun run = {
            .command = &command,
            .inputs = inputs,
            .input_count = input_count,
            .path = path,
            .lanes = lanes,
            .pinned_cpus = pinned_cpus_seen ? pinned_cpus : NULL,
            .stage_clock = stage_clock,
            .pp_ticks = pp_ticks,
            .arena = &arena,
            .scratch = &scratch,
            .output_arena = &output_arena,
            .release = release,
            .result = 1,
        };
        result = check_driver_run(&run, announced);
        if (tier1_report)
        {
            check_tier1_print_report(stderr, &lanes->tier1_report);
        }
        if (helper_report)
        {
            check_helper_print_report(stderr, &lanes->helper_report);
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
            fprintf(stderr, "worker-budget\tmode=%s\tlimit=%u\trequested=%u\tgranted=%u\tactive=%u\tgrown=%u\tworked=%u\tfallback=%u\n", mode,
                    worker_budget.maximum, worker_budget.requested, worker_budget.granted, lanes->active, lanes->grown, lanes->worked, (uint32_t)lanes->again);
        }
    }
    worker_budget_close(&worker_budget);
    return result;
}

// The semantic driver's command (check_driver_command), run by the calling thread, or collectively by every lane of a
// session's group (#905): lane 0 runs the command, and tells the other lanes of its flow, which they run with it, and of
// its exit status, which they return too.
static int run_check_command_mode(int count, char** arguments, bool executable, bool release)
{
    int result = 1;
    // A lazy session's lane 0 learns how its lanes started before it meets them: on a failed start it is alone.
    lane_session_settle();
    const bool collective = lane_session_current() != NULL && lane_count() > 1;
    if (collective && lane_index() != 0)
    {
        CheckDriverRun* run = (CheckDriverRun*)(uintptr_t)lane_park_broadcast(0, 0);
        if (run != NULL)
        {
            check_driver_flow(run);
        }
        result = (int)lane_park_broadcast(0, 0);
    }
    else
    {
        bool announced = false;
        result = check_driver_command(count, arguments, executable, release, &announced);
        if (collective && !announced)
        {
            lane_park_broadcast(0, 0);
        }
        if (collective)
        {
            lane_park_broadcast((uint64_t)(uint32_t)result, 0);
        }
    }
    return result;
}

// Tests and other reusable callers release all owned mappings before returning. Only main's terminal source
// commands select the other mode: the kernel tears their mappings down once when the process exits, after the
// stage report and stdio flush, rather than through each subsystem's individual munmap calls first.
static int run_check_command(int count, char** arguments, bool executable)
{
    return run_check_command_mode(count, arguments, executable, true);
}

