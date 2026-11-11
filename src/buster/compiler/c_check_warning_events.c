// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Sparse semantic warning facts, shared by readers, checked streams and initializer plans. IDs name policy, not
// bits in a token/node column. A published view is immutable: later appends never change its prefix. Deferred
// readers borrow a frozen prefix and own separate suffix buffers; the unit merge copies/remaps only those suffixes.
// Include after the arena and TypeId definitions. No function here chooses severity or renders text.
#ifndef BUSTER_C_CHECK_WARNING_EVENTS_INCLUDED
#define BUSTER_C_CHECK_WARNING_EVENTS_INCLUDED

#include "c_check_warning_ids.c"

_Static_assert(CHECK_WARNING_BASE_ID_COUNT == 25, "sparse warning IDs follow the stable shared prefix");

enum
{
    CHECK_WARNING_ID_NON_LITERAL_NULL = 25,
    CHECK_WARNING_ID_LITERAL_RANGE = 26,
    CHECK_WARNING_ID_ORDERED_POINTER_INTEGER = 27,
    CHECK_WARNING_ID_MISSING_DECLARATIONS = 28,
    CHECK_WARNING_ID_PLAIN_COMPLEX = 29,
    CHECK_WARNING_ID_VARIABLE_SIZED_MEMBER = 30,
    CHECK_WARNING_ID_STATIC_LOCAL_INLINE = 31,
    CHECK_WARNING_ID_MANY_SCALAR_BRACES = 32,
    CHECK_WARNING_ID_INITIALIZER_OVERRIDES = 33,
    CHECK_WARNING_ID_UNDEFINED_INTERNAL = 34,
    CHECK_WARNING_ID_C23_PARAMETER = 35,
    CHECK_WARNING_ID_UNRECOGNIZED_PRAGMA = 36,
    CHECK_WARNING_ID_IGNORED_PRAGMA = 37,
    CHECK_WARNING_ID_SOURCE_OPENMP = 38,
    CHECK_WARNING_ID_LIBRARY_REDECLARATION = 39,
    CHECK_WARNING_ID_BUILTIN_HEADER = 40,
    CHECK_WARNING_ID_UNKNOWN_ATTRIBUTE = 41,
    CHECK_WARNING_ID_DIVISION_BY_ZERO = 42,
    CHECK_WARNING_ID_CONSTANT_CONVERSION = 43,
    CHECK_WARNING_ID_ARRAY_BOUNDS = 44,
    CHECK_WARNING_ID_SIGN_COMPARE = 45,
    CHECK_WARNING_ID_UNUSED_COMPARISON = 46,
    CHECK_WARNING_ID_UNUSED_VALUE = 47,
    CHECK_WARNING_ID_LOGICAL_NOT_PARENTHESES = 48,
    CHECK_WARNING_ID_PARENTHESES = 49,
    CHECK_WARNING_ID_EMPTY_BODY = 50,
    CHECK_WARNING_ID_SELF_ASSIGN = 51,
    CHECK_WARNING_ID_RETURN_STACK_ADDRESS = 52,
    CHECK_WARNING_ID_FORMAT = 53,
    CHECK_WARNING_ID_FORMAT_SECURITY = 54,
    CHECK_WARNING_ID_UNUSED_VARIABLE = 55,
    CHECK_WARNING_ID_UNUSED_FUNCTION = 56,
    CHECK_WARNING_ID_UNUSED_PARAMETER = 57,
    CHECK_WARNING_ID_UNINITIALIZED = 58,
    CHECK_WARNING_ID_SWITCH = 59,
    CHECK_WARNING_ID_DANGLING_ELSE = 60,
    CHECK_WARNING_ID_UNUSED_BUT_SET_PARAMETER = 61,
    CHECK_WARNING_ID_UNUSED_CONST_VARIABLE = 62,
    CHECK_WARNING_ID_FORMAT_EXTRA_ARGS = 63,
    CHECK_WARNING_ID_FORMAT_INSUFFICIENT_ARGS = 64,
    CHECK_WARNING_ID_FORMAT_INVALID_SPECIFIER = 65,
    CHECK_WARNING_ID_EMPTY_CASE_RANGE = 66,
    CHECK_WARNING_ID_GCC_COMPAT = 67,
    CHECK_WARNING_ID_GNU_ALIGNOF_EXPRESSION = 68,
    CHECK_WARNING_ID_DEPRECATED_DECLARATIONS = 69,
    CHECK_WARNING_ID_CFSTRING_LITERAL = 70,
    CHECK_WARNING_ID_UNSUPPORTED_AVAILABILITY_GUARD = 71,
    CHECK_WARNING_ID_AVAILABILITY = 72,
    CHECK_WARNING_ID_COUNTED_BY_ELEMENT = 73,
    CHECK_WARNING_ID_EXTERN_INITIALIZER = 74,
    CHECK_WARNING_ID_DEPRECATED_ATTRIBUTES = 75,
    CHECK_WARNING_ID_UNKNOWN_SANITIZERS = 76,
    CHECK_WARNING_ID_OVERFLOW_BEHAVIOR = 77,
    CHECK_WARNING_ID_NSOBJECT_ATTRIBUTE = 78,
    CHECK_WARNING_ID_INDEPENDENT_CLASS = 79,
    CHECK_WARNING_ID_ASSUME = 80,
    CHECK_WARNING_ID_DUPLICATE_DECLSPEC = 81,
    CHECK_WARNING_ID_INVALID_NORETURN = 82,
    CHECK_WARNING_ID_PRIVATE_EXTERN = 83,
    CHECK_WARNING_ID_LITERAL_CONVERSION = 84,
    CHECK_WARNING_ID_WEAK_UNDECLARED = 85,
    CHECK_WARNING_ID_REDEFINE_EXTNAME = 86,
    CHECK_WARNING_ID_THREAD_SAFETY_ATTRIBUTES = 87,
    CHECK_WARNING_ID_NODEREF = 88,
    CHECK_WARNING_ID_EXCESSIVE_REGSAVE = 89,
    CHECK_WARNING_ID_TCB_ENFORCEMENT = 90,
    CHECK_WARNING_ID_INVALID_VERSION_AVAILABILITY = 91,
    // An unknown escape sequence in an unevaluated string literal, before its error (#147): the driver's alone, no event's.
    CHECK_WARNING_ID_UNKNOWN_ESCAPE_SEQUENCE = 92,
    CHECK_WARNING_ID_VOID_PTR_DEREFERENCE = 93,
    // An ordinary character constant of more than one character (#1447): two or three, or past four, -Wmultichar's; four,
    // -Wfour-char-constants' (ignored by default); past four, clang's warning of no option too.
    CHECK_WARNING_ID_MULTICHAR = 94,
    CHECK_WARNING_ID_FOUR_CHAR_CONSTANTS = 95,
    CHECK_WARNING_ID_CHARACTER_TOO_LONG = 96,
    CHECK_WARNING_ID_NULL_DEREFERENCE = 97,
    // A warning diagnose_if's condition gives (#1447, c_check_condition.c): -Wuser-defined-warnings.
    CHECK_WARNING_ID_USER_DEFINED_WARNINGS = 98,
    // clang's -Wfunction-multiversion (#1447, c_check_cpu_dispatch.c): a cpu_specific or cpu_dispatch CPU list with two
    // names of one mangling, and a cpu_dispatch function's body.
    CHECK_WARNING_ID_FUNCTION_MULTIVERSION = 99,
    CHECK_WARNING_ID_UNUSED_RESULT = 100,
    // A signed `<<` converted to _Bool (#1447, clang's DiagnoseIntInBoolContext): -Wint-in-bool-context, in -Wmost and so
    // -Wall. Its constant forms, and a conditional of integer constants, are -Wtautological-constant-compare's, on by default.
    CHECK_WARNING_ID_INT_IN_BOOL_CONTEXT = 101,
    CHECK_WARNING_ID_TAUTOLOGICAL_CONSTANT_COMPARE = 102,
    // A constant a bit-field's assignment or initialization truncates (#1447, clang's AnalyzeBitFieldAssignment):
    // -Wbitfield-constant-conversion, and its 1 to a signed one-bit bit-field, -Wsingle-bit-bitfield-constant-conversion
    // under it, both under -Wconstant-conversion and on by default.
    CHECK_WARNING_ID_BITFIELD_CONSTANT_CONVERSION = 103,
    CHECK_WARNING_ID_SINGLE_BIT_BITFIELD_CONSTANT_CONVERSION = 104,
    // The usual arithmetic conversions of an enumeration and another enumeration or a floating type (#1447, clang's
    // checkEnumArithmeticConversions): -Wenum-compare's comparisons, on by default; -Wenum-enum-conversion's arithmetic and
    // bitwise operations, -Wenum-compare-conditional's conditionals and -Wenum-float-conversion's mixes with a floating
    // type, under -Wenum-conversion and so -Wconversion; and -Wanon-enum-enum-conversion's, an unnamed enumeration's.
    CHECK_WARNING_ID_ENUM_COMPARE = 105,
    CHECK_WARNING_ID_ENUM_ENUM_CONVERSION = 106,
    CHECK_WARNING_ID_ENUM_COMPARE_CONDITIONAL = 107,
    CHECK_WARNING_ID_ENUM_FLOAT_CONVERSION = 108,
    CHECK_WARNING_ID_ANON_ENUM_ENUM_CONVERSION = 109,
    // A for loop's condition variables its loop never changes, and its variable both its header and its body's last
    // statement increment or decrement (#1447, clang's CheckForLoopConditionalStatement and CheckForRedundantIteration):
    // -Wfor-loop-analysis, off by default, under -Wloop-analysis and -Wmost and so -Wall.
    CHECK_WARNING_ID_FOR_LOOP_ANALYSIS = 110,
    // An implicit conversion from one named enumeration to another (#1447, the end of clang's CheckImplicitConversion), an
    // enumerator taken as its enumeration: -Wimplicit-enum-enum-cast, on by default, under -Wenum-conversion.
    CHECK_WARNING_ID_IMPLICIT_ENUM_ENUM_CAST = 111,
    // A tag a prototype's parameter list declares first (#1447, clang's ActOnTag): -Wvisibility, on by default.
    CHECK_WARNING_ID_VISIBILITY = 112,
    // A C23 record's redefinition whose attributes differ (#1447, c_check_tag_compatible.c): clang's -Wodr, an error by
    // default (check_input_odr_severity).
    CHECK_WARNING_ID_ODR = 113,
    // An encoding prefix on an unevaluated string literal (#1447): -Winvalid-unevaluated-string, on by default.
    CHECK_WARNING_ID_INVALID_UNEVALUATED_STRING = 114,
    // -pedantic's warnings of an unevaluated string literal's escapes (#1447): a GNU one's (-Wpedantic) and a delimited or
    // named one's (-Wdelimited-escape-sequence-extension), off by default, extensions -pedantic gives.
    CHECK_WARNING_ID_NONSTANDARD_ESCAPE = 115,
    CHECK_WARNING_ID_DELIMITED_ESCAPE = 116,
    // A target_clones attribute that mixes a string of several versions with other strings (#1838, clang's
    // SemaX86::checkTargetClonesAttr): -Wtarget-clones-mixed-specifiers, on by default, under -Wfunction-multiversion.
    CHECK_WARNING_ID_TARGET_CLONES_MIXED_SPECIFIERS = 117,
    // __builtin_cpu_supports of a feature clang doesn't know (#1843, clang's SemaX86 validateCpuSupports): its warning of no
    // option, whose call is false.
    CHECK_WARNING_ID_INVALID_CPU_SUPPORTS = 118,
    // An alias whose function type isn't its aliasee's (#1849, clang's code generation at -c): -Wattribute-alias. (Its ID
    // past the ones other pull requests take.)
    CHECK_WARNING_ID_ATTRIBUTE_ALIAS = 119,
    // A call's argument whose type its type tag doesn't match (#1447, clang's CheckArgumentWithTypeTag, the attributes
    // argument_with_type_tag, pointer_with_type_tag and type_tag_for_datatype): -Wtype-safety, on by default.
    CHECK_WARNING_ID_TYPE_SAFETY = 120,
    // An unevaluated operand with a side effect (#1447, clang's CheckUnaryExprOrTypeTraitOperand and
    // CreateGenericSelectionExpr): -Wunevaluated-expression, on by default, under -Wunused-value and so -Wunused and -Wall.
    CHECK_WARNING_ID_UNEVALUATED_EXPRESSION = 121,
    // An enum's fixed underlying type with const or volatile, which it drops (#1952, clang's ActOnTag):
    // -Wunderlying-cv-qualifier-ignored, on by default.
    CHECK_WARNING_ID_UNDERLYING_CV_QUALIFIER = 122,
    // A `#pragma pack (push, ...)` left open at the unit's end, or a pack value an included file changes (#1849, clang's
    // DiagnoseUnterminatedPragmaAlignPack): -Wpragma-pack, on by default, under -Wpragmas.
    CHECK_WARNING_ID_PRAGMA_PACK = 123,
    // `#pragma pack(show)`'s value, and a pop with both a name and an alignment (#1849, clang's ActOnPragmaPack): warnings
    // of no option.
    CHECK_WARNING_ID_PRAGMA_PACK_NOTICE = 124,
    // A C-style cast from a pointer to a narrower integer type (#1943, clang's CheckCStyleCast), on by default:
    // -Wpointer-to-int-cast's, with -Wvoid-pointer-to-int-cast's from a pointer to void under it, and to an enumeration
    // -Wpointer-to-enum-cast's, with -Wvoid-pointer-to-enum-cast's under it.
    CHECK_WARNING_ID_POINTER_TO_INT_CAST = 125,
    CHECK_WARNING_ID_VOID_POINTER_TO_INT_CAST = 126,
    CHECK_WARNING_ID_POINTER_TO_ENUM_CAST = 127,
    CHECK_WARNING_ID_VOID_POINTER_TO_ENUM_CAST = 128,
    // An object whose tentative definitions leave it an array of unknown bound at the unit's end (#2238, clang's
    // ActOnEndOfTranslationUnit): -Wtentative-definition-array, on by default.
    CHECK_WARNING_ID_TENTATIVE_ARRAY = 129,
    // swift_name's and swift_async_name's Swift name that doesn't fit its declaration (#1447, clang's SemaSwift::DiagnoseName):
    // -Wswift-name-attribute, on by default. (Its ID past the ones other pull requests take.)
    CHECK_WARNING_ID_SWIFT_NAME_ATTRIBUTE = 130,
    // A generic selection's association of array type or of a qualified type, which its controlling expression's lvalue
    // conversion never selects (#1447, clang's CreateGenericSelectionExpr): -Wunreachable-code-generic-assoc, on by
    // default, under -Wunreachable-code.
    CHECK_WARNING_ID_UNREACHABLE_GENERIC_ASSOC = 131,
    // Two unsequenced modifications of a variable, or one and a read of it (#1953, clang's SequenceChecker,
    // c_check_unsequenced.c): -Wunsequenced, on by default, under -Wsequence-point and so -Wmost and -Wall. (Its ID past
    // -Wunreachable-code-generic-assoc's, 131.)
    CHECK_WARNING_ID_UNSEQUENCED = 132,
    // A const object, or a struct with a const member, without an initializer (#2168, clang's -Wdefault-const-init-*): its
    // automatic storage's, default on, and its static storage's, off by default.
    CHECK_WARNING_ID_DEFAULT_CONST_INIT_VAR_UNSAFE = 133,
    CHECK_WARNING_ID_DEFAULT_CONST_INIT_FIELD_UNSAFE = 134,
    CHECK_WARNING_ID_DEFAULT_CONST_INIT_VAR = 135,
    // -Wc++-compat's own policy, which no event gives: clang's fake diagnostic in that group
    // (warn_cxx_compat_hack_fake_diagnostic_do_not_emit), whose policy makes the unsafe default-const-init words say "and
    // is incompatible with C++" (#2168).
    CHECK_WARNING_ID_CXX_COMPAT = 136,
    // GCC's cast to a union from a member's type (#2301, clang's CheckCStyleCast): -Wgnu-union-cast, an extension off by
    // default, under -Wgnu and -Wpedantic. (Its ID past -Wc++-compat's, 136.)
    CHECK_WARNING_ID_GNU_UNION_CAST = 137,
    // GNU's designators (#2202, clang's parser's ParseInitializerWithPotentialDesignator): an array designation without its
    // `=` and an old-style `name:` field designator, on by default, and an array range designator, -pedantic's, all
    // -Wgnu-designator's.
    CHECK_WARNING_ID_GNU_DESIGNATOR = 138,
    CHECK_WARNING_ID_GNU_ARRAY_RANGE = 139,
    // An integer added to a string literal (#2190, clang's diagnoseStringPlusInt): -Wstring-plus-int, on by default.
    CHECK_WARNING_ID_STRING_PLUS_INT = 140,
    // A C23 attribute list before C23 (#2409): -Wc23-extensions' clang extension, off by default, -pedantic's.
    CHECK_WARNING_ID_C23_ATTRIBUTES = 141,
    CHECK_WARNING_ID_COUNT,
};

typedef enum CheckWarningEventKind
{
    CHECK_WARNING_EVENT_MESSAGE,
    CHECK_WARNING_EVENT_LATE_ATTRIBUTE,
    CHECK_WARNING_EVENT_GNU_INLINE_WITHOUT_INLINE,
    CHECK_WARNING_EVENT_GNU_ARRAY_FOLD,
    CHECK_WARNING_EVENT_GNU_ICE_FOLD,
    CHECK_WARNING_EVENT_NON_LITERAL_NULL,
    CHECK_WARNING_EVENT_LITERAL_OVERFLOW,
    CHECK_WARNING_EVENT_LITERAL_UNDERFLOW,
    CHECK_WARNING_EVENT_ORDERED_POINTER_INTEGER,
    CHECK_WARNING_EVENT_EMPTY_DECLARATION,
    CHECK_WARNING_EVENT_TYPEDEF_NAMELESS,
    CHECK_WARNING_EVENT_STANDALONE_SPECIFIER,
    CHECK_WARNING_EVENT_PLAIN_COMPLEX,
    CHECK_WARNING_EVENT_VARIABLE_SIZED_MEMBER,
    CHECK_WARNING_EVENT_STATIC_LOCAL_INLINE,
    CHECK_WARNING_EVENT_MANY_SCALAR_BRACES,
    CHECK_WARNING_EVENT_INITIALIZER_OVERRIDES,
    CHECK_WARNING_EVENT_UNDEFINED_INTERNAL,
    CHECK_WARNING_EVENT_C23_PARAMETER,
    // A retained directive, including at parser EOF; source and payload.token are PP coordinates. With
    // BUSTER_CHECK_WARNING_SYMBOL_PRAGMA, payload.data names its symbol-pragma history record plus one.
    CHECK_WARNING_EVENT_PRAGMA,
    CHECK_WARNING_EVENT_LIBRARY_REDECLARATION,
    CHECK_WARNING_EVENT_BUILTIN_HEADER,
    // An attribute clang doesn't know, which it ignores (#147): payload.name its name's identifier, payload.data its scope's
    // (0 for none) with BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE where it is a C23 attribute ([[...]]) rather than GNU's.
    CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE,
    // `__clang__`'s expansion where a C23 attribute's scope or name goes, which clang reads as `_Clang` (#147, c_parser.c's
    // parse_clang_scope_numbers): -Wignored-attributes' warning at it.
    CHECK_WARNING_EVENT_CLANG_MACRO_SCOPE,
    CHECK_WARNING_EVENT_UNUSED_VARIABLE,
    CHECK_WARNING_EVENT_UNUSED_FUNCTION,
    CHECK_WARNING_EVENT_UNUSED_PARAMETER,
    // An attribute on a declaration of none of its subjects (#147, check_input_attribute_subjects): its payload as
    // CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's, -Wignored-attributes' warning.
    CHECK_WARNING_EVENT_ATTRIBUTE_SUBJECT,
    // A function type's attribute on a declarator of no function type (#147, check_input_attribute_type_warnings):
    // payload.name the attribute's name, payload.first_type the type there, payload.data its typedef spelling;
    // payload.token its declarator name (an abstract declarator's position) and second_type its full type retain
    // occurrence-specific parentheses.
    CHECK_WARNING_EVENT_FUNCTION_TYPE_ATTRIBUTE,
    CHECK_WARNING_EVENT_NONSTRING_TYPE,
    CHECK_WARNING_EVENT_TRANSPARENT_UNION,
    // A GNU attribute GCC knows after a function definition's declarator (clang's parser's warning, #147): payload.name
    // its name as written.
    CHECK_WARNING_EVENT_GCC_ATTRIBUTE_POSITION,
    // nonnull's warnings (clang's handleNonNullAttr, #147): an index of a parameter that is no pointer, a function of no
    // pointer parameter, and arguments on a parameter's. payload.name the attribute's name, payload.data its scope's
    // (0 for none) with BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE.
    CHECK_WARNING_EVENT_NONNULL_POINTERS,
    CHECK_WARNING_EVENT_NONNULL_NO_POINTERS,
    CHECK_WARNING_EVENT_NONNULL_PARAMETER_ARGUMENTS,
    // A C23 declaration attribute after a declarator chunk (#147, check_input_standard_place); a gnu:: type attribute
    // there (-Wgcc-compat); a calling convention x86-64 hasn't on a function; an attribute clang ignores in that syntax
    // or place (noderef, vector_size). Payload as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's.
    CHECK_WARNING_EVENT_TYPE_POSITION,
    CHECK_WARNING_EVENT_GCC_TYPE_ATTRIBUTE,
    CHECK_WARNING_EVENT_CONVENTION_UNSUPPORTED,
    CHECK_WARNING_EVENT_ATTRIBUTE_IGNORED,
    // A tag declaration's attribute (#147, check_input_tag_attribute_warning): after a bare declaration's name (its tag's
    // keyword in data: 0 struct, 1 union, 2 enum), or after the tag's definition (data as above, a note at it).
    CHECK_WARNING_EVENT_TAG_ATTRIBUTE_PLACE,
    CHECK_WARNING_EVENT_TAG_ATTRIBUTE_DEFINED,
    // A GNU declaration attribute in a type name (#147, check_input_attribute_in_type_name), named as written.
    CHECK_WARNING_EVENT_TYPE_NAME_ATTRIBUTE,
    // _Alignof or alignof of an expression (#147, check_input_alignof_expression): its keyword in the payload's name.
    CHECK_WARNING_EVENT_ALIGNOF_EXPRESSION,
    // A use of what a deprecated attribute marks (#147, check_input_deprecated_uses): the used name, and the attribute's
    // message's string token in the payload's token (or none).
    CHECK_WARNING_EVENT_DEPRECATED_USE,
    // A CFString builtin's literal that isn't UTF-8 (#148, check_input_cfstring_literal), at the literal.
    CHECK_WARNING_EVENT_CFSTRING_TRUNCATED,
    // A __builtin_available that guards nothing (#148, check_input_availability_prepare), at its keyword, and a version
    // whose separators differ (`1.2_3`), at its number.
    CHECK_WARNING_EVENT_UNGUARDED_AVAILABILITY,
    CHECK_WARNING_EVENT_VERSION_SEPARATORS,
    // A counted_by family attribute on a flexible array member of structs with flexible array members (#148,
    // check_input_counted_by_member), which clang drops: payload.first_type the element's type, payload.name the
    // attribute's kind less one.
    CHECK_WARNING_EVENT_COUNTED_BY_ELEMENT,
    // -Wignored-attributes' warnings of attributes clang's handlers ignore (#147, check_input_attribute_subjects): weak_import
    // on a definition, and internal_linkage on a parameter (named as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's are), at the
    // attribute; internal_linkage on a variable of local storage, at the variable's name; nocf_check without -fcf-protection,
    // at the attribute (check_input_attribute_read_item).
    CHECK_WARNING_EVENT_ATTRIBUTE_DEFINITION,
    CHECK_WARNING_EVENT_ATTRIBUTE_VARIABLES_FUNCTIONS,
    CHECK_WARNING_EVENT_INTERNAL_LINKAGE_LOCAL,
    CHECK_WARNING_EVENT_NOCF_CHECK_IGNORED,
    // A file-scope extern variable's initializer (clang's warn_extern_init), at the variable's name.
    CHECK_WARNING_EVENT_EXTERN_INITIALIZER,
    // mode's vector mode (#147, check_input_attribute_mode: -Wdeprecated-attributes), at the attribute.
    CHECK_WARNING_EVENT_VECTOR_MODE,
    // alloc_size's and alloc_align's function that returns no pointer (#147, -Wignored-attributes), at the attribute, named
    // as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's are.
    CHECK_WARNING_EVENT_RETURN_POINTERS,
    CHECK_WARNING_EVENT_RETURN_POINTERS_REFERENCES,
    // An argument clang's handler doesn't know (#147, -Wignored-attributes): visibility's string, at it, and format's kind,
    // at the attribute; named as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's are, the argument's token in the payload's token
    // (a string literal's, with those after it, or an identifier, without a keyword's underscores).
    CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED,
    // format's first argument to check on a function that isn't variadic (#147, -Wgcc-compat), at the declaration's name,
    // named as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's are.
    CHECK_WARNING_EVENT_GCC_VARIADIC,
    // target's string clang's checkTargetAttr doesn't take (#147, check_input_target_warning), at the string: the string's
    // first token in the payload's token, the warning's case and its piece of the string in its data.
    CHECK_WARNING_EVENT_TARGET_UNSUPPORTED,
    // no_sanitize's sanitizer clang doesn't know (-Wunknown-sanitizers), at its string, and one a variable of static
    // storage can't take, at the variable's name (-Wignored-attributes): the string's first token in the payload's token,
    // named as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's are.
    CHECK_WARNING_EVENT_UNKNOWN_SANITIZER,
    CHECK_WARNING_EVENT_SANITIZER_GLOBAL,
    // A function type's attribute on a tag's declaration (#147, check_input_attribute_subjects): "'X' attribute only applies
    // to functions and methods", named as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's are.
    CHECK_WARNING_EVENT_FUNCTIONS_METHODS,
    // The retain-ownership attributes' warnings (#147, check_input_attribute_ownership), at the declaration's first token
    // (ns_returns_retained's of a function type at the attribute), named as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's are, the
    // consumed ones' as their names alone.
    CHECK_WARNING_EVENT_OWNERSHIP_RETURN_OBJECT,
    CHECK_WARNING_EVENT_OWNERSHIP_RETURN_POINTER,
    CHECK_WARNING_EVENT_OWNERSHIP_PARAMETER_CF,
    CHECK_WARNING_EVENT_OWNERSHIP_PARAMETER_OS,
    CHECK_WARNING_EVENT_OWNERSHIP_FUNCTIONS,
    CHECK_WARNING_EVENT_OWNERSHIP_FUNCTIONS_PARAMETERS,
    CHECK_WARNING_EVENT_OWNERSHIP_RETAINED,
    CHECK_WARNING_EVENT_CONSUMED_OBJECT,
    CHECK_WARNING_EVENT_CONSUMED_POINTER,
    // nomerge's variable of no function pointer type (#147, check_input_attribute_handled), at the attribute: the variable's
    // name in the payload's token. overflow_behavior's without its experimental option, at the attribute.
    CHECK_WARNING_EVENT_NOMERGE_POINTER,
    CHECK_WARNING_EVENT_OVERFLOW_BEHAVIOR,
    // A C23 type attribute that slides from a declaration to its specifiers (#147, -Wdeprecated-attributes), at it.
    CHECK_WARNING_EVENT_TYPE_ON_DECLARATION,
    // AMDGPU's num_sgpr and num_vgpr (#147, -Wdeprecated-declarations), at the attribute.
    CHECK_WARNING_EVENT_REGISTER_COUNT_DEPRECATED,
    // Static analyses' and Objective-C's attributes' warnings (#147, check_input_attribute_annotations): iboutlet's,
    // mig_server_routine's, malloc_span's (with its CHECK_WARNING_NOTE_SPAN_REASON), NSObject's, objc_independent_class's
    // two, lifetime_capture_by's deprecated arguments, and return_typestate's quoted unsupported argument.
    CHECK_WARNING_EVENT_IBOUTLET,
    CHECK_WARNING_EVENT_MIG_ROUTINE,
    CHECK_WARNING_EVENT_SPAN_ONLY,
    CHECK_WARNING_EVENT_NSOBJECT_TYPEDEF,
    CHECK_WARNING_EVENT_INDEPENDENT_TYPEDEF,
    CHECK_WARNING_EVENT_INDEPENDENT_POINTER,
    CHECK_WARNING_EVENT_CAPTURE_DEPRECATED,
    CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_QUOTED,
    // An assume statement's expression of (potential) side effects (#147, check_input_statement_attribute_kinds).
    CHECK_WARNING_EVENT_ASSUME_SIDE_EFFECTS,
    // vec_type_hint's second one of another type (#147, -Wignored-attributes), at it.
    CHECK_WARNING_EVENT_DIFFERENT_ARGUMENTS,
    // nomerge on a statement that evaluates no call (#147, -Wignored-attributes), at the statement; GNU's noinline and
    // always_inline there; and a statement's noinline or always_inline over a callee's always_inline, flatten or noinline.
    CHECK_WARNING_EVENT_NO_CALLS,
    CHECK_WARNING_EVENT_NOINLINE_STATEMENT,
    CHECK_WARNING_EVENT_ALWAYS_INLINE_STATEMENT,
    CHECK_WARNING_EVENT_PRECEDENCE_ALWAYS_INLINE,
    CHECK_WARNING_EVENT_PRECEDENCE_FLATTEN,
    CHECK_WARNING_EVENT_PRECEDENCE_NOINLINE,
    // An if's branches both clang::likely or both clang::unlikely (#2291, -Wignored-attributes), at the then branch's, with
    // CHECK_WARNING_NOTE_CONFLICTING_ATTRIBUTE at the else branch's.
    CHECK_WARNING_EVENT_LIKELIHOOD_CONFLICT,
    // An initializer's use of a member an unavailable attribute marks (#147, check_init_member_use): clang's error, which
    // no warning option maps, staged with the walk's warnings (check_driver_initializer_warning_events gives it). The
    // member's name in the payload's name, the attribute's message's string token in its token (or none); its note,
    // CHECK_WARNING_NOTE_UNAVAILABLE_HERE, at the member's name.
    CHECK_WARNING_EVENT_UNAVAILABLE_USE,
    // Repeated declaration specifiers: payload.name is Clang's canonical keyword spelling.
    CHECK_WARNING_EVENT_DUPLICATE_DECLSPEC,
    // A return written in a noreturn function, including unreachable returns; payload.name names the function.
    CHECK_WARNING_EVENT_NORETURN_RETURN,
    CHECK_WARNING_EVENT_NORETURN_FALLS,
    CHECK_WARNING_EVENT_PRIVATE_EXTERN,
    CHECK_WARNING_EVENT_THREAD_CAPABILITY,
    CHECK_WARNING_EVENT_THREAD_ARGUMENT,
    CHECK_WARNING_EVENT_THREAD_METHOD,
    CHECK_WARNING_EVENT_THREAD_POINTER,
    CHECK_WARNING_EVENT_THREAD_CONTEXT,
    CHECK_WARNING_EVENT_THREAD_FUNCTION_POINTER,
    CHECK_WARNING_EVENT_THREAD_PARAMETER,
    CHECK_WARNING_EVENT_NODEREF_PLACEMENT,
    CHECK_WARNING_EVENT_NODEREF_DEREFERENCE,
    CHECK_WARNING_EVENT_NODEREF_CONVERSION,
    // A call in a TCB's function of a function in none of its TCBs (#1447, check_input_tcb_calls), at the call's first token:
    // payload.name the callee's name, payload.token the caller's TCB attribute's string token.
    CHECK_WARNING_EVENT_TCB_VIOLATION,
    // The availability attribute's warnings (#1447, c_check_attribute_checks_input.c), -Wavailability's but the invalid
    // version's (-Winvalid-version-availability): a platform clang doesn't know, at it (payload.token); unavailable that
    // overrides the stages, at its keyword; an invalid version of a platform's OS, at the platform, and its note
    // (CHECK_WARNING_NOTE_IMPLICIT_VERSION), payload.data indexing CheckInput.availability_texts; Swift's stages and
    // Fuchsia's minor version, at the attribute; a stage's version before an earlier stage's, at the attribute
    // (payload.data as the invalid version's); and a redeclaration's availability that doesn't match, at it, with its note
    // at the other (CHECK_WARNING_NOTE_PREVIOUS_ATTRIBUTE).
    CHECK_WARNING_EVENT_AVAILABILITY_PLATFORM,
    CHECK_WARNING_EVENT_AVAILABILITY_UNAVAILABLE,
    CHECK_WARNING_EVENT_AVAILABILITY_VERSION,
    CHECK_WARNING_EVENT_AVAILABILITY_SWIFT,
    CHECK_WARNING_EVENT_AVAILABILITY_FUCHSIA,
    CHECK_WARNING_EVENT_AVAILABILITY_ORDERING,
    CHECK_WARNING_EVENT_AVAILABILITY_MISMATCH,
    // Objective-C's pointer type attributes where their type takes them not (#1447, check_input_attribute_objc), at the
    // attribute: objc_gc's "only applies to pointer types", objc_ownership's "... Objective-C object or block pointer types"
    // (weak's named '__weak'); payload.first_type the type there, payload.second_type the declarator's, payload.token
    // its name (or where it would be), payload.data the type's typedef's spelling.
    CHECK_WARNING_EVENT_OBJC_POINTER_TYPE,
    CHECK_WARNING_EVENT_OBJC_OBJECT_TYPE,
    CHECK_WARNING_EVENT_OBJC_WEAK_TYPE,
    // Their handlers' warning of an identifier argument they don't know, "'X' attribute argument not supported: 'Y'", as
    // CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_QUOTED's but in the declarator's type's phase.
    CHECK_WARNING_EVENT_OBJC_UNSUPPORTED,
    // A call in an interrupt service routine (payload.data 0) or a no_caller_saved_registers function (1) of a callee
    // without no_caller_saved_registers (#147, check_input_interrupt_calls: -Wexcessive-regsave), at the callee, with a
    // CHECK_WARNING_NOTE_DECLARED_HERE at a named callee's declaration.
    CHECK_WARNING_EVENT_EXCESSIVE_REGSAVE,
    // An indirection through a pointer to void (#147, clang's CheckIndirectionOperand: -Wvoid-ptr-dereference), at its `*`:
    // payload.first_type the operand's type, payload.data its typedef's spelling.
    CHECK_WARNING_EVENT_VOID_PTR_DEREFERENCE,
    // An unknown escape sequence of an evaluated literal (#1447, -Wunknown-escape-sequence): payload.data is its
    // backslash's offset in the literal's token, payload.name its character.
    CHECK_WARNING_EVENT_UNKNOWN_ESCAPE,
    // A multi-character character constant's warnings (#1447): -Wmultichar's or -Wfour-char-constants', and past four
    // characters "character constant too long for its type", each the description's message.
    CHECK_WARNING_EVENT_MULTICHAR,
    CHECK_WARNING_EVENT_CHARACTER_TOO_LONG,
    // A warning diagnose_if's condition gives (#1447, c_check_condition.c), at the call's `)` or at the function's use:
    // payload.token the message's first string token, with a CHECK_WARNING_NOTE_DIAGNOSE_IF at the attribute; or, of a
    // warning group (payload.data, BUSTER_CHECK_WARNING_GROUP_ERROR), clang's custom diagnostic of that group, without it.
    CHECK_WARNING_EVENT_DIAGNOSE_IF,
    // -Wfunction-multiversion's (#1447): a CPU list's repeated mangling, at the attribute's name, and a cpu_dispatch
    // function's body, at its `{`.
    CHECK_WARNING_EVENT_CPU_LIST_DUPLICATE,
    CHECK_WARNING_EVENT_CPU_DISPATCH_BODY,
    // The address-space attributes' warnings (#1447, check_input_address_space): opencl_global_device's and
    // opencl_global_host's -Wdeprecated-attributes, at the attribute as clang's parser gives it; and a second attribute of
    // a type's own space, -Wduplicate-decl-specifier's, at it.
    CHECK_WARNING_EVENT_ADDRESS_SPACE_DEPRECATED,
    CHECK_WARNING_EVENT_ADDRESS_SPACE_IDENTICAL,
    // objc_gc's weak qualifier on an automatic variable's or a member's type (#1447): -Wignored-attributes', at its name.
    CHECK_WARNING_EVENT_GC_WEAK_LOCAL,
    CHECK_WARNING_EVENT_GC_WEAK_FIELD,
    // No message: an enumeration mix the constant reader's binary or conditional operator may make (#1447), an array
    // bound's or an enumerator value's, which no graph node holds. token the operator's (a conditional's `:`),
    // payload.first_type and second_type its operands' types as clang's getEnumCoercedType gives them, payload.name and
    // data their typedef spellings (0 for none). check_common_enum_arithmetic reports it as the graph's operators.
    CHECK_WARNING_EVENT_ENUM_MIX,
    // target_clones' versions (#1838, c_check_target_versions.c), each at its string literal's quote plus its offset in
    // the trimmed string (BUSTER_CHECK_TARGET_CLONES_CPU's payload): -Wignored-attributes' unsupported one,
    // -Wfunction-multiversion's duplicate one, and -Wtarget-clones-mixed-specifiers' at the first string.
    CHECK_WARNING_EVENT_TARGET_CLONES_UNSUPPORTED,
    CHECK_WARNING_EVENT_TARGET_CLONES_DUPLICATE,
    CHECK_WARNING_EVENT_TARGET_CLONES_MIXED,
    // A tag a prototype's parameter list (or an old-style definition's declarations) declares first, which no declaration
    // outside it names (#1447, -Wvisibility): at its name, an unnamed enum's keyword; payload.first_type the tag's type.
    CHECK_WARNING_EVENT_PROTOTYPE_TAG,
    // A tag a prototype's list defines again (#1447, -Wvisibility): at its name, payload.name the name, with a
    // CHECK_WARNING_NOTE_PREVIOUS_DEFINITION at the earlier definition.
    CHECK_WARNING_EVENT_PROTOTYPE_REDEFINITION,
    // A C23 record's redefinition whose attributes (payload.data 0) or a member's (1) differ from the earlier's, as a
    // warning (#1447, -Wno-error=odr or a pragma's): at its name, with a CHECK_WARNING_NOTE_ATTRIBUTE_HERE at each list's
    // differing attribute, the earlier's first.
    CHECK_WARNING_EVENT_ODR_ATTRIBUTE,
    // An unevaluated string literal's encoding prefix (#1447): "encoding prefix 'P' on an unevaluated string literal has no
    // effect", at the token, payload.data the prefix's length (1 or 2: u8). A literal with an escape clang refuses there
    // has its warnings from the driver instead, before the escapes' (check_driver_escapes).
    CHECK_WARNING_EVENT_UNEVALUATED_PREFIX,
    // A delimited or named escape sequence of an unevaluated string literal clang takes (#1447, -pedantic's
    // -Wdelimited-escape-sequence-extension): at the token, payload.data 1 for a named one's "named escape sequences are a
    // Clang extension", 0 for "delimited escape sequences are a C2y extension".
    CHECK_WARNING_EVENT_DELIMITED_ESCAPE,
    // A sizeof's, an alignof's or a _Generic's unevaluated operand with a side effect (#1447,
    // check_warning_graph_unevaluated), at its expression's location: the description's message.
    CHECK_WARNING_EVENT_UNEVALUATED_EXPRESSION,
    // A bit-precise constant's suffix (wb or uwb) before C23 (#1447): -Wc23-extensions' "'_BitInt' suffix for literals is a
    // C23 extension" (CHECK_WARNING_ID_C23_PARAMETER's option), at the constant.
    CHECK_WARNING_EVENT_BIT_INT_SUFFIX,
    // A C23 attribute list before C23 (#2409, CHECK_WARNING_ID_C23_ATTRIBUTES): -Wc23-extensions' "[[]] attributes are a
    // C23 extension", at its first `[`, as clang's parser gives it (ext_c_attributes).
    CHECK_WARNING_EVENT_C23_ATTRIBUTES,
    // __builtin_cpu_supports's literal of a feature clang doesn't know (#1843, check_input_cpu_supports_literal), at the
    // builtin's name: the description's message.
    CHECK_WARNING_EVENT_INVALID_CPU_SUPPORTS,
    // An enum's fixed underlying type's const and volatile, dropped (#1952): "'const' qualifier in enumeration underlying
    // type ignored", or "'const' and 'volatile' qualifiers ...", at the base's first token that isn't a qualifier;
    // payload.data the qualifiers (BUSTER_TYPE_CONST, BUSTER_TYPE_VOLATILE).
    CHECK_WARNING_EVENT_UNDERLYING_CV_QUALIFIER,
    // An object's acting tentative definition, an array of unknown bound at the unit's end (#2238): "tentative array
    // definition assumed to have one element", at its name.
    CHECK_WARNING_EVENT_TENTATIVE_ARRAY,
    // swift_newtype's and swift_wrapper's argument other than `struct` or `enum` (#1447, check_input_swift_newtype_parse),
    // at it: "'X' attribute argument not supported: 'Y'", Y the token's identifier or keyword as written (payload.token),
    // or "(null)" for another token, as clang streams the token's IdentifierInfo.
    CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_TOKEN,
    // A cast from a pointer to a narrower integer type (#1943, check_warning_graph_pointer_cast), at the cast's `(`: "cast
    // to smaller integer type T from P", payload.first_type the operand's type after decay and payload.name its typedef's
    // spelling, payload.second_type the cast's type and payload.data its.
    CHECK_WARNING_EVENT_POINTER_TO_INT_CAST,
    // A default-initialized const object (#2168): payload.first_type its type, payload.data its typedef or typeof
    // specifier's spelling (check_input_specifier_spelling); its ID chooses the words.
    CHECK_WARNING_EVENT_DEFAULT_CONST_INIT,
    // A comma operator's left operand in an attribute's expression argument whose value clang's isUnusedResultAWarning
    // says is unused (#1447, check_input_attribute_unused): payload.data a BUSTER_CHECK_UNUSED_* message,
    // -Wunused-value's "left operand of comma operator has no effect" or its `void *` cast's question, or
    // -Wunused-comparison's, with a CHECK_WARNING_NOTE_COMPARISON_ASSIGNMENT at an equality's.
    CHECK_WARNING_EVENT_UNUSED_RESULT,
    // GNU's escape sequence (\e, \E, \(, \{, \[ or \%) in an evaluated literal (#1447, -pedantic's -Wpedantic): "use of
    // non-standard escape character '\c'" at its backslash, payload.name the character and payload.data the backslash's
    // offset in the token.
    CHECK_WARNING_EVENT_NONSTANDARD_ESCAPE,
    // swift_name's and swift_async_name's warnings (#1447, check_input_attribute_swift_name), at the name's string literal:
    // payload.data's low five bits the message (BUSTER_CHECK_SWIFT_NAME_*), then the expected and the given parameter
    // counts in 13 bits each. Its K&R function's is -Wignored-attributes', the others -Wswift-name-attribute's.
    CHECK_WARNING_EVENT_SWIFT_NAME,
    // A generic association that is never selected (#1447, check_generic_unreachable): at its type name's first token
    // that isn't a qualifier, payload.first_type its type, payload.name its typedef's spelling (or 0), payload.data 0 for an
    // array's, 1 for a qualified one's.
    CHECK_WARNING_EVENT_UNREACHABLE_ASSOCIATION,
    // GCC's cast to a union (#2301, check_warning_graph_union_cast), at the cast's `(`: its description's words.
    CHECK_WARNING_EVENT_GNU_UNION_CAST,
    // A GNU designator's -Wgnu-designator warning (#2202, check_input_gnu_designator) at its token: payload.data one of the
    // BUSTER_CHECK_GNU_DESIGNATOR_* forms.
    CHECK_WARNING_EVENT_GNU_DESIGNATOR,
    CHECK_WARNING_EVENT_KIND_COUNT,
} CheckWarningEventKind;

// CHECK_WARNING_EVENT_GNU_DESIGNATOR's forms (#2202): an array designation without its `=` (at the value's first token), an
// old-style field designator (at its name) and an array range (at its `...`).
#define BUSTER_CHECK_GNU_DESIGNATOR_MISSING_EQUAL 0u
#define BUSTER_CHECK_GNU_DESIGNATOR_OLD_STYLE 1u
#define BUSTER_CHECK_GNU_DESIGNATOR_RANGE 2u

// CHECK_WARNING_EVENT_SWIFT_NAME's messages (#1447), as clang's SemaSwift words them.
#define BUSTER_CHECK_SWIFT_NAME_FUNCTION 0u
#define BUSTER_CHECK_SWIFT_NAME_BASE 1u
#define BUSTER_CHECK_SWIFT_NAME_CONTEXT 2u
#define BUSTER_CHECK_SWIFT_NAME_PARAMETER 3u
#define BUSTER_CHECK_SWIFT_NAME_DECLARATION 4u
#define BUSTER_CHECK_SWIFT_NAME_SUBSCRIPT_ACCESSOR 5u
#define BUSTER_CHECK_SWIFT_NAME_SUBSCRIPT_PARAMETER 6u
#define BUSTER_CHECK_SWIFT_NAME_SUBSCRIPT_SELF 7u
#define BUSTER_CHECK_SWIFT_NAME_MISSING_PARAMETERS 8u
#define BUSTER_CHECK_SWIFT_NAME_SETTER 9u
#define BUSTER_CHECK_SWIFT_NAME_SELVES 10u
#define BUSTER_CHECK_SWIFT_NAME_GETTER 11u
#define BUSTER_CHECK_SWIFT_NAME_NO_NEW_VALUE 12u
#define BUSTER_CHECK_SWIFT_NAME_NEW_VALUES 13u
#define BUSTER_CHECK_SWIFT_NAME_GETTER_NEW_VALUE 14u
#define BUSTER_CHECK_SWIFT_NAME_COUNT 15u
#define BUSTER_CHECK_SWIFT_NAME_NO_PARAMETERS 16u
#define BUSTER_CHECK_SWIFT_NAME_PROTOTYPE 17u

// CHECK_WARNING_EVENT_TARGET_CLONES_*'s payload (#1838): `name` the version's first byte in its string's decoded text (it
// ends at the next comma, trimmed), and `data` its place's offset from the trimmed text's start, with
// BUSTER_CHECK_TARGET_CLONES_CPU where an unsupported one is a CPU's.
#define BUSTER_CHECK_TARGET_CLONES_CPU (1u << 31)

// CHECK_WARNING_EVENT_UNUSED_RESULT's message (payload.data).
#define BUSTER_CHECK_UNUSED_COMMA 0u
#define BUSTER_CHECK_UNUSED_VOID_POINTER 1u
#define BUSTER_CHECK_UNUSED_EQUALITY 2u
#define BUSTER_CHECK_UNUSED_INEQUALITY 3u
#define BUSTER_CHECK_UNUSED_RELATIONAL 4u

// CHECK_WARNING_EVENT_TRANSPARENT_UNION's case, in its data's low byte (its bits above it), and its note's.
#define BUSTER_CHECK_TRANSPARENT_INCOMPLETE 1u
#define BUSTER_CHECK_TRANSPARENT_EMPTY 2u
#define BUSTER_CHECK_TRANSPARENT_FLOAT 3u
#define BUSTER_CHECK_TRANSPARENT_VECTOR 4u
#define BUSTER_CHECK_TRANSPARENT_SIZE 5u
#define BUSTER_CHECK_TRANSPARENT_ALIGNMENT 6u

typedef enum CheckWarningNoteKind
{
    CHECK_WARNING_NOTE_PRIOR_INITIALIZATION,
    CHECK_WARNING_NOTE_INLINE_LINKAGE,
    CHECK_WARNING_NOTE_USED_HERE,
    CHECK_WARNING_NOTE_PREVIOUS_DEFINITION,
    CHECK_WARNING_NOTE_TRANSPARENT_FIRST_FIELD,
    CHECK_WARNING_NOTE_CONFLICTING_ATTRIBUTE,
    CHECK_WARNING_NOTE_DEPRECATED_HERE,
    // Why malloc_span's function returns no span-like structure (check_input_span_reason's data).
    CHECK_WARNING_NOTE_SPAN_REASON,
    CHECK_WARNING_NOTE_UNAVAILABLE_HERE,
    CHECK_WARNING_NOTE_PRIVATE_EXTERN,
    CHECK_WARNING_NOTE_NODEREF_DECLARATION,
    // An invalid availability version's canonical one (payload.data indexing CheckInput.availability_texts), and the
    // previous attribute of a redeclaration's availability (#1447).
    CHECK_WARNING_NOTE_IMPLICIT_VERSION,
    CHECK_WARNING_NOTE_PREVIOUS_ATTRIBUTE,
    // "'f' declared here" (clang's note_entity_declared_at), payload.name the entity's name.
    CHECK_WARNING_NOTE_DECLARED_HERE,
    // "from 'diagnose_if' attribute on 'f':" (#1447), payload.name the function's name.
    CHECK_WARNING_NOTE_DIAGNOSE_IF,
    // "attribute 'packed' here" (#1447, CHECK_WARNING_EVENT_ODR_ATTRIBUTE's), payload.name the attribute's name.
    CHECK_WARNING_NOTE_ATTRIBUTE_HERE,
    // "member 'm' declared 'const' here" (#2168), payload.name the member's name.
    CHECK_WARNING_NOTE_CONST_MEMBER,
    // "use '=' to turn this equality comparison into an assignment" (payload.data BUSTER_CHECK_UNUSED_EQUALITY), or "use '|='
    // to turn this inequality comparison into an or-assignment" (BUSTER_CHECK_UNUSED_INEQUALITY).
    CHECK_WARNING_NOTE_COMPARISON_ASSIGNMENT,
    CHECK_WARNING_NOTE_KIND_COUNT,
} CheckWarningNoteKind;

typedef enum CheckWarningCompletion
{
    // Complete with this parser token in lookahead, before consuming it.
    CHECK_WARNING_COMPLETE_AT_TOKEN,
    // Complete after consuming this delimiter and lexing the following parser token (or EOF).
    CHECK_WARNING_COMPLETE_AFTER_DELIMITER,
    // Complete after all EOF preprocessing events; the located declaration still governs source policy.
    CHECK_WARNING_COMPLETE_TU_END,
    CHECK_WARNING_COMPLETION_COUNT,
} CheckWarningCompletion;

// The order of the warnings completed at the translation unit's end, as their `suborder` (#2141): clang's
// Sema::ActOnEndOfTranslationUnit diagnoses unterminated pack pushes, then #pragma weak's undeclared identifiers, then
// internal functions used but not defined, then tentative definitions' const objects, and the unused file-scope
// declarations last. Those keep suborder 0, which
// orders after every nonzero one at the same boundary.
typedef enum CheckWarningUnitEnd
{
    CHECK_WARNING_UNIT_END_UNUSED,
    CHECK_WARNING_UNIT_END_PACK,
    CHECK_WARNING_UNIT_END_WEAK,
    CHECK_WARNING_UNIT_END_UNDEFINED,
} CheckWarningUnitEnd;

// Per-event source policy is independent of the ID: GNU array-bound folds warn by default, whereas an accepted
// nonstandard ICE fold can start ignored under the same -Wgnu-folding-constant policy. Completion/lookahead is also
// independent of the diagnostic location: a later preprocessing diagnostic can precede an earlier source warning.
#define BUSTER_CHECK_WARNING_DEFAULT_IGNORED 0x01u
// A semantic proof prevents publication, independently of every requested warning policy (for example sizeof).
#define BUSTER_CHECK_WARNING_SUPPRESSED 0x02u
// CHECK_WARNING_EVENT_PRAGMA's payload.data indexes CheckInputSymbolPragmas only with this discriminator.
#define BUSTER_CHECK_WARNING_SYMBOL_PRAGMA 0x04u
// CHECK_WARNING_EVENT_PRAGMA's payload.data indexes CheckInputMsPragmas only with this discriminator.
#define BUSTER_CHECK_WARNING_MS_PRAGMA 0x08u
// A CHECK_WARNING_EVENT_LATE_ATTRIBUTE of an implicit attribute, which clang gives no location (#147: interrupt's used
// after a definition), its token the attribute's that made it, for its policy's place.
#define BUSTER_CHECK_WARNING_UNLOCATED 0x10u
// CHECK_WARNING_EVENT_PRAGMA's payload.data indexes CheckInputPackPragmas only with this discriminator (#1849: a
// `#pragma pack`, `#pragma options align` or `#pragma align` warning, whose payload.name is its CheckPackMessage).
#define BUSTER_CHECK_WARNING_PACK_PRAGMA 0x20u
#define BUSTER_CHECK_WARNING_NO_SOURCE UINT32_MAX
// CHECK_WARNING_EVENT_DIAGNOSE_IF's payload.data bit of a grouped diagnose_if's "error" (#1447): its low bits are its
// warning group's index in pp_features_warning plus one, 0 for none.
#define BUSTER_CHECK_WARNING_GROUP_ERROR 0x80000000u
// CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's payload.data bit of a C23 attribute's spelling, past its scope's identifier.
#define BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE 0x80000000u

typedef struct CheckWarningPayload CheckWarningPayload;
struct CheckWarningPayload
{
    // Only these two fields contain TypeIds and need type remapping when a deferred body's facts are merged.
    // A nonzero ID must be valid in the producing reader's type table; zero means that the kind needs no type.
    TypeId first_type;
    TypeId second_type;
    // An IdentId in source consumers; standalone graph clients need not include the interner.
    uint32_t name;
    // Related TU parser token when the kind uses one, or NO_SOURCE. UNUSED_VARIABLE retains a for-header's closing
    // parenthesis here, separate from completion at the body's end, so rejected-unit filtering finds the scope's start.
    // Other UNUSED_VARIABLE scopes have NO_SOURCE. Kinds without a related location ignore it;
    // their zero-initialized field is also a valid token whenever a primary can be published.
    uint32_t token;
    uint32_t data;
};

typedef struct CheckWarningEvent CheckWarningEvent;
struct CheckWarningEvent
{
    uint32_t id;
    uint32_t kind;
    uint32_t token;
    // PP output source index for an exact boundary, or NO_SOURCE for the ordinary source of token.
    uint32_t source;
    uint32_t flags;
    uint32_t completion_kind;
    // Parser token for AT_TOKEN/AFTER_DELIMITER; NO_SOURCE for TU_END. Attached notes inherit this completion.
    uint32_t completion;
    // Optional order inside a completed construct, independent of its source location and publication boundary.
    uint64_t suborder;
    CheckWarningPayload payload;
    uint32_t note_first;
    uint32_t note_count;
};

typedef struct CheckWarningNote CheckWarningNote;
struct CheckWarningNote
{
    uint32_t kind;
    uint32_t token;
    uint32_t source;
    CheckWarningPayload payload;
};

typedef struct CheckWarningEvents CheckWarningEvents;
struct CheckWarningEvents
{
    const CheckWarningEvent* events;
    uint32_t count;
    const CheckWarningNote* notes;
    uint32_t note_count;
};

typedef struct CheckWarningBuffer CheckWarningBuffer;
struct CheckWarningBuffer
{
    Arena* arena;
    CheckWarningEvent* events;
    uint32_t count;
    uint32_t capacity;
    CheckWarningNote* notes;
    uint32_t note_count;
    uint32_t note_capacity;
};

static __attribute__((unused)) CheckWarningEvents check_warning_view(const CheckWarningBuffer* buffer)
{
    const CheckWarningEvents result = {
        .events = buffer->count != 0 ? buffer->events : NULL,
        .count = buffer->count,
        .notes = buffer->note_count != 0 ? buffer->notes : NULL,
        .note_count = buffer->note_count,
    };
    return result;
}

// Carrier publication and diagnostic rendering share the same flag contract. History discriminators describe a
// pragma's payload without affecting its warning policy, and are invalid on every other event kind.
static bool check_warning_flags_valid(uint32_t kind, uint32_t flags)
{
    const uint32_t histories = BUSTER_CHECK_WARNING_SYMBOL_PRAGMA | BUSTER_CHECK_WARNING_MS_PRAGMA | BUSTER_CHECK_WARNING_PACK_PRAGMA;
    const uint32_t payload = flags & histories;
    return (flags & ~(BUSTER_CHECK_WARNING_DEFAULT_IGNORED | BUSTER_CHECK_WARNING_SUPPRESSED | histories | BUSTER_CHECK_WARNING_UNLOCATED)) == 0 &&
           (payload == 0 || kind == CHECK_WARNING_EVENT_PRAGMA) && (payload & (payload - 1)) == 0 &&
           ((flags & BUSTER_CHECK_WARNING_UNLOCATED) == 0 || kind == CHECK_WARNING_EVENT_LATE_ATTRIBUTE);
}

static uint32_t check_warning_capacity(uint32_t capacity, uint32_t needed)
{
    uint32_t result = capacity != 0 ? capacity : 16;
    while (result < needed)
    {
        result = result <= UINT32_MAX / 2 ? result * 2 : needed;
    }
    return result;
}

// Append one primary and its attached notes atomically. Existing prefix records are never rewritten, including
// when either allocation fails. Arena suffix rollback preserves other owners' earlier allocations and views.
static __attribute__((unused)) bool check_warning_append(CheckWarningBuffer* buffer, const CheckWarningEvent* event,
                                                        const CheckWarningNote* notes, uint32_t note_count)
{
    bool valid = buffer != NULL && buffer->arena != NULL && event != NULL && event->id < CHECK_WARNING_ID_COUNT &&
                 event->kind < CHECK_WARNING_EVENT_KIND_COUNT && event->token != UINT32_MAX &&
                 check_warning_flags_valid(event->kind, event->flags) &&
                 event->completion_kind < CHECK_WARNING_COMPLETION_COUNT &&
                 ((event->completion_kind == CHECK_WARNING_COMPLETE_TU_END) == (event->completion == UINT32_MAX)) &&
                 buffer->count <= buffer->capacity && (buffer->count == 0 || buffer->events != NULL) &&
                 buffer->note_count <= buffer->note_capacity && (buffer->note_count == 0 || buffer->notes != NULL) &&
                 buffer->count < UINT32_MAX && note_count <= UINT32_MAX - buffer->note_count &&
                 (note_count == 0 || notes != NULL);
    for (uint32_t i = 0; valid && i < note_count; i += 1)
    {
        valid = notes[i].kind < CHECK_WARNING_NOTE_KIND_COUNT && notes[i].token != UINT32_MAX;
    }
    if (valid)
    {
        Arena* arena = buffer->arena;
        const uint64_t mark = arena->length;
        const uint32_t count = buffer->count + 1;
        const uint32_t note_end = buffer->note_count + note_count;
        const uint32_t capacity = check_warning_capacity(buffer->capacity, count);
        const uint32_t note_capacity = note_end != 0 ? check_warning_capacity(buffer->note_capacity, note_end) : buffer->note_capacity;
        CheckWarningEvent* events = buffer->events;
        CheckWarningNote* written_notes = buffer->notes;
        if (capacity != buffer->capacity)
        {
            events = BUSTER_ARENA_PUSH_ARRAY(arena, CheckWarningEvent, capacity);
            if (events != NULL && buffer->count != 0)
            {
                memcpy(events, buffer->events, (uint64_t)buffer->count * sizeof(*events));
            }
        }
        if (note_capacity != buffer->note_capacity)
        {
            written_notes = BUSTER_ARENA_PUSH_ARRAY(arena, CheckWarningNote, note_capacity);
            if (written_notes != NULL && buffer->note_count != 0)
            {
                memcpy(written_notes, buffer->notes, (uint64_t)buffer->note_count * sizeof(*written_notes));
            }
        }
        valid = events != NULL && (note_end == 0 || written_notes != NULL);
        if (valid)
        {
            // Save the input before publishing: it may itself be in an earlier immutable event view.
            CheckWarningEvent added = *event;
            added.note_first = buffer->note_count;
            added.note_count = note_count;
            if (note_count != 0)
            {
                memmove(written_notes + buffer->note_count, notes, (uint64_t)note_count * sizeof(*notes));
            }
            events[buffer->count] = added;
            buffer->events = events;
            buffer->count = count;
            buffer->capacity = capacity;
            buffer->notes = written_notes;
            buffer->note_count = note_end;
            buffer->note_capacity = note_capacity;
        }
        else
        {
            arena_pop_to(arena, mark);
        }
    }
    return valid;
}

#endif
